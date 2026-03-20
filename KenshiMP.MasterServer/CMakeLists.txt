add_executable(KenshiMP.MasterServer
    main.cpp
)

target_include_directories(KenshiMP.MasterServer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

if(WIN32)
    target_link_libraries(KenshiMP.MasterServer PRIVATE
        KenshiMP.Common
        enet
        spdlog::spdlog
        nlohmann_json::nlohmann_json
        ws2_32
        winmm
    )
else()
    target_link_libraries(KenshiMP.MasterServer PRIVATE
        KenshiMP.Common
        enet
        spdlog::spdlog
        nlohmann_json::nlohmann_json
        Threads::Threads
    )
endif()
