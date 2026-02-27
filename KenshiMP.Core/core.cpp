#include "core.h"
#include "hooks/render_hooks.h"
#include "hooks/input_hooks.h"
#include "hooks/entity_hooks.h"
#include "hooks/movement_hooks.h"
#include "hooks/combat_hooks.h"
#include "hooks/world_hooks.h"
#include "hooks/save_hooks.h"
#include "hooks/time_hooks.h"
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
        auto logger = spdlog::basic_logger_mt("kenshi_online", "KenshiOnline.log", true);
        spdlog::set_default_logger(logger);
        spdlog::set_level(spdlog::level::debug);
        spdlog::flush_on(spdlog::level::debug);
    } catch (...) {
        // Fallback: no file logging
    }

    spdlog::info("=== Kenshi-Online v{}.{}.{} Initializing ===", 0, 1, 0);

    // Load config
    std::string configPath = ClientConfig::GetDefaultPath();
    m_config.Load(configPath);
    spdlog::info("Config loaded from: {}", configPath);

    // Initialize game offsets (CE fallbacks)
    game::InitOffsetsFromScanner();

    // Initialize subsystems
    if (!InitScanner()) {
        spdlog::error("Scanner initialization failed");
        // Continue anyway - overlay will still work
    }

    if (!HookManager::Get().Initialize()) {
        spdlog::error("HookManager initialization failed");
        return false;
    }

    if (!InitHooks()) {
        spdlog::error("Hook installation failed (some hooks may still work)");
    }

    if (!InitNetwork()) {
        spdlog::error("Network initialization failed");
    }

    // Initialize packet handler
    InitPacketHandler();

    // Set up SpawnManager callback: when a character is spawned by SpawnManager,
    // link the real game object to the entity registry.
    m_spawnManager.SetOnSpawnedCallback(
        [this](EntityID netId, void* gameObject) {
            m_entityRegistry.SetGameObject(netId, gameObject);
            spdlog::info("Core: SpawnManager linked entity {} to game object 0x{:X}",
                         netId, reinterpret_cast<uintptr_t>(gameObject));
        });

    if (!InitUI()) {
        spdlog::error("UI initialization failed");
    }

    m_running = true;

    // Start network thread
    m_networkThread = std::thread(&Core::NetworkThreadFunc, this);

    spdlog::info("=== Kenshi-Online Initialized Successfully ===");
    return true;
}

void Core::Shutdown() {
    spdlog::info("Kenshi-Online shutting down...");

    m_running = false;
    m_connected = false;

    if (m_networkThread.joinable()) {
        m_networkThread.join();
    }

    m_client.Disconnect();
    m_overlay.Shutdown();
    HookManager::Get().Shutdown();

    // Save config
    m_config.Save(ClientConfig::GetDefaultPath());

    spdlog::info("Kenshi-Online shutdown complete");
}

bool Core::InitScanner() {
    if (!m_scanner.Init(nullptr)) {
        spdlog::error("Failed to init scanner for main executable");
        return false;
    }

    bool resolved = ResolveGameFunctions(m_scanner, m_gameFuncs);
    if (!resolved) {
        spdlog::warn("Game functions minimally resolved: false - some features may not work");
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

        for (auto& check : checks) {
            if (!check.address) continue;
            auto sig = FunctionAnalyzer::Analyze(
                reinterpret_cast<uintptr_t>(check.address), check.name);
            if (sig.IsValid()) {
                bool ok = FunctionAnalyzer::ValidateSignature(sig, check.hookParamCount);
                if (!ok) {
                    spdlog::warn("Core: SIGNATURE MISMATCH for '{}' — hook expects {} params, analysis suggests ~{}",
                                 check.name, check.hookParamCount, sig.estimatedParams);
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

    return resolved;
}

bool Core::InitHooks() {
    bool allOk = true;

    // D3D11 Present hook (for ImGui overlay) - always install this
    if (!render_hooks::Install()) {
        spdlog::error("Failed to install render hooks");
        allOk = false;
    }

    // Input hooks
    if (!input_hooks::Install()) {
        spdlog::warn("Failed to install input hooks");
    }

    // ── Entity hooks: ENABLED with disable-call-reenable pattern ──
    // CharacterSpawn starts with `mov rax, rsp` which breaks MinHook trampolines.
    // The hook temporarily disables itself, calls the original directly, then re-enables.
    if (m_gameFuncs.CharacterSpawn) {
        entity_hooks::Install();
    }

    // ── Movement hooks: DISABLED ──
    // SetPosition: signature is (void*, Vec3*) not (void*, f, f, f) — hook signature wrong
    // MoveTo: pattern scanner found MID-FUNCTION address (0x...E3) — cannot hook
    spdlog::info("InitHooks: movement_hooks SKIPPED (SetPosition sig mismatch + MoveTo mid-function)");

    // ── Combat hooks: DISABLED (signatures unverified, not critical for MVP) ──
    spdlog::info("InitHooks: combat_hooks SKIPPED (signatures unverified)");

    // ── World hooks: DISABLED (ZoneLoad sig mismatch, BuildingPlace fires during load) ──
    spdlog::info("InitHooks: world_hooks SKIPPED (signatures unverified)");

    // ── Save/Time hooks: DISABLED ──
    if (m_gameFuncs.SaveGame) {
        save_hooks::Install();  // Already a no-op pass-through
    }
    spdlog::info("InitHooks: time_hooks SKIPPED (no pattern found)");

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

void Core::OnGameLoaded() {
    if (m_gameLoaded.exchange(true)) return; // Only run once

    spdlog::info("=== Core: Game world loaded ===");

    // Run deferred PlayerBase discovery
    bool needsRetry = (m_gameFuncs.PlayerBase == 0);
    if (!needsRetry && m_gameFuncs.PlayerBase != 0) {
        uintptr_t val = 0;
        needsRetry = !Memory::Read(m_gameFuncs.PlayerBase, val) || val == 0 ||
                     val < 0x10000 || val > 0x00007FFFFFFFFFFF;
    }

    if (needsRetry) {
        spdlog::info("Core: Running deferred global discovery (PlayerBase=0x{:X})...",
                     m_gameFuncs.PlayerBase);
        if (RetryGlobalDiscovery(m_scanner, m_gameFuncs)) {
            game::SetResolvedPlayerBase(m_gameFuncs.PlayerBase);
            spdlog::info("Core: Deferred discovery SUCCESS — PlayerBase=0x{:X}",
                         m_gameFuncs.PlayerBase);
        } else {
            spdlog::warn("Core: Deferred discovery failed — PlayerBase still unresolved");
        }
    } else {
        spdlog::info("Core: PlayerBase already valid at 0x{:X}", m_gameFuncs.PlayerBase);
    }
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

void Core::OnGameTick(float deltaTime) {
    if (!m_connected) return;

    // ── Diagnostic: log tick periodically ──
    static int s_tickCount = 0;
    static auto s_lastTickLog = std::chrono::steady_clock::now();
    s_tickCount++;
    auto tickNow = std::chrono::steady_clock::now();
    auto tickElapsed = std::chrono::duration_cast<std::chrono::seconds>(tickNow - s_lastTickLog);
    if (tickElapsed.count() >= 5) {
        spdlog::debug("Core::OnGameTick: {} ticks in last {}s (dt={:.4f}), entities={}",
                      s_tickCount, tickElapsed.count(), deltaTime,
                      m_entityRegistry.GetEntityCount());
        s_tickCount = 0;
        s_lastTickLog = tickNow;
    }

    // Process any pending spawn requests (must happen on game thread)
    m_spawnManager.ProcessSpawnQueue();

    // One-shot heap scan: once the factory is captured but we have few templates,
    // run a heap scan to discover all GameData entries.
    static bool heapScanned = false;
    if (!heapScanned && m_spawnManager.IsReady() && m_spawnManager.GetTemplateCount() < 10) {
        spdlog::info("Core: Triggering GameData heap scan...");
        m_spawnManager.ScanGameDataHeap();
        heapScanned = true;
        spdlog::info("Core: Heap scan complete, {} templates available",
                     m_spawnManager.GetTemplateCount());
    }

    // Update interpolation for remote entities
    m_interpolation.Update(deltaTime);

    // ── Step 1: Read local player characters and send position updates ──
    // Only send if we have the player base pointer resolved
    if (m_gameFuncs.PlayerBase != 0) {
        game::CharacterIterator iter;

        // Log character iterator state on first tick and periodically
        static bool s_firstIterLog = true;
        if (s_firstIterLog) {
            int iterCount = 0;
            game::CharacterIterator countIter;
            while (countIter.HasNext()) { countIter.Next(); iterCount++; }
            spdlog::info("Core: CharacterIterator found {} characters on first tick", iterCount);
            s_firstIterLog = false;
        }

        PacketWriter batchWriter;
        bool hasBatchData = false;
        uint8_t batchCount = 0;

        while (iter.HasNext()) {
            game::CharacterAccessor character = iter.Next();
            if (!character.IsValid()) continue;

            // Check if this character is locally owned
            void* charPtr = reinterpret_cast<void*>(character.GetPtr());
            EntityID netId = m_entityRegistry.GetNetId(charPtr);
            if (netId == INVALID_ENTITY) {
                // All characters from the player's CharacterIterator are player-controlled.
                // Register with our local player ID as owner.
                Vec3 regPos = character.GetPosition();
                std::string regName = character.GetName();
                netId = m_entityRegistry.Register(
                    charPtr, EntityType::PlayerCharacter, m_localPlayerId);
                spdlog::info("Core: AUTO-REGISTERED local character netId={} name='{}' pos=({:.1f},{:.1f},{:.1f}) ptr=0x{:X}",
                             netId, regName, regPos.x, regPos.y, regPos.z,
                             reinterpret_cast<uintptr_t>(charPtr));

                // Notify server about this character so it gets a server entity ID
                {
                    Vec3 pos = character.GetPosition();
                    Quat rot = character.GetRotation();
                    uint32_t compQuat = rot.Compress();

                    // Store position so FindLocalEntityNear can match when
                    // the server confirms with S2C_EntitySpawn.
                    m_entityRegistry.UpdatePosition(netId, pos);

                    uintptr_t factionPtr = character.GetFactionPtr();
                    uint32_t factionId = 0;
                    if (factionPtr != 0) {
                        Memory::Read(factionPtr + 0x08, factionId);
                    }

                    // Try to read template name from GameData pointer at +0x28
                    std::string templateName;
                    uint32_t templateId = 0;
                    uintptr_t charAddr = character.GetPtr();
                    uintptr_t gdPtr = 0;
                    if (Memory::Read(charAddr + 0x28, gdPtr) && gdPtr != 0) {
                        templateName = SpawnManager::ReadKenshiString(gdPtr + 0x28);
                        Memory::Read(gdPtr + 0x08, templateId);
                    }

                    PacketWriter writer;
                    writer.WriteHeader(MessageType::C2S_EntitySpawnReq);
                    writer.WriteU32(netId);
                    writer.WriteU8(static_cast<uint8_t>(EntityType::PlayerCharacter));
                    writer.WriteU32(m_localPlayerId);
                    writer.WriteU32(templateId);
                    writer.WriteF32(pos.x);
                    writer.WriteF32(pos.y);
                    writer.WriteF32(pos.z);
                    writer.WriteU32(compQuat);
                    writer.WriteU32(factionId);
                    uint16_t nameLen = static_cast<uint16_t>(
                        std::min<size_t>(templateName.size(), 255));
                    writer.WriteU16(nameLen);
                    if (nameLen > 0) {
                        writer.WriteRaw(templateName.data(), nameLen);
                    }

                    m_client.SendReliable(writer.Data(), writer.Size());
                    spdlog::info("Core: Sent C2S_EntitySpawnReq for auto-registered entity netId={}", netId);
                }
            }

            // Copy entity info under lock to avoid dangling pointer (audit fix #7)
            EntityInfo infoCopy;
            {
                auto* info = m_entityRegistry.GetInfo(netId);
                if (!info || info->ownerPlayerId != m_localPlayerId) continue;
                infoCopy = *info;
            }

            // ── Equipment diff/send ──
            uintptr_t invPtr = character.GetInventoryPtr();
            if (invPtr != 0) {
                game::InventoryAccessor inventory(invPtr);
                for (int slot = 0; slot < static_cast<int>(EquipSlot::Count); slot++) {
                    uint32_t current = inventory.GetEquipment(static_cast<EquipSlot>(slot));
                    if (current != infoCopy.lastEquipment[slot]) {
                        // Send equipment change to server
                        PacketWriter equipWriter;
                        equipWriter.WriteHeader(MessageType::C2S_EquipmentUpdate);
                        MsgEquipmentUpdate equipMsg{};
                        equipMsg.entityId = netId;
                        equipMsg.slot = static_cast<uint8_t>(slot);
                        equipMsg.itemTemplateId = current;
                        equipWriter.WriteRaw(&equipMsg, sizeof(equipMsg));
                        m_client.SendReliable(equipWriter.Data(), equipWriter.Size());

                        // Update tracking
                        m_entityRegistry.UpdateEquipment(netId, slot, current);
                    }
                }
            }

            // Read current position/rotation
            Vec3 pos = character.GetPosition();
            Quat rot = character.GetRotation();

            // Check if position changed beyond threshold
            float dist = pos.DistanceTo(infoCopy.lastPosition);
            if (dist < KMP_POS_CHANGE_THRESHOLD) continue;

            // Compute moveSpeed from position delta (reliable, no offset needed)
            float computedSpeed = 0.f;
            float timeSinceLast = (infoCopy.lastUpdateTick > 0)
                ? deltaTime  // Approximate: we send once per tick
                : 0.f;
            if (timeSinceLast > 0.001f) {
                computedSpeed = dist / timeSinceLast;
            }

            // Try reading from memory first; if offset unavailable, use computed
            float speed = character.GetMoveSpeed();
            if (speed <= 0.f && computedSpeed > 0.f) {
                speed = computedSpeed;
            }

            // Derive animation state from speed when offset is unavailable
            uint8_t animState = character.GetAnimState();
            if (animState == 0 && speed > 0.5f) {
                // 1 = walking, 2 = running (synthetic states for remote display)
                animState = (speed > 5.0f) ? 2 : 1;
            }

            // Start batch if needed
            if (!hasBatchData) {
                batchWriter.WriteHeader(MessageType::C2S_PositionUpdate);
                // Reserve space for count byte — we'll fill it later
                hasBatchData = true;
            }

            // Write this character's position to the batch
            CharacterPosition cp;
            cp.entityId = netId;
            cp.posX = pos.x;
            cp.posY = pos.y;
            cp.posZ = pos.z;
            cp.compressedQuat = rot.Compress();
            cp.animStateId = animState;
            cp.moveSpeed = static_cast<uint8_t>(std::min(255.f, speed / 15.f * 255.f));
            cp.flags = (speed > 3.0f) ? 0x01 : 0x00;

            batchWriter.WriteRaw(&cp, sizeof(cp));
            batchCount++;

            // Update local tracking
            m_entityRegistry.UpdatePosition(netId, pos);
            m_entityRegistry.UpdateRotation(netId, rot);
        }

        // Send batched position update
        if (hasBatchData && batchCount > 0) {
            // We need to insert the count byte after the header.
            // Since PacketWriter doesn't support insert, build the final packet manually.
            PacketWriter finalWriter;
            finalWriter.WriteHeader(MessageType::C2S_PositionUpdate);
            finalWriter.WriteU8(batchCount);
            // Copy the character data (skip the header in batchWriter)
            size_t headerSize = sizeof(PacketHeader);
            if (batchWriter.Size() > headerSize) {
                finalWriter.WriteRaw(batchWriter.Data() + headerSize,
                                     batchWriter.Size() - headerSize);
            }
            m_client.SendUnreliable(finalWriter.Data(), finalWriter.Size());
            spdlog::trace("Core: Sent {} position updates", batchCount);
        }
    }

    // ── Step 2: Apply interpolated positions to remote entities ──
    auto remoteEntities = m_entityRegistry.GetRemoteEntities();
    float now = static_cast<float>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count()) / 1000.f;

    // NOTE: MoveTo is NOT safe to call — pattern scanner found a mid-function address
    // (0x...E3, not aligned). Using WritePosition (SetPosition with Vec3*) instead.

    for (EntityID remoteId : remoteEntities) {
        Vec3 interpPos;
        Quat interpRot;
        uint8_t moveSpeed = 0;
        uint8_t animState = 0;

        if (m_interpolation.GetInterpolated(remoteId, now, interpPos, interpRot,
                                             moveSpeed, animState)) {
            // Get the game object for this remote entity
            void* gameObj = m_entityRegistry.GetGameObject(remoteId);
            if (gameObj) {
                game::CharacterAccessor accessor(gameObj);

                // Write position via the corrected SetPosition(this, Vec3*) function
                accessor.WritePosition(interpPos);

                // Write rotation to cached rotation offset
                auto& offsets = game::GetOffsets().character;
                if (offsets.rotation >= 0) {
                    uintptr_t charPtr = reinterpret_cast<uintptr_t>(gameObj);
                    Memory::Write(charPtr + offsets.rotation, interpRot);
                }
            }

            // Update registry tracking
            m_entityRegistry.UpdatePosition(remoteId, interpPos);
            m_entityRegistry.UpdateRotation(remoteId, interpRot);
        }
    }

    // ── Step 3: Update zone interest manager ──
    // Use the first local player's position for zone tracking
    if (m_gameFuncs.PlayerBase != 0) {
        uintptr_t playerPtr = 0;
        if (Memory::Read(m_gameFuncs.PlayerBase, playerPtr) && playerPtr != 0 &&
            playerPtr > 0x10000 && playerPtr < 0x00007FFFFFFFFFFF) {
            game::CharacterAccessor firstChar(reinterpret_cast<void*>(playerPtr));
            if (firstChar.IsValid()) {
                Vec3 playerPos = firstChar.GetPosition();
                // Zone interest manager update would go here
                // ZoneInterestManager::Get().UpdateLocalZone(playerPos);
            }
        }
    }
}

} // namespace kmp
