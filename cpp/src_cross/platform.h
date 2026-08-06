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

// Get current time in milliseconds
int64_t getTimeMs();

} // namespace argos
