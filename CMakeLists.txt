cmake_minimum_required(VERSION 3.20)
project(KenshiMP VERSION 0.1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Force x64
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "KenshiMP requires a 64-bit build")
endif()

# Windows-specific
add_definitions(-DWIN32_LEAN_AND_MEAN -DNOMINMAX -D_CRT_SECURE_NO_WARNINGS)

# ── Third-party dependencies ──

# ENet
add_subdirectory(lib/enet)

# MinHook
add_subdirectory(lib/minhook)

# Dear ImGui (built as static lib)
add_library(imgui STATIC
    lib/imgui/imgui.cpp
    lib/imgui/imgui_demo.cpp
    lib/imgui/imgui_draw.cpp
    lib/imgui/imgui_tables.cpp
    lib/imgui/imgui_widgets.cpp
    lib/imgui/backends/imgui_impl_dx11.cpp
    lib/imgui/backends/imgui_impl_win32.cpp
)
target_include_directories(imgui PUBLIC lib/imgui lib/imgui/backends)

# spdlog (header-only mode)
add_subdirectory(lib/spdlog EXCLUDE_FROM_ALL)

# nlohmann/json (header-only)
set(JSON_BuildTests OFF CACHE INTERNAL "")
add_subdirectory(lib/json EXCLUDE_FROM_ALL)

# ── Our projects ──
add_subdirectory(KenshiMP.Common)
add_subdirectory(KenshiMP.Scanner)
add_subdirectory(KenshiMP.Core)
add_subdirectory(KenshiMP.Server)
add_subdirectory(KenshiMP.Injector)
