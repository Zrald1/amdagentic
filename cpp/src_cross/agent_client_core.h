#pragma once
#include <string>
#include <vector>
#include <functional>
#include <atomic>
#include "platform.h"

// Cross-platform chat message and agent client
// This is a platform-agnostic version of AgentClient that uses the platform abstraction.

struct ChatMessageCore {
    std::string role;    // "system", "user", or "assistant"
    std::string content;
};

using StreamCallbackCore = std::function<bool(const std::string& delta)>;

class AgentClientCore {
public:
    AgentClientCore();
    ~AgentClientCore();

    void setServerUrl(const std::string& url);
    void setApiKey(const std::string& key);
    void setModel(const std::string& model);

    std::string chat(const std::string& userMessage);
    std::string chatStreaming(const std::string& userMessage, StreamCallbackCore callback);

    void clearHistory();
    std::atomic<bool> m_abort{false};

private:
    std::string m_serverUrl = "https://developer.amd.com.cn/radeon/api/v1";
    std::string m_apiKey = "rc-c042ad0acc56669f7b46e70f924189b5ac51664ce329f5b2";
    std::string m_model = "DeepSeek-V4-Flash";
    std::string m_fallbackModel = "MiniCPM5-1B";
    std::string m_systemPrompt;
    std::vector<ChatMessageCore> m_history;

    void initSystemPrompt();
    std::string buildJsonBody(const std::vector<ChatMessageCore>& messages, bool stream);
    std::string parseSSEChunk(const std::string& chunk);
    bool hasToolTags(const std::string& response);
    std::string executeTools(const std::string& response);
    std::string stripToolTags(const std::string& response);
    std::string chatWithMessages(const std::vector<ChatMessageCore>& messages);
    std::string chatWithMessagesStreaming(const std::vector<ChatMessageCore>& messages,
                                           StreamCallbackCore callback);
};
