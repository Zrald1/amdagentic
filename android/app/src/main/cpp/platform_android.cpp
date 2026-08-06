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

// Use libcurl for HTTP on Android — but to avoid external deps, we use a simple
// raw socket implementation for the HTTP POST. For production, link libcurl.

#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sstream>

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

int64_t getTimeMs() {
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

// Simple HTTP client using raw sockets (no external deps)
// Supports HTTPS via proxy or plain HTTP. For HTTPS, we'd need to link against
// BoringSSL/OpenSSL. For now, this handles plain HTTP.
// The AI server URL can be configured to use HTTP on local network.

struct UrlParts {
    std::string protocol;
    std::string host;
    int port;
    std::string path;
};

static bool parseUrl(const std::string& url, UrlParts& out) {
    size_t protoEnd = url.find("://");
    if (protoEnd == std::string::npos) return false;
    out.protocol = url.substr(0, protoEnd);
    size_t hostStart = protoEnd + 3;
    size_t pathStart = url.find('/', hostStart);
    size_t portStart = url.find(':', hostStart);
    if (portStart != std::string::npos && (pathStart == std::string::npos || portStart < pathStart)) {
        out.host = url.substr(hostStart, portStart - hostStart);
        out.port = std::stoi(url.substr(portStart + 1, pathStart - portStart - 1));
    } else {
        out.port = (out.protocol == "https") ? 443 : 80;
        out.host = url.substr(hostStart, pathStart - hostStart);
    }
    out.path = (pathStart != std::string::npos) ? url.substr(pathStart) : "/";
    return true;
}

static std::string httpRequest(const std::string& url, const std::string& headers,
                               const std::string& body, bool stream,
                               std::function<bool(const std::string&)> callback) {
    UrlParts parts;
    if (!parseUrl(url, parts)) {
        LOGE("Failed to parse URL: %s", url.c_str());
        return "";
    }

    if (parts.protocol == "https") {
        LOGE("HTTPS not supported in raw socket mode. Use HTTP or link libcurl.");
        return "";
    }

    // Resolve hostname
    struct hostent* he = gethostbyname(parts.host.c_str());
    if (!he) {
        LOGE("DNS resolution failed for %s", parts.host.c_str());
        return "";
    }

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        LOGE("Socket creation failed");
        return "";
    }

    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(parts.port);
    memcpy(&addr.sin_addr, he->h_addr_list[0], he->h_length);

    // Set timeout (30s)
    struct timeval tv = {30, 0};
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        LOGE("Connection failed to %s:%d", parts.host.c_str(), parts.port);
        close(sock);
        return "";
    }

    // Build HTTP request
    std::ostringstream req;
    req << "POST " << parts.path << " HTTP/1.1\r\n";
    req << "Host: " << parts.host << "\r\n";
    req << "Content-Type: application/json\r\n";
    req << "Content-Length: " << body.size() << "\r\n";
    req << "Connection: close\r\n";
    req << headers;
    req << "\r\n" << body;

    std::string reqStr = req.str();
    if (send(sock, reqStr.c_str(), reqStr.size(), 0) < 0) {
        LOGE("Send failed");
        close(sock);
        return "";
    }

    // Read response
    std::string fullResponse;
    char buf[8192];
    ssize_t n;
    while ((n = recv(sock, buf, sizeof(buf), 0)) > 0) {
        fullResponse.append(buf, n);
        if (stream && callback) {
            if (!callback(std::string(buf, n))) break;
        }
    }
    close(sock);

    if (!stream) {
        // Extract body from HTTP response
        size_t bodyStart = fullResponse.find("\r\n\r\n");
        if (bodyStart != std::string::npos) {
            return fullResponse.substr(bodyStart + 4);
        }
        return fullResponse;
    }

    return fullResponse;
}

std::string httpPost(const std::string& url, const std::string& headers, const std::string& body) {
    return httpRequest(url, headers, body, false, nullptr);
}

std::string httpPostStream(const std::string& url, const std::string& headers,
                           const std::string& body,
                           std::function<bool(const std::string&)> callback) {
    return httpRequest(url, headers, body, true, callback);
}

} // namespace argos
