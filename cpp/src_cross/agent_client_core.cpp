#include "agent_client_core.h"
#include "argos_tools_core.h"
#include <sstream>
#include <cstring>
#include <thread>
#include <chrono>

// Minimal JSON string escaper
static std::string jsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:   out += c;
        }
    }
    return out;
}

AgentClientCore::AgentClientCore() {
    initSystemPrompt();
}

AgentClientCore::~AgentClientCore() {}

void AgentClientCore::setServerUrl(const std::string& url) { m_serverUrl = url; }
void AgentClientCore::setApiKey(const std::string& key) { m_apiKey = key; }
void AgentClientCore::setModel(const std::string& model) { m_model = model; }

void AgentClientCore::clearHistory() { m_history.clear(); }

void AgentClientCore::initSystemPrompt() {
    m_systemPrompt =
        "You are Argos, named after the faithful dog of Odysseus. "
        "You are a loyal, vigilant AI companion that lives on the user's device. "
        "You have a golden Spartan helmet and red eyes. "
        "You are direct, concise, and protective. "
        "You speak with the loyalty and devotion of a faithful companion.\n\n"
        "=== TOOL USAGE — CRITICAL RULES ===\n"
        "1. To call a tool, include: [TOOL:tool_name argument]\n"
        "2. The tag MUST start with [TOOL: and end with ]\n"
        "3. NEVER say you will check something and then NOT include a tool tag.\n"
        "4. Do NOT invent tool names.\n\n"
        "=== AVAILABLE TOOLS ===\n"
        "1. [TOOL:list_files <dirpath>] — List files in a directory.\n"
        "2. [TOOL:cmd <command>] — Execute a shell command.\n"
        "3. [TOOL:read <filepath>] — Read file contents.\n"
        "4. [TOOL:search <query>] — Web search.\n"
        "5. [TOOL:screen_apps] — List open apps.\n"
        "6. [TOOL:recall] — Load conversation memory.\n"
        "7. [TOOL:rag_search <query>] — Search synced folders.\n"
        "8. [TOOL:search_files <dirpath>|<query>] — Search file content.\n\n"
        "Do NOT auto-trigger RAG on simple messages. Only use RAG tools when needed.\n"
        "If primary model is unavailable, fallback model is used automatically.";
}

std::string AgentClientCore::buildJsonBody(const std::vector<ChatMessageCore>& messages, bool stream) {
    std::string body = "{\"model\":\"" + m_model + "\",\"stream\":" + (stream ? "true" : "false") + ",\"messages\":[";
    for (size_t i = 0; i < messages.size(); i++) {
        if (i > 0) body += ",";
        body += "{\"role\":\"" + jsonEscape(messages[i].role) + "\",\"content\":\"" + jsonEscape(messages[i].content) + "\"}";
    }
    body += "]}";
    return body;
}

std::string AgentClientCore::parseSSEChunk(const std::string& chunk) {
    std::string result;
    size_t pos = 0;
    while (pos < chunk.size()) {
        size_t lineEnd = chunk.find('\n', pos);
        if (lineEnd == std::string::npos) lineEnd = chunk.size();
        std::string line = chunk.substr(pos, lineEnd - pos);
        pos = lineEnd + 1;

        if (line.substr(0, 6) == "data: ") {
            std::string data = line.substr(6);
            if (data == "[DONE]") break;
            // Find "content":"..."
            size_t contentPos = data.find("\"content\":\"");
            if (contentPos != std::string::npos) {
                contentPos += 11;
                size_t endQuote = data.find("\"", contentPos);
                if (endQuote != std::string::npos) {
                    std::string content = data.substr(contentPos, endQuote - contentPos);
                    // Unescape
                    std::string unescaped;
                    for (size_t i = 0; i < content.size(); i++) {
                        if (content[i] == '\\' && i + 1 < content.size()) {
                            char next = content[i + 1];
                            if (next == 'n') unescaped += '\n';
                            else if (next == 'r') unescaped += '\r';
                            else if (next == 't') unescaped += '\t';
                            else if (next == '"') unescaped += '"';
                            else if (next == '\\') unescaped += '\\';
                            else unescaped += content[i];
                            i++;
                        } else {
                            unescaped += content[i];
                        }
                    }
                    result += unescaped;
                }
            }
        }
    }
    return result;
}

bool AgentClientCore::hasToolTags(const std::string& response) {
    return response.find("[TOOL:") != std::string::npos;
}

std::string AgentClientCore::stripToolTags(const std::string& response) {
    std::string result = response;
    size_t pos = 0;
    while ((pos = result.find("[TOOL:", pos)) != std::string::npos) {
        size_t end = result.find(']', pos);
        if (end == std::string::npos) break;
        result.erase(pos, end - pos + 1);
    }
    // Also strip [Tool result:...] and [Tool Results]...
    pos = 0;
    while ((pos = result.find("[Tool ", pos)) != std::string::npos) {
        size_t end = result.find(']', pos);
        if (end == std::string::npos) break;
        result.erase(pos, end - pos + 1);
    }
    return result;
}

std::string AgentClientCore::executeTools(const std::string& response) {
    std::string results;
    size_t pos = 0;
    while ((pos = response.find("[TOOL:", pos)) != std::string::npos) {
        size_t end = response.find(']', pos);
        if (end == std::string::npos) break;
        std::string tag = response.substr(pos + 6, end - pos - 6);
        // Parse tool name and args
        size_t spacePos = tag.find(' ');
        std::string toolName = (spacePos != std::string::npos) ? tag.substr(0, spacePos) : tag;
        std::string toolArg = (spacePos != std::string::npos) ? tag.substr(spacePos + 1) : "";

        // Dispatch to tools core
        std::string result = argos_tools::dispatch_tool(toolName, toolArg);
        results += "[Tool result: " + toolName + "]\n" + result + "\n";
        pos = end + 1;
    }
    return results;
}

std::string AgentClientCore::chatWithMessages(const std::vector<ChatMessageCore>& messages) {
    std::string body = buildJsonBody(messages, false);
    std::string headers = "Authorization: Bearer " + m_apiKey + "\r\n";
    std::string response = argos::httpPost(m_serverUrl + "/chat/completions", headers, body);

    // Parse JSON response — find "content":"..."
    size_t contentPos = response.find("\"content\":\"");
    if (contentPos != std::string::npos) {
        contentPos += 11;
        size_t endQuote = response.find("\"", contentPos);
        if (endQuote != std::string::npos) {
            std::string content = response.substr(contentPos, endQuote - contentPos);
            // Unescape
            std::string unescaped;
            for (size_t i = 0; i < content.size(); i++) {
                if (content[i] == '\\' && i + 1 < content.size()) {
                    char next = content[i + 1];
                    if (next == 'n') unescaped += '\n';
                    else if (next == 't') unescaped += '\t';
                    else if (next == '"') unescaped += '"';
                    else if (next == '\\') unescaped += '\\';
                    else unescaped += content[i];
                    i++;
                } else {
                    unescaped += content[i];
                }
            }
            return unescaped;
        }
    }
    return "[Error: Failed to parse response]";
}

std::string AgentClientCore::chatWithMessagesStreaming(const std::vector<ChatMessageCore>& messages,
                                                        StreamCallbackCore callback) {
    std::string body = buildJsonBody(messages, true);
    std::string headers = "Authorization: Bearer " + m_apiKey + "\r\n";

    std::string fullResponse;
    std::string leftover;

    std::string rawResponse = argos::httpPostStream(m_serverUrl + "/chat/completions", headers, body,
        [&](const std::string& chunk) -> bool {
            if (m_abort.load()) return false;
            std::string data = leftover + chunk;
            std::string delta = parseSSEChunk(data);
            if (!delta.empty()) {
                fullResponse += delta;
                if (callback && !callback(delta)) return false;
            }
            // Keep incomplete lines
            size_t lastNewline = data.rfind('\n');
            if (lastNewline != std::string::npos) {
                leftover = data.substr(lastNewline + 1);
            } else {
                leftover = data;
            }
            return true;
        });

    if (fullResponse.empty()) {
        // Try non-streaming fallback
        return chatWithMessages(messages);
    }
    return fullResponse;
}

std::string AgentClientCore::chat(const std::string& userMessage) {
    m_abort.store(false);
    m_history.push_back({"user", userMessage});

    // Save to memory
    argos_tools::rag_memory_save_conversation("user", userMessage);

    std::string finalResponse;

    for (int iteration = 0; iteration < 8; iteration++) {
        if (m_abort.load()) break;

        std::vector<ChatMessageCore> messages;
        messages.push_back({"system", m_systemPrompt});
        for (const auto& msg : m_history) {
            messages.push_back(msg);
        }

        std::string response = chatWithMessages(messages);

        if (!hasToolTags(response)) {
            finalResponse = response;
            break;
        }

        std::string toolResults = executeTools(response);
        m_history.push_back({"assistant", response});
        m_history.push_back({"system", "[Tool Results]\n" + toolResults});
        m_history.push_back({"user", "Based on the tool results above, give me a clear answer."});
        finalResponse = stripToolTags(response);

        if (m_history.size() > 30) {
            m_history.erase(m_history.begin(), m_history.begin() + (m_history.size() - 30));
        }
    }

    m_history.push_back({"assistant", finalResponse});
    argos_tools::rag_memory_save_conversation("assistant", finalResponse);

    if (m_history.size() > 20) {
        m_history.erase(m_history.begin(), m_history.begin() + (m_history.size() - 20));
    }

    return finalResponse;
}

std::string AgentClientCore::chatStreaming(const std::string& userMessage, StreamCallbackCore callback) {
    m_abort.store(false);
    m_history.push_back({"user", userMessage});
    argos_tools::rag_memory_save_conversation("user", userMessage);

    std::string finalResponse;

    // Load memory on first message
    std::string memoryContext;
    if (m_history.size() <= 1) {
        std::string memory = argos_tools::rag_memory_load_conversation(5);
        if (memory.size() > 10 && memory.size() < 2000 && memory != "[]") {
            memoryContext = "[Recent conversation memory]\n" + memory + "\n[End of memory]";
        }
    }

    for (int iteration = 0; iteration < 8; iteration++) {
        if (m_abort.load()) break;

        std::vector<ChatMessageCore> messages;
        messages.push_back({"system", m_systemPrompt});
        if (iteration == 0 && !memoryContext.empty()) {
            messages.push_back({"system", memoryContext});
        }
        for (const auto& msg : m_history) {
            messages.push_back(msg);
        }

        if (iteration == 0) {
            finalResponse = chatWithMessagesStreaming(messages, callback);
        } else {
            finalResponse = chatWithMessages(messages);
        }

        if (!hasToolTags(finalResponse)) break;

        std::string toolResults = executeTools(finalResponse);
        m_history.push_back({"assistant", finalResponse});
        m_history.push_back({"system", "[Tool Results]\n" + toolResults});
        m_history.push_back({"user", "Based on the tool results above, give me a clear answer."});

        if (m_history.size() > 30) {
            m_history.erase(m_history.begin(), m_history.begin() + (m_history.size() - 30));
        }
    }

    m_history.push_back({"assistant", finalResponse});
    argos_tools::rag_memory_save_conversation("assistant", finalResponse);

    if (m_history.size() > 20) {
        m_history.erase(m_history.begin(), m_history.begin() + (m_history.size() - 20));
    }

    return finalResponse;
}
