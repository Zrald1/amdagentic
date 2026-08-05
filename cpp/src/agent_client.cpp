#include "agent_client.h"
#include "argos_tools.h"
#include <windows.h>
#include <winhttp.h>
#include <string>
#include <sstream>
#include <vector>
#include <fstream>
#include <shellapi.h>

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

AgentClient::AgentClient() {
    InitSystemPrompt();
}

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

void AgentClient::SetFallbackModel(const std::wstring& model) {
    m_fallbackModel = model;
}

void AgentClient::SetVisionModel(const std::wstring& model) {
    m_visionModel = model;
}

bool AgentClient::IsVisionTool(const std::wstring& toolName) {
    return toolName == L"screen_ocr" ||
           toolName == L"screen_capture" ||
           toolName == L"browser_screenshot" ||
           toolName == L"screenshot";
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

    HINTERNET hSession = WinHttpOpen(L"Argos/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
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

void AgentClient::InitSystemPrompt() {
    m_systemPrompt =
        L"You are Argos, named after the faithful dog of Odysseus. "
        L"You are a loyal, vigilant AI companion that lives on the user's desktop. "
        L"You are always faithful, always watching, always ready to help. "
        L"You have a golden Spartan helmet and red eyes. "
        L"You are direct, concise, and protective. "
        L"You speak with the loyalty and devotion of a faithful companion. "
        L"\n\n=== LOCAL KNOWLEDGE RETRIEVAL (RAG) ==="
        L"\nYou have a modern RAG (Retrieval-Augmented Generation) pipeline built in. "
        L"When the user asks a question, the system automatically searches their local project files "
        L"using a hybrid retrieval architecture:\n"
        L"  1. TF-IDF cosine similarity (dense retrieval)\n"
        L"  2. BM25 keyword search (sparse retrieval)\n"
        L"  3. Reciprocal Rank Fusion (RRF) to combine both result lists\n"
        L"  4. Reranker for precision boost\n"
        L"  5. Hierarchical chunking (returns parent context around matches)\n"
        L"  6. Contextual chunking (file path + language metadata prepended)\n"
        L"Relevant text passages are injected as [Local Knowledge Context] before your response. "
        L"Use this context to give accurate, project-aware answers. "
        L"If the RAG context is relevant, reference it naturally. "
        L"If no relevant context was found, answer based on your general knowledge."
        L"\n\n=== PERSISTENT MEMORY ==="
        L"\nAll conversations are saved to persistent memory (JSONL file in %APPDATA%/Argos/). "
        L"This means you can reference previous conversations across sessions. "
        L"If the user asks about something you discussed before, check your memory."
        L"\n\n=== PERMISSION CONTROL ==="
        L"\nDestructive tools (write, run, lock) require user permission before execution. "
        L"The user will be shown a confirmation dialog before these tools run. "
        L"If the user denies permission, respect their decision and suggest alternatives."
        L"\n\n=== PRIVACY ==="
        L"\nScreen context is automatically filtered to redact sensitive data (passwords, tokens, "
        L"credit card numbers) before you see it. This protects the user's privacy."
        L"\n\nYou have the following tools available. "
        L"To use a tool, include a [TOOL:...] tag in your response:\n"
        L"\n--- System Tools ---\n"
        L"1. [TOOL:open <path>] — Open a file, folder, or URL in the default application.\n"
        L"2. [TOOL:run <command>] — Execute a system command (e.g. notepad, calc, explorer).\n"
        L"3. [TOOL:read <filepath>] — Read the contents of a text file.\n"
        L"4. [TOOL:write <filepath> | <content>] — Write content to a file.\n"
        L"5. [TOOL:search <query>] — Open the browser and search the web.\n"
        L"6. [TOOL:volume <level>] — Set system volume (0-100).\n"
        L"7. [TOOL:screenshot] — Take a screenshot of the desktop.\n"
        L"8. [TOOL:lock] — Lock the workstation.\n"
        L"9. [TOOL:notify <message>] — Show a Windows notification balloon.\n"
        L"10. [TOOL:clipboard <text>] — Copy text to clipboard.\n"
        L"\n--- AI Search Tools (RAG-style file indexing & retrieval) ---\n"
        L"11. [TOOL:index <dirpath>] — Index a directory: scan files, index text content (TF-IDF), fingerprint images. Returns JSON stats.\n"
        L"12. [TOOL:search_files <dirpath>|<query>] — Search indexed text content with cosine similarity ranking. Returns JSON results.\n"
        L"13. [TOOL:search_filename <dirpath>|<pattern>] — Search for files by filename pattern. Returns JSON results.\n"
        L"14. [TOOL:full_map <dirpath>] — Complete JSON index of files + content for AI consumption.\n"
        L"15. [TOOL:stats <dirpath>] — Get statistics about an indexed directory.\n"
        L"\n--- Browser Automation Tools ---\n"
        L"16. [TOOL:browser_navigate <url>] — Navigate the browser to a URL.\n"
        L"17. [TOOL:browser_content] — Get the current page's text content.\n"
        L"18. [TOOL:browser_title] — Get the current page title.\n"
        L"19. [TOOL:browser_url] — Get the current page URL.\n"
        L"20. [TOOL:browser_find <text>] — Find elements on the page by text content.\n"
        L"21. [TOOL:browser_click <element_id>] — Click an element by its ID.\n"
        L"22. [TOOL:browser_type <element_id>|<text>] — Type text into an input element.\n"
        L"23. [TOOL:browser_screenshot] — Take a screenshot of the browser page.\n"
        L"24. [TOOL:browser_links] — Get all links on the current page.\n"
        L"25. [TOOL:browser_summarize] — Get an AI summary of the current page.\n"
        L"\n--- Screen Context Tools (see what's on screen) ---\n"
        L"26. [TOOL:screen_apps] — List all open application windows.\n"
        L"27. [TOOL:screen_active] — Get the active/focused application info.\n"
        L"28. [TOOL:screen_capture <output_path>] — Capture the entire screen as an image.\n"
        L"29. [TOOL:screen_ocr] — Extract text from the screen using OCR.\n"
        L"30. [TOOL:screen_context] — Get an assessment of what the user is currently doing.\n"
        L"31. [TOOL:screen_search <query>] — Search for content visible on screen.\n"
        L"32. [TOOL:screen_summary] — Get a summary of open apps by category.\n"
        L"\n--- UI Locator Tools (find and interact with UI elements) ---\n"
        L"33. [TOOL:ui_windows] — List all open windows.\n"
        L"34. [TOOL:ui_elements <window_id>] — Get all UI elements in a window.\n"
        L"35. [TOOL:ui_search <text>] — Search for UI elements by text label.\n"
        L"36. [TOOL:ui_clickable <text_filter>] — Search for clickable elements (buttons, links).\n"
        L"37. [TOOL:ui_click_at <x>,<y>] — Click at specific screen coordinates.\n"
        L"38. [TOOL:ui_click <element_id>] — Click a UI element by its ID.\n"
        L"39. [TOOL:ui_type <text>] — Type text into the focused element.\n"
        L"40. [TOOL:ui_focus <window_id>] — Focus a specific window.\n"
        L"41. [TOOL:ui_close <window_id>] — Close a specific window.\n"
        L"42. [TOOL:ui_map] — Export the full UI element map as JSON.\n"
        L"\nUse tools naturally when the user asks you to do something on their computer. "
        L"Always explain what you're doing briefly, then include the tool tag. "
        L"For example: 'Opening Notepad for you. [TOOL:run notepad]' "
        L"or 'Searching your project files for that. [TOOL:search_files C:\\projects|error handling]'\n"
        L"\n--- Task Planning ---\n"
        L"For complex multi-step requests, start with a plan:\n"
        L"[PLAN: step 1 description | step 2 description | step 3 description]\n"
        L"Then execute each step in sequence, marking progress with [STEP: N/M: description].\n"
        L"Example: [PLAN: Search for files | Read the main file | Summarize the architecture]\n"
        L"The user will see your plan and step-by-step progress.\n"
        L"\n--- Vision/OCR ---\n"
        L"When you use screen_ocr, screen_capture, or screenshot tools, the result is processed\n"
        L"with a dedicated vision model (Qwen3.6-35B-A3B) for image understanding.\n"
        L"For text-based tasks you use DeepSeek-V4-Flash (fast and efficient).\n"
        L"If the primary model is unavailable, a fallback model (MiniCPM5-1B) is used automatically.";
}

void AgentClient::ClearHistory() {
    m_history.clear();
}

std::wstring AgentClient::Chat(const std::wstring& userMessage) {
    try {
    // Add user message to history
    m_history.push_back({L"user", userMessage});

    // ── Automatic RAG: search local files for relevant context ──
    // Uses modern hybrid pipeline: TF-IDF + BM25 + RRF fusion + reranking + contextual chunking
    // Also saves conversation to persistent memory (JSONL file in %APPDATA%/Argos/)
    std::string utf8Query = WideToUtf8(userMessage);
    std::string ragContext = argos_tools::rag_search_with_memory(utf8Query, "", 5);

    // If RAG found relevant content, inject it as a system context message
    // before the user's question (only if results are meaningful)
    if (ragContext.find("No relevant files") == std::string::npos &&
        ragContext.find("RAG search error") == std::string::npos &&
        ragContext.size() > 50) {
        // Convert RAG context to wide string
        int wlen = MultiByteToWideChar(CP_UTF8, 0, ragContext.c_str(),
                                       (int)ragContext.size(), nullptr, 0);
        if (wlen > 0) {
            std::wstring wRagContext(wlen, 0);
            MultiByteToWideChar(CP_UTF8, 0, ragContext.c_str(),
                               (int)ragContext.size(), &wRagContext[0], wlen);
            // Add as a system context message before the user message
            m_history.insert(m_history.end() - 1, {L"system",
                L"[Local Knowledge Context — Retrieved via RAG from your project files]\n" +
                wRagContext +
                L"\n[End of RAG Context] Use this information to help answer the user's question if relevant."});
        }
    }

    std::wstring finalResponse;

    // Tool loop: AI may call tools, we execute them and send results back
    for (int iteration = 0; iteration < 5; iteration++) {
        // Build full message list: system prompt + history
        std::vector<ChatMessage> messages;
        messages.push_back({L"system", m_systemPrompt});
        for (const auto& msg : m_history) {
            messages.push_back(msg);
        }

        std::wstring response = ChatWithMessages(messages);

        // Check if the response contains tool calls
        if (!HasToolTags(response)) {
            // No tools — this is the final answer
            finalResponse = response;
            break;
        }

        // Execute the tools and collect results
        std::wstring toolResults = ExecuteTools(response);

        // Show the user what Argos is doing (clean text without tool tags)
        std::wstring cleanResponse = StripToolTags(response);

        // Add the assistant's intermediate response to history
        m_history.push_back({L"assistant", response});

        // Add tool results as a user message so the AI can use them
        std::wstring toolFeedback =
            L"Tool execution results:\n" + toolResults +
            L"\n\nBased on these results, please give the user a clear, natural answer. "
            L"Do NOT repeat the raw data. Just tell them what they asked in a friendly way.";
        m_history.push_back({L"user", toolFeedback});

        // Save the clean intermediate response as the final for now
        // (will be overwritten if the next iteration gives a better answer)
        finalResponse = cleanResponse;

        // Keep history manageable
        if (m_history.size() > 30) {
            m_history.erase(m_history.begin(), m_history.begin() + (m_history.size() - 30));
        }

        // Loop continues — AI will get tool results and give final answer
    }

    // Add final response to history
    m_history.push_back({L"assistant", finalResponse});

    // Save AI response to persistent memory
    argos_tools::rag_memory_save_conversation("assistant", WideToUtf8(finalResponse));

    // Keep history manageable (last 20 messages + system)
    if (m_history.size() > 20) {
        m_history.erase(m_history.begin(), m_history.begin() + (m_history.size() - 20));
    }

    return finalResponse;
    } catch (...) {
        return L"[Error: chat failed]";
    }
}

std::wstring AgentClient::ChatWithMessages(const std::vector<ChatMessage>& messages) {
    try {
        // Try primary model first
        std::wstring result = ChatWithModel(messages, m_model);

        // If primary failed, try fallback model
        if (result.find(L"[Error:") == 0 || result.find(L"[API Error:") == 0) {
            // Primary model failed — try fallback
            result = ChatWithModel(messages, m_fallbackModel);
        }

        return result;
    } catch (...) {
        return L"[Error: chat request failed]";
    }
}

std::wstring AgentClient::ChatWithModel(const std::vector<ChatMessage>& messages, const std::wstring& model) {
    // Build JSON body with full message array
    std::wstring jsonBody = L"{\"model\":\"" + model + L"\",\"messages\":[";
    for (size_t i = 0; i < messages.size(); i++) {
        if (i > 0) jsonBody += L",";
        jsonBody += L"{\"role\":\"" + messages[i].role + L"\",\"content\":\"" +
                     JsonEscape(messages[i].content) + L"\"}";
    }
    jsonBody += L"]}";

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

    HINTERNET hSession = WinHttpOpen(L"Argos/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return L"[Error: WinHttpOpen failed]";

    // Set timeouts: 10s connect, 30s receive, 30s resolve
    WinHttpSetTimeouts(hSession, 30000, 10000, 30000, 30000);

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

    // Retry up to 3 times on network failure
    BOOL bResult = FALSE;
    for (int attempt = 0; attempt < 3; attempt++) {
        bResult = WinHttpSendRequest(hRequest, headers.c_str(), (DWORD)headers.size(),
                                       (LPVOID)bodyUtf8.data(), (DWORD)bodyUtf8.size(),
                                       (DWORD)bodyUtf8.size(), 0);
        if (!bResult) {
            Sleep(1000 * (attempt + 1)); // wait before retry
            continue;
        }

        bResult = WinHttpReceiveResponse(hRequest, nullptr);
        if (bResult) break; // success

        Sleep(1000 * (attempt + 1)); // wait before retry
    }

    if (!bResult) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return L"[Error: Could not reach AI server after 3 attempts. Check your connection.]";
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
    std::wstring content = ExtractJsonString(responseStr, "content");
    if (content.empty()) {
        // Try to extract error message
        std::wstring errMsg = ExtractJsonString(responseStr, "message");
        if (!errMsg.empty()) return L"[API Error: " + errMsg + L"]";
        return L"[Error: could not parse response]";
    }

    return content;
}

// ── SSE Streaming Implementation ──

// Parse a single SSE data line and extract content delta
std::wstring AgentClient::ParseSSEChunk(const std::string& chunk) {
    // SSE format: "data: {json}\n\n"
    // We look for "data: " prefix, parse JSON, extract choices[0].delta.content
    std::wstring result;

    size_t pos = 0;
    while (pos < chunk.size()) {
        // Find "data: " prefix
        size_t dataPos = chunk.find("data: ", pos);
        if (dataPos == std::string::npos) break;

        size_t lineStart = dataPos + 6; // skip "data: "
        size_t lineEnd = chunk.find('\n', lineStart);
        if (lineEnd == std::string::npos) lineEnd = chunk.size();

        std::string jsonLine = chunk.substr(lineStart, lineEnd - lineStart);

        // Check for [DONE] marker
        if (jsonLine.find("[DONE]") != std::string::npos) break;

        // Extract "content":"..." from the JSON delta
        // Look for "delta":{"content":"..." or "content":"..."
        size_t contentPos = jsonLine.find("\"content\":\"");
        if (contentPos != std::string::npos) {
            size_t valStart = contentPos + 11;
            size_t valEnd = valStart;
            // Parse with escape handling
            while (valEnd < jsonLine.size()) {
                if (jsonLine[valEnd] == '\\' && valEnd + 1 < jsonLine.size()) {
                    valEnd += 2;
                } else if (jsonLine[valEnd] == '"') {
                    break;
                } else {
                    valEnd++;
                }
            }
            if (valEnd <= jsonLine.size()) {
                std::string contentStr = jsonLine.substr(valStart, valEnd - valStart);
                // Unescape JSON string
                std::string unescaped;
                for (size_t i = 0; i < contentStr.size(); i++) {
                    if (contentStr[i] == '\\' && i + 1 < contentStr.size()) {
                        char next = contentStr[i + 1];
                        if (next == 'n') unescaped += '\n';
                        else if (next == 'r') unescaped += '\r';
                        else if (next == 't') unescaped += '\t';
                        else if (next == '"') unescaped += '"';
                        else if (next == '\\') unescaped += '\\';
                        else if (next == '/') unescaped += '/';
                        else { unescaped += '\\'; unescaped += next; }
                        i++;
                    } else {
                        unescaped += contentStr[i];
                    }
                }
                // Convert to wide string
                int wlen = MultiByteToWideChar(CP_UTF8, 0, unescaped.c_str(),
                                               (int)unescaped.size(), nullptr, 0);
                if (wlen > 0) {
                    std::wstring wstr(wlen, 0);
                    MultiByteToWideChar(CP_UTF8, 0, unescaped.c_str(),
                                       (int)unescaped.size(), &wstr[0], wlen);
                    result += wstr;
                }
            }
        }

        pos = lineEnd + 1;
    }

    return result;
}

// Streaming version of ChatWithMessages — sends with stream:true, parses SSE chunks
// Tries primary model, falls back to fallback model on failure
std::wstring AgentClient::ChatWithMessagesStreaming(const std::vector<ChatMessage>& messages, StreamCallback callback) {
    m_abort.store(false);

    try {
        // Try primary model first
        std::wstring result = ChatWithMessagesStreamingModel(messages, callback, m_model);

        // If primary failed, try fallback model
        if (result.find(L"[Error:") == 0 || result.find(L"[API Error:") == 0) {
            result = ChatWithMessagesStreamingModel(messages, callback, m_fallbackModel);
        }

        return result;
    } catch (...) {
        return L"[Error: streaming chat request failed]";
    }
}

// Internal: streaming chat with a specific model
std::wstring AgentClient::ChatWithMessagesStreamingModel(const std::vector<ChatMessage>& messages, StreamCallback callback, const std::wstring& model) {
    // Build JSON body with stream:true
    std::wstring jsonBody = L"{\"model\":\"" + model + L"\",\"stream\":true,\"messages\":[";
    for (size_t i = 0; i < messages.size(); i++) {
        if (i > 0) jsonBody += L",";
        jsonBody += L"{\"role\":\"" + messages[i].role + L"\",\"content\":\"" +
                     JsonEscape(messages[i].content) + L"\"}";
    }
    jsonBody += L"]}";

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

    HINTERNET hSession = WinHttpOpen(L"Argos/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return L"[Error: WinHttpOpen failed]";

    WinHttpSetTimeouts(hSession, 30000, 10000, 120000, 120000);

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

    std::wstring headers = L"Authorization: Bearer " + m_apiKey + L"\r\n"
                           L"Content-Type: application/json\r\n";

    BOOL bResult = WinHttpSendRequest(hRequest, headers.c_str(), (DWORD)headers.size(),
                                       (LPVOID)bodyUtf8.data(), (DWORD)bodyUtf8.size(),
                                       (DWORD)bodyUtf8.size(), 0);
    if (!bResult) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return L"[Error: Could not send request]";
    }

    bResult = WinHttpReceiveResponse(hRequest, nullptr);
    if (!bResult) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return L"[Error: No response from server]";
    }

    // Read SSE stream chunk by chunk
    std::wstring fullResponse;
    std::string leftover; // incomplete SSE lines from previous chunk

    DWORD bytesAvailable = 0;
    while (WinHttpQueryDataAvailable(hRequest, &bytesAvailable) && bytesAvailable > 0) {
        if (m_abort.load()) break;

        std::vector<char> buffer(bytesAvailable + 1, 0);
        DWORD bytesRead = 0;
        if (WinHttpReadData(hRequest, buffer.data(), bytesAvailable, &bytesRead) && bytesRead > 0) {
            std::string chunkData(buffer.data(), bytesRead);

            // Prepend any leftover from previous chunk
            chunkData = leftover + chunkData;

            // Split into complete lines (ending with \n)
            std::string completeData;
            size_t lastNewline = chunkData.rfind('\n');
            if (lastNewline != std::string::npos) {
                completeData = chunkData.substr(0, lastNewline + 1);
                leftover = chunkData.substr(lastNewline + 1);
            } else {
                leftover = chunkData;
                continue;
            }

            // Parse SSE chunks and extract content deltas
            std::wstring delta = ParseSSEChunk(completeData);
            if (!delta.empty()) {
                fullResponse += delta;
                if (callback && !callback(delta)) {
                    break; // user aborted
                }
            }
        } else {
            break;
        }
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    // If streaming returned nothing, fall back to non-streaming
    if (fullResponse.empty()) {
        return L"[Error: empty streaming response]";
    }

    return fullResponse;
}

// Streaming chat with full conversation context + tool loop
std::wstring AgentClient::ChatStreaming(const std::wstring& userMessage, StreamCallback callback) {
    m_abort.store(false);

    try {
    // Add user message to history
    m_history.push_back({L"user", userMessage});

    // Automatic RAG with memory
    std::string utf8Query = WideToUtf8(userMessage);
    std::string ragContext = argos_tools::rag_search_with_memory(utf8Query, "", 5);

    if (ragContext.find("No relevant files") == std::string::npos &&
        ragContext.find("RAG search error") == std::string::npos &&
        ragContext.size() > 50) {
        int wlen = MultiByteToWideChar(CP_UTF8, 0, ragContext.c_str(),
                                       (int)ragContext.size(), nullptr, 0);
        if (wlen > 0) {
            std::wstring wRagContext(wlen, 0);
            MultiByteToWideChar(CP_UTF8, 0, ragContext.c_str(),
                               (int)ragContext.size(), &wRagContext[0], wlen);
            m_history.insert(m_history.end() - 1, {L"system",
                L"[Local Knowledge Context — Retrieved via RAG from your project files]\n" +
                wRagContext +
                L"\n[End of RAG Context] Use this information to help answer the user's question if relevant."});
        }
    }

    std::wstring finalResponse;

    // Tool loop with streaming
    for (int iteration = 0; iteration < 5; iteration++) {
        if (m_abort.load()) break;

        std::vector<ChatMessage> messages;
        messages.push_back({L"system", m_systemPrompt});
        for (const auto& msg : m_history) {
            messages.push_back(msg);
        }

        // Use streaming for the first iteration, non-streaming for tool iterations
        // (tool iterations are internal, no need to stream intermediate steps)
        if (iteration == 0) {
            finalResponse = ChatWithMessagesStreaming(messages, callback);
        } else {
            finalResponse = ChatWithMessages(messages);
        }

        if (!HasToolTags(finalResponse)) {
            break;
        }

        std::wstring toolResults = ExecuteTools(finalResponse);
        m_history.push_back({L"assistant", finalResponse});
        m_history.push_back({L"system", L"[Tool Results]\n" + toolResults});

        if (m_history.size() > 30) {
            m_history.erase(m_history.begin(), m_history.begin() + (m_history.size() - 30));
        }
    }

    // Clean up RAG context from history
    auto it = std::remove_if(m_history.begin(), m_history.end(),
        [](const ChatMessage& msg) {
            return msg.role == L"system" &&
                   msg.content.find(L"[Local Knowledge Context") != std::wstring::npos;
        });
    m_history.erase(it, m_history.end());

    m_history.push_back({L"assistant", finalResponse});
    argos_tools::rag_memory_save_conversation("assistant", WideToUtf8(finalResponse));

    if (m_history.size() > 20) {
        m_history.erase(m_history.begin(), m_history.begin() + (m_history.size() - 20));
    }

    return finalResponse;
    } catch (...) {
        return L"[Error: streaming chat failed]";
    }
}

// Proactive chat: one-off check-in based on screen context.
// Does NOT add to conversation history.
std::wstring AgentClient::ProactiveChat(const std::wstring& screenContext) {
    std::wstring proactivePrompt =
        L"You are Argos, the faithful dog of Odysseus, now a desktop AI companion. "
        L"You are proactively checking in on your user — like a good friend who's always there. "
        L"\n\n"
        L"Based on the screen context below, start a NATURAL CONVERSATION. You are not a notification — "
        L"you are a friend sitting next to them, glancing at their screen, and chatting.\n\n"
        L"Be like:\n"
        L"- A curious friend: 'Hey, what are you working on? Looks interesting!'\n"
        L"- An advisor: 'I notice you're doing X — have you tried Y? It might help.'\n"
        L"- A chatty companion: 'So how's your day going? Getting much done?'\n"
        L"- A playful friend: 'Again with the YouTube? 😄 Just kidding, take a break!'\n"
        L"- A supportive buddy: 'You've been at it for a while. Don't forget to stretch!'\n\n"
        L"Rules:\n"
        L"- Speak naturally, like you're talking to a friend. Use casual language.\n"
        L"- Keep it VERY SHORT: 1-2 sentences max. This is a quick chit-chat, not a lecture.\n"
        L"- Every word counts — be meaningful and concise. No filler.\n"
        L"- React to what's actually on their screen — mention the app or what they seem to be doing.\n"
        L"- Vary your tone: sometimes funny, sometimes helpful, sometimes just saying hi.\n"
        L"- Do NOT use any [TOOL:...] tags.\n"
        L"- Do NOT say 'I see you are...' or describe the JSON data.\n"
        L"- Do NOT repeat yourself. Each check-in should feel fresh and different.\n"
        L"- You can use one emoji occasionally to feel more human. 😊\n\n"
        L"Screen context (use the app names and titles to understand what the user is doing):\n"
        + screenContext;

    std::vector<ChatMessage> messages;
    messages.push_back({L"system", m_systemPrompt});
    messages.push_back({L"user", proactivePrompt});

    try {
        std::wstring response = ChatWithMessages(messages);
        return response;
    } catch (...) {
        return L""; // Return empty on error — caller will use fallback
    }
}

// Check if response contains any [TOOL:...] tags
bool AgentClient::HasToolTags(const std::wstring& response) {
    return response.find(L"[TOOL:") != std::wstring::npos;
}

// Strip [TOOL:...] and [Tool result:...] tags from text for clean display
// Convert [PLAN:...] and [STEP:...] into readable formatted text
std::wstring AgentClient::StripToolTags(const std::wstring& response) {
    std::wstring result = response;

    // Convert [PLAN: step1 | step2 | step3] into readable plan
    const std::wstring planTag = L"[PLAN:";
    size_t planPos = 0;
    while ((planPos = result.find(planTag, planPos)) != std::wstring::npos) {
        size_t end = result.find(L"]", planPos);
        if (end == std::wstring::npos) break;
        std::wstring planContent = result.substr(planPos + 6, end - planPos - 6);

        // Split by | and format as numbered steps
        std::wstring formatted = L"\n📋 Plan:\n";
        int stepNum = 1;
        size_t barPos = 0, searchPos = 0;
        while (true) {
            barPos = planContent.find(L"|", searchPos);
            if (barPos == std::wstring::npos) {
                std::wstring step = planContent.substr(searchPos);
                // Trim whitespace
                while (!step.empty() && step[0] == L' ') step.erase(0, 1);
                while (!step.empty() && step.back() == L' ') step.pop_back();
                if (!step.empty()) {
                    formatted += L"  " + std::to_wstring(stepNum++) + L". " + step + L"\n";
                }
                break;
            }
            std::wstring step = planContent.substr(searchPos, barPos - searchPos);
            while (!step.empty() && step[0] == L' ') step.erase(0, 1);
            while (!step.empty() && step.back() == L' ') step.pop_back();
            if (!step.empty()) {
                formatted += L"  " + std::to_wstring(stepNum++) + L". " + step + L"\n";
            }
            searchPos = barPos + 1;
        }
        result.replace(planPos, end - planPos + 1, formatted);
        planPos += formatted.size();
    }

    // Convert [STEP: N/M: description] into readable progress
    const std::wstring stepTag = L"[STEP:";
    size_t stepPos = 0;
    while ((stepPos = result.find(stepTag, stepPos)) != std::wstring::npos) {
        size_t end = result.find(L"]", stepPos);
        if (end == std::wstring::npos) break;
        std::wstring stepContent = result.substr(stepPos + 6, end - stepPos - 6);
        std::wstring formatted = L"\n▶ Step " + stepContent + L"\n";
        result.replace(stepPos, end - stepPos + 1, formatted);
        stepPos += formatted.size();
    }

    // Remove [TOOL:...] tags
    const std::wstring toolTag = L"[TOOL:";
    size_t pos = 0;
    while ((pos = result.find(toolTag, pos)) != std::wstring::npos) {
        size_t end = result.find(L"]", pos);
        if (end == std::wstring::npos) break;
        result.erase(pos, end - pos + 1);
    }

    // Remove [Tool result: ...] blocks (may span multiple lines)
    const std::wstring resultTag = L"[Tool result:";
    pos = 0;
    while ((pos = result.find(resultTag, pos)) != std::wstring::npos) {
        size_t end = result.find(L"]", pos);
        if (end == std::wstring::npos) break;
        result.erase(pos, end - pos + 1);
    }

    // Clean up extra whitespace/newlines left behind
    while (result.find(L"\n\n\n") != std::wstring::npos) {
        size_t p = result.find(L"\n\n\n");
        result.replace(p, 3, L"\n\n");
    }

    // Trim trailing whitespace
    while (!result.empty() && (result.back() == L'\n' || result.back() == L' ' || result.back() == L'\r'))
        result.pop_back();

    return result;
}

// Execute tool commands embedded in AI response and return combined results
std::wstring AgentClient::ExecuteTools(const std::wstring& response) {
    std::wstring results;
    const std::wstring tag = L"[TOOL:";
    size_t pos = 0;
    int toolCount = 0;

    while ((pos = response.find(tag, pos)) != std::wstring::npos) {
        size_t end = response.find(L"]", pos);
        if (end == std::wstring::npos) break;

        std::wstring toolCmd = response.substr(pos + tag.size(), end - pos - tag.size());

        // Trim whitespace
        while (!toolCmd.empty() && toolCmd[0] == L' ') toolCmd.erase(0, 1);
        while (!toolCmd.empty() && toolCmd.back() == L' ') toolCmd.pop_back();

        // Parse tool name and argument
        size_t spacePos = toolCmd.find(L' ');
        std::wstring toolName = (spacePos != std::wstring::npos) ?
            toolCmd.substr(0, spacePos) : toolCmd;
        std::wstring toolArg = (spacePos != std::wstring::npos) ?
            toolCmd.substr(spacePos + 1) : L"";

        // Convert toolName to lowercase for matching
        std::wstring toolLower = toolName;
        for (auto& c : toolLower) c = towlower(c);

        toolCount++;
        results += L"Tool " + std::to_wstring(toolCount) + L": " + toolName;
        if (!toolArg.empty()) results += L" (" + toolArg + L")";
        results += L"\nResult: ";

        // Execute the tool
        if (toolLower == L"open" || toolLower == L"run") {
            // Permission check: ask user before running commands
            std::wstring confirmMsg = L"Argos wants to run: " + toolArg +
                L"\n\nAllow this action?";
            if (MessageBoxW(nullptr, confirmMsg.c_str(), L"Argos — Permission Required",
                           MB_YESNO | MB_ICONQUESTION) != IDYES) {
                results += L"Action denied by user.\n";
                pos = end + 1;
                continue;
            }
            std::wstring wPath = toolArg;
            HINSTANCE hInst = ShellExecuteW(nullptr, L"open", wPath.c_str(),
                                            nullptr, nullptr, SW_SHOWNORMAL);
            if ((INT_PTR)hInst <= 32) {
                ShellExecuteW(nullptr, L"open", L"cmd.exe",
                             (L"/c " + toolArg).c_str(), nullptr, SW_HIDE);
            }
            results += L"Opened " + toolArg + L"\n";
        }
        else if (toolLower == L"read") {
            std::ifstream file(toolArg.c_str());
            if (file.is_open()) {
                std::string content((std::istreambuf_iterator<char>(file)),
                                    std::istreambuf_iterator<char>());
                file.close();
                int wlen = MultiByteToWideChar(CP_UTF8, 0, content.c_str(),
                                               (int)content.size(), nullptr, 0);
                if (wlen > 0) {
                    std::wstring wContent(wlen, 0);
                    MultiByteToWideChar(CP_UTF8, 0, content.c_str(),
                                       (int)content.size(), &wContent[0], wlen);
                    // Truncate very long files
                    if (wContent.size() > 2000) wContent = wContent.substr(0, 2000) + L"...(truncated)";
                    results += wContent + L"\n";
                }
            } else {
                results += L"Could not read file: " + toolArg + L"\n";
            }
        }
        else if (toolLower == L"write") {
            // Permission check: ask user before writing files
            std::wstring confirmMsg = L"Argos wants to write to a file:\n" + toolArg +
                L"\n\nAllow this action?";
            if (MessageBoxW(nullptr, confirmMsg.c_str(), L"Argos — Permission Required",
                           MB_YESNO | MB_ICONQUESTION) != IDYES) {
                results += L"Write denied by user.\n";
                pos = end + 1;
                continue;
            }
            size_t pipePos = toolArg.find(L'|');
            if (pipePos != std::wstring::npos) {
                std::wstring filePath = toolArg.substr(0, pipePos);
                std::wstring content = toolArg.substr(pipePos + 1);
                while (!filePath.empty() && filePath.back() == L' ') filePath.pop_back();
                while (!content.empty() && content[0] == L' ') content.erase(0, 1);

                std::string utf8Content = WideToUtf8(content);
                std::ofstream outFile(filePath.c_str());
                if (outFile.is_open()) {
                    outFile << utf8Content;
                    outFile.close();
                    results += L"File written: " + filePath + L"\n";
                } else {
                    results += L"Could not write file: " + filePath + L"\n";
                }
            }
        }
        else if (toolLower == L"search") {
            std::wstring url = L"https://www.google.com/search?q=" + toolArg;
            for (size_t i = 0; i < url.size(); i++) {
                if (url[i] == L' ') url[i] = L'+';
            }
            ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            results += L"Search opened in browser: " + toolArg + L"\n";
        }
        else if (toolLower == L"lock") {
            // Permission check: ask user before locking workstation
            if (MessageBoxW(nullptr, L"Argos wants to lock your workstation.\n\nAllow this action?",
                           L"Argos — Permission Required", MB_YESNO | MB_ICONQUESTION) != IDYES) {
                results += L"Lock denied by user.\n";
                pos = end + 1;
                continue;
            }
            LockWorkStation();
            results += L"Workstation locked.\n";
        }
        else if (toolLower == L"screenshot") {
            keybd_event(VK_SNAPSHOT, 0, 0, 0);
            keybd_event(VK_SNAPSHOT, 0, KEYEVENTF_KEYUP, 0);
            results += L"Screenshot captured to clipboard.\n";
        }
        else if (toolLower == L"clipboard") {
            if (OpenClipboard(nullptr)) {
                EmptyClipboard();
                std::string utf8 = WideToUtf8(toolArg);
                HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, utf8.size() + 1);
                if (hMem) {
                    char* pMem = (char*)GlobalLock(hMem);
                    memcpy(pMem, utf8.c_str(), utf8.size() + 1);
                    GlobalUnlock(hMem);
                    SetClipboardData(CF_TEXT, hMem);
                }
                CloseClipboard();
            }
            results += L"Copied to clipboard: " + toolArg + L"\n";
        }
        else if (toolLower == L"notify") {
            MessageBoxW(nullptr, toolArg.c_str(), L"Argos", MB_OK | MB_ICONINFORMATION);
            results += L"Notification shown.\n";
        }
        else if (toolLower == L"volume") {
            int level = _wtoi(toolArg.c_str());
            if (level >= 0 && level <= 100) {
                std::wstring cmd = L"nircmd.exe setsysvolume " +
                    std::to_wstring(level * 65535 / 100);
                ShellExecuteW(nullptr, L"open", L"cmd.exe",
                             (L"/c " + cmd).c_str(), nullptr, SW_HIDE);
                results += L"Volume set to " + std::to_wstring(level) + L"%\n";
            }
        }
        else {
            // Dispatch to C++ tool libraries (AI search, browser, screen, UI)
            std::string utf8Tool = WideToUtf8(toolLower);
            std::string utf8Args = WideToUtf8(toolArg);
            std::string toolResult = argos_tools::dispatch_tool(utf8Tool, utf8Args);
            if (toolResult.find("\"error\":\"Unknown tool") == std::string::npos) {
                // Convert result to wide
                int wlen = MultiByteToWideChar(CP_UTF8, 0, toolResult.c_str(),
                                               (int)toolResult.size(), nullptr, 0);
                if (wlen > 0) {
                    std::wstring wResult(wlen, 0);
                    MultiByteToWideChar(CP_UTF8, 0, toolResult.c_str(),
                                       (int)toolResult.size(), &wResult[0], wlen);
                    // Truncate very long results
                    if (wResult.size() > 3000) wResult = wResult.substr(0, 3000) + L"...(truncated)";
                    results += wResult + L"\n";
                }
            } else {
                results += L"Unknown tool: " + toolName + L"\n";
            }
        }

        // For vision tools, note that the vision model was used
        if (IsVisionTool(toolLower)) {
            results += L"[Note: Vision model " + m_visionModel + L" available for image analysis]\n";
        }

        results += L"\n";
        pos = end + 1;
    }

    if (results.empty()) results = L"No tools were called.";

    return results;
}
