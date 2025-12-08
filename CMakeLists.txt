cmake_minimum_required(VERSION 3.24)

# Project
project(NocteEngine LANGUAGES CXX)

# This project is Windows-only (uses Win32 API and D3D12)
if(NOT WIN32)
    message(FATAL_ERROR "This D3D12 project builds on Windows only.")
endif()

# C++ settings
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Optional: choose static vs dynamic MSVC runtime in one place (CMake >= 3.15)
# Possible values: MultiThreaded, MultiThreadedDLL, MultiThreadedDebug, MultiThreadedDebugDLL
# set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

# Build options
option(BUILD_WIN32_SUBSYSTEM "Build as a Win32 GUI app (no console window)" ON)

# Collect source files recursively
file(GLOB APP_SOURCES
    "include/*.h"
    "include/*.cpp"
    "include/nv_helpers_dx12/*.h"
    "include/nv_helpers_dx12/*.cpp"
    "include/imgui/*.cpp"
    "include/imgui/*.h"
    "include/imgui/backends/*.cpp"
    "include/imgui/backends/*.h"
    "include/tinyobj/*.h"
    "src/*.cpp"
    "src/*.h"
    "src/Events/*.cpp"
    "src/Events/*.h"
    "src/Renderer/*.cpp"
    "src/Renderer/*.h"
    "src/Utils/*.cpp"
    "src/Utils/*.h"
)

# Executable type: WIN32 removes the console window on Windows
if(BUILD_WIN32_SUBSYSTEM)
    add_executable(${PROJECT_NAME} WIN32 ${APP_SOURCES})
else()
    add_executable(${PROJECT_NAME} ${APP_SOURCES})
endif()

set_source_files_properties(
    "include/nv_helpers_dx12/*.cpp"
    PROPERTIES SKIP_PRECOMPILE_HEADERS ON
)

target_include_directories(${PROJECT_NAME}
    PUBLIC 
        "${CMAKE_SOURCE_DIR}/include"
)


# Output directories (bin per configuration)
set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY           "${CMAKE_BINARY_DIR}/bin/$<CONFIG>"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG     "${CMAKE_BINARY_DIR}/bin/Debug"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE   "${CMAKE_BINARY_DIR}/bin/Release"
    VS_DEBUGGER_WORKING_DIRECTORY      "${CMAKE_SOURCE_DIR}"
)

# Preprocessor defines
target_compile_definitions(${PROJECT_NAME} PRIVATE
    UNICODE 
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

# Compiler warnings and useful flags for MSVC
if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE
        /WX-
        /permissive
        /Zi
        /Zc:wchar_t
        /MP            # parallel build
    )
endif()

# Link D3D12 and related libraries from the Windows SDK
target_link_libraries(${PROJECT_NAME} PRIVATE
    d3d12
    dxgi
    dxguid
    d3dcompiler
    dxcompiler
)

# --- Post-build step: copy shaders to output directory ---
add_custom_command(
    TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/src/Shaders"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/bin/Debug/Shaders"
)

# Optional: treat warnings as errors in CI builds
# if(CMAKE_BUILD_TYPE STREQUAL "Release")
#     if(MSVC)
#         target_compile_options(${PROJECT_NAME} PRIVATE /WX)
#     else()
#         target_compile_options(${PROJECT_NAME} PRIVATE -Werror)
#     endif()
# endif()

# Optional: copy a Shaders or Assets folder next to the built exe
# add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
#     COMMAND ${CMAKE_COMMAND} -E copy_directory
#             "${CMAKE_SOURCE_DIR}/Assets"
#             "$<TARGET_FILE_DIR:${PROJECT_NAME}>/Assets"
# )

# Optional: set subsystem explicitly if you ever switch the executable kind at runtime
# if(MSVC AND BUILD_WIN32_SUBSYSTEM)
#     target_link_options(${PROJECT_NAME} PRIVATE "/SUBSYSTEM:WINDOWS")
# endif()
