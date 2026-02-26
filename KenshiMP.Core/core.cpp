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
#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <chrono>

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
        spdlog::set_level(spdlog::level::info);
        spdlog::flush_on(spdlog::level::info);
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

    // Game function hooks (only if we found the functions)
    if (m_gameFuncs.CharacterSpawn) {
        entity_hooks::Install();
    }
    if (m_gameFuncs.CharacterSetPosition || m_gameFuncs.CharacterMoveTo) {
        movement_hooks::Install();
    }
    if (m_gameFuncs.ApplyDamage) {
        combat_hooks::Install();
    }
    if (m_gameFuncs.ZoneLoad) {
        world_hooks::Install();
    }
    if (m_gameFuncs.SaveGame) {
        save_hooks::Install();
    }
    if (m_gameFuncs.TimeUpdate) {
        time_hooks::Install();
    }

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

void Core::NetworkThreadFunc() {
    spdlog::info("Network thread started");

    while (m_running) {
        if (m_connected) {
            m_client.Update();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    spdlog::info("Network thread stopped");
}

void Core::OnGameTick(float deltaTime) {
    if (!m_connected) return;

    // Update interpolation for remote entities
    m_interpolation.Update(deltaTime);

    // ── Step 1: Read local player characters and send position updates ──
    // Only send if we have the player base pointer resolved
    if (m_gameFuncs.PlayerBase != 0) {
        game::CharacterIterator iter;
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
                // Not yet registered — register local player characters
                if (character.IsPlayerControlled()) {
                    netId = m_entityRegistry.Register(charPtr, EntityType::PlayerCharacter);
                    spdlog::debug("Core: Auto-registered local character netId={}", netId);
                } else {
                    continue; // Skip non-player NPCs for auto-registration
                }
            }

            auto* info = m_entityRegistry.GetInfo(netId);
            if (!info || info->ownerPlayerId != m_localPlayerId) continue;

            // Read current position/rotation
            Vec3 pos = character.GetPosition();
            Quat rot = character.GetRotation();

            // Check if position changed beyond threshold
            if (pos.DistanceTo(info->lastPosition) < KMP_POS_CHANGE_THRESHOLD) continue;

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
            cp.animStateId = character.GetAnimState();
            float speed = character.GetMoveSpeed();
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
        }
    }

    // ── Step 2: Apply interpolated positions to remote entities ──
    auto remoteEntities = m_entityRegistry.GetRemoteEntities();
    float now = static_cast<float>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count()) / 1000.f;

    for (EntityID remoteId : remoteEntities) {
        Vec3 interpPos;
        Quat interpRot;

        if (m_interpolation.GetInterpolated(remoteId, now, interpPos, interpRot)) {
            // Get the game object for this remote entity
            void* gameObj = m_entityRegistry.GetGameObject(remoteId);
            if (gameObj) {
                // Write interpolated position/rotation directly to game memory
                auto& offsets = game::GetOffsets().character;
                uintptr_t charPtr = reinterpret_cast<uintptr_t>(gameObj);

                if (offsets.position >= 0) {
                    Memory::Write(charPtr + offsets.position, interpPos.x);
                    Memory::Write(charPtr + offsets.position + 4, interpPos.y);
                    Memory::Write(charPtr + offsets.position + 8, interpPos.z);
                }
                if (offsets.rotation >= 0) {
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
        if (Memory::Read(m_gameFuncs.PlayerBase, playerPtr) && playerPtr != 0) {
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
