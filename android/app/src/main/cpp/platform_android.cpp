#include "platform.h"
#include <android/log.h>
#include <android/native_window.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include <cstring>
#include <fstream>

#define TAG "Argos"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

// HTTP is handled via JNI -> Java HttpURLConnection (supports HTTPS)
// We just need the JNI environment here

#include <jni.h>

namespace argos {

static std::string s_appDataDir = "/data/data/com.argos.companion/files";

void setAppDataDir(const char* dir) {
    if (dir) s_appDataDir = dir;
}

std::string getAppDataDir() {
    return s_appDataDir;
}

void log(const char* message) {
    LOGI("%s", message);
}

// JNI VM pointer set by native_main.cpp
static JavaVM* s_jvm = nullptr;
static jobject s_service = nullptr;

void setJniForHttp(void* jvm, void* service) {
    s_jvm = (JavaVM*)jvm;
    s_service = (jobject)service;
}

// Call Java's httpPostJava method via JNI
static std::string callJavaHttpPost(const std::string& url, const std::string& headers,
                                     const std::string& body, bool stream) {
    if (!s_jvm || !s_service) {
        LOGE("JNI not initialized for HTTP");
        return "[Error: JNI not initialized]";
    }
    
    JNIEnv* env = nullptr;
    bool attached = false;
    if (s_jvm->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK) {
        s_jvm->AttachCurrentThread(&env, nullptr);
        attached = true;
    }
    if (!env) {
        LOGE("Failed to get JNIEnv");
        return "[Error: Failed to get JNIEnv]";
    }
    
    jclass cls = env->GetObjectClass(s_service);
    if (!cls) {
        LOGE("Failed to get service class");
        if (attached) s_jvm->DetachCurrentThread();
        return "[Error: Failed to get service class]";
    }
    
    jmethodID mid = env->GetMethodID(cls, "httpPostJava", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Z)Ljava/lang/String;");
    if (!mid) {
        LOGE("Failed to find httpPostJava method");
        env->DeleteLocalRef(cls);
        if (attached) s_jvm->DetachCurrentThread();
        return "[Error: httpPostJava method not found]";
    }
    
    jstring jurl = env->NewStringUTF(url.c_str());
    jstring jheaders = env->NewStringUTF(headers.c_str());
    jstring jbody = env->NewStringUTF(body.c_str());
    jboolean jstream = stream ? JNI_TRUE : JNI_FALSE;
    
    jstring jresult = (jstring) env->CallObjectMethod(s_service, mid, jurl, jheaders, jbody, jstream);
    
    std::string result;
    if (jresult) {
        const char* chars = env->GetStringUTFChars(jresult, nullptr);
        if (chars) {
            result = chars;
            env->ReleaseStringUTFChars(jresult, chars);
        }
        env->DeleteLocalRef(jresult);
    }
    
    env->DeleteLocalRef(jurl);
    env->DeleteLocalRef(jheaders);
    env->DeleteLocalRef(jbody);
    env->DeleteLocalRef(cls);
    
    if (env->ExceptionCheck()) {
        env->ExceptionDescribe();
        env->ExceptionClear();
        if (result.empty()) result = "[Error: Java exception in HTTP request]";
    }
    
    if (attached) s_jvm->DetachCurrentThread();
    return result;
}

int64_t getTimeMs() {
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

std::string httpPost(const std::string& url, const std::string& headers, const std::string& body) {
    LOGI("httpPost via Java: %s", url.c_str());
    std::string result = callJavaHttpPost(url, headers, body, false);
    LOGI("httpPost result len=%zu", result.size());
    return result;
}

std::string httpPostStream(const std::string& url, const std::string& headers,
                           const std::string& body,
                           std::function<bool(const std::string&)> callback) {
    LOGI("httpPostStream via Java: %s", url.c_str());
    std::string result = callJavaHttpPost(url, headers, body, true);
    LOGI("httpPostStream result len=%zu", result.size());
    if (callback && !result.empty() && result[0] != '[') {
        callback(result);
    }
    return result;
}

// Generic JNI helper: call a Java method on s_service that takes one String arg and returns String
static std::string callJavaStringMethod(const char* methodName, const char* sig, const std::string& arg) {
    if (!s_jvm || !s_service) {
        return "{\"error\":\"JNI not initialized\"}";
    }
    JNIEnv* env = nullptr;
    bool attached = false;
    if (s_jvm->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK) {
        s_jvm->AttachCurrentThread(&env, nullptr);
        attached = true;
    }
    if (!env) return "{\"error\":\"Failed to get JNIEnv\"}";

    jclass cls = env->GetObjectClass(s_service);
    if (!cls) {
        if (attached) s_jvm->DetachCurrentThread();
        return "{\"error\":\"Failed to get service class\"}";
    }
    jmethodID mid = env->GetMethodID(cls, methodName, sig);
    if (!mid) {
        env->DeleteLocalRef(cls);
        if (attached) s_jvm->DetachCurrentThread();
        return "{\"error\":\"Method not found: " + std::string(methodName) + "\"}";
    }

    jstring jarg = env->NewStringUTF(arg.c_str());
    jstring jresult = (jstring) env->CallObjectMethod(s_service, mid, jarg);

    std::string result;
    if (jresult) {
        const char* chars = env->GetStringUTFChars(jresult, nullptr);
        if (chars) {
            result = chars;
            env->ReleaseStringUTFChars(jresult, chars);
        }
        env->DeleteLocalRef(jresult);
    }
    env->DeleteLocalRef(jarg);
    env->DeleteLocalRef(cls);

    if (env->ExceptionCheck()) {
        env->ExceptionDescribe();
        env->ExceptionClear();
        if (result.empty()) result = "{\"error\":\"Java exception in " + std::string(methodName) + "\"}";
    }
    if (attached) s_jvm->DetachCurrentThread();
    return result;
}

// Generic JNI helper: call a Java method on s_service that takes one int arg and returns String
static std::string callJavaIntMethod(const char* methodName, const char* sig, int arg) {
    if (!s_jvm || !s_service) {
        return "{\"error\":\"JNI not initialized\"}";
    }
    JNIEnv* env = nullptr;
    bool attached = false;
    if (s_jvm->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK) {
        s_jvm->AttachCurrentThread(&env, nullptr);
        attached = true;
    }
    if (!env) return "{\"error\":\"Failed to get JNIEnv\"}";

    jclass cls = env->GetObjectClass(s_service);
    if (!cls) {
        if (attached) s_jvm->DetachCurrentThread();
        return "{\"error\":\"Failed to get service class\"}";
    }
    jmethodID mid = env->GetMethodID(cls, methodName, sig);
    if (!mid) {
        env->DeleteLocalRef(cls);
        if (attached) s_jvm->DetachCurrentThread();
        return "{\"error\":\"Method not found: " + std::string(methodName) + "\"}";
    }

    jstring jresult = (jstring) env->CallObjectMethod(s_service, mid, (jint)arg);

    std::string result;
    if (jresult) {
        const char* chars = env->GetStringUTFChars(jresult, nullptr);
        if (chars) {
            result = chars;
            env->ReleaseStringUTFChars(jresult, chars);
        }
        env->DeleteLocalRef(jresult);
    }
    env->DeleteLocalRef(cls);

    if (env->ExceptionCheck()) {
        env->ExceptionDescribe();
        env->ExceptionClear();
        if (result.empty()) result = "{\"error\":\"Java exception in " + std::string(methodName) + "\"}";
    }
    if (attached) s_jvm->DetachCurrentThread();
    return result;
}

// Generic JNI helper: call a Java method on s_service with no args, returns String
static std::string callJavaNoArgMethod(const char* methodName, const char* sig) {
    if (!s_jvm || !s_service) {
        return "{\"error\":\"JNI not initialized\"}";
    }
    JNIEnv* env = nullptr;
    bool attached = false;
    if (s_jvm->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK) {
        s_jvm->AttachCurrentThread(&env, nullptr);
        attached = true;
    }
    if (!env) return "{\"error\":\"Failed to get JNIEnv\"}";

    jclass cls = env->GetObjectClass(s_service);
    if (!cls) {
        if (attached) s_jvm->DetachCurrentThread();
        return "{\"error\":\"Failed to get service class\"}";
    }
    jmethodID mid = env->GetMethodID(cls, methodName, sig);
    if (!mid) {
        env->DeleteLocalRef(cls);
        if (attached) s_jvm->DetachCurrentThread();
        return "{\"error\":\"Method not found: " + std::string(methodName) + "\"}";
    }

    jstring jresult = (jstring) env->CallObjectMethod(s_service, mid);

    std::string result;
    if (jresult) {
        const char* chars = env->GetStringUTFChars(jresult, nullptr);
        if (chars) {
            result = chars;
            env->ReleaseStringUTFChars(jresult, chars);
        }
        env->DeleteLocalRef(jresult);
    }
    env->DeleteLocalRef(cls);

    if (env->ExceptionCheck()) {
        env->ExceptionDescribe();
        env->ExceptionClear();
        if (result.empty()) result = "{\"error\":\"Java exception in " + std::string(methodName) + "\"}";
    }
    if (attached) s_jvm->DetachCurrentThread();
    return result;
}

// ── Browser / Screen interaction platform functions ──

std::string openUrl(const std::string& url) {
    LOGI("openUrl: %s", url.c_str());
    return callJavaStringMethod("openUrlJava", "(Ljava/lang/String;)Ljava/lang/String;", url);
}

std::string getScreenText() {
    LOGI("getScreenText");
    return callJavaNoArgMethod("getScreenTextJava", "()Ljava/lang/String;");
}

std::string getActiveApp() {
    LOGI("getActiveApp");
    return callJavaNoArgMethod("getActiveAppJava", "()Ljava/lang/String;");
}

std::string clickText(const std::string& text) {
    LOGI("clickText: %s", text.c_str());
    return callJavaStringMethod("clickTextJava", "(Ljava/lang/String;)Ljava/lang/String;", text);
}

std::string typeText(const std::string& text) {
    LOGI("typeText: %s", text.c_str());
    return callJavaStringMethod("typeTextJava", "(Ljava/lang/String;)Ljava/lang/String;", text);
}

std::string scrollScreen(int direction) {
    LOGI("scrollScreen: %d", direction);
    return callJavaIntMethod("scrollScreenJava", "(I)Ljava/lang/String;", direction);
}

} // namespace argos
