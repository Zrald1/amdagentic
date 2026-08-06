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
static jobject g_activity = nullptr;

// Helper: call Java method on UI thread
static void callJavaMethod(const char* methodName, const char* sig, const std::string& arg) {
    if (!g_jvm || !g_activity) return;
    JNIEnv* env = nullptr;
    bool attached = false;
    if (g_jvm->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK) {
        g_jvm->AttachCurrentThread(&env, nullptr);
        attached = true;
    }
    if (!env) return;

    jclass cls = env->GetObjectClass(g_activity);
    if (cls) {
        jmethodID mid = env->GetMethodID(cls, methodName, sig);
        if (mid) {
            if (sig[0] == '(' && sig[1] == 'L') {
                // String argument
                jstring jstr = env->NewStringUTF(arg.c_str());
                env->CallVoidMethod(g_activity, mid, jstr);
                env->DeleteLocalRef(jstr);
            } else {
                env->CallVoidMethod(g_activity, mid);
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

    g_robot.init(&g_renderer);

    auto lastTime = std::chrono::high_resolution_clock::now();

    while (g_running.load() && !g_paused.load()) {
        auto now = std::chrono::high_resolution_clock::now();
        float dt = std::chrono::duration<float>(now - lastTime).count();
        lastTime = now;

        g_robot.update(dt);

        // Clear and render
        glClearColor(0.05f, 0.05f, 0.08f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        g_robot.render();

        g_renderer.render();
    }

    g_renderer.destroy();
    LOGI("Render loop ended");
}

extern "C" {

JNIEXPORT void JNICALL
Java_com_argos_companion_MainActivity_nativeInit(JNIEnv* env, jobject activity, jobject surfaceView) {
    LOGI("nativeInit");

    env->GetJavaVM(&g_jvm);
    g_activity = env->NewGlobalRef(activity);

    // Get Surface from SurfaceView
    jclass svClass = env->GetObjectClass(surfaceView);
    jmethodID getHolder = env->GetMethodID(svClass, "getHolder", "()Landroid/view/SurfaceHolder;");
    jobject holder = env->CallObjectMethod(surfaceView, getHolder);
    if (!holder) {
        LOGE("SurfaceHolder is null — surface not ready yet");
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

    // Start render thread
    g_running.store(true);
    g_paused.store(false);
    g_renderThread = std::thread(renderLoop);
}

JNIEXPORT void JNICALL
Java_com_argos_companion_MainActivity_nativeSendChat(JNIEnv* env, jobject activity, jstring message) {
    const char* msg = env->GetStringUTFChars(message, nullptr);
    std::string userMsg(msg);
    env->ReleaseStringUTFChars(message, msg);

    LOGI("nativeSendChat: %s", userMsg.c_str());

    g_robot.setThinking(true);

    // Run chat in background thread
    std::thread([userMsg]() {
        std::string accumulated;
        std::string response = g_agent.chatStreaming(userMsg,
            [&](const std::string& delta) -> bool {
                accumulated += delta;
                callJavaMethod("onChatStream", "(Ljava/lang/String;)V", accumulated);
                return !g_agent.m_abort.load();
            });

        g_robot.setThinking(false);
        g_robot.setTalking(true);

        if (g_agent.m_abort.load()) {
            callJavaMethod("onChatError", "(Ljava/lang/String;)V", "Cancelled");
        } else {
            callJavaMethod("onChatResponse", "(Ljava/lang/String;)V", response);
        }
    }).detach();
}

JNIEXPORT void JNICALL
Java_com_argos_companion_MainActivity_nativeResume(JNIEnv* env, jobject activity) {
    LOGI("nativeResume");
    g_paused.store(false);
}

JNIEXPORT void JNICALL
Java_com_argos_companion_MainActivity_nativeOnTouch(JNIEnv* env, jobject activity, jfloat x, jfloat y, jint action) {
    g_robot.onTouch(x, y, action);
}

JNIEXPORT void JNICALL
Java_com_argos_companion_MainActivity_nativePause(JNIEnv* env, jobject activity) {
    LOGI("nativePause");
    g_paused.store(true);
}

JNIEXPORT void JNICALL
Java_com_argos_companion_MainActivity_nativeDestroy(JNIEnv* env, jobject activity) {
    LOGI("nativeDestroy");
    g_running.store(false);
    g_agent.m_abort.store(true);
    if (g_renderThread.joinable()) g_renderThread.join();

    if (g_window) {
        ANativeWindow_release(g_window);
        g_window = nullptr;
    }
    if (g_activity) {
        env->DeleteGlobalRef(g_activity);
        g_activity = nullptr;
    }
}

} // extern "C"
