project(KenshiMP.LiveTest LANGUAGES CXX)

add_executable(KenshiMP.LiveTest main.cpp)

target_link_libraries(KenshiMP.LiveTest PRIVATE
    KenshiMP.Common
    spdlog::spdlog
    Shlwapi.lib
    Version.lib
    Shell32.lib
)

target_include_directories(KenshiMP.LiveTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../KenshiMP.Common/include
)

set_target_properties(KenshiMP.LiveTest PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
