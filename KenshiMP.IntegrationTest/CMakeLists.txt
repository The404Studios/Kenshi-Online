add_executable(KenshiMP.IntegrationTest
    main.cpp
)

target_include_directories(KenshiMP.IntegrationTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(KenshiMP.IntegrationTest PRIVATE
    KenshiMP.Common
    enet
    ws2_32
    winmm
)
