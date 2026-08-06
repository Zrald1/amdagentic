#include "agent_client_core.h"
#include "argos_tools_core.h"
#include "platform.h"
#include <sstream>
#include <cstring>
#include <thread>
#include <chrono>

// Find the closing quote of a JSON string value, skipping \" escaped quotes
static size_t findEndQuote(const std::string& s, size_t start) {
    size_t pos = start;
    while (pos < s.size()) {
        if (s[pos] == '\\') {
            pos += 2; // skip escaped char
            continue;
        }
        if (s[pos] == '"') return pos;
        pos++;
    }
    return std::string::npos;
}

// Extract "content" value from a JSON string, handling optional spaces and escaped quotes
// Also handles array format: "content":[{"type":"text","text":"..."}]
static std::string extractContent(const std::string& json) {
    // Try "content":"..." or "content": "..."
    size_t keyPos = json.find("\"content\"");
    while (keyPos != std::string::npos) {
        size_t colonPos = json.find(':', keyPos);
        if (colonPos == std::string::npos) break;
        
        // Skip whitespace after colon
        size_t valueStart = colonPos + 1;
        while (valueStart < json.size() && (json[valueStart] == ' ' || json[valueStart] == '\t')) {
            valueStart++;
        }
        
        if (valueStart >= json.size()) break;
        
        if (json[valueStart] == '"') {
            // String value: "content":"..."
            size_t contentStart = valueStart + 1;
            size_t endQuote = findEndQuote(json, contentStart);
            if (endQuote != std::string::npos) {
                std::string content = json.substr(contentStart, endQuote - contentStart);
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
                        else if (next == '/') unescaped += '/';
                        else unescaped += content[i];
                        i++;
                    } else {
                        unescaped += content[i];
                    }
                }
                return unescaped;
            }
        } else if (json[valueStart] == '[') {
            // Array format: "content":[{"type":"text","text":"..."}]
            // Extract text from first object
            size_t textKey = json.find("\"text\"", valueStart);
            if (textKey != std::string::npos) {
                size_t textColon = json.find(':', textKey);
                if (textColon != std::string::npos) {
                    size_t textStart = textColon + 1;
                    while (textStart < json.size() && (json[textStart] == ' ' || json[textStart] == '\t')) {
                        textStart++;
                    }
                    if (textStart < json.size() && json[textStart] == '"') {
                        size_t contentStart = textStart + 1;
                        size_t endQuote = findEndQuote(json, contentStart);
                        if (endQuote != std::string::npos) {
                            std::string content = json.substr(contentStart, endQuote - contentStart);
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
                                    else if (next == '/') unescaped += '/';
                                    else unescaped += content[i];
                                    i++;
                                } else {
                                    unescaped += content[i];
                                }
                            }
                            return unescaped;
                        }
                    }
                }
            }
        }
        
        // Look for next occurrence of "content"
        keyPos = json.find("\"content\"", keyPos + 9);
    }
    return "";
}

// Extract "reasoning_content" value from a JSON string (DeepSeek reasoning model)
// Uses same logic as extractContent but searches for "reasoning_content" key
static std::string extractReasoningContent(const std::string& json) {
    size_t keyPos = json.find("\"reasoning_content\"");
    while (keyPos != std::string::npos) {
        size_t colonPos = json.find(':', keyPos);
        if (colonPos == std::string::npos) break;
        
        size_t valueStart = colonPos + 1;
        while (valueStart < json.size() && (json[valueStart] == ' ' || json[valueStart] == '\t')) {
            valueStart++;
        }
        
        if (valueStart >= json.size()) break;
        
        // Check for null: "reasoning_content":null
        if (json[valueStart] == 'n' && valueStart + 4 <= json.size() &&
            json.substr(valueStart, 4) == "null") {
            return "";
        }
        
        if (json[valueStart] == '"') {
            size_t contentStart = valueStart + 1;
            size_t endQuote = findEndQuote(json, contentStart);
            if (endQuote != std::string::npos) {
                std::string content = json.substr(contentStart, endQuote - contentStart);
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
                        else if (next == '/') unescaped += '/';
                        else unescaped += content[i];
                        i++;
                    } else {
                        unescaped += content[i];
                    }
                }
                return unescaped;
            }
        }
        
        keyPos = json.find("\"reasoning_content\"", keyPos + 18);
    }
    return "";
}

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
        "4. [TOOL:recall] — Load conversation memory.\n"
        "5. [TOOL:forget] — Clear conversation memory.\n\n"
        "Do NOT auto-trigger tools on simple messages. Only use tools when needed.\n"
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

std::string AgentClientCore::parseSSEChunk(const std::string& chunk, std::string* thoughts) {
    std::string result;
    std::string thoughtsAccum;
    size_t pos = 0;
    while (pos < chunk.size()) {
        size_t lineEnd = chunk.find('\n', pos);
        if (lineEnd == std::string::npos) lineEnd = chunk.size();
        std::string line = chunk.substr(pos, lineEnd - pos);
        // Trim \r for \r\n line endings
        if (!line.empty() && line.back() == '\r') line.pop_back();
        pos = lineEnd + 1;

        // Handle "data:" or "data: " (with optional space)
        if (line.substr(0, 5) == "data:") {
            size_t dataStart = 5;
            if (dataStart < line.size() && line[dataStart] == ' ') dataStart++;
            std::string data = line.substr(dataStart);
            
            if (data == "[DONE]") break;
            if (data.empty()) continue;
            
            // Extract reasoning content (thoughts) if requested
            if (thoughts) {
                std::string reasoning = extractReasoningContent(data);
                if (!reasoning.empty()) {
                    thoughtsAccum += reasoning;
                }
            }
            
            std::string content = extractContent(data);
            if (!content.empty()) {
                result += content;
            }
        }
    }
    if (thoughts) *thoughts = thoughtsAccum;
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
    std::string url = m_serverUrl + "/chat/completions";
    argos::log(("POST " + url + " body_len=" + std::to_string(body.size())).c_str());
    std::string response = argos::httpPost(url, headers, body);
    argos::log(("HTTP response len=" + std::to_string(response.size())).c_str());
    if (response.size() > 0) {
        std::string preview = response.substr(0, response.size() > 500 ? 500 : response.size());
        argos::log(("Response: " + preview).c_str());
    }

    std::string content = extractContent(response);
    if (!content.empty()) {
        return content;
    }
    
    argos::log(("Failed to parse response, full response: " + response.substr(0, response.size() > 1000 ? 1000 : response.size())).c_str());
    return "[Error: Failed to parse response]";
}

std::string AgentClientCore::chatWithMessagesStreaming(const std::vector<ChatMessageCore>& messages,
                                                        StreamCallbackCore callback,
                                                        ThoughtsCallbackCore thoughtsCallback) {
    std::string body = buildJsonBody(messages, true);
    std::string headers = "Authorization: Bearer " + m_apiKey + "\r\n";
    std::string url = m_serverUrl + "/chat/completions";
    argos::log(("POST streaming " + url).c_str());

    std::string fullResponse;
    std::string fullThoughts;
    std::string leftover;

    std::string rawResponse = argos::httpPostStream(url, headers, body,
        [&](const std::string& chunk) -> bool {
            if (m_abort.load()) return false;
            std::string data = leftover + chunk;
            std::string thoughts;
            std::string delta = parseSSEChunk(data, &thoughts);
            if (!thoughts.empty()) {
                fullThoughts += thoughts;
                if (thoughtsCallback) thoughtsCallback(fullThoughts);
            }
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

    if (!rawResponse.empty() && rawResponse.find("[Error:") == 0) {
        argos::log(("HTTP error in streaming: " + rawResponse.substr(0, 200)).c_str());
        return rawResponse;
    }

    if (fullResponse.empty()) {
        argos::log("Streaming response empty, falling back to non-streaming");
        return chatWithMessages(messages);
    }
    argos::log(("Streaming done, response len=" + std::to_string(fullResponse.size()) +
                " thoughts len=" + std::to_string(fullThoughts.size())).c_str());
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

std::string AgentClientCore::chatStreaming(const std::string& userMessage, StreamCallbackCore callback,
                              ThoughtsCallbackCore thoughtsCallback) {
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
            finalResponse = chatWithMessagesStreaming(messages, callback, thoughtsCallback);
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
