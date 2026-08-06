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
)

add_library(argos SHARED ${ARGOS_SOURCES})

target_include_directories(argos PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CROSS_DIR}
)

target_link_libraries(argos
    ${log-lib}
    ${egl-lib}
    ${gles-lib}
    ${android-lib}
)
