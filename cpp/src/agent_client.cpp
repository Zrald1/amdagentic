#include "agent_client.h"
#include <windows.h>
#include <winhttp.h>

#pragma comment(lib, "winhttp.lib")

AgentClient::AgentClient() {}

AgentClient::~AgentClient() {}

void AgentClient::SetServerUrl(const std::wstring& url) {
    m_serverUrl = url;
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

    std::wstring fullUrl = m_serverUrl + L"/health";
    WinHttpCrackUrl(fullUrl.c_str(), (DWORD)fullUrl.length(), 0, &urlComp);

    HINTERNET hSession = WinHttpOpen(L"Aria/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return false;

    HINTERNET hConnect = WinHttpConnect(hSession, hostName, urlComp.nPort, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); return false; }

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", urlPath,
                                             nullptr, WINHTTP_NO_REFERER,
                                             WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
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
    return L"[Agent client ready — connect to GPU server to enable tasks]";
}
