cmake_minimum_required(VERSION 3.15)
project(KenshiOnlineMod)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Create the DLL
add_library(KenshiOnlineMod SHARED
    KenshiOnlineMod.cpp
)

# Link against Windows libraries
target_link_libraries(KenshiOnlineMod
    ws2_32
)

# Set output directory
set_target_properties(KenshiOnlineMod PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# Platform-specific settings
if(MSVC)
    # MSVC specific settings
    target_compile_options(KenshiOnlineMod PRIVATE /W4 /MP)
    set_target_properties(KenshiOnlineMod PROPERTIES
        LINK_FLAGS "/SUBSYSTEM:WINDOWS"
    )
endif()

# Add definition for DLL export
target_compile_definitions(KenshiOnlineMod PRIVATE
    KENSHI_MOD_EXPORTS
    _CRT_SECURE_NO_WARNINGS
)
