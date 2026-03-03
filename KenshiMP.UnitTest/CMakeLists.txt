add_executable(KenshiMP.UnitTest
    main.cpp

    # Compile game-facing source files directly into the test.
    # These are the SAME files that run inside Kenshi via KenshiMP.Core.dll.
    ${CMAKE_SOURCE_DIR}/KenshiMP.Core/game/game_character.cpp
    ${CMAKE_SOURCE_DIR}/KenshiMP.Core/sync/entity_registry.cpp
    ${CMAKE_SOURCE_DIR}/KenshiMP.Core/sync/interpolation.cpp
)

target_include_directories(KenshiMP.UnitTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/KenshiMP.Core          # for game/game_types.h, sync/entity_registry.h
    ${CMAKE_SOURCE_DIR}/KenshiMP.Core/game      # for game_types.h direct includes
    ${CMAKE_SOURCE_DIR}/KenshiMP.Core/sync      # for entity_registry.h, interpolation.h
)

target_link_libraries(KenshiMP.UnitTest PRIVATE
    KenshiMP.Common
    KenshiMP.Scanner
    spdlog::spdlog
)
