add_executable(KenshiMP.TestClient
    main.cpp
)

target_include_directories(KenshiMP.TestClient PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(KenshiMP.TestClient PRIVATE
    KenshiMP.Common
    enet
    ws2_32
    winmm
)
