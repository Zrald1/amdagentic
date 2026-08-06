#pragma once
#include <string>
#include <functional>

// Platform abstraction layer for Argos cross-platform support.
// Each platform (Windows, Android, iOS, macOS, Linux) implements these.

namespace argos {

// HTTP request (POST with JSON body). Returns response body or empty on error.
std::string httpPost(const std::string& url, const std::string& headers, const std::string& body);

// HTTP streaming request. Calls callback with each chunk. Returns full response.
std::string httpPostStream(const std::string& url, const std::string& headers,
                           const std::string& body,
                           std::function<bool(const std::string& chunk)> callback);

// Get app data directory for persistent storage
std::string getAppDataDir();

// Set app data directory (called by platform init)
void setAppDataDir(const char* dir);

// Log a message (platform-specific: OutputDebugString, __android_log_print, etc.)
void log(const char* message);

// Set JNI environment for HTTP requests (Android only — uses Java HttpURLConnection for HTTPS)
void setJniForHttp(void* jvm, void* service);

// ── Browser / Screen interaction (platform-specific) ──
// On Android these use Accessibility Service via JNI.
// On other platforms they may be stubs.

// Open a URL in the device's default browser
std::string openUrl(const std::string& url);

// Get all text content currently visible on screen (any app, including browser)
std::string getScreenText();

// Get the package name of the currently focused app
std::string getActiveApp();

// Click on the first element containing the given text
std::string clickText(const std::string& text);

// Type text into the currently focused input field
std::string typeText(const std::string& text);

// Scroll the current screen: direction 0=up, 1=down
std::string scrollScreen(int direction);

// Get current time in milliseconds
int64_t getTimeMs();

} // namespace argos
