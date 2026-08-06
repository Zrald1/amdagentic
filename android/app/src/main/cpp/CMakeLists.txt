cmake_minimum_required(VERSION 3.22.1)
project(argos)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find Android log and EGL libraries
find_library(log-lib log)
find_library(egl-lib EGL)
find_library(gles-lib GLESv3)
find_library(android-lib android)

# Source files for Argos Android
set(ARGOS_SOURCES
    native_main.cpp
    egl_renderer.cpp
    robot_gles.cpp
    platform_android.cpp
    # Shared core (platform-abstracted versions)
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../cpp/src_cross/agent_client_core.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../cpp/src_cross/argos_tools_core.cpp
)

add_library(argos SHARED ${ARGOS_SOURCES})

target_include_directories(argos PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../cpp/src_cross
)

target_link_libraries(argos
    ${log-lib}
    ${egl-lib}
    ${gles-lib}
    ${android-lib}
)
