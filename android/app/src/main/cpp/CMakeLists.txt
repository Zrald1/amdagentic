cmake_minimum_required(VERSION 3.22.1)
project(argos)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find Android log and EGL libraries
find_library(log-lib log)
find_library(egl-lib EGL)
find_library(gles-lib GLESv3)
find_library(android-lib android)

# Cross-platform source dir (5 levels up from android/app/src/main/cpp/)
set(CROSS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../cpp/src_cross)

# ── whisper.cpp integration ──
set(WHISPER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../../../third_party/whisper.cpp)

# Build whisper.cpp as static library (disable examples, tests, etc.)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build shared libs" FORCE)
set(WHISPER_BUILD_TESTS OFF CACHE BOOL "Build tests" FORCE)
set(WHISPER_BUILD_EXAMPLES OFF CACHE BOOL "Build examples" FORCE)
set(WHISPER_BUILD_SERVER OFF CACHE BOOL "Build server" FORCE)
set(GGML_OPENBLAS OFF CACHE BOOL "OpenBLAS" FORCE)
set(GGML_BLAS OFF CACHE BOOL "BLAS" FORCE)
set(GGML_CUDA OFF CACHE BOOL "CUDA" FORCE)
set(GGML_VULKAN OFF CACHE BOOL "Vulkan" FORCE)
set(GGML_NATIVE OFF CACHE BOOL "Native" FORCE)
set(GGML_AVX OFF CACHE BOOL "AVX" FORCE)
set(GGML_AVX2 OFF CACHE BOOL "AVX2" FORCE)
set(GGML_FMA OFF CACHE BOOL "FMA" FORCE)
set(GGML_F16C OFF CACHE BOOL "F16C" FORCE)
set(GGML_OPENMP OFF CACHE BOOL "OpenMP" FORCE)

# Add whisper.cpp subdirectory (builds ggml + whisper)
add_subdirectory(${WHISPER_DIR} ${CMAKE_CURRENT_BINARY_DIR}/whisper_build)

# Source files for Argos Android
set(ARGOS_SOURCES
    native_main.cpp
    egl_renderer.cpp
    robot_gles.cpp
    platform_android.cpp
    # Shared core (platform-abstracted versions)
    ${CROSS_DIR}/agent_client_core.cpp
    ${CROSS_DIR}/argos_tools_core.cpp
    ${CROSS_DIR}/ui_inspector.cpp
    ${CROSS_DIR}/whisper_wrapper.cpp
)

add_library(argos SHARED ${ARGOS_SOURCES})

target_include_directories(argos PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CROSS_DIR}
    ${WHISPER_DIR}/include
    ${WHISPER_DIR}/ggml/include
)

target_link_libraries(argos
    ${log-lib}
    ${egl-lib}
    ${gles-lib}
    ${android-lib}
    whisper
    ggml
)
