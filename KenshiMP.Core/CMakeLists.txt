add_library(KenshiMP.Core SHARED
    dllmain.cpp
    core.cpp
    # Hooks
    hooks/render_hooks.cpp
    hooks/input_hooks.cpp
    hooks/entity_hooks.cpp
    hooks/movement_hooks.cpp
    hooks/combat_hooks.cpp
    hooks/world_hooks.cpp
    hooks/save_hooks.cpp
    hooks/time_hooks.cpp
    # Game wrappers
    game/game_character.cpp
    game/game_squad.cpp
    game/game_world.cpp
    game/game_inventory.cpp
    # Networking
    net/client.cpp
    net/packet_handler.cpp
    # Sync
    sync/entity_registry.cpp
    sync/interpolation.cpp
    sync/ownership.cpp
    sync/zone_interest.cpp
    # UI
    ui/overlay.cpp
    ui/chat.cpp
    ui/player_list.cpp
    ui/connection_ui.cpp
)

target_include_directories(KenshiMP.Core PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(KenshiMP.Core PRIVATE
    KenshiMP.Common
    KenshiMP.Scanner
    enet
    imgui
    spdlog::spdlog
    nlohmann_json::nlohmann_json
    d3d11
    dxgi
    d3dcompiler
)

# Export Ogre plugin symbols
target_compile_definitions(KenshiMP.Core PRIVATE KMP_EXPORTS)
