@echo off
REM Build script for Aria C++ desktop companion.
REM Uses MSVC (Visual Studio 2022 Build Tools) directly — no CMake needed.
REM
REM Usage: build.bat        (debug build)
REM        build.bat release (release build)

setlocal enabledelayedexpansion

REM ── Find Visual Studio ──────────────────────────────────────────────
set VSWHERE="C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq tokens=*" %%i in (`%VSWHERE% -latest -products * -property installationPath`) do set VS_PATH=%%i
if "%VS_PATH%"=="" (
    echo ERROR: Visual Studio 2022 not found.
    exit /b 1
)

REM ── Set up build environment ─────────────────────────────────────────
call "%VS_PATH%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
if errorlevel 1 (
    echo ERROR: Failed to initialize MSVC environment.
    exit /b 1
)

REM ── Configuration ────────────────────────────────────────────────────
set CONFIG=Debug
if /i "%1"=="release" set CONFIG=Release

set OUT_DIR=build\%CONFIG%
if not exist %OUT_DIR% mkdir %OUT_DIR%

REM ── Compiler flags ───────────────────────────────────────────────────
set CXXFLAGS=/std:c++17 /EHsc /W3 /DUNICODE /D_UNICODE /I"src" /I"src\tools"
if /i "%CONFIG%"=="Release" (
    set CXXFLAGS=%CXXFLAGS% /O2 /DNDEBUG /MD
) else (
    set CXXFLAGS=%CXXFLAGS% /Od /Zi /MDd
)

REM ── Linker flags ────────────────────────────────────────────────────
set LDFLAGS=/SUBSYSTEM:WINDOWS
set LIBS=d2d1.lib dwmapi.lib winhttp.lib shell32.lib user32.lib gdi32.lib ole32.lib oleaut32.lib uiautomationcore.lib

REM ── Source files ────────────────────────────────────────────────────
set SOURCES=src\main.cpp src\window_manager.cpp src\robot_renderer.cpp src\tray_icon.cpp src\agent_client.cpp src\argos_tools.cpp
set TOOL_SOURCES=src\tools\text_utils.cpp src\tools\file_mapper.cpp src\tools\vector_store.cpp src\tools\image_hasher.cpp src\tools\content_indexer.cpp src\tools\search_engine.cpp src\tools\json_writer.cpp src\tools\browser_tool.cpp src\tools\screen_context.cpp src\tools\ui_locator.cpp src\tools\computer_use_tool.cpp

REM ── Compile and link ────────────────────────────────────────────────
echo Building Argos (%CONFIG%)...
cl %CXXFLAGS% %SOURCES% %TOOL_SOURCES% /Fe:%OUT_DIR%\argos.exe /Fo%OUT_DIR%\ /link %LDFLAGS% %LIBS%

if errorlevel 1 (
    echo.
    echo BUILD FAILED
    exit /b 1
)

echo.
echo BUILD SUCCEEDED: %OUT_DIR%\argos.exe
echo Run it: %OUT_DIR%\argos.exe
