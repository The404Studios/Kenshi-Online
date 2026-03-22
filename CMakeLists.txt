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

# Force Release-mode container layout even in Debug builds.
# Kenshi's DLLs (MyGUI, Ogre, etc.) are Release builds — their std::string/vector
# have no _Container_proxy pointer (24/32 bytes). Debug mode adds an 8-byte proxy,
# causing ABI mismatch when passing containers across DLL boundaries.
add_definitions(-D_ITERATOR_DEBUG_LEVEL=0)

# ── Third-party dependencies ──

# ENet (upstream doesn't export include dirs to consumers)
add_subdirectory(lib/enet)
target_include_directories(enet PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/lib/enet/include)

# MinHook
add_subdirectory(lib/minhook)

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
add_subdirectory(KenshiMP.MasterServer)
add_subdirectory(KenshiMP.Injector)
add_subdirectory(KenshiMP.TestClient)
add_subdirectory(KenshiMP.IntegrationTest)
add_subdirectory(KenshiMP.UnitTest)
add_subdirectory(KenshiMP.LiveTest)
