#include "agent_client.h"
#include <windows.h>
#include <winhttp.h>
#include <string>
#include <sstream>
#include <vector>

#pragma comment(lib, "winhttp.lib")

// Minimal JSON string escaper
static std::wstring JsonEscape(const std::wstring& s) {
    std::wstring out;
    out.reserve(s.size() + 8);
    for (wchar_t c : s) {
        switch (c) {
            case L'"':  out += L"\\\""; break;
            case L'\\': out += L"\\\\"; break;
            case L'\n': out += L"\\n";  break;
            case L'\r': out += L"\\r";  break;
            case L'\t': out += L"\\t";  break;
            default:
                if (c < 0x20) {
                    wchar_t buf[8];
                    swprintf_s(buf, L"\\u%04x", (unsigned)c);
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    return out;
}

// Extract the value for a given key from a flat JSON string.
// Handles nested quotes minimally — good enough for OpenAI-style responses.
static std::wstring ExtractJsonString(const std::string& json, const std::string& key) {
    std::string needle = "\"" + key + "\":\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return L"";
    pos += needle.size();
    std::string result;
    while (pos < json.size() && json[pos] != '"') {
        if (json[pos] == '\\' && pos + 1 < json.size()) {
            char next = json[pos + 1];
            if (next == 'n')      result += '\n';
            else if (next == 'r') result += '\r';
            else if (next == 't') result += '\t';
            else if (next == '"') result += '"';
            else if (next == '\\') result += '\\';
            else if (next == '/') result += '/';
            else                  result += next;
            pos += 2;
        } else {
            result += json[pos];
            pos++;
        }
    }
    // Convert UTF-8 to wide string
    std::wstring wide;
    int wlen = MultiByteToWideChar(CP_UTF8, 0, result.c_str(), (int)result.size(), nullptr, 0);
    if (wlen > 0) {
        wide.resize(wlen);
        MultiByteToWideChar(CP_UTF8, 0, result.c_str(), (int)result.size(), &wide[0], wlen);
    }
    return wide;
}

// Convert wide string to UTF-8
static std::string WideToUtf8(const std::wstring& ws) {
    std::string utf8;
    int len = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), nullptr, 0, nullptr, nullptr);
    if (len > 0) {
        utf8.resize(len);
        WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), &utf8[0], len, nullptr, nullptr);
    }
    return utf8;
}

AgentClient::AgentClient() {}

AgentClient::~AgentClient() {}

void AgentClient::SetServerUrl(const std::wstring& url) {
    m_serverUrl = url;
}

void AgentClient::SetApiKey(const std::wstring& key) {
    m_apiKey = key;
}

void AgentClient::SetModel(const std::wstring& model) {
    m_model = model;
}

bool AgentClient::IsServerAlive() {
    URL_COMPONENTS urlComp = {};
    urlComp.dwStructSize = sizeof(urlComp);
    wchar_t hostName[256] = {0};
    wchar_t urlPath[256] = {0};
    urlComp.lpszHostName = hostName;
    urlComp.dwHostNameLength = 255;
    urlComp.lpszUrlPath = urlPath;
    urlComp.dwUrlPathLength = 255;

    std::wstring fullUrl = m_serverUrl + L"/chat/completions";
    WinHttpCrackUrl(fullUrl.c_str(), (DWORD)fullUrl.length(), 0, &urlComp);

    HINTERNET hSession = WinHttpOpen(L"Aria/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return false;

    HINTERNET hConnect = WinHttpConnect(hSession, hostName, urlComp.nPort, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); return false; }

    DWORD flags = (urlComp.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", urlPath,
                                             nullptr, WINHTTP_NO_REFERER,
                                             WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!hRequest) { WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return false; }

    BOOL bResult = WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                       WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    if (bResult) {
        bResult = WinHttpReceiveResponse(hRequest, nullptr);
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    return bResult == TRUE;
}

std::wstring AgentClient::Chat(const std::wstring& userMessage) {
    // Build JSON body: {"model":"...","messages":[{"role":"user","content":"..."}]}
    std::wstring jsonBody = L"{\"model\":\"" + m_model +
        L"\",\"messages\":[{\"role\":\"user\",\"content\":\"" +
        JsonEscape(userMessage) + L"\"}]}";

    std::string bodyUtf8 = WideToUtf8(jsonBody);

    // Parse server URL
    URL_COMPONENTS urlComp = {};
    urlComp.dwStructSize = sizeof(urlComp);
    wchar_t hostName[256] = {0};
    wchar_t urlPath[512] = {0};
    urlComp.lpszHostName = hostName;
    urlComp.dwHostNameLength = 255;
    urlComp.lpszUrlPath = urlPath;
    urlComp.dwUrlPathLength = 511;

    std::wstring fullUrl = m_serverUrl + L"/chat/completions";
    if (!WinHttpCrackUrl(fullUrl.c_str(), (DWORD)fullUrl.length(), 0, &urlComp)) {
        return L"[Error: invalid server URL]";
    }

    HINTERNET hSession = WinHttpOpen(L"Aria/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return L"[Error: WinHttpOpen failed]";

    HINTERNET hConnect = WinHttpConnect(hSession, hostName, urlComp.nPort, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); return L"[Error: WinHttpConnect failed]"; }

    DWORD flags = (urlComp.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", urlPath,
                                             nullptr, WINHTTP_NO_REFERER,
                                             WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return L"[Error: WinHttpOpenRequest failed]";
    }

    // Build headers
    std::wstring headers = L"Authorization: Bearer " + m_apiKey + L"\r\n"
                           L"Content-Type: application/json\r\n";

    BOOL bResult = WinHttpSendRequest(hRequest, headers.c_str(), (DWORD)headers.size(),
                                       (LPVOID)bodyUtf8.data(), (DWORD)bodyUtf8.size(),
                                       (DWORD)bodyUtf8.size(), 0);
    if (!bResult) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return L"[Error: WinHttpSendRequest failed]";
    }

    bResult = WinHttpReceiveResponse(hRequest, nullptr);
    if (!bResult) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return L"[Error: WinHttpReceiveResponse failed]";
    }

    // Read response body
    std::string responseStr;
    DWORD bytesAvailable = 0;
    while (WinHttpQueryDataAvailable(hRequest, &bytesAvailable) && bytesAvailable > 0) {
        std::vector<char> buffer(bytesAvailable + 1, 0);
        DWORD bytesRead = 0;
        if (WinHttpReadData(hRequest, buffer.data(), bytesAvailable, &bytesRead) && bytesRead > 0) {
            responseStr.append(buffer.data(), bytesRead);
        } else {
            break;
        }
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    if (responseStr.empty()) {
        return L"[Error: empty response from server]";
    }

    // Extract choices[0].message.content from JSON
    // Find "content":" inside the first choice's message
    std::wstring content = ExtractJsonString(responseStr, "content");
    if (content.empty()) {
        // Try to extract error message
        std::wstring errMsg = ExtractJsonString(responseStr, "message");
        if (!errMsg.empty()) return L"[API Error: " + errMsg + L"]";
        return L"[Error: could not parse response]";
    }

    return content;
}
