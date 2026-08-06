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
#include <errno.h>
#include <string.h>

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
        return "[Error: Failed to parse URL: " + url + "]";
    }

    LOGI("HTTP %s %s:%d%s stream=%d", parts.protocol.c_str(), parts.host.c_str(), parts.port, parts.path.c_str(), stream);

    if (parts.protocol == "https") {
        LOGE("HTTPS not supported in raw socket mode. Use HTTP or link libcurl.");
        return "[Error: HTTPS not supported. Use HTTP URL.]";
    }

    // Resolve hostname using getaddrinfo (more reliable than gethostbyname on Android)
    struct addrinfo hints = {};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo* result = nullptr;
    int dnsErr = getaddrinfo(parts.host.c_str(), nullptr, &hints, &result);
    if (dnsErr != 0 || !result) {
        LOGE("DNS resolution failed for %s: %s", parts.host.c_str(), gai_strerror(dnsErr));
        return "[Error: DNS resolution failed for " + parts.host + "]";
    }

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        LOGE("Socket creation failed: %s", strerror(errno));
        freeaddrinfo(result);
        return "[Error: Socket creation failed]";
    }

    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(parts.port);
    memcpy(&addr.sin_addr, &((struct sockaddr_in*)result->ai_addr)->sin_addr, sizeof(struct in_addr));
    freeaddrinfo(result);

    // Set timeout (30s)
    struct timeval tv = {30, 0};
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    LOGI("Connecting to %s:%d...", parts.host.c_str(), parts.port);
    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        LOGE("Connection failed to %s:%d: %s", parts.host.c_str(), parts.port, strerror(errno));
        close(sock);
        return "[Error: Connection failed to " + parts.host + ":" + std::to_string(parts.port) + "]";
    }
    LOGI("Connected to %s:%d", parts.host.c_str(), parts.port);

    // Build HTTP request
    std::ostringstream req;
    req << "POST " << parts.path << " HTTP/1.1\r\n";
    req << "Host: " << parts.host << "\r\n";
    req << "Content-Type: application/json\r\n";
    req << "Content-Length: " << body.size() << "\r\n";
    req << "Connection: close\r\n";
    if (stream) {
        req << "Accept: text/event-stream\r\n";
    }
    req << headers;
    req << "\r\n" << body;

    std::string reqStr = req.str();
    LOGI("Sending request, len=%zu", reqStr.size());
    if (send(sock, reqStr.c_str(), reqStr.size(), 0) < 0) {
        LOGE("Send failed: %s", strerror(errno));
        close(sock);
        return "[Error: Send failed]";
    }
    LOGI("Request sent, waiting for response...");

    // Read response
    std::string fullBody;
    std::string accumulated;
    bool headersStripped = false;
    bool isChunked = false;
    int httpStatus = 0;
    char buf[8192];
    ssize_t n;
    while ((n = recv(sock, buf, sizeof(buf), 0)) > 0) {
        accumulated.append(buf, n);

        if (!headersStripped) {
            // Find end of HTTP headers
            size_t headerEnd = accumulated.find("\r\n\r\n");
            if (headerEnd != std::string::npos) {
                std::string respHeaders = accumulated.substr(0, headerEnd);
                LOGI("HTTP response headers: %s", respHeaders.c_str());

                // Parse HTTP status code from first line: "HTTP/1.1 200 OK"
                size_t spacePos = respHeaders.find(' ');
                if (spacePos != std::string::npos) {
                    httpStatus = atoi(respHeaders.c_str() + spacePos + 1);
                    LOGI("HTTP status code: %d", httpStatus);
                }

                isChunked = respHeaders.find("chunked") != std::string::npos;

                std::string bodyAfterHeaders = accumulated.substr(headerEnd + 4);
                accumulated = bodyAfterHeaders;
                headersStripped = true;

                if (!isChunked && stream && callback && !bodyAfterHeaders.empty()) {
                    if (!callback(bodyAfterHeaders)) {
                        close(sock);
                        return bodyAfterHeaders;
                    }
                }
                if (!stream) {
                    fullBody = bodyAfterHeaders;
                }
            }
        } else {
            // Headers already stripped
            if (!isChunked) {
                std::string chunk(buf, n);
                if (stream && callback) {
                    if (!callback(chunk)) break;
                }
                if (!stream) {
                    fullBody += chunk;
                }
            }
            // For chunked: data is already in accumulated
        }
    }
    
    if (n < 0) {
        LOGE("recv error: %s", strerror(errno));
    }
    close(sock);
    LOGI("Connection closed, total body bytes=%zu", headersStripped ? (isChunked ? accumulated.size() : fullBody.size()) : 0);

    // Check HTTP status
    if (httpStatus != 0 && (httpStatus < 200 || httpStatus >= 300)) {
        std::string errBody = isChunked ? accumulated : fullBody;
        LOGE("HTTP error %d, body: %s", httpStatus, errBody.substr(0, 500).c_str());
        return "[Error: HTTP " + std::to_string(httpStatus) + ": " + errBody.substr(0, 200) + "]";
    }

    if (isChunked) {
        // Decode chunked body from accumulated
        std::string decoded;
        size_t pos = 0;
        while (pos < accumulated.size()) {
            size_t lineEnd = accumulated.find("\r\n", pos);
            if (lineEnd == std::string::npos) break;
            std::string sizeStr = accumulated.substr(pos, lineEnd - pos);
            size_t semi = sizeStr.find(';');
            if (semi != std::string::npos) sizeStr = sizeStr.substr(0, semi);
            unsigned long chunkSize = strtoul(sizeStr.c_str(), nullptr, 16);
            if (chunkSize == 0) break;
            size_t dataStart = lineEnd + 2;
            if (dataStart + chunkSize > accumulated.size()) break;
            std::string chunkData = accumulated.substr(dataStart, chunkSize);
            decoded += chunkData;
            pos = dataStart + chunkSize + 2;

            // For streaming, send each decoded chunk to callback
            if (stream && callback && !chunkData.empty()) {
                if (!callback(chunkData)) break;
            }
        }
        LOGI("Chunked decoded, len=%zu", decoded.size());
        return decoded;
    }

    if (!stream) {
        if (!headersStripped) {
            // Headers never found — try to extract from accumulated
            size_t bodyStart = accumulated.find("\r\n\r\n");
            if (bodyStart != std::string::npos) {
                return accumulated.substr(bodyStart + 4);
            }
            LOGE("No HTTP headers found in response, raw: %s", accumulated.substr(0, 200).c_str());
            return "[Error: No HTTP headers in response]";
        }
        LOGI("Non-streaming response len=%zu", fullBody.size());
        return fullBody;
    }

    return accumulated;
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
