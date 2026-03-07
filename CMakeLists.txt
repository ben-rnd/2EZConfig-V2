cmake_minimum_required(VERSION 3.20)
project(2EZConfig VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Windows XP target
add_compile_definitions(
    _WIN32_WINNT=0x0501
    WINVER=0x0501
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    UNICODE
    _UNICODE
    __USE_MINGW_ANSI_STDIO=1
)

# MinGW cross-compile flags
add_compile_options(
    -march=i686
    -mstackrealign
    -Wall
    -Wextra
    -Wno-unused-parameter
)

add_link_options(
    -static
    -static-libgcc
    -static-libstdc++
    -Wl,--disable-dynamicbase
    -Wl,--image-base=0x400000
    -Wl,--enable-stdcall-fixup
)

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_compile_options(-O2 -ffunction-sections -fdata-sections)
    add_link_options(-s -Wl,--gc-sections)
endif()

# Dependencies
include(FetchContent)

# GLFW 3.3.x — last version supporting Windows XP
# PATCH_COMMAND: replace GetModuleHandleExW (Vista+) with runtime detection
# so the binary loads on XP SP3. See patches/glfw-xp-compat.patch.
FetchContent_Declare(glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.3.10
    GIT_SHALLOW    TRUE
    PATCH_COMMAND  ${CMAKE_COMMAND} -P ${CMAKE_SOURCE_DIR}/patches/apply-glfw-xp-compat.cmake
)
set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glfw)

# ImGui
FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.91.1
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(imgui)

add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl2.cpp
)
target_include_directories(imgui PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
target_link_libraries(imgui PUBLIC glfw opengl32)
target_compile_options(imgui PRIVATE -w)

# nlohmann/json — JSON serialization library
FetchContent_Declare(nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE
)
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
set(JSON_Install    OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(nlohmann_json)

# usb-hid-usage — two-file C library (Apache-2.0) — usage page/ID to human-readable name
FetchContent_Declare(usbhidusage
    GIT_REPOSITORY https://github.com/smokris/usb-hid-usage.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(usbhidusage)

add_subdirectory(src/libs/hardlock)
add_subdirectory(src/libs/injector)
add_subdirectory(src/libs/patches)
add_subdirectory(src/libs/settings)
add_subdirectory(src/libs/input)
add_subdirectory(src/libs/bindings)
add_subdirectory(src/2ezconfig-exe)
add_subdirectory(src/2ez-dll)

