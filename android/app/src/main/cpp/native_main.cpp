#include <jni.h>
#include <android/native_window_jni.h>
#include <android/log.h>
#include <android/surface_control.h>
#include <pthread.h>
#include <unistd.h>
#include <string>
#include <thread>
#include <atomic>
#include <chrono>
#include <cstdio>

#include "egl_renderer.h"
#include "robot_gles.h"
#include "agent_client_core.h"
#include "platform.h"

#define TAG "Argos"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

static EglRenderer g_renderer;
static RobotGles g_robot;
static AgentClientCore g_agent;
static ANativeWindow* g_window = nullptr;
static std::atomic<bool> g_running(false);
static std::atomic<bool> g_paused(false);
static std::thread g_renderThread;
static JavaVM* g_jvm = nullptr;
static jobject g_service = nullptr;
static float g_screenWidth = 1080.0f;
static float g_screenHeight = 1920.0f;

// Helper: call Java method on UI thread
static void callJavaMethod(const char* methodName, const char* sig, const std::string& arg) {
    if (!g_jvm || !g_service) return;
    JNIEnv* env = nullptr;
    bool attached = false;
    if (g_jvm->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK) {
        g_jvm->AttachCurrentThread(&env, nullptr);
        attached = true;
    }
    if (!env) return;

    jclass cls = env->GetObjectClass(g_service);
    if (cls) {
        jmethodID mid = env->GetMethodID(cls, methodName, sig);
        if (mid) {
            if (sig[0] == '(' && sig[1] == 'L') {
                // String argument
                jstring jstr = env->NewStringUTF(arg.c_str());
                env->CallVoidMethod(g_service, mid, jstr);
                env->DeleteLocalRef(jstr);
            } else {
                env->CallVoidMethod(g_service, mid);
            }
        }
        env->DeleteLocalRef(cls);
    }

    if (attached) g_jvm->DetachCurrentThread();
}

static void renderLoop() {
    LOGI("Render loop started");

    if (!g_window) {
        LOGE("No window in render loop");
        return;
    }

    if (!g_renderer.init(g_window)) {
        LOGE("EGL renderer init failed");
        return;
    }

    g_robot.init(&g_renderer, g_screenWidth, g_screenHeight);

    // Enable blending for transparent overlay
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    auto lastTime = std::chrono::high_resolution_clock::now();

    while (g_running.load()) {
        if (g_paused.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            lastTime = std::chrono::high_resolution_clock::now();
            continue;
        }

        auto now = std::chrono::high_resolution_clock::now();
        float dt = std::chrono::duration<float>(now - lastTime).count();
        lastTime = now;

        // Cap dt to prevent huge jumps after pause
        if (dt > 0.1f) dt = 0.016f;

        g_robot.update(dt);

        // Check for tap events and notify Java
        if (g_robot.consumeHeadTap()) {
            callJavaMethod("onHeadTap", "()V", "");
        }
        if (g_robot.consumeBodyTap()) {
            callJavaMethod("onBodyTap", "()V", "");
        }

        // Send robot position to Java so the overlay window can follow
        {
            char posBuf[128];
            float rx = g_robot.getScreenX();
            float ry = g_robot.getScreenY();
            float rs = g_robot.getRobotSize();
            snprintf(posBuf, sizeof(posBuf), "%.1f,%.1f,%.1f", rx, ry, rs);
            callJavaMethod("onRobotPosition", "(Ljava/lang/String;)V", posBuf);
        }

        // Clear with transparent — this is an overlay on top of other apps
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        g_robot.render();

        g_renderer.render();
    }

    g_renderer.destroy();
    LOGI("Render loop ended");
}

extern "C" {

JNIEXPORT void JNICALL
Java_com_argos_companion_FloatingRobotService_nativeInit(JNIEnv* env, jobject service, jobject surfaceView, jfloat screenWidth, jfloat screenHeight) {
    LOGI("nativeInit screen=%.0fx%.0f", screenWidth, screenHeight);

    env->GetJavaVM(&g_jvm);
    if (g_service) {
        env->DeleteGlobalRef(g_service);
    }
    g_service = env->NewGlobalRef(service);
    g_screenWidth = screenWidth;
    g_screenHeight = screenHeight;

    // Stop any existing render thread before starting a new one
    g_running.store(false);
    if (g_renderThread.joinable()) {
        g_renderThread.join();
    }

    // Release old window if any
    if (g_window) {
        ANativeWindow_release(g_window);
        g_window = nullptr;
    }

    // Get Surface from SurfaceView
    jclass svClass = env->GetObjectClass(surfaceView);
    jmethodID getHolder = env->GetMethodID(svClass, "getHolder", "()Landroid/view/SurfaceHolder;");
    jobject holder = env->CallObjectMethod(surfaceView, getHolder);
    if (!holder) {
        LOGE("SurfaceHolder is null — surface not ready yet");
        env->DeleteLocalRef(svClass);
        return;
    }
    jclass holderClass = env->GetObjectClass(holder);
    jmethodID getSurface = env->GetMethodID(holderClass, "getSurface", "()Landroid/view/Surface;");
    jobject surface = env->CallObjectMethod(holder, getSurface);
    if (!surface) {
        LOGE("Surface is null — surface not ready yet");
        env->DeleteLocalRef(holderClass);
        env->DeleteLocalRef(holder);
        env->DeleteLocalRef(svClass);
        return;
    }
    g_window = ANativeWindow_fromSurface(env, surface);
    env->DeleteLocalRef(surface);
    env->DeleteLocalRef(holderClass);
    env->DeleteLocalRef(holder);
    env->DeleteLocalRef(svClass);

    if (g_window) {
        LOGI("Got ANativeWindow: %p, size: %dx%d", g_window,
             ANativeWindow_getWidth(g_window), ANativeWindow_getHeight(g_window));
    } else {
        LOGE("ANativeWindow_fromSurface returned null");
        return;
    }

    // Set app data dir
    argos::setAppDataDir("/data/data/com.argos.companion/files");

    // Set JNI for HTTP requests (uses Java HttpURLConnection for HTTPS support)
    argos::setJniForHttp(g_jvm, g_service);

    // Start render thread
    g_running.store(true);
    g_paused.store(false);
    g_renderThread = std::thread(renderLoop);
}

JNIEXPORT void JNICALL
Java_com_argos_companion_FloatingRobotService_nativeSendChat(JNIEnv* env, jobject service, jstring message) {
    const char* msg = env->GetStringUTFChars(message, nullptr);
    std::string userMsg(msg);
    env->ReleaseStringUTFChars(message, msg);

    LOGI("nativeSendChat: %s", userMsg.c_str());

    // Get current app context from Java (proactive screen awareness)
    std::string currentAppContext;
    {
        jclass cls = env->GetObjectClass(service);
        jmethodID mid = env->GetMethodID(cls, "getCurrentAppContext", "()Ljava/lang/String;");
        if (mid) {
            jstring jresult = (jstring) env->CallObjectMethod(service, mid);
            if (jresult) {
                const char* chars = env->GetStringUTFChars(jresult, nullptr);
                if (chars) {
                    currentAppContext = chars;
                    env->ReleaseStringUTFChars(jresult, chars);
                }
                env->DeleteLocalRef(jresult);
            }
        }
        env->DeleteLocalRef(cls);
        if (env->ExceptionCheck()) env->ExceptionClear();
    }

    // Prepend current app context to the message
    if (!currentAppContext.empty()) {
        userMsg = "[Context: User is currently using " + currentAppContext + "]\n" + userMsg;
    }

    g_robot.setThinking(true);

    // Run chat in background thread
    std::thread([userMsg]() {
        LOGI("Chat thread started for message: %s", userMsg.c_str());

        auto startTime = std::chrono::high_resolution_clock::now();

        std::string accumulated;
        std::string response = g_agent.chatStreaming(userMsg,
            [&](const std::string& delta) -> bool {
                // Check for reset signal (sent before follow-up iterations)
                if (delta == "\x01RESET\x01") {
                    accumulated.clear();
                    return !g_agent.m_abort.load();
                }
                accumulated += delta;
                // Strip [TOOL:...] tags from displayed text
                std::string display = accumulated;
                size_t pos = 0;
                while ((pos = display.find("[TOOL:", pos)) != std::string::npos) {
                    size_t end = display.find(']', pos);
                    if (end == std::string::npos) break;
                    display.erase(pos, end - pos + 1);
                }
                // Clean up whitespace
                auto cleanStart = display.find_first_not_of(" \t\n\r");
                if (cleanStart == std::string::npos) display = "";
                else if (cleanStart > 0) display = display.substr(cleanStart);
                if (!display.empty()) {
                    callJavaMethod("onChatStream", "(Ljava/lang/String;)V", display);
                }
                return !g_agent.m_abort.load();
            },
            [&](const std::string& thoughts) {
                callJavaMethod("onChatThoughts", "(Ljava/lang/String;)V", thoughts);
            },
            [&](const std::string& status) {
                callJavaMethod("onToolStatus", "(Ljava/lang/String;)V", status);
            });

        auto endTime = std::chrono::high_resolution_clock::now();
        auto durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();

        LOGI("Chat response: %s (len=%zu, time=%lldms)", response.c_str(), response.size(), (long long)durationMs);

        g_robot.setThinking(false);
        g_robot.setTalking(true);

        // Send metrics to Java
        char metricsBuf[128];
        snprintf(metricsBuf, sizeof(metricsBuf), "%lldms|%zuchars", (long long)durationMs, response.size());
        callJavaMethod("onChatMetrics", "(Ljava/lang/String;)V", metricsBuf);

        if (g_agent.m_abort.load()) {
            callJavaMethod("onChatError", "(Ljava/lang/String;)V", "Cancelled");
        } else if (response.empty()) {
            callJavaMethod("onChatError", "(Ljava/lang/String;)V", "No response from server. Check network connection.");
        } else if (response.find("[Error:") != std::string::npos) {
            callJavaMethod("onChatError", "(Ljava/lang/String;)V", response);
        } else {
            callJavaMethod("onChatResponse", "(Ljava/lang/String;)V", response);
        }
    }).detach();
}

JNIEXPORT void JNICALL
Java_com_argos_companion_FloatingRobotService_nativeSetPosition(JNIEnv* env, jobject service, jfloat x, jfloat y) {
    g_robot.setScreenPosition(x, y);
}

JNIEXPORT void JNICALL
Java_com_argos_companion_FloatingRobotService_nativeResume(JNIEnv* env, jobject service) {
    LOGI("nativeResume");
    g_paused.store(false);
}

JNIEXPORT void JNICALL
Java_com_argos_companion_FloatingRobotService_nativeOnTouch(JNIEnv* env, jobject service, jfloat x, jfloat y, jint action) {
    g_robot.onTouch(x, y, action);
}

JNIEXPORT void JNICALL
Java_com_argos_companion_FloatingRobotService_nativePause(JNIEnv* env, jobject service) {
    LOGI("nativePause");
    g_paused.store(true);
}

JNIEXPORT void JNICALL
Java_com_argos_companion_FloatingRobotService_nativeDestroy(JNIEnv* env, jobject service) {
    LOGI("nativeDestroy");
    g_running.store(false);
    g_agent.m_abort.store(true);
    if (g_renderThread.joinable()) g_renderThread.join();

    if (g_window) {
        ANativeWindow_release(g_window);
        g_window = nullptr;
    }
    if (g_service) {
        env->DeleteGlobalRef(g_service);
        g_service = nullptr;
    }
}

} // extern "C"
