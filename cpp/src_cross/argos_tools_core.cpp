#include "argos_tools_core.h"
#include "platform.h"
#include <sstream>
#include <fstream>
#include <dirent.h>
#include <sys/stat.h>
#include <cstdio>
#include <cstring>

#ifdef __ANDROID__
#include <android/log.h>
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "Argos", __VA_ARGS__)
#else
#define LOGI(...)
#endif

namespace argos_tools {

static std::string getMemoryFilePath() {
    return argos::getAppDataDir() + "/conversation_memory.jsonl";
}

bool rag_memory_save_conversation(const std::string& role, const std::string& content) {
    std::string path = getMemoryFilePath();
    // Ensure directory exists
    mkdir(argos::getAppDataDir().c_str(), 0777);

    FILE* f = fopen(path.c_str(), "a");
    if (!f) return false;

    std::string escaped;
    for (char c : content) {
        if (c == '"') escaped += "\\\"";
        else if (c == '\\') escaped += "\\\\";
        else if (c == '\n') escaped += "\\n";
        else if (c == '\r') escaped += "\\r";
        else if (c == '\t') escaped += "\\t";
        else escaped += c;
    }

    fprintf(f, "{\"role\":\"%s\",\"content\":\"%s\"}\n", role.c_str(), escaped.c_str());
    fclose(f);
    return true;
}

std::string rag_memory_load_conversation(size_t max_messages) {
    std::string path = getMemoryFilePath();
    FILE* f = fopen(path.c_str(), "r");
    if (!f) return "[]";

    std::vector<std::string> lines;
    char buffer[8192];
    while (fgets(buffer, sizeof(buffer), f)) {
        lines.push_back(std::string(buffer));
    }
    fclose(f);

    size_t start = (lines.size() > max_messages) ? lines.size() - max_messages : 0;
    std::string result = "[\n";
    for (size_t i = start; i < lines.size(); i++) {
        result += "  " + lines[i];
        if (i < lines.size() - 1) result += ",";
        result += "\n";
    }
    result += "]";
    return result;
}

// Tool implementations
static std::string tool_list_files(const std::string& dirPath) {
    std::string path = dirPath;
    if (path.empty()) path = ".";
    std::ostringstream json;
    json << "{\"directory\":\"" << path << "\",\"files\":[";

    DIR* dir = opendir(path.c_str());
    if (!dir) {
        json << "],\"error\":\"Cannot open directory: " << path << "\"}";
        return json.str();
    }

    struct dirent* entry;
    bool first = true;
    while ((entry = readdir(dir)) != nullptr) {
        if (!first) json << ",";
        first = false;
        json << "{\"name\":\"" << entry->d_name << "\"";
        json << ",\"type\":\"" << (entry->d_type == DT_DIR ? "directory" : "file") << "\"";
        json << "}";
    }
    closedir(dir);
    json << "]}";
    return json.str();
}

static std::string tool_read_file(const std::string& filePath) {
    std::ifstream f(filePath, std::ios::binary);
    if (!f.is_open()) return "{\"error\":\"Cannot read file: " + filePath + "\"}";
    std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (content.size() > 5000) content = content.substr(0, 5000) + "...(truncated)";
    return content;
}

static std::string tool_cmd(const std::string& command) {
    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) return "{\"error\":\"Command failed\"}";
    std::string output;
    char buf[4096];
    while (fgets(buf, sizeof(buf), pipe)) {
        output += buf;
    }
    pclose(pipe);
    if (output.empty()) return "Command executed (no output).";
    if (output.size() > 3000) output = output.substr(0, 3000) + "...(truncated)";
    return output;
}

static std::string tool_recall() {
    return rag_memory_load_conversation(10);
}

static std::string tool_forget() {
    std::string path = getMemoryFilePath();
    if (remove(path.c_str()) == 0) return "{\"status\":\"Memory cleared\"}";
    return "{\"status\":\"No memory to clear\"}";
}

std::string dispatch_tool(const std::string& tool_name, const std::string& args) {
    std::string name = tool_name;
    // Lowercase
    for (auto& c : name) c = tolower(c);

    if (name == "list_files" || name == "dir" || name == "ls") {
        return tool_list_files(args);
    }
    if (name == "read") {
        return tool_read_file(args);
    }
    if (name == "cmd" || name == "command" || name == "shell") {
        return tool_cmd(args);
    }
    if (name == "recall") {
        return tool_recall();
    }
    if (name == "forget") {
        return tool_forget();
    }
    if (name == "rag_search" || name == "search_files" || name == "search_filename") {
        return "{\"error\":\"RAG search not available on this platform\"}";
    }
    if (name == "screen_apps" || name == "screen_active") {
        return "{\"error\":\"Screen context not available on this platform\"}";
    }

    return "{\"error\":\"Unknown tool: " + name + "\"}";
}

} // namespace argos_tools
