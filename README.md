# Argos — Agentic AI Companion (Android + Windows Desktop)

> Named after the faithful dog of Odysseus — Argos waits, watches, and acts.

An AI companion robot that lives on your screen as a floating overlay. On **Android**, it floats on top of any app with a 3D OpenGL ES robot. On **Windows**, it renders as a transparent always-on-top window. Click the robot to chat, ask questions, or instruct it to browse the web, read screen content, execute commands, and more.

Powered by **DeepSeek-V4-Flash** with SSE streaming, tool calling, and reasoning/thoughts display.

## Architecture

```
┌─────────────────────────────────────────────────────┐
│  Android App (Java + C++ JNI)                       │
│  ┌──────────┐  ┌──────────────┐  ┌───────────────┐  │
│  │ 3D Robot │  │ Speech Bubble│  │ Accessibility │  │
│  │ (GLES)   │  │ (Chat UI)    │  │ Service       │  │
│  └──────────┘  └──────────────┘  └───────┬───────┘  │
│         │                            │              │
│         └──────── JNI Bridge ────────┤              │
│                        │             │              │
│              ┌─────────▼─────────────▼──────┐       │
│              │  C++ Core (cross-platform)   │       │
│              │  • Agent Client (SSE stream) │       │
│              │  • Tool Dispatcher           │       │
│              │  • Memory Persistence        │       │
│              └──────────────┬───────────────┘       │
└─────────────────────────────┼───────────────────────┘
                              │ HTTPS (SSE streaming)
                              ▼
                   ┌─────────────────────┐
                   │  DeepSeek API       │
                   │  (OpenAI-compatible)│
                   └─────────────────────┘

Windows Desktop (C++ / Win32 / OpenGL)
  • Same C++ core (agent_client_core, argos_tools_core)
  • Win32 transparent overlay window
  • UI Automation (browser, screen reading)
  • System tray integration
```

## Features

### AI Chat
- **SSE streaming** — Responses stream in real-time, token by token
- **Reasoning/thoughts display** — AI's internal reasoning (DeepSeek `reasoning_content`) shown as italic thoughts in the speech bubble before the answer
- **Conversation memory** — Persists across sessions in JSONL files
- **Fallback model** — Automatically falls back to `MiniCPM5-1B` if primary model is unavailable

### AI Tools (Tool Calling)
The AI can call tools using `[TOOL:tool_name args]` syntax. Tools execute in a loop (up to 8 iterations) — the AI calls a tool, sees the result, and continues reasoning.

| Tool | Android | Windows | Description |
|------|---------|---------|-------------|
| `list_files` | ✅ | ✅ | List files in a directory |
| `read` | ✅ | ✅ | Read file contents |
| `cmd` | ✅ | ✅ | Execute shell commands |
| `open_url` | ✅ | ✅ | Open a URL in the browser |
| `screen_text` | ✅ | ✅ | Read all text visible on screen |
| `screen_active` | ✅ | ✅ | Get the currently focused app |
| `click_text` | ✅ | ✅ | Click an element by its text |
| `type_text` | ✅ | ✅ | Type text into the focused input |
| `scroll` | ✅ | ✅ | Scroll the screen up/down |
| `recall` | ✅ | ✅ | Load conversation memory |
| `forget` | ✅ | ✅ | Clear conversation memory |
| `rag_search` | ❌ | ✅ | RAG-based file content search |
| `browser_screenshot` | ❌ | ✅ | Take browser screenshots |

### Android-Specific
- **3D OpenGL ES robot** — Custom-rendered robot with idle animations, thinking state, and talking state
- **Floating overlay** — Robot floats on top of any app, draggable
- **Speech bubble** — Chat interface with streaming text, thoughts display, and tool status
- **Accessibility Service** — Reads screen content from any app (browser, settings, etc.) via Android Accessibility API
- **JNI HTTP bridge** — Uses Java `HttpURLConnection` for HTTPS support via JNI

### Windows Desktop-Specific
- **Transparent overlay window** — Always-on-top, click-through when idle
- **Custom 2.5D robot renderer** — State-driven animations (idle, walk, greet, work, celebrate, sleep)
- **UI Automation** — Browser interaction via Windows UI Automation API
- **System tray** — Hide/show/quit from tray

## Build

### Android (Debug APK)
```bash
cd android
gradle assembleDebug
# Output: android/app/build/outputs/apk/debug/app-debug.apk
```

**Requirements:**
- Android SDK (API 24+)
- NDK 26.3.11579264
- CMake 3.22.1
- Java 17

### Windows Desktop
```bash
cd cpp
build.bat
```

**Requirements:**
- Visual Studio 2022 (C++ workload)
- Windows 10/11

### CI Builds
GitHub Actions builds Android APK on every push. Download from the **Actions** tab → latest run → **Artifacts**.

## Android Permissions

| Permission | Purpose |
|-----------|---------|
| `INTERNET` | API calls to DeepSeek server |
| `SYSTEM_ALERT_WINDOW` | Floating robot overlay |
| `FOREGROUND_SERVICE` | Keep robot alive in background |
| `BIND_ACCESSIBILITY_SERVICE` | Read screen content, interact with browser |

On first launch, the app guides you through granting both **Overlay** and **Accessibility** permissions.

## Project Structure

```
android/app/src/main/
├── java/com/argos/companion/
│   ├── MainActivity.java           # Permission prompts, service start
│   ├── FloatingRobotService.java   # Overlay window, speech bubble, JNI bridge
│   └── ArgosAccessibilityService.java  # Screen reading, clicking, typing
├── cpp/
│   ├── native_main.cpp             # JNI entry, render loop, chat thread
│   ├── platform_android.cpp        # JNI HTTP bridge, browser/screen functions
│   ├── egl_renderer.cpp            # EGL setup for OpenGL ES
│   ├── robot_gles.cpp              # 3D robot rendering (GLES)
│   └── CMakeLists.txt              # Android NDK build config
└── res/xml/
    └── argos_accessibility_config.xml  # Accessibility service config

cpp/src_cross/                      # Shared C++ core (Android + Windows)
├── agent_client_core.cpp           # AI chat, SSE parsing, tool execution loop
├── agent_client_core.h             # AgentClientCore class
├── argos_tools_core.cpp            # Tool implementations + dispatch
├── argos_tools_core.h              # Tool declarations
└── platform.h                      # Platform abstraction interface

cpp/src/                            # Windows desktop specific
├── main.cpp                        # Win32 entry point, overlay window
├── robot_renderer.cpp              # 2.5D robot renderer (GDI/OpenGL)
├── agent_client.cpp                # Full desktop agent with all tools
├── argos_tools.cpp                 # Desktop tools (RAG, UI Automation)
├── window_manager.cpp              # Transparent overlay window
├── tray_icon.cpp                   # System tray
└── tools/                          # Browser, screen, UI automation tools
```

## Tech Stack

- **Language:** C++17 (core), Java (Android UI)
- **AI Model:** DeepSeek-V4-Flash (OpenAI-compatible API)
- **Streaming:** Server-Sent Events (SSE) with real-time token streaming
- **Android Rendering:** OpenGL ES 3.0 (custom 3D robot)
- **Windows Rendering:** Custom 2.5D painter + OpenGL
- **Browser Interaction:** Android Accessibility Service / Windows UI Automation
- **Memory:** JSONL persistent conversation storage
- **Build:** CMake (Android NDK), MSBuild (Windows)

## How It Works

1. **Tap the robot's head** → speech bubble opens
2. **Type a message** → AI streams thoughts (italic) then response in real-time
3. **AI decides to use a tool** → tool executes → result feeds back to AI → AI continues
4. **Example: "Search for cats on Google"**
   - AI calls `[TOOL:open_url https://www.google.com/search?q=cats]`
   - Browser opens, 2-second delay for page load
   - AI calls `[TOOL:screen_text]` → reads search results
   - AI summarizes the results for you

## License

MIT

## Configuration

The app works out of the box with default settings. To override the API key, server URL, or model, create a config file at:

**Android:** `/data/data/com.argos.companion/files/argos_config.txt`
**Windows:** `%APPDATA%/Argos/argos_config.txt` (or same directory as exe)

```ini
server_url=https://your-server.com/api/v1
api_key=your-api-key-here
model=YourModelName
```

The config file is optional — defaults are used if not found.
