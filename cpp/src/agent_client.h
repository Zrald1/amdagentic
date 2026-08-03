#pragma once
#include <string>

// HTTP client that talks to the GPU server's OpenAI-compatible API
// (llama-server) using WinHTTP. This is a thin client — all inference
// happens on the remote AMD Radeon GPU server.
class AgentClient {
public:
    AgentClient();
    ~AgentClient();

    void SetServerUrl(const std::wstring& url);

    // Send a chat completion request (non-streaming for now).
    // Returns the assistant's response text.
    std::wstring Chat(const std::wstring& userMessage);

    // Health check — returns true if the server is reachable.
    bool IsServerAlive();

private:
    std::wstring m_serverUrl = L"http://localhost:8080";
};
