add_library(KenshiMP.Scanner STATIC
    src/scanner.cpp
    src/scanner_engine.cpp
    src/patterns.cpp
    src/memory.cpp
    src/hook_manager.cpp
    src/mov_rax_rsp_fix.cpp
    src/function_analyzer.cpp
    src/pdata_enumerator.cpp
    src/string_analyzer.cpp
    src/vtable_scanner.cpp
    src/call_graph.cpp
    src/orchestrator.cpp
)

target_include_directories(KenshiMP.Scanner PUBLIC include)
target_link_libraries(KenshiMP.Scanner PUBLIC KenshiMP.Common minhook spdlog::spdlog dbghelp)
