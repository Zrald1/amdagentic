# Aria — Agentic AI Desktop Companion (AMD Radeon)

A robot companion that lives on your desktop as a transparent always-on-top
overlay. It walks around, greets you, and when you click it you can ask it to
do tasks (web research, file ops, todos, code help). All inference runs on a
remote **AMD Radeon GPU server** via llama.cpp's OpenAI-compatible API.

Same Flutter codebase ships to **Android** as a thin client that connects to
your GPU server over LAN.

## Architecture

```
Flutter app (Windows overlay / Android thin client)
        │  HTTP (OpenAI-compatible /v1/chat/completions, SSE streaming)
        ▼
GPU server (llama.cpp + Vulkan/HIPBLAS on AMD Radeon)
```

The UI and inference are fully decoupled. Point the app at any
OpenAI-compatible server by overriding `GPU_SERVER_URL` at build time.

## Features

- **2.5D robot mascot** — custom-painted with state-driven animations:
  idle, walking, waving greeting, thinking (working), celebrating, sleeping.
- **Behavior FSM** — randomly roams the bottom of your screen, greets on
  click, shows a "working" animation with progress ring while the agent
  executes.
- **Click-to-prompt** — click the mascot → type a task → watch it stream the
  response in real time.
- **Engine panel** — shows GPU server connection status, model, and
  AMD Radeon branding.
- **System tray** — hide/show/quit on Windows.
- **Cross-platform** — same code builds for Windows desktop and Android.

## Build

### Prerequisites
- Flutter 3.44+ (stable)
- For Windows: Windows 10/11 with Visual Studio 2022 (C++ workload)
- For Android: Android SDK + Java 17

### Local build
```bash
flutter pub get
flutter run -d windows      # desktop
flutter run -d android      # phone (thin client)
```

### Point at your GPU server
```bash
flutter run --dart-define=GPU_SERVER_URL=http://YOUR_SERVER_IP:8080
```

### CI builds (download artifacts)
GitHub Actions builds both Android APK and Windows EXE on every push.
Download them from the **Actions** tab → latest run → **Artifacts**.

## GPU server setup (AMD Radeon)

### Option A — Vulkan (works on all AMD GPUs, including Windows)
```bash
cmake -B build -DGGML_VULKAN=ON
cmake --build build --config Release
./build/bin/llama-server -m qwen2.5-7b-instruct-q4_k_m.gguf -ngl 99 --host 0.0.0.0 --port 8080
```

### Option B — ROCm/HIPBLAS (Linux, RDNA2/3)
```bash
cmake -B build -DGGML_HIPBLAS=ON
cmake --build build --config Release
./build/bin/llama-server -m qwen2.5-7b-instruct-q4_k_m.gguf -ngl 99 --host 0.0.0.0 --port 8080
```

### Recommended models
| VRAM   | Model                          | Size (Q4_K_M) |
|--------|--------------------------------|---------------|
| 8 GB   | Qwen2.5-7B-Instruct            | ~4.5 GB       |
| 16 GB  | Qwen2.5-14B-Instruct           | ~9 GB         |
| 4 GB   | Qwen2.5-3B-Instruct            | ~2 GB         |

Qwen2.5 is chosen for reliable native function-calling.

## Project structure
```
lib/
  main.dart                    # entry point, window init
  app.dart                     # root widget
  config/app_config.dart       # GPU server URL, model, mascot settings
  system/
    window_setup.dart          # transparent always-on-top overlay
    tray_controller.dart       # system tray (hide/show/quit)
  mascot/
    mascot_state.dart          # state enum + status payload
    mascot_controller.dart     # behavior FSM (idle/walk/greet/work)
    robot_painter.dart         # custom-painted 2.5D robot
    mascot_stage.dart          # rendering + interaction widget
  agent/
    agent_bridge.dart          # HTTP client → OpenAI-compatible API
    agent_service.dart         # orchestrates task + updates mascot
  ui/
    prompt_overlay.dart         # click-to-prompt panel + thought bubble
    engine_panel.dart          # GPU server status panel
```

## Roadmap
- [ ] Rive .riv mascot (swap custom painter for state-machine asset)
- [ ] Tool calling (web search, RAG, file ops, todos)
- [ ] Benchmark panel (tok/s, VRAM, quantization toggle)
- [ ] iOS support
- [ ] On-device Android inference via Vulkan

## AMD Radeon optimization
- Vulkan backend for universal AMD GPU support
- ROCm/HIPBLAS path for Linux RDNA2/3
- Full GPU layer offload (`-ngl 99`)
- KV cache quantization (`--cache-type-k q8_0`)
- Streaming SSE for responsive UX
- Batched prompt processing for RAG
