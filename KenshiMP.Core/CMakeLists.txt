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
    hooks/game_tick_hooks.cpp
    hooks/inventory_hooks.cpp
    hooks/squad_hooks.cpp
    hooks/faction_hooks.cpp
    hooks/building_hooks.cpp
    hooks/ai_hooks.cpp
    hooks/resource_hooks.cpp
    hooks/squad_spawn_hooks.h
    hooks/squad_spawn_hooks.cpp
    hooks/char_tracker_hooks.h
    hooks/char_tracker_hooks.cpp
    # Game wrappers
    game/game_character.cpp
    game/game_squad.cpp
    game/game_world.cpp
    game/game_inventory.cpp
    game/game_building.cpp
    game/game_faction.cpp
    game/game_stats.cpp
    game/spawn_manager.cpp
    game/player_controller.cpp
    game/loading_orchestrator.cpp
    game/asset_facilitator.cpp
    game/lobby_manager.h
    game/lobby_manager.cpp
    game/shared_save_sync.cpp
    # Networking
    net/client.cpp
    net/packet_handler.cpp
    net/server_query.cpp
    # Sync
    sync/entity_registry.cpp
    sync/interpolation.cpp
    sync/ownership.cpp
    sync/zone_interest.cpp
    sync/entity_resolver.cpp
    sync/zone_engine.cpp
    sync/player_engine.cpp
    sync/sync_orchestrator.cpp
    sync/pipeline_orchestrator.cpp
    sync/sync_facilitator.cpp
    # System / orchestrator
    sys/task_orchestrator.cpp
    sys/command_registry.cpp
    sys/builtin_commands.cpp
    # UI (native MyGUI only)
    ui/overlay.cpp
    ui/mygui_bridge.cpp
    ui/native_menu.cpp
    ui/native_hud.cpp
)

target_include_directories(KenshiMP.Core PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(KenshiMP.Core PRIVATE
    KenshiMP.Common
    KenshiMP.Scanner
    enet
    spdlog::spdlog
    nlohmann_json::nlohmann_json
    d3d11
    dxgi
    d3dcompiler
)

# Export Ogre plugin symbols
target_compile_definitions(KenshiMP.Core PRIVATE KMP_EXPORTS)

# Generate MAP file for crash debugging
target_link_options(KenshiMP.Core PRIVATE /MAP)

# Auto-copy DLL to Kenshi game directory so Ogre can load it as a plugin
set(KENSHI_DIR "${CMAKE_SOURCE_DIR}/..")
if(EXISTS "${KENSHI_DIR}/kenshi_x64.exe")
    add_custom_command(TARGET KenshiMP.Core POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:KenshiMP.Core>"
            "${KENSHI_DIR}/KenshiMP.Core.dll"
        COMMENT "Copying KenshiMP.Core.dll to Kenshi directory"
    )

    # Auto-deploy MyGUI layout files to Kenshi data directories
    set(LAYOUT_SRC "${CMAKE_SOURCE_DIR}/dist")
    set(LAYOUT_DST "${KENSHI_DIR}/data/gui/layout")
    foreach(LAYOUT_FILE Kenshi_MainMenu.layout Kenshi_MultiplayerPanel.layout Kenshi_MultiplayerHUD.layout)
        if(EXISTS "${LAYOUT_SRC}/${LAYOUT_FILE}")
            add_custom_command(TARGET KenshiMP.Core POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${LAYOUT_SRC}/${LAYOUT_FILE}"
                    "${LAYOUT_DST}/${LAYOUT_FILE}"
                COMMENT "Deploying ${LAYOUT_FILE} to Kenshi data"
            )
        endif()
    endforeach()
endif()
