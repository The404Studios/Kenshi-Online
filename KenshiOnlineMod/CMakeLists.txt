cmake_minimum_required(VERSION 3.15)
project(KenshiOnlineMod)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Fetch ImGui
include(FetchContent)
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.90.1
)
FetchContent_MakeAvailable(imgui)

# Fetch MinHook for function hooking
FetchContent_Declare(
    minhook
    GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
    GIT_TAG v1.3.3
)
FetchContent_MakeAvailable(minhook)

# MinHook source files (build as part of our project)
set(MINHOOK_SOURCES
    ${minhook_SOURCE_DIR}/src/buffer.c
    ${minhook_SOURCE_DIR}/src/hook.c
    ${minhook_SOURCE_DIR}/src/trampoline.c
    ${minhook_SOURCE_DIR}/src/hde/hde32.c
    ${minhook_SOURCE_DIR}/src/hde/hde64.c
)

# ImGui source files
set(IMGUI_SOURCES
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp
)

# Memory system source files
set(MEMORY_SOURCES
    Memory/PatternScanner.cpp
    Memory/KenshiGameBridge.cpp
)

# Memory system headers
set(MEMORY_HEADERS
    Memory/KenshiMemoryStructures.h
    Memory/PatternScanner.h
    Memory/KenshiGameBridge.h
)

# Create the DLL
add_library(KenshiOnlineMod SHARED
    KenshiOnlineMod.cpp
    Overlay/Overlay.cpp
    Overlay/OverlayUI.cpp
    Hooks/D3D11Hook.cpp
    Hooks/InputHook.cpp
    ${MEMORY_SOURCES}
    ${MEMORY_HEADERS}
    ${IMGUI_SOURCES}
    ${MINHOOK_SOURCES}
)

# Include directories
target_include_directories(KenshiOnlineMod PRIVATE
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
    ${minhook_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Link against Windows libraries and DirectX
target_link_libraries(KenshiOnlineMod PRIVATE
    ws2_32
    d3d11
    dxgi
    d3dcompiler
    psapi  # For GetModuleInformation in PatternScanner
)

# Set output directory
set_target_properties(KenshiOnlineMod PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)

# Platform-specific settings
if(MSVC)
    target_compile_options(KenshiOnlineMod PRIVATE /W3 /MP /EHsc)
    set_target_properties(KenshiOnlineMod PROPERTIES
        LINK_FLAGS "/SUBSYSTEM:WINDOWS"
    )
    # Disable specific warnings for ImGui
    target_compile_options(KenshiOnlineMod PRIVATE /wd4244 /wd4267 /wd4996)
endif()

# Add definitions
target_compile_definitions(KenshiOnlineMod PRIVATE
    KENSHI_MOD_EXPORTS
    _CRT_SECURE_NO_WARNINGS
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)
