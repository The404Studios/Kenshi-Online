#include "core.h"
#include "sys/command_registry.h"
#include "hooks/render_hooks.h"
#include "hooks/input_hooks.h"
#include "hooks/entity_hooks.h"
#include "hooks/movement_hooks.h"
#include "hooks/combat_hooks.h"
#include "hooks/world_hooks.h"
#include "hooks/save_hooks.h"
#include "hooks/time_hooks.h"
#include "hooks/game_tick_hooks.h"
#include "hooks/inventory_hooks.h"
#include "hooks/squad_hooks.h"
#include "hooks/faction_hooks.h"
#include "hooks/building_hooks.h"
#include "hooks/ai_hooks.h"
#include "game/game_types.h"
#include "game/game_inventory.h"
#include "kmp/protocol.h"
#include "kmp/messages.h"
#include "kmp/constants.h"
#include "kmp/memory.h"
#include "kmp/function_analyzer.h"
#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <chrono>
#include <algorithm>
#include <Windows.h>

namespace kmp {

// Forward declaration
void InitPacketHandler();

Core& Core::Get() {
    static Core instance;
    return instance;
}

bool Core::Initialize() {
    // Set up logging
    try {
        // PID-based log filename so multiple instances don't clobber each other
        DWORD pid = GetCurrentProcessId();
        std::string logFile = "KenshiOnline_" + std::to_string(pid) + ".log";
        auto logger = spdlog::basic_logger_mt("kenshi_online", logFile, true);
        spdlog::set_default_logger(logger);
        spdlog::set_level(spdlog::level::debug);
        spdlog::flush_on(spdlog::level::debug);
    } catch (...) {
        // Fallback: no file logging
    }

    OutputDebugStringA("KMP: === Kenshi-Online v0.1.0 Initializing ===\n");
    spdlog::info("=== Kenshi-Online v{}.{}.{} Initializing ===", 0, 1, 0);

    m_nativeHud.LogStep("INIT", "Kenshi Online v0.1.0 starting...");

    // Load config
    std::string configPath = ClientConfig::GetDefaultPath();
    m_config.Load(configPath);
    m_nativeHud.LogStep("INIT", "Config loaded");

    // Initialize game offsets (CE fallbacks)
    game::InitOffsetsFromScanner();
    m_nativeHud.LogStep("INIT", "Game offsets initialized");

    // Initialize pattern scanner
    m_nativeHud.LogStep("SCAN", "Pattern scanner starting...");
    if (!InitScanner()) {
        m_nativeHud.LogStep("ERR", "Pattern scanner FAILED");
    } else {
        m_nativeHud.LogStep("OK", "Pattern scanner resolved game functions");
    }

    // Initialize MinHook
    m_nativeHud.LogStep("HOOK", "MinHook initializing...");
    if (!HookManager::Get().Initialize()) {
        m_nativeHud.LogStep("ERR", "MinHook FAILED - cannot install hooks");
        return false;
    }
    m_nativeHud.LogStep("OK", "MinHook ready");

    // Loading guard starts FALSE — only set true when CharacterCreate burst
    // detects actual game loading (not at main menu).
    // m_isLoading = false; // already default
    m_nativeHud.LogStep("INIT", "Loading guard inactive (waiting for game load)");

    // Install hooks
    m_nativeHud.LogStep("HOOK", "Installing game hooks...");
    if (!InitHooks()) {
        m_nativeHud.LogStep("WARN", "Some hooks failed to install");
    } else {
        m_nativeHud.LogStep("OK", "All hooks installed");
    }

    // Initialize networking
    m_nativeHud.LogStep("NET", "Network (ENet) initializing...");
    if (!InitNetwork()) {
        m_nativeHud.LogStep("ERR", "Network init FAILED");
    } else {
        m_nativeHud.LogStep("OK", "Network ready (ENet)");
    }

    // Initialize packet handler
    InitPacketHandler();
    m_nativeHud.LogStep("NET", "Packet handler registered");

    // Set up SpawnManager callback
    m_spawnManager.SetOnSpawnedCallback(
        [this](EntityID netId, void* gameObject) {
            m_entityRegistry.SetGameObject(netId, gameObject);
            const auto* info = m_entityRegistry.GetInfo(netId);
            PlayerID owner = info ? info->ownerPlayerId : 0;
            m_playerController.OnRemoteCharacterSpawned(netId, gameObject, owner);
            game::CharacterAccessor accessor(gameObject);
            std::string charName = accessor.GetName();
            spdlog::info("Core: SpawnManager linked entity {} to game object 0x{:X} (name='{}')",
                         netId, reinterpret_cast<uintptr_t>(gameObject), charName);
            m_overlay.AddSystemMessage("Remote player spawned: " + charName);
        });
    m_nativeHud.LogStep("GAME", "Spawn manager callback set");

    if (!InitUI()) {
        m_nativeHud.LogStep("WARN", "UI init returned false (lazy init later)");
    }

    m_running = true;

    // Register slash commands
    CommandRegistry::Get().RegisterBuiltins();
    m_nativeHud.LogStep("CMD", "Slash commands registered");

    // Start background task orchestrator
    m_orchestrator.Start(2);
    m_nativeHud.LogStep("SYS", "Task orchestrator started (2 workers)");

    // Start network thread
    m_networkThread = std::thread(&Core::NetworkThreadFunc, this);
    m_nativeHud.LogStep("NET", "Network thread started");

    m_nativeHud.LogStep("OK", "=== Initialization complete ===");
    m_nativeHud.LogStep("INIT", "Waiting for game to load...");
    m_nativeHud.LogStep("INIT", "Press F1 for menu | Insert to toggle log");

    OutputDebugStringA("KMP: === Kenshi-Online Initialized Successfully ===\n");
    spdlog::info("=== Kenshi-Online Initialized Successfully ===");
    return true;
}

void Core::Shutdown() {
    spdlog::info("Kenshi-Online shutting down...");

    m_running = false;
    m_connected = false;

    // Stop orchestrator before joining network thread
    m_orchestrator.Stop();

    if (m_networkThread.joinable()) {
        m_networkThread.join();
    }

    m_client.Disconnect();
    m_nativeHud.Shutdown();
    m_overlay.Shutdown();
    HookManager::Get().Shutdown();

    // Save config
    m_config.Save(ClientConfig::GetDefaultPath());

    spdlog::info("Kenshi-Online shutdown complete");
}

bool Core::InitScanner() {
    // ══════════════════════════════════════════════════════════════
    //  LEGACY SCANNER (kept for backward compatibility)
    // ══════════════════════════════════════════════════════════════
    if (!m_scanner.Init(nullptr)) {
        spdlog::error("Failed to init scanner for main executable");
        return false;
    }

    // ══════════════════════════════════════════════════════════════
    //  PATTERN ORCHESTRATOR — Enhanced multi-phase discovery
    // ══════════════════════════════════════════════════════════════
    //  7-phase pipeline:
    //    1. .pdata enumeration (every function in the exe)
    //    2. String discovery + cross-reference analysis
    //    3. VTable scanning + RTTI class hierarchy mapping
    //    4. SIMD batch pattern scan (all patterns in one pass)
    //    5. String xref fallback (for patterns that failed)
    //    6. Call graph analysis + label propagation
    //    7. Global pointer validation
    // ══════════════════════════════════════════════════════════════

    OrchestratorConfig orchConfig;
    orchConfig.enablePData     = true;
    orchConfig.enableStrings   = true;
    orchConfig.enableVTables   = true;
    orchConfig.enableCallGraph = true;
    orchConfig.enableBatchScan = true;
    orchConfig.enableLabelPropagation = true;
    orchConfig.stringMinLength = 4;
    orchConfig.callGraphDepth  = 2;
    orchConfig.fullCallGraph   = false; // Targeted graph for performance
    orchConfig.scanWideStrings = false;

    if (m_patternOrchestrator.Init(nullptr, orchConfig)) {
        // Register all built-in patterns (populates GameFunctions via target pointers)
        m_patternOrchestrator.RegisterBuiltinPatterns(m_gameFuncs);

        // Run the full 7-phase pipeline
        auto report = m_patternOrchestrator.Run();

        m_nativeHud.LogStep("SCAN",
            "Orchestrator: " + std::to_string(report.totalResolved) + "/" +
            std::to_string(report.totalEntries) + " resolved (" +
            std::to_string(report.pdataFunctions) + " .pdata funcs, " +
            std::to_string(report.vtablesFound) + " vtables, " +
            std::to_string(report.labeledFunctions) + " labeled)");
    }

    // Always run legacy scanner too — ensures PlayerBase discovery + proven-working path
    {
        bool resolved = ResolveGameFunctions(m_scanner, m_gameFuncs);
        if (!resolved) {
            spdlog::warn("Game functions minimally resolved: false - some features may not work");
        }
    }

    // ── Function Signature Analysis ──
    // Analyze prologues of all hooked functions to validate our signatures.
    {
        std::vector<FunctionSignature> sigs;

        struct HookSigCheck {
            const char* name;
            void* address;
            int hookParamCount; // params in our hook typedef
        };

        HookSigCheck checks[] = {
            {"CharacterSpawn",       m_gameFuncs.CharacterSpawn,       2}, // factory, templateData
            {"CharacterDestroy",     m_gameFuncs.CharacterDestroy,     1}, // character
            {"CharacterSetPosition", m_gameFuncs.CharacterSetPosition, 2}, // character, Vec3*
            {"CharacterMoveTo",      m_gameFuncs.CharacterMoveTo,      0}, // MID-FUNCTION — do not hook/call
            {"ApplyDamage",          m_gameFuncs.ApplyDamage,          6}, // target, attacker, bodyPart, cut, blunt, pierce
            {"CharacterDeath",       m_gameFuncs.CharacterDeath,       2}, // character, killer
            {"ZoneLoad",             m_gameFuncs.ZoneLoad,             3}, // zoneMgr, zoneX, zoneY
            {"ZoneUnload",           m_gameFuncs.ZoneUnload,           3}, // zoneMgr, zoneX, zoneY
            {"BuildingPlace",        m_gameFuncs.BuildingPlace,        5}, // world, building, x, y, z
            {"SaveGame",             m_gameFuncs.SaveGame,             2}, // saveManager, saveName
            {"LoadGame",             m_gameFuncs.LoadGame,             2}, // saveManager, saveName
        };

        // Map from check name to m_gameFuncs pointer so we can null mismatches
        std::unordered_map<std::string, void**> funcPtrs = {
            {"CharacterMoveTo",      &m_gameFuncs.CharacterMoveTo},
            {"BuildingPlace",        &m_gameFuncs.BuildingPlace},
            {"SaveGame",             &m_gameFuncs.SaveGame},
            {"LoadGame",             &m_gameFuncs.LoadGame},
        };

        for (auto& check : checks) {
            if (!check.address) continue;
            auto sig = FunctionAnalyzer::Analyze(
                reinterpret_cast<uintptr_t>(check.address), check.name);
            if (sig.IsValid()) {
                bool ok = FunctionAnalyzer::ValidateSignature(sig, check.hookParamCount);
                if (!ok) {
                    spdlog::warn("Core: SIGNATURE MISMATCH for '{}' — hook expects {} params, analysis suggests ~{} — DISABLING",
                                 check.name, check.hookParamCount, sig.estimatedParams);
                    // Null out the function pointer so it won't be hooked (prevents crashes)
                    auto it = funcPtrs.find(check.name);
                    if (it != funcPtrs.end() && it->second) {
                        *(it->second) = nullptr;
                        spdlog::warn("Core: Nulled '{}' to prevent crash from bad pattern match", check.name);
                    }
                }
                sigs.push_back(std::move(sig));
            }
        }

        FunctionAnalyzer::LogAnalysis(sigs);
    }

    // Bridge PlayerBase to the game_character module
    if (m_gameFuncs.PlayerBase != 0) {
        game::SetResolvedPlayerBase(m_gameFuncs.PlayerBase);
        spdlog::info("Core: PlayerBase bridged to game_character at 0x{:X}", m_gameFuncs.PlayerBase);
    }

    // Bridge CharacterSetPosition function to game_character module
    if (m_gameFuncs.CharacterSetPosition) {
        game::SetGameSetPositionFn(m_gameFuncs.CharacterSetPosition);
        spdlog::info("Core: SetPosition function bridged at 0x{:X}",
                     reinterpret_cast<uintptr_t>(m_gameFuncs.CharacterSetPosition));
    }

    return m_gameFuncs.IsMinimallyResolved();
}

// Forward declaration — defined below OnGameLoaded
static bool SEH_InstallEntityHooks();

bool Core::InitHooks() {
    bool allOk = true;

    // ══════════════════════════════════════════════════════════════
    // ImGui rendering REMOVED — Ogre3D/DX11 conflict.
    // Using Present hook for HWND/WndProc + OnGameTick only.
    // UI is native MyGUI menu.
    // ══════════════════════════════════════════════════════════════

    // D3D11 Present hook (WndProc input + OnGameTick fallback, NO ImGui rendering)
    m_nativeHud.LogStep("HOOK", "D3D11 Present hook...");
    if (!render_hooks::Install()) {
        m_nativeHud.LogStep("ERR", "Present hook FAILED");
        allOk = false;
    } else {
        m_nativeHud.LogStep("OK", "Present hook installed (WndProc + frame tick)");
    }

    // Input hooks: handled by WndProc in render_hooks now
    // input_hooks not needed

    // ═══════════════════════════════════════════════════════════════════
    // CharacterCreate hook installed IMMEDIATELY — it must be active during
    // game loading to capture the factory pointer and template database.
    // The hook has SEH protection and burst detection, making it safe during
    // loading. All OTHER gameplay hooks remain deferred to OnGameLoaded().
    // ═══════════════════════════════════════════════════════════════════
    if (m_gameFuncs.CharacterSpawn) {
        m_nativeHud.LogStep("HOOK", "Entity hooks (CharacterCreate)...");
        if (SEH_InstallEntityHooks()) {
            m_nativeHud.LogStep("OK", "CharacterCreate hook active (captures factory during loading)");
        } else {
            m_nativeHud.LogStep("ERR", "CharacterCreate hook FAILED");
            allOk = false;
        }
    } else {
        m_nativeHud.LogStep("ERR", "CharacterSpawn pattern NOT FOUND");
    }

    m_nativeHud.LogStep("HOOK", "Other gameplay hooks DEFERRED (after game loads)");

    return allOk;
}

bool Core::InitNetwork() {
    return m_client.Initialize();
}

bool Core::InitUI() {
    // Overlay is initialized lazily when the D3D11 device is available
    // (happens in the Present hook)
    return true;
}

// ── SEH wrappers for OnGameLoaded sub-steps (no C++ objects allowed in __try) ──
static bool SEH_InstallEntityHooks() {
    __try {
        return entity_hooks::Install();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        OutputDebugStringA("KMP: SEH CRASH in entity_hooks::Install()\n");
        return false;
    }
}

static void SEH_PlayerControllerOnGameWorldLoaded(PlayerController& pc) {
    __try {
        pc.OnGameWorldLoaded();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        OutputDebugStringA("KMP: SEH CRASH in PlayerController::OnGameWorldLoaded()\n");
    }
}

static bool SEH_RetryGlobalDiscovery(PatternScanner& scanner, GameFunctions& funcs) {
    __try {
        return RetryGlobalDiscovery(scanner, funcs);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        OutputDebugStringA("KMP: SEH CRASH in RetryGlobalDiscovery()\n");
        return false;
    }
}

static void SEH_ScanGameDataHeap(SpawnManager& sm) {
    __try {
        sm.ScanGameDataHeap();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        OutputDebugStringA("KMP: SEH CRASH in ScanGameDataHeap()\n");
    }
}

void Core::OnGameLoaded() {
    if (m_gameLoaded.exchange(true)) return; // Only run once

    m_nativeHud.LogStep("GAME", "=== Game world loaded! ===");
    OutputDebugStringA("KMP: === Core::OnGameLoaded() START ===\n");

    // ── Clear loading guard ──
    m_isLoading = false;
    building_hooks::SetLoading(false);
    inventory_hooks::SetLoading(false);
    squad_hooks::SetLoading(false);
    faction_hooks::SetLoading(false);
    m_nativeHud.LogStep("GAME", "Loading guard cleared (hooks active)");

    // ═══ Install remaining gameplay hooks now that loading is complete ═══
    // (CharacterCreate was already installed in InitHooks to capture the loading burst)
    m_nativeHud.LogStep("HOOK", "Installing deferred gameplay hooks...");

    // ═══ ESSENTIAL MULTIPLAYER HOOKS ═══
    // Only install hooks with verified patterns. Non-essential hooks (save,
    // inventory, building, combat, world) are skipped to avoid heap corruption
    // from unverified patterns.
    m_nativeHud.LogStep("HOOK", "Non-essential hooks SKIPPED (save/inventory/building/combat/world)");
    spdlog::info("Core: Skipping non-essential hooks (save/inventory/building/combat/world)");

    // Time hooks — ESSENTIAL (drives OnGameTick from the game's own time system)
    if (m_gameFuncs.TimeUpdate) {
        m_nativeHud.LogStep("HOOK", "Time hooks...");
        if (time_hooks::Install()) {
            m_nativeHud.LogStep("OK", "Time hooks installed");
        } else {
            m_nativeHud.LogStep("WARN", "Time hooks partial failure");
        }
    }

    // GameFrameUpdate hook — ESSENTIAL (direct spawn fallback + deferred probes)
    // This hook processes the spawn queue when no natural CharacterCreate events fire.
    // Without it, remote characters can never be spawned after game loading.
    if (m_gameFuncs.GameFrameUpdate) {
        m_nativeHud.LogStep("HOOK", "Game tick hooks (spawn processor)...");
        if (game_tick_hooks::Install()) {
            m_nativeHud.LogStep("OK", "Game tick hooks installed (spawn fallback active)");
        } else {
            m_nativeHud.LogStep("WARN", "Game tick hooks failed");
        }
    } else {
        m_nativeHud.LogStep("WARN", "GameFrameUpdate not found — spawn fallback unavailable");
    }

    // Movement hooks — position sync (only if patterns were validated)
    if (m_gameFuncs.CharacterSetPosition || m_gameFuncs.CharacterMoveTo) {
        m_nativeHud.LogStep("HOOK", "Movement hooks...");
        if (movement_hooks::Install()) {
            m_nativeHud.LogStep("OK", "Movement hooks installed");
        } else {
            m_nativeHud.LogStep("WARN", "Movement hooks partial failure");
        }
    } else {
        m_nativeHud.LogStep("HOOK", "Movement hooks SKIPPED (patterns not found)");
    }

    // Squad hooks — ESSENTIAL for squad injection (remote characters need squad membership)
    // AddCharacterToLocalSquad requires s_origSquadAddMember, which is populated by Install()
    if (m_gameFuncs.SquadCreate || m_gameFuncs.SquadAddMember) {
        m_nativeHud.LogStep("HOOK", "Squad hooks (squad injection)...");
        if (squad_hooks::Install()) {
            m_nativeHud.LogStep("OK", "Squad hooks installed (squad injection available)");
        } else {
            m_nativeHud.LogStep("WARN", "Squad hooks failed");
        }
    } else {
        m_nativeHud.LogStep("WARN", "Squad functions not found — squad injection unavailable");
    }

    // AI hooks — defense-in-depth for remote character AI controller management
    if (m_gameFuncs.AICreate || m_gameFuncs.AIPackages) {
        m_nativeHud.LogStep("HOOK", "AI hooks...");
        if (ai_hooks::Install()) {
            m_nativeHud.LogStep("OK", "AI hooks installed");
        } else {
            m_nativeHud.LogStep("WARN", "AI hooks partial failure");
        }
    } else {
        m_nativeHud.LogStep("HOOK", "AI hooks SKIPPED (patterns not found)");
    }

    // Run deferred PlayerBase discovery FIRST so OnGameWorldLoaded can use CharacterIterator
    m_nativeHud.LogStep("SCAN", "PlayerBase discovery...");
    bool needsRetry = (m_gameFuncs.PlayerBase == 0);
    if (!needsRetry && m_gameFuncs.PlayerBase != 0) {
        uintptr_t val = 0;
        needsRetry = !Memory::Read(m_gameFuncs.PlayerBase, val) || val == 0 ||
                     val < 0x10000 || val > 0x00007FFFFFFFFFFF;
    }

    if (needsRetry) {
        m_nativeHud.LogStep("SCAN", "Retrying global discovery...");
        if (SEH_RetryGlobalDiscovery(m_scanner, m_gameFuncs)) {
            game::SetResolvedPlayerBase(m_gameFuncs.PlayerBase);
            m_nativeHud.LogStep("OK", "Global discovery succeeded");
        } else {
            m_nativeHud.LogStep("WARN", "Global discovery failed — faction capture will use entity_hooks bootstrap");
        }
    } else {
        m_nativeHud.LogStep("OK", "PlayerBase already valid");
    }

    // Always set GameWorld bridge (CharacterIterator uses it as fallback when PlayerBase fails)
    if (m_gameFuncs.GameWorldSingleton != 0) {
        game::SetResolvedGameWorld(m_gameFuncs.GameWorldSingleton);
        m_nativeHud.LogStep("OK", "GameWorld bridge set (fallback for CharacterIterator)");
    }

    // Now notify player controller (can use CharacterIterator if PlayerBase is resolved)
    m_nativeHud.LogStep("GAME", "Player controller: OnGameWorldLoaded...");
    SEH_PlayerControllerOnGameWorldLoaded(m_playerController);
    m_nativeHud.LogStep("OK", "Player controller ready");

    // ═══ Verify spawn system readiness ═══
    m_nativeHud.LogStep("GAME", "Verifying spawn system...");
    bool spawnReady = m_spawnManager.VerifyReadiness();
    if (spawnReady) {
        m_nativeHud.LogStep("OK", "Spawn system ready");
    } else {
        m_nativeHud.LogStep("WARN", "Spawn system NOT ready — remote characters may fail");
    }

    // Early heap scan — try even without factory, as long as we have ANY data to bootstrap from
    if (m_spawnManager.GetManagerPointer() != 0 || m_spawnManager.GetTemplateCount() > 0) {
        m_nativeHud.LogStep("GAME", "Early heap scan for templates...");
        SEH_ScanGameDataHeap(m_spawnManager);
        m_nativeHud.LogStep("OK", "Heap scan done (" + std::to_string(m_spawnManager.GetTemplateCount()) + " templates)");
    } else if (m_spawnManager.IsReady()) {
        // Factory captured but no manager pointer yet — still try scan
        m_nativeHud.LogStep("GAME", "Heap scan (factory only, no manager ptr)...");
        SEH_ScanGameDataHeap(m_spawnManager);
        m_nativeHud.LogStep("OK", "Heap scan done (" + std::to_string(m_spawnManager.GetTemplateCount()) + " templates)");
    }

    m_nativeHud.LogStep("GAME", "Ready! Press F1 for multiplayer menu");
    OutputDebugStringA("KMP: === Core::OnGameLoaded() COMPLETE ===\n");
}

void Core::NetworkThreadFunc() {
    spdlog::info("Network thread started");

    while (m_running) {
        // Always pump ENet events — handles async connect, receive, and disconnect
        m_client.Update();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    spdlog::info("Network thread stopped");
}

void Core::SendExistingEntitiesToServer() {
    // After connecting, scan existing characters and register+send them.
    // IMPORTANT: Only send characters from the player's faction (squad members).
    // Sending ALL characters would flood the server with hundreds of NPCs.
    //
    // TWO character sources are tried:
    //   1. CharacterIterator (PlayerBase/GameWorld — fast but may fail on Steam)
    //   2. entity_hooks loading cache (populated during game loading — always available)

    int count = 0;
    int skippedFaction = 0;

    // ── Resolve faction ──
    // Priority: PlayerController → entity_hooks captured faction
    uintptr_t playerFaction = m_playerController.GetLocalFactionPtr();
    if (playerFaction == 0) {
        playerFaction = entity_hooks::GetCapturedFaction();
        if (playerFaction != 0) {
            spdlog::info("Core: Using entity_hooks captured faction 0x{:X} (PlayerController had none)",
                         playerFaction);
            m_playerController.SetLocalFactionPtr(playerFaction);
        }
    }

    // ── Strategy 1: CharacterIterator ──
    game::CharacterIterator iter;
    bool iteratorWorked = (iter.Count() > 0);

    if (iteratorWorked && playerFaction != 0) {
        spdlog::info("Core: Scanning {} characters via CharacterIterator (faction=0x{:X})...",
                     iter.Count(), playerFaction);

        while (iter.HasNext()) {
            game::CharacterAccessor character = iter.Next();
            if (!character.IsValid()) continue;

            Vec3 pos = character.GetPosition();
            if (pos.x == 0.f && pos.y == 0.f && pos.z == 0.f) continue;

            uintptr_t charFaction = character.GetFactionPtr();
            if (charFaction != playerFaction) {
                skippedFaction++;
                continue;
            }

            void* gameObj = reinterpret_cast<void*>(character.GetPtr());
            if (m_entityRegistry.GetNetId(gameObj) != INVALID_ENTITY) continue;

            EntityID netId = m_entityRegistry.Register(gameObj, EntityType::NPC, m_localPlayerId);
            m_entityRegistry.UpdatePosition(netId, pos);

            Quat rot = character.GetRotation();
            m_entityRegistry.UpdateRotation(netId, rot);

            uintptr_t factionPtr = character.GetFactionPtr();
            uint32_t factionId = 0;
            if (factionPtr != 0) Memory::Read(factionPtr + 0x08, factionId);

            std::string charName = character.GetName();

            PacketWriter writer;
            writer.WriteHeader(MessageType::C2S_EntitySpawnReq);
            writer.WriteU32(netId);
            writer.WriteU8(static_cast<uint8_t>(EntityType::NPC));
            writer.WriteU32(m_localPlayerId);
            writer.WriteU32(0);
            writer.WriteF32(pos.x);
            writer.WriteF32(pos.y);
            writer.WriteF32(pos.z);
            writer.WriteU32(rot.Compress());
            writer.WriteU32(factionId);
            uint16_t nameLen = static_cast<uint16_t>(std::min<size_t>(charName.size(), 255));
            writer.WriteU16(nameLen);
            if (nameLen > 0) writer.WriteRaw(charName.data(), nameLen);

            m_client.SendReliable(writer.Data(), writer.Size());
            count++;
        }
    }

    // ── Strategy 2: entity_hooks loading cache fallback ──
    // If CharacterIterator found nothing OR faction was unknown, try the loading cache.
    // Characters were cached during Hook_CharacterCreate before connection.
    if (count == 0) {
        const auto& cached = entity_hooks::GetLoadingCharacters();

        if (!cached.empty()) {
            // If faction STILL unknown, take it from the most common faction in the cache
            if (playerFaction == 0 && !cached.empty()) {
                // Use first character's faction as player faction heuristic
                for (const auto& cc : cached) {
                    if (cc.factionPtr != 0) {
                        playerFaction = cc.factionPtr;
                        m_playerController.SetLocalFactionPtr(playerFaction);
                        spdlog::info("Core: FALLBACK — captured faction 0x{:X} from loading cache", playerFaction);
                        break;
                    }
                }
            }

            spdlog::info("Core: CharacterIterator {} — using loading cache ({} chars, faction=0x{:X})",
                         iteratorWorked ? "found no squad members" : "FAILED (0 chars)",
                         cached.size(), playerFaction);

            for (const auto& cc : cached) {
                if (cc.x == 0.f && cc.y == 0.f && cc.z == 0.f) continue;

                // Apply faction filter (with cache fallback: if NO faction, send first 4 chars)
                if (playerFaction != 0 && cc.factionPtr != playerFaction) {
                    skippedFaction++;
                    continue;
                }
                // If still no faction at all, cap at 4 characters to avoid NPC flood
                if (playerFaction == 0 && count >= 4) break;

                if (m_entityRegistry.GetNetId(cc.gameObj) != INVALID_ENTITY) continue;

                Vec3 pos(cc.x, cc.y, cc.z);
                EntityID netId = m_entityRegistry.Register(cc.gameObj, EntityType::NPC, m_localPlayerId);
                m_entityRegistry.UpdatePosition(netId, pos);

                // Read rotation from game object (may be stale but good enough)
                game::CharacterAccessor accessor(cc.gameObj);
                Quat rot = accessor.GetRotation();
                m_entityRegistry.UpdateRotation(netId, rot);

                std::string charName = accessor.GetName();

                PacketWriter writer;
                writer.WriteHeader(MessageType::C2S_EntitySpawnReq);
                writer.WriteU32(netId);
                writer.WriteU8(static_cast<uint8_t>(EntityType::NPC));
                writer.WriteU32(m_localPlayerId);
                writer.WriteU32(0);
                writer.WriteF32(pos.x);
                writer.WriteF32(pos.y);
                writer.WriteF32(pos.z);
                writer.WriteU32(rot.Compress());
                writer.WriteU32(cc.factionId);
                uint16_t nameLen = static_cast<uint16_t>(std::min<size_t>(charName.size(), 255));
                writer.WriteU16(nameLen);
                if (nameLen > 0) writer.WriteRaw(charName.data(), nameLen);

                m_client.SendReliable(writer.Data(), writer.Size());
                count++;
            }
        } else {
            spdlog::warn("Core: Both CharacterIterator AND loading cache empty — no characters to send");
        }
    }

    spdlog::info("Core: Sent {} squad characters to server (skipped {} non-squad NPCs, "
                 "iterator={}, cache={})",
                 count, skippedFaction,
                 iteratorWorked ? "ok" : "FAILED",
                 entity_hooks::GetLoadingCharacters().size());

    if (count > 0) {
        m_overlay.AddSystemMessage("Syncing " + std::to_string(count) + " squad characters...");
    }
}

void Core::OnGameTick(float deltaTime) {
    // ── Pre-check diagnostics ──
    static int s_preCheckCount = 0;
    s_preCheckCount++;
    if (s_preCheckCount <= 5 || s_preCheckCount % 3000 == 0) {
        char buf[128];
        sprintf_s(buf, "KMP: OnGameTick PRE-CHECK #%d connected=%d\n",
                  s_preCheckCount, m_connected.load() ? 1 : 0);
        OutputDebugStringA(buf);
    }

    if (!m_connected) return;

    // ── Per-frame dedup guard ──
    // OnGameTick is driven from BOTH time_hooks (TimeUpdate) and render_hooks (Present)
    // as a redundancy measure. But the pipeline (buffer swap, background work kick)
    // is NOT idempotent — double-swapping reverses the first swap. Use a minimum
    // interval to ensure only one full tick processes per frame (~6ms = 166fps max).
    {
        static auto s_lastTickTime = std::chrono::steady_clock::time_point{};
        auto now = std::chrono::steady_clock::now();
        auto sinceLast = std::chrono::duration_cast<std::chrono::microseconds>(now - s_lastTickTime);
        if (sinceLast.count() < 4000) return; // Skip if called within 4ms (same frame)
        s_lastTickTime = now;
    }

    // ── Step tracking: log which step we're at so SEH crashes can be diagnosed ──
    static int s_lastCompletedStep = -1;
    static int s_tickCallCount = 0;
    s_tickCallCount++;

    // Log first few successful entries to confirm OnGameTick is running
    if (s_tickCallCount <= 5) {
        char buf[128];
        sprintf_s(buf, "KMP: OnGameTick ENTERED #%d (dt=%.4f)\n", s_tickCallCount, deltaTime);
        OutputDebugStringA(buf);
        spdlog::info("Core::OnGameTick ENTERED (call #{}, dt={:.4f}, lastStep={})",
                     s_tickCallCount, deltaTime, s_lastCompletedStep);
    }
    s_lastCompletedStep = 0;

    // ── Step 1: Entity scan with retry ──
    // SendExistingEntitiesToServer may fail on first attempt if CharacterIterator
    // can't resolve PlayerBase/GameWorld. Retry every 150 ticks (~1 second) for
    // up to 30 seconds until at least one character is sent.
    if (!m_initialEntityScanDone) {
        static int s_entityScanRetries = 0;
        static constexpr int MAX_ENTITY_SCAN_RETRIES = 45; // ~30 seconds at 150fps
        static constexpr int RETRY_INTERVAL_TICKS = 150;

        bool shouldAttempt = (s_entityScanRetries == 0) ||
                             (s_tickCallCount % RETRY_INTERVAL_TICKS == 0);

        if (shouldAttempt) {
            if (s_entityScanRetries == 0) {
                spdlog::info("Core::OnGameTick: Step 1 — SendExistingEntitiesToServer (first attempt)");
                m_nativeHud.LogStep("GAME", "Scanning local squad characters...");
            }

            SendExistingEntitiesToServer();
            auto localCount = m_entityRegistry.GetPlayerEntities(m_localPlayerId).size();
            s_entityScanRetries++;

            if (localCount > 0) {
                m_initialEntityScanDone = true;
                m_nativeHud.LogStep("OK", "Sent " + std::to_string(localCount) + " characters to server");
                spdlog::info("Core: Entity scan succeeded on attempt {} — {} characters", s_entityScanRetries, localCount);
            } else if (s_entityScanRetries >= MAX_ENTITY_SCAN_RETRIES) {
                m_initialEntityScanDone = true;
                m_nativeHud.LogStep("WARN", "Entity scan: no squad characters found after " +
                                    std::to_string(s_entityScanRetries) + " attempts");
                spdlog::warn("Core: Entity scan exhausted {} retries — 0 characters found", s_entityScanRetries);
            } else if (s_entityScanRetries <= 3 || s_entityScanRetries % 10 == 0) {
                spdlog::info("Core: Entity scan attempt {} found 0 chars — will retry", s_entityScanRetries);
            }
        }
    }

    // ── Step 1b: Deferred re-scan after faction bootstrap ──
    // entity_hooks bootstraps the faction from the first character it sees.
    // After that, we re-scan to register the player's existing squad members
    // (which loaded before hooks were installed).
    if (m_needsEntityRescan.exchange(false)) {
        spdlog::info("Core::OnGameTick: Faction bootstrap triggered — re-scanning existing characters");
        m_nativeHud.LogStep("GAME", "Faction bootstrapped — re-scanning squad...");
        SendExistingEntitiesToServer();
        auto localCount = m_entityRegistry.GetPlayerEntities(m_localPlayerId).size();
        m_nativeHud.LogStep("OK", "Re-scan found " + std::to_string(localCount) + " squad characters");
        if (localCount > 0) {
            m_initialEntityScanDone = true; // Stop retrying
        }
    }
    s_lastCompletedStep = 1;

    // ── Step 2: Wait for previous frame's background work ──
    if (m_pipelineStarted) {
        m_orchestrator.WaitForFrameWork();
    }
    s_lastCompletedStep = 2;

    // ── Step 3: Swap double buffers ──
    if (m_pipelineStarted) {
        std::swap(m_readBuffer, m_writeBuffer);
    }
    s_lastCompletedStep = 3;

    // ── Step 4: Update interpolation timers ──
    m_interpolation.Update(deltaTime);
    s_lastCompletedStep = 4;

    // ── Step 5: Apply remote positions from read buffer (game thread only) ──
    if (m_pipelineStarted) {
        ApplyRemotePositions();
    }
    s_lastCompletedStep = 5;

    // ── Step 5b: Poll local character positions (replaces SetPosition hook) ──
    // SetPosition hook crashes due to `mov rax, rsp` trampoline corruption.
    // Instead, we poll local character positions once per tick and send updates.
    PollLocalPositions();

    // ── Step 6: Send cached position packets from read buffer ──
    if (m_pipelineStarted) {
        SendCachedPackets();
    }
    s_lastCompletedStep = 6;

    // ── Step 7: Handle spawn queue (game thread — calls factory) ──
    HandleSpawnQueue();
    s_lastCompletedStep = 7;

    // ── Step 8: Handle host teleport (one-time joiner teleport) ──
    HandleHostTeleport();
    s_lastCompletedStep = 8;

    // ── Step 9: Kick off background work for this frame ──
    m_frameData[m_writeBuffer].Clear();
    KickBackgroundWork();
    m_pipelineStarted = true;
    s_lastCompletedStep = 9;

    // ── Step 10: Diagnostics ──
    UpdateDiagnostics(deltaTime);
    s_lastCompletedStep = 10;
}

// ════════════════════════════════════════════════════════════════════════════
// Staged Pipeline Methods
// ════════════════════════════════════════════════════════════════════════════

// SEH-protected per-entity position/rotation write.
// WritePosition follows chains of game memory pointers (physics engine, Havok, etc.)
// that may become invalid if the character is freed or mid-transition.
// Without SEH, an AV here → exit(1) → atexit crash on freed trampolines.
// CharacterAccessor and Vec3/Quat are trivially destructible (safe with __try).
static bool SEH_WritePositionRotation(void* gameObj, Vec3 pos, Quat rot) {
    __try {
        game::CharacterAccessor accessor(gameObj);
        accessor.WritePosition(pos);

        auto& offsets = game::GetOffsets().character;
        if (offsets.rotation >= 0) {
            uintptr_t charPtr = reinterpret_cast<uintptr_t>(gameObj);
            Memory::Write(charPtr + offsets.rotation, rot);
        }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        static int s_writeAvCount = 0;
        if (++s_writeAvCount <= 10) {
            char buf[128];
            sprintf_s(buf, "KMP: SEH_WritePositionRotation CRASHED for gameObj 0x%p\n", gameObj);
            OutputDebugStringA(buf);
        }
        return false;
    }
}

void Core::ApplyRemotePositions() {
    auto& readFrame = m_frameData[m_readBuffer];
    if (!readFrame.ready) return;

    static int s_applyCount = 0;
    static int s_noObjCount = 0;
    static int s_avCount = 0;
    int applied = 0;
    int noObj = 0;
    int avErrors = 0;

    for (auto& result : readFrame.remoteResults) {
        if (!result.valid) continue;

        void* gameObj = m_entityRegistry.GetGameObject(result.netId);
        if (!gameObj) {
            noObj++;
            continue;
        }

        // SEH-protected write — catches AV from freed/invalid game objects
        if (SEH_WritePositionRotation(gameObj, result.position, result.rotation)) {
            // Update registry tracking (safe — our own code)
            m_entityRegistry.UpdatePosition(result.netId, result.position);
            m_entityRegistry.UpdateRotation(result.netId, result.rotation);
            applied++;
        } else {
            avErrors++;
            // Unlink the bad game object so we stop crashing every frame
            m_entityRegistry.SetGameObject(result.netId, nullptr);
        }
    }

    s_applyCount += applied;
    s_noObjCount += noObj;
    s_avCount += avErrors;

    // Log first few applications, then every 100th
    if (applied > 0 && (s_applyCount <= 5 || s_applyCount % 100 == 0)) {
        spdlog::info("Core::ApplyRemotePositions: applied {} this frame (total={}, noObj={}, avErrors={})",
                     applied, s_applyCount, s_noObjCount, s_avCount);
    }
}

// SEH-protected position read from a game character object.
// Returns false if the read crashes (freed object, invalid pointer chain).
static bool SEH_ReadPosition(void* gameObj, Vec3& outPos, Quat& outRot) {
    __try {
        game::CharacterAccessor accessor(gameObj);
        outPos = accessor.GetPosition();
        outRot = accessor.GetRotation();
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void Core::PollLocalPositions() {
    if (!m_connected || m_localPlayerId == 0) return;

    // Throttle to tick rate
    static auto s_lastPollTime = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - s_lastPollTime);
    if (elapsed.count() < KMP_TICK_INTERVAL_MS) return;
    s_lastPollTime = now;

    auto localEntities = m_entityRegistry.GetPlayerEntities(m_localPlayerId);
    if (localEntities.empty()) return;

    static int s_pollsSent = 0;

    for (EntityID netId : localEntities) {
        auto* info = m_entityRegistry.GetInfo(netId);
        if (!info) continue;

        void* gameObj = m_entityRegistry.GetGameObject(netId);
        if (!gameObj) continue;

        Vec3 pos;
        Quat rotation;
        if (!SEH_ReadPosition(gameObj, pos, rotation)) {
            // Object freed or invalid — unlink
            m_entityRegistry.SetGameObject(netId, nullptr);
            continue;
        }

        // Skip if position hasn't changed enough
        if (pos.DistanceTo(info->lastPosition) < KMP_POS_CHANGE_THRESHOLD) continue;

        // Compute move speed from position delta
        float elapsedSec = elapsed.count() / 1000.f;
        float dist = pos.DistanceTo(info->lastPosition);
        float moveSpeed = (elapsedSec > 0.001f) ? dist / elapsedSec : 0.f;

        uint32_t compQuat = rotation.Compress();

        // Derive animation state from speed
        uint8_t animState = 0;
        if (moveSpeed > 5.0f) animState = 2; // running
        else if (moveSpeed > 0.5f) animState = 1; // walking

        uint8_t moveSpeedU8 = static_cast<uint8_t>(
            std::min(255.f, moveSpeed / 15.f * 255.f));

        uint16_t flags = 0;
        if (moveSpeed > 3.0f) flags |= 0x01; // running

        PacketWriter writer;
        writer.WriteHeader(MessageType::C2S_PositionUpdate);
        writer.WriteU8(1);
        writer.WriteU32(netId);
        writer.WriteF32(pos.x);
        writer.WriteF32(pos.y);
        writer.WriteF32(pos.z);
        writer.WriteU32(compQuat);
        writer.WriteU8(animState);
        writer.WriteU8(moveSpeedU8);
        writer.WriteU16(flags);

        m_client.SendUnreliable(writer.Data(), writer.Size());

        m_entityRegistry.UpdatePosition(netId, pos);
        m_entityRegistry.UpdateRotation(netId, rotation);

        s_pollsSent++;
        if (s_pollsSent <= 20 || s_pollsSent % 200 == 0) {
            spdlog::debug("Core::PollLocalPositions: sent #{} netId={} pos=({:.1f},{:.1f},{:.1f}) speed={:.1f}",
                          s_pollsSent, netId, pos.x, pos.y, pos.z, moveSpeed);
        }
    }
}

void Core::SendCachedPackets() {
    auto& readFrame = m_frameData[m_readBuffer];
    if (!readFrame.ready || readFrame.packetBytes.empty()) return;

    m_client.SendUnreliable(readFrame.packetBytes.data(), readFrame.packetBytes.size());
}

void Core::HandleHostTeleport() {
    if (!m_hasHostSpawnPoint || m_spawnTeleportDone || m_isHost) return;

    if (!m_hostTpTimerStarted) {
        m_hostTpTimerStarted = true;
        m_hostTpTimer = std::chrono::steady_clock::now();
        spdlog::info("Core: Host spawn point received ({:.1f}, {:.1f}, {:.1f}), "
                     "waiting 2s before teleport",
                     m_hostSpawnPoint.x, m_hostSpawnPoint.y, m_hostSpawnPoint.z);
        m_nativeHud.LogStep("GAME", "Host position received, teleporting in 2s...");
    }

    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - m_hostTpTimer);

    if (elapsed.count() < 2) return;

    auto localEntities = m_entityRegistry.GetPlayerEntities(m_localPlayerId);
    if (localEntities.empty()) {
        spdlog::debug("Core: Waiting for local entities to register before teleporting...");
        return;
    }

    int teleported = 0;
    for (EntityID netId : localEntities) {
        void* gameObj = m_entityRegistry.GetGameObject(netId);
        if (!gameObj) continue;

        game::CharacterAccessor accessor(gameObj);
        if (!accessor.IsValid()) continue;

        Vec3 spawnPos = m_hostSpawnPoint;
        spawnPos.x += static_cast<float>(teleported % 4) * 3.0f;
        spawnPos.z += static_cast<float>(teleported / 4) * 3.0f;

        if (accessor.WritePosition(spawnPos)) {
            m_entityRegistry.UpdatePosition(netId, spawnPos);
            teleported++;
        }
    }

    if (teleported > 0) {
        m_spawnTeleportDone = true;
        spdlog::info("Core: Teleported {} local characters to host at ({:.1f}, {:.1f}, {:.1f})",
                     teleported, m_hostSpawnPoint.x, m_hostSpawnPoint.y, m_hostSpawnPoint.z);
        m_overlay.AddSystemMessage("Teleported to host location!");
        m_nativeHud.AddSystemMessage("Teleported " + std::to_string(teleported)
                                     + " characters to host!");
        m_nativeHud.LogStep("OK", "Teleported to host location!");
    }
}

void Core::HandleSpawnQueue() {
    // One-shot heap scan
    static bool heapScanned = false;
    if (!heapScanned && m_spawnManager.IsReady()) {
        if (m_spawnManager.GetManagerPointer() != 0 || m_spawnManager.GetTemplateCount() < 10) {
            spdlog::info("Core: Triggering GameData heap scan (manager=0x{:X}, templates={})...",
                         m_spawnManager.GetManagerPointer(), m_spawnManager.GetTemplateCount());
            m_spawnManager.ScanGameDataHeap();
            heapScanned = true;
            spdlog::info("Core: Heap scan complete, {} templates available",
                         m_spawnManager.GetTemplateCount());
        }
    }

    // Spawn queue fallback logic
    static auto s_lastSpawnLog = std::chrono::steady_clock::now();
    static auto s_firstPendingTime = std::chrono::steady_clock::time_point{};
    static bool s_hasPendingTimer = false;
    static int s_directSpawnAttempts = 0;
    static bool s_shownWaitingMsg = false;
    static bool s_shownTimeoutMsg = false;

    size_t pending = m_spawnManager.GetPendingSpawnCount();

    if (pending > 0 && !s_hasPendingTimer) {
        s_firstPendingTime = std::chrono::steady_clock::now();
        s_hasPendingTimer = true;
        s_shownWaitingMsg = false;
        s_shownTimeoutMsg = false;
        spdlog::info("Core: {} spawn(s) queued — waiting for game to create an NPC...", pending);
        m_nativeHud.LogStep("GAME", std::to_string(pending) + " spawn(s) queued");
        m_nativeHud.AddSystemMessage("Waiting for game to create an NPC for remote player...");
    } else if (pending == 0) {
        s_hasPendingTimer = false;
        s_directSpawnAttempts = 0;
        s_shownWaitingMsg = false;
        s_shownTimeoutMsg = false;
    }

    // Periodic status updates while waiting
    if (pending > 0 && s_hasPendingTimer) {
        auto pendingDuration = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - s_firstPendingTime);

        // At 10s, show encouraging message
        if (pendingDuration.count() >= 10 && !s_shownWaitingMsg) {
            s_shownWaitingMsg = true;
            m_nativeHud.AddSystemMessage("Still waiting for NPC creation... Walk near a town or camp.");
            m_nativeHud.LogStep("GAME", "Waiting for NPC spawn event (walk near town)...");
        }

        // At 30s, show timeout warning
        if (pendingDuration.count() >= 30 && !s_shownTimeoutMsg) {
            s_shownTimeoutMsg = true;
            spdlog::warn("Core: Spawn queue waiting 30s+ — no NPC creation events detected. "
                         "The CharacterCreate hook may not be firing.");
            m_nativeHud.AddSystemMessage("Spawn timeout! Try walking near NPCs or entering a town.");
            m_nativeHud.LogStep("WARN", "Spawn queue timeout (30s) — need NPC activity");
        }
    }

    auto sinceLog = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - s_lastSpawnLog);
    if (sinceLog.count() >= 5 && pending > 0) {
        s_lastSpawnLog = std::chrono::steady_clock::now();
        spdlog::info("Core::HandleSpawnQueue: {} pending spawns (inPlaceCount={}, charTemplates={}, "
                     "factoryReady={}, hasPreCall={})",
                     pending, entity_hooks::GetInPlaceSpawnCount(),
                     m_spawnManager.GetCharacterTemplateCount(),
                     m_spawnManager.IsReady(),
                     m_spawnManager.HasPreCallData());
    }

    if (pending > 0 && s_hasPendingTimer && m_spawnManager.HasPreCallData()) {
        auto pendingDuration = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - s_firstPendingTime);

        if (pendingDuration.count() >= 5 && s_directSpawnAttempts < 10) {
            static auto s_lastDirectAttempt = std::chrono::steady_clock::time_point{};
            auto sinceLast = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - s_lastDirectAttempt);

            if (s_directSpawnAttempts == 0 || sinceLast.count() >= 3) {
                s_lastDirectAttempt = std::chrono::steady_clock::now();
                s_directSpawnAttempts++;

                spdlog::info("Core: FALLBACK DIRECT SPAWN attempt #{} ({} pending, "
                             "{}s since queued)", s_directSpawnAttempts, pending,
                             pendingDuration.count());
                m_nativeHud.LogStep("GAME", "Fallback spawn attempt #" +
                              std::to_string(s_directSpawnAttempts) + "...");

                SpawnRequest req;
                if (m_spawnManager.PopNextSpawn(req)) {
                    void* character = m_spawnManager.SpawnCharacterDirect(&req.position);
                    if (character) {
                        spdlog::info("Core: FALLBACK SPAWN SUCCESS! entity={} char=0x{:X}",
                                     req.netId, reinterpret_cast<uintptr_t>(character));

                        game::CharacterAccessor accessor(character);
                        if (req.position.x != 0.f || req.position.y != 0.f || req.position.z != 0.f) {
                            accessor.WritePosition(req.position);
                        }

                        // 1. Link to EntityRegistry
                        m_entityRegistry.SetGameObject(req.netId, character);
                        m_entityRegistry.UpdatePosition(req.netId, req.position);

                        // 2. Set name + faction
                        m_playerController.OnRemoteCharacterSpawned(
                            req.netId, character, req.owner);

                        // 3. Mark as remote-controlled (AI decisions overridden)
                        ai_hooks::MarkRemoteControlled(character);

                        // 4. Squad injection (engine exploit — makes char selectable)
                        squad_hooks::AddCharacterToLocalSquad(character);

                        // 5. Set isPlayerControlled flag (engine exploit)
                        game::WritePlayerControlled(
                            reinterpret_cast<uintptr_t>(character), true);

                        // 6. Schedule deferred AnimClass probe
                        game::ScheduleDeferredAnimClassProbe(
                            reinterpret_cast<uintptr_t>(character));

                        m_nativeHud.LogStep("OK", "Remote player spawned!");
                        m_overlay.AddSystemMessage("Remote player character spawned!");
                        m_nativeHud.AddSystemMessage("Remote player spawned nearby!");
                    } else {
                        spdlog::warn("Core: FALLBACK SPAWN FAILED for entity {} (attempt #{})",
                                     req.netId, s_directSpawnAttempts);
                        req.retryCount++;
                        if (req.retryCount < MAX_SPAWN_RETRIES) {
                            m_spawnManager.RequeueSpawn(req);
                        }
                        m_nativeHud.LogStep("WARN", "Spawn attempt failed, retrying...");
                    }
                }
            }
        }
    }
}

void Core::KickBackgroundWork() {
    m_orchestrator.PostFrameWork([this] { BackgroundReadEntities(); });
    m_orchestrator.PostFrameWork([this] { BackgroundInterpolate(); });
}

void Core::UpdateDiagnostics(float deltaTime) {
    static int s_tickCount = 0;
    static auto s_lastTickLog = std::chrono::steady_clock::now();
    s_tickCount++;
    auto tickNow = std::chrono::steady_clock::now();
    auto tickElapsed = std::chrono::duration_cast<std::chrono::seconds>(tickNow - s_lastTickLog);
    if (tickElapsed.count() >= 5) {
        spdlog::info("Core::OnGameTick: {} ticks in last {}s (dt={:.4f}), entities={}, remote={}",
                      s_tickCount, tickElapsed.count(), deltaTime,
                      m_entityRegistry.GetEntityCount(),
                      m_entityRegistry.GetRemoteCount());
        s_tickCount = 0;
        s_lastTickLog = tickNow;
    }
}

// ════════════════════════════════════════════════════════════════════════════
// Background Worker Methods (run on orchestrator threads)
// ════════════════════════════════════════════════════════════════════════════

void Core::BackgroundReadEntities() {
    auto& writeFrame = m_frameData[m_writeBuffer];

    auto localEntities = m_entityRegistry.GetPlayerEntities(m_localPlayerId);

    static bool s_firstIterLog = true;
    if (s_firstIterLog && !localEntities.empty()) {
        spdlog::info("Core: Background read — {} local entities in registry", localEntities.size());
        s_firstIterLog = false;
    }

    struct PendingPos {
        CharacterPosition cp;
        EntityID netId;
        Vec3 pos;
        Quat rot;
    };
    std::vector<PendingPos> pendingPositions;

    for (EntityID netId : localEntities) {
        void* gameObj = m_entityRegistry.GetGameObject(netId);
        if (!gameObj) continue;

        game::CharacterAccessor character(gameObj);
        if (!character.IsValid()) continue;

        EntityInfo infoCopy;
        {
            auto* info = m_entityRegistry.GetInfo(netId);
            if (!info || info->isRemote) continue;
            infoCopy = *info;
        }

        Vec3 pos = character.GetPosition();
        Quat rot = character.GetRotation();

        if (pos.x == 0.f && pos.y == 0.f && pos.z == 0.f) continue;

        float dist = pos.DistanceTo(infoCopy.lastPosition);
        if (dist < KMP_POS_CHANGE_THRESHOLD) continue;

        float computedSpeed = 0.f;
        if (infoCopy.lastUpdateTick > 0) {
            // Use a nominal deltaTime for background computation
            computedSpeed = dist / 0.016f; // ~60fps assumption
        }

        float speed = character.GetMoveSpeed();
        if (speed <= 0.f && computedSpeed > 0.f) {
            speed = computedSpeed;
        }

        uint8_t animState = character.GetAnimState();
        if (animState == 0 && speed > 0.5f) {
            animState = (speed > 5.0f) ? 2 : 1;
        }

        // Store in frame data
        CachedEntityPos cached;
        cached.netId = netId;
        cached.position = pos;
        cached.rotation = rot;
        cached.speed = speed;
        cached.animState = animState;
        cached.dirty = true;
        writeFrame.localEntities.push_back(cached);

        PendingPos pp;
        pp.cp.entityId = netId;
        pp.cp.posX = pos.x;
        pp.cp.posY = pos.y;
        pp.cp.posZ = pos.z;
        pp.cp.compressedQuat = rot.Compress();
        pp.cp.animStateId = animState;
        pp.cp.moveSpeed = static_cast<uint8_t>(std::min(255.f, speed / 15.f * 255.f));
        pp.cp.flags = (speed > 3.0f) ? 0x01 : 0x00;
        pp.netId = netId;
        pp.pos = pos;
        pp.rot = rot;
        pendingPositions.push_back(pp);
    }

    // Build pre-serialized packet
    if (!pendingPositions.empty()) {
        PacketWriter writer;
        writer.WriteHeader(MessageType::C2S_PositionUpdate);
        writer.WriteU8(static_cast<uint8_t>(pendingPositions.size()));
        for (auto& pp : pendingPositions) {
            writer.WriteRaw(&pp.cp, sizeof(pp.cp));
            // Update registry tracking (shared_mutex inside)
            m_entityRegistry.UpdatePosition(pp.netId, pp.pos);
            m_entityRegistry.UpdateRotation(pp.netId, pp.rot);
        }
        writeFrame.packetBytes = std::move(writer.Buffer());
    }

    writeFrame.ready = true;
}

void Core::BackgroundInterpolate() {
    auto& writeFrame = m_frameData[m_writeBuffer];

    auto remoteEntities = m_entityRegistry.GetRemoteEntities();
    float now = static_cast<float>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count()) / 1000.f;

    static int s_interpCallCount = 0;
    int validCount = 0;
    s_interpCallCount++;

    for (EntityID remoteId : remoteEntities) {
        CachedRemoteResult result;
        result.netId = remoteId;

        uint8_t moveSpeed = 0;
        uint8_t animState = 0;
        if (m_interpolation.GetInterpolated(remoteId, now,
                                             result.position, result.rotation,
                                             moveSpeed, animState)) {
            result.moveSpeed = moveSpeed;
            result.animState = animState;
            result.valid = true;
            validCount++;
        }

        writeFrame.remoteResults.push_back(result);
    }

    // Log first few calls + every 200th
    if (!remoteEntities.empty() && (s_interpCallCount <= 5 || s_interpCallCount % 200 == 0)) {
        spdlog::info("Core::BackgroundInterpolate: {} remote entities, {} valid interp results (call #{})",
                     remoteEntities.size(), validCount, s_interpCallCount);
    }
}

bool Core::TeleportToNearestRemotePlayer() {
    if (!m_connected) {
        m_nativeHud.AddSystemMessage("Not connected to a server.");
        return false;
    }

    // Find the nearest remote entity with a valid game object and position
    auto remoteEntities = m_entityRegistry.GetRemoteEntities();
    if (remoteEntities.empty()) {
        m_nativeHud.AddSystemMessage("No remote players found.");
        spdlog::info("Core: TeleportToNearest — no remote entities");
        return false;
    }

    // Get our first local entity's position as reference
    auto localEntities = m_entityRegistry.GetPlayerEntities(m_localPlayerId);
    if (localEntities.empty()) {
        m_nativeHud.AddSystemMessage("No local characters registered.");
        return false;
    }

    Vec3 localPos(0, 0, 0);
    void* firstLocalObj = m_entityRegistry.GetGameObject(localEntities[0]);
    if (firstLocalObj) {
        game::CharacterAccessor localAccessor(firstLocalObj);
        localPos = localAccessor.GetPosition();
    }

    // Find the nearest remote entity with a valid position
    float bestDist = 1e18f;
    Vec3 bestPos(0, 0, 0);
    EntityID bestId = INVALID_ENTITY;
    PlayerID bestOwner = 0;

    for (EntityID remoteId : remoteEntities) {
        auto* info = m_entityRegistry.GetInfo(remoteId);
        if (!info) continue;

        Vec3 rPos = info->lastPosition;
        if (rPos.x == 0.f && rPos.y == 0.f && rPos.z == 0.f) continue;

        float dist = localPos.DistanceTo(rPos);
        if (dist < bestDist) {
            bestDist = dist;
            bestPos = rPos;
            bestId = remoteId;
            bestOwner = info->ownerPlayerId;
        }
    }

    if (bestId == INVALID_ENTITY) {
        m_nativeHud.AddSystemMessage("No remote players with valid positions found.");
        return false;
    }

    // Get the remote player's name for display
    auto* rp = m_playerController.GetRemotePlayer(bestOwner);
    std::string targetName = rp ? rp->playerName : ("Player_" + std::to_string(bestOwner));

    // Teleport all local entities to the target position
    int teleported = 0;
    for (EntityID netId : localEntities) {
        void* gameObj = m_entityRegistry.GetGameObject(netId);
        if (!gameObj) continue;

        game::CharacterAccessor accessor(gameObj);
        if (!accessor.IsValid()) continue;

        Vec3 tpPos = bestPos;
        tpPos.x += static_cast<float>(teleported % 4) * 3.0f;
        tpPos.z += static_cast<float>(teleported / 4) * 3.0f;

        if (accessor.WritePosition(tpPos)) {
            m_entityRegistry.UpdatePosition(netId, tpPos);
            teleported++;
        }
    }

    if (teleported > 0) {
        spdlog::info("Core: Teleported {} characters to {} at ({:.1f}, {:.1f}, {:.1f}) [dist={:.0f}]",
                     teleported, targetName, bestPos.x, bestPos.y, bestPos.z, bestDist);
        m_nativeHud.AddSystemMessage("Teleported to " + targetName + "!");
        m_overlay.AddSystemMessage("Teleported " + std::to_string(teleported) +
                                   " characters to " + targetName);
        return true;
    }

    m_nativeHud.AddSystemMessage("Teleport failed — no valid local characters.");
    return false;
}

} // namespace kmp
