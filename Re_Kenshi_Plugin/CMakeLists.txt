cmake_minimum_required(VERSION 3.20)
project(Re_Kenshi_Plugin VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Options
option(BUILD_SHARED_LIBS "Build shared library" ON)

# Source files
set(SOURCES
    src/dllmain.cpp
    src/OgreOverlay.cpp
    src/InputHandler.cpp
    src/IPCClient.cpp
    src/UIRenderer.cpp
    src/MessageProtocol.cpp
    src/MemoryScanner.cpp
    src/KenshiStructures.cpp
    src/KenshiAdvancedStructures.cpp
    src/D3D11Hook.cpp
    src/ImGuiRenderer.cpp
    src/GameEventManager.cpp
    src/MultiplayerSyncManager.cpp
    src/PerformanceProfiler.cpp
    src/Configuration.cpp
    src/TestingUtilities.cpp
    src/Logger.cpp
    src/Utilities.cpp
    src/PatternDatabase.cpp
    src/GameStateManager.cpp
)

set(HEADERS
    include/Re_Kenshi_Plugin.h
    include/OgreOverlay.h
    include/InputHandler.h
    include/IPCClient.h
    include/UIRenderer.h
    include/MessageProtocol.h
    include/MemoryScanner.h
    include/KenshiStructures.h
    include/KenshiAdvancedStructures.h
    include/D3D11Hook.h
    include/ImGuiRenderer.h
    include/GameEventManager.h
    include/MultiplayerSyncManager.h
    include/PerformanceProfiler.h
    include/Configuration.h
    include/TestingUtilities.h
    include/Logger.h
    include/Utilities.h
    include/PatternDatabase.h
    include/GameStateManager.h
)

# Create library
add_library(${PROJECT_NAME} SHARED ${SOURCES} ${HEADERS})

# Include directories
target_include_directories(${PROJECT_NAME}
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Windows-specific settings
if(WIN32)
    target_compile_definitions(${PROJECT_NAME} PRIVATE
        _WIN32_WINNT=0x0601
        NOMINMAX
        WIN32_LEAN_AND_MEAN
    )

    # Link against Windows libraries
    target_link_libraries(${PROJECT_NAME} PRIVATE
        kernel32
        user32
        gdi32
        psapi
        d3d11
        dxgi
    )
endif()

# OGRE (assuming it's installed or available)
# find_package(OGRE REQUIRED)
# target_link_libraries(${PROJECT_NAME} PRIVATE OGRE::OGRE)

# For now, we'll add OGRE manually when we know the path
# target_include_directories(${PROJECT_NAME} PRIVATE "path/to/ogre/include")

# ImGui (optional - for advanced UI)
# Add ImGui as a subdirectory or use find_package
# add_subdirectory(vendor/imgui)
# target_link_libraries(${PROJECT_NAME} PRIVATE imgui)

# RapidJSON for JSON serialization
target_include_directories(${PROJECT_NAME} PRIVATE vendor/rapidjson/include)

# Install
install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)
