add_library(KenshiMP.Scanner STATIC
    src/scanner.cpp
    src/patterns.cpp
    src/memory.cpp
    src/hook_manager.cpp
    src/function_analyzer.cpp
)

target_include_directories(KenshiMP.Scanner PUBLIC include)
target_link_libraries(KenshiMP.Scanner PUBLIC KenshiMP.Common minhook spdlog::spdlog)
