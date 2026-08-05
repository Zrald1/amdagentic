#pragma once
#include <string>

// HTTP client that talks to the AMD Radeon Developer API
// (OpenAI-compatible) using WinHTTP.
class AgentClient {
public:
    AgentClient();
    ~AgentClient();

    void SetServerUrl(const std::wstring& url);
    void SetApiKey(const std::wstring& key);
    void SetModel(const std::wstring& model);

    // Send a chat completion request (non-streaming).
    // Returns the assistant's response text.
    std::wstring Chat(const std::wstring& userMessage);

    // Health check — returns true if the server is reachable.
    bool IsServerAlive();

private:
    std::wstring m_serverUrl = L"https://developer.amd.com.cn/radeon/api/v1";
    std::wstring m_apiKey = L"rc-c042ad0acc56669f7b46e70f924189b5ac51664ce329f5b2";
    std::wstring m_model = L"Qwen3.6-35B-A3B";
};
