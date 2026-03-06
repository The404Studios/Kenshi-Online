#include "../core.h"
#include "../game/game_types.h"
#include "../game/game_inventory.h"
#include "../game/spawn_manager.h"
#include "../game/player_controller.h"
#include "../hooks/entity_hooks.h"
#include "../hooks/time_hooks.h"
#include "../hooks/squad_hooks.h"
#include "../hooks/faction_hooks.h"
#include "kmp/protocol.h"
#include "kmp/messages.h"
#include "kmp/memory.h"
#include <spdlog/spdlog.h>
#include <unordered_set>

namespace kmp {

// Forward declarations for game function call types
// NOTE: CharacterMoveTo removed — pattern scanner found mid-function address, not safe to call
using ApplyDamageFn = void(__fastcall*)(void* target, void* attacker,
                                         int bodyPart, float cut, float blunt, float pierce);
using CharacterDeathFn = void(__fastcall*)(void* character, void* killer);

// Handles incoming packets from the server and dispatches to appropriate systems.
class PacketHandler {
public:
    static void Initialize() {
        Core::Get().GetClient().SetPacketCallback(
            [](const uint8_t* data, size_t size, int channel) {
                HandlePacket(data, size, channel);
            });
    }

    static void HandlePacket(const uint8_t* data, size_t size, int channel) {
        if (size < sizeof(PacketHeader)) {
            spdlog::warn("PacketHandler: Packet too small ({} bytes)", size);
            return;
        }

        PacketReader reader(data, size);
        PacketHeader header;
        if (!reader.ReadHeader(header)) return;

        // Debug: log every packet for first 100, then every 50th
        static int s_packetNum = 0;
        s_packetNum++;
        if (s_packetNum <= 100 || s_packetNum % 50 == 0) {
            spdlog::debug("PacketHandler: pkt #{} type={} size={} ch={}",
                          s_packetNum, static_cast<int>(header.type), size, channel);
        }

        // ── SAFE messages (work without game world) ──
        // These are pure connection/UI messages that don't access game objects.
        switch (header.type) {
        case MessageType::S2C_HandshakeAck:
            HandleHandshakeAck(reader);
            return;
        case MessageType::S2C_HandshakeReject:
            HandleHandshakeReject(reader);
            return;
        case MessageType::S2C_PlayerJoined:
            HandlePlayerJoined(reader);
            return;
        case MessageType::S2C_PlayerLeft:
            // PlayerLeft teleports entities underground — needs game loaded
            if (!Core::Get().IsGameLoaded()) {
                spdlog::debug("PacketHandler: Deferring PlayerLeft (game not loaded)");
                return;
            }
            HandlePlayerLeft(reader);
            return;
        case MessageType::S2C_ChatMessage:
            HandleChatMessage(reader);
            return;
        case MessageType::S2C_SystemMessage:
            HandleSystemMessage(reader);
            return;
        case MessageType::S2C_AdminResponse:
            HandleAdminResponse(reader);
            return;
        case MessageType::S2C_TradeResult:
            HandleTradeResult(reader);
            return;
        case MessageType::S2C_PipelineSnapshot: {
            PlayerID sender;
            if (!reader.ReadU32(sender)) return;
            Core::Get().GetPipelineOrch().OnRemoteSnapshot(sender, reader.Current(), reader.Remaining());
            return;
        }
        case MessageType::S2C_PipelineEvent: {
            PlayerID sender;
            if (!reader.ReadU32(sender)) return;
            Core::Get().GetPipelineOrch().OnRemoteEvent(sender, reader.Current(), reader.Remaining());
            return;
        }
        default:
            break; // Fall through to game-world-dependent messages below
        }

        // ── GAME-WORLD messages (require IsGameLoaded) ──
        // All remaining messages access game objects, memory, or function pointers.
        // They MUST NOT run before the game world exists.
        if (!Core::Get().IsGameLoaded()) {
            // Entity spawns and world snapshots are safe to queue (SpawnManager holds them)
            // but everything else must be dropped.
            switch (header.type) {
            case MessageType::S2C_EntitySpawn:
                HandleEntitySpawn(reader);
                return;
            case MessageType::S2C_WorldSnapshot:
                HandleWorldSnapshot(reader);
                return;
            case MessageType::S2C_TimeSync:
                HandleTimeSync(reader);
                return;
            default:
                spdlog::debug("PacketHandler: Dropping message 0x{:02X} (game not loaded)",
                              static_cast<uint8_t>(header.type));
                return;
            }
        }

        switch (header.type) {
        // ── Entity ──
        case MessageType::S2C_EntitySpawn:
            HandleEntitySpawn(reader);
            break;
        case MessageType::S2C_EntityDespawn:
            HandleEntityDespawn(reader);
            break;

        // ── Movement ──
        case MessageType::S2C_PositionUpdate:
            HandlePositionUpdate(reader);
            break;
        case MessageType::S2C_MoveCommand:
            HandleMoveCommand(reader);
            break;

        // ── Combat ──
        case MessageType::S2C_CombatHit:
            HandleCombatHit(reader);
            break;
        case MessageType::S2C_CombatDeath:
            HandleCombatDeath(reader);
            break;
        case MessageType::S2C_CombatKO:
            HandleCombatKO(reader);
            break;

        // ── World ──
        case MessageType::S2C_TimeSync:
            HandleTimeSync(reader);
            break;
        case MessageType::S2C_WorldSnapshot:
            HandleWorldSnapshot(reader);
            break;
        case MessageType::S2C_BuildPlaced:
            HandleBuildPlaced(reader);
            break;

        // ── Stats ──
        case MessageType::S2C_StatUpdate:
            HandleStatUpdate(reader);
            break;
        case MessageType::S2C_HealthUpdate:
            HandleHealthUpdate(reader);
            break;
        case MessageType::S2C_EquipmentUpdate:
            HandleEquipmentUpdate(reader);
            break;

        // ── Inventory ──
        case MessageType::S2C_InventoryUpdate:
            HandleInventoryUpdate(reader);
            break;

        // ── Squad ──
        case MessageType::S2C_SquadCreated:
            HandleSquadCreated(reader);
            break;
        case MessageType::S2C_SquadMemberUpdate:
            HandleSquadMemberUpdate(reader);
            break;

        // ── Faction ──
        case MessageType::S2C_FactionRelation:
            HandleFactionRelation(reader);
            break;

        // ── Building sync ──
        case MessageType::S2C_BuildDestroyed:
            HandleBuildDestroyed(reader);
            break;
        case MessageType::S2C_BuildProgress:
            HandleBuildProgressUpdate(reader);
            break;
        case MessageType::S2C_DoorState:
            HandleDoorState(reader);
            break;

        default:
            spdlog::debug("PacketHandler: Unknown message type 0x{:02X}", static_cast<uint8_t>(header.type));
            break;
        }
    }

private:
    static void HandleHandshakeAck(PacketReader& reader) {
        MsgHandshakeAck msg;
        if (!reader.ReadRaw(&msg, sizeof(msg))) return;

        auto& core = Core::Get();
        core.SetLocalPlayerId(msg.playerId);
        core.SetConnected(true);

        // Determine if we're the host (player ID 1 = first connected = host)
        if (msg.currentPlayers <= 1) {
            core.SetIsHost(true);
            spdlog::info("PacketHandler: We are the HOST");
        }

        // Initialize the player controller with our ID and name
        core.GetPlayerController().InitializeLocalPlayer(msg.playerId, core.GetConfig().playerName);

        // Initialize sync orchestrator
        if (auto* so = core.GetSyncOrchestrator()) {
            so->Initialize(msg.playerId, core.GetConfig().playerName);
        }

        // Re-initialize pipeline debugger with correct player ID
        // (it was constructed with ID=0 during Core::Initialize before handshake)
        core.GetPipelineOrch().Shutdown();
        core.GetPipelineOrch().Initialize(msg.playerId, core.GetEntityRegistry(),
            core.GetSpawnManager(), core.GetLoadingOrch(), core.GetClient(), core.GetNativeHud());

        // Re-enable entity hooks that were suspended during game loading
        entity_hooks::ResumeForNetwork();

        spdlog::info("PacketHandler: Handshake accepted! Player ID: {}, Players: {}/{}",
                     msg.playerId, msg.currentPlayers, msg.maxPlayers);

        core.GetNativeHud().LogStep("NET", "Connected! Player " + std::to_string(msg.playerId)
                              + " (" + std::to_string(msg.currentPlayers) + "/"
                              + std::to_string(msg.maxPlayers) + ")");
        core.GetNativeHud().LogStep("OK", core.IsHost() ? "You are the HOST" : "You are a JOINER");

        core.GetOverlay().AddSystemMessage(
            "Connected to server! Player " + std::to_string(msg.playerId) +
            " (" + std::to_string(msg.currentPlayers) + "/" +
            std::to_string(msg.maxPlayers) + ")");

        // Apply initial time sync from handshake
        time_hooks::SetServerTime(msg.timeOfDay, 1.0f);
    }

    static void HandleHandshakeReject(PacketReader& reader) {
        MsgHandshakeReject msg;
        if (!reader.ReadRaw(&msg, sizeof(msg))) return;

        spdlog::warn("PacketHandler: Connection rejected (code={}): {}", msg.reasonCode, msg.reasonText);
        auto& core = Core::Get();

        core.GetOverlay().AddSystemMessage(
            std::string("Connection rejected: ") + msg.reasonText);
        core.GetNativeHud().LogStep("ERR", std::string("Rejected: ") + msg.reasonText);
    }

    static void HandlePlayerJoined(PacketReader& reader) {
        MsgPlayerJoined msg;
        if (!reader.ReadRaw(&msg, sizeof(msg))) return;

        auto& core = Core::Get();

        // Skip self — server broadcasts PlayerJoined to ALL clients including the sender.
        // Without this guard, we'd register ourselves as a "remote" player.
        if (msg.playerId == core.GetLocalPlayerId()) {
            spdlog::debug("PacketHandler: Ignoring own PlayerJoined (ID: {})", msg.playerId);
            return;
        }

        spdlog::info("PacketHandler: Player '{}' joined (ID: {})", msg.playerName, msg.playerId);
        core.GetOverlay().AddSystemMessage(
            std::string(msg.playerName) + " joined the game");
        core.GetNativeHud().AddSystemMessage(std::string(msg.playerName) + " joined the game");
        core.GetOverlay().AddPlayer({msg.playerId, msg.playerName, 0, false});
        core.GetPlayerController().RegisterRemotePlayer(msg.playerId, msg.playerName);

        // Register with sync orchestrator engines
        if (auto* so = core.GetSyncOrchestrator()) {
            so->GetPlayerEngine().OnRemotePlayerJoined(msg.playerId, msg.playerName);
        }
    }

    static void HandlePlayerLeft(PacketReader& reader) {
        MsgPlayerLeft msg;
        if (!reader.ReadRaw(&msg, sizeof(msg))) return;

        spdlog::info("PacketHandler: Player {} left (reason: {})", msg.playerId, msg.reason);

        auto& core = Core::Get();
        // Get player name before removing
        auto* rp = core.GetPlayerController().GetRemotePlayer(msg.playerId);
        std::string leftName = rp ? rp->playerName : ("Player_" + std::to_string(msg.playerId));
        core.GetNativeHud().AddSystemMessage(leftName + " left the game");
        core.GetOverlay().RemovePlayer(msg.playerId);
        core.GetPlayerController().RemoveRemotePlayer(msg.playerId);

        // Notify sync orchestrator engines
        if (auto* so = core.GetSyncOrchestrator()) {
            so->GetPlayerEngine().OnRemotePlayerLeft(msg.playerId);
            so->GetZoneEngine().RemovePlayer(msg.playerId);
            so->GetResolver().ClearInterest(msg.playerId);
        }

        // Clean up all entities owned by the disconnected player.
        // Since CharacterDestroy hook is disabled (pattern found wrong function),
        // we can't call the game's destructor. Instead, teleport stale characters
        // underground so they're not visible, then unregister from tracking.
        auto& registry = core.GetEntityRegistry();
        auto entities = registry.GetPlayerEntities(msg.playerId);
        for (EntityID eid : entities) {
            void* gameObj = registry.GetGameObject(eid);
            if (gameObj) {
                // Clear isPlayerControlled so the character is removed from the
                // host's squad panel — this is the key fix for "host controls both
                // characters after second client disconnects".
                game::WritePlayerControlled(reinterpret_cast<uintptr_t>(gameObj), false);
                // Move character far underground so it's invisible
                game::CharacterAccessor accessor(gameObj);
                Vec3 underground(0.f, -10000.f, 0.f);
                accessor.WritePosition(underground);
                spdlog::debug("PacketHandler: Cleared control + teleported entity {} underground", eid);
            }
            core.GetInterpolation().RemoveEntity(eid);
            registry.Unregister(eid);
        }
        if (!entities.empty()) {
            spdlog::info("PacketHandler: Removed {} entities from player {}",
                         entities.size(), msg.playerId);
            core.GetOverlay().AddSystemMessage(
                "Cleaned up " + std::to_string(entities.size()) + " remote entities");
        }
    }

    // ── Entity Spawn ──
    static void HandleEntitySpawn(PacketReader& reader) {
        uint32_t entityId, templateId, factionId;
        uint8_t type;
        uint32_t ownerId;
        float px, py, pz;
        uint32_t compQuat;

        reader.ReadU32(entityId);
        reader.ReadU8(type);
        reader.ReadU32(ownerId);
        reader.ReadU32(templateId);
        reader.ReadVec3(px, py, pz);
        reader.ReadU32(compQuat);
        reader.ReadU32(factionId);

        // Read optional template name (length-prefixed string appended after fixed fields)
        std::string templateName;
        uint16_t nameLen = 0;
        if (reader.Remaining() >= 2) {
            reader.ReadU16(nameLen);
            if (nameLen > 0 && nameLen <= 255 && reader.Remaining() >= nameLen) {
                templateName.resize(nameLen);
                reader.ReadRaw(templateName.data(), nameLen);
            }
        }

        auto& core = Core::Get();
        auto& registry = core.GetEntityRegistry();
        Vec3 spawnPos(px, py, pz);
        Quat rot = Quat::Decompress(compQuat);

        // If this is our own entity being confirmed by the server, remap the
        // local entity ID to the server-assigned ID instead of spawning a duplicate.
        if (ownerId == core.GetLocalPlayerId()) {
            EntityID localId = registry.FindLocalEntityNear(spawnPos, ownerId);
            if (localId != INVALID_ENTITY && localId != entityId) {
                if (registry.RemapEntityId(localId, entityId)) {
                    spdlog::info("PacketHandler: Remapped own entity {} -> server ID {}",
                                 localId, entityId);
                } else {
                    spdlog::warn("PacketHandler: Failed to remap own entity {} -> {}",
                                 localId, entityId);
                }
            } else if (localId == entityId) {
                // Already has the correct ID (unlikely but possible)
                spdlog::debug("PacketHandler: Own entity {} already has correct server ID", entityId);
            } else {
                spdlog::warn("PacketHandler: No local entity found near ({:.1f},{:.1f},{:.1f}) to remap for server ID {}",
                             px, py, pz, entityId);
            }
            return; // Don't spawn — we already have the character in-game
        }

        spdlog::info("PacketHandler: Entity spawn id={} type={} owner={} template='{}' at ({:.1f}, {:.1f}, {:.1f})",
                     entityId, type, ownerId, templateName, px, py, pz);

        // Log to HUD for visibility
        core.GetNativeHud().LogStep("NET", "Remote entity spawn: id=" + std::to_string(entityId)
                              + " owner=" + std::to_string(ownerId)
                              + " '" + templateName + "'");

        // Save the first remote player character's position as host spawn point.
        // When a joiner receives entity spawns from the host, they'll teleport there.
        if (!core.HasHostSpawnPoint() && spawnPos.x != 0.f && spawnPos.z != 0.f) {
            core.SetHostSpawnPoint(spawnPos);
            spdlog::info("PacketHandler: Host spawn point set to ({:.1f}, {:.1f}, {:.1f})",
                         spawnPos.x, spawnPos.y, spawnPos.z);
            core.GetNativeHud().LogStep("GAME", "Host position: ("
                                  + std::to_string((int)spawnPos.x) + ","
                                  + std::to_string((int)spawnPos.y) + ","
                                  + std::to_string((int)spawnPos.z) + ")");
        }

        // Register in entity registry as remote (gameObject=nullptr until spawned)
        registry.RegisterRemote(entityId, static_cast<EntityType>(type), ownerId, spawnPos);

        // Add initial interpolation snapshot
        float now = static_cast<float>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count()) / 1000.f;
        core.GetInterpolation().AddSnapshot(entityId, now, spawnPos, rot);

        // Queue a real character spawn via SpawnManager.
        // ALWAYS queue — even if SpawnManager isn't ready yet.
        // In-place replay doesn't use the template name; it replays the factory's
        // pre-call struct. The queue just needs entries to drain when the next
        // CharacterCreate fires. If we skip queuing, the entity becomes a permanent
        // ghost that never gets a real game object.
        {
            auto& spawnMgr = core.GetSpawnManager();
            SpawnRequest req;
            req.netId        = entityId;
            req.owner        = ownerId;
            req.type         = static_cast<EntityType>(type);
            req.templateName = templateName;
            req.position     = spawnPos;
            req.rotation     = rot;
            req.templateId   = templateId;
            req.factionId    = factionId;
            spawnMgr.QueueSpawn(req);

            // Notify on HUD
            auto* rp = core.GetPlayerController().GetRemotePlayer(ownerId);
            std::string ownerName = rp ? rp->playerName : ("Player_" + std::to_string(ownerId));
            if (spawnMgr.IsReady()) {
                core.GetNativeHud().AddSystemMessage("Spawning " + ownerName + "'s character...");
            } else {
                core.GetNativeHud().AddSystemMessage("Queued " + ownerName + "'s character (waiting for game event)...");
                spdlog::info("PacketHandler: SpawnManager not ready yet — entity {} queued for deferred spawn", entityId);
            }
        }
    }

    static void HandleEntityDespawn(PacketReader& reader) {
        uint32_t entityId;
        uint8_t reason;
        reader.ReadU32(entityId);
        reader.ReadU8(reason);

        spdlog::info("PacketHandler: Entity despawn id={} reason={}", entityId, reason);

        auto& core = Core::Get();
        void* gameObj = core.GetEntityRegistry().GetGameObject(entityId);
        if (gameObj) {
            // Clear isPlayerControlled so the character is removed from the host's
            // squad panel and can no longer be selected/controlled.
            game::WritePlayerControlled(reinterpret_cast<uintptr_t>(gameObj), false);
            // Teleport underground so the character is not visible in the world
            game::CharacterAccessor accessor(gameObj);
            Vec3 underground(0.f, -10000.f, 0.f);
            accessor.WritePosition(underground);
        }
        core.GetEntityRegistry().SetGameObject(entityId, nullptr);
        core.GetInterpolation().RemoveEntity(entityId);
        core.GetEntityRegistry().Unregister(entityId);
    }

    // ── Position Update ──
    static void HandlePositionUpdate(PacketReader& reader) {
        uint32_t sourcePlayer;
        uint8_t count;
        reader.ReadU32(sourcePlayer);
        reader.ReadU8(count);

        auto& interp = Core::Get().GetInterpolation();
        float now = static_cast<float>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count()) / 1000.f;

        for (uint8_t i = 0; i < count; i++) {
            CharacterPosition pos;
            reader.ReadRaw(&pos, sizeof(pos));

            Vec3 position(pos.posX, pos.posY, pos.posZ);
            Quat rotation = Quat::Decompress(pos.compressedQuat);

            interp.AddSnapshot(pos.entityId, now, position, rotation,
                               pos.moveSpeed, pos.animStateId);
        }

        // Update player engine with position + activity
        if (auto* so = Core::Get().GetSyncOrchestrator()) {
            so->GetPlayerEngine().RecordActivity(sourcePlayer);
        }
    }

    // ── Move Command ──
    static void HandleMoveCommand(PacketReader& reader) {
        MsgMoveCommand msg;
        reader.ReadRaw(&msg, sizeof(msg));

        spdlog::debug("PacketHandler: Move command for entity {} to ({:.1f}, {:.1f}, {:.1f})",
                      msg.entityId, msg.targetX, msg.targetY, msg.targetZ);

        auto& core = Core::Get();
        auto& registry = core.GetEntityRegistry();
        auto& funcs = core.GetGameFunctions();

        // MoveTo function address is mid-function (not safe to call).
        // Use interpolation system to handle smooth movement instead.
        {
            float now = static_cast<float>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count()) / 1000.f;
            Vec3 target(msg.targetX, msg.targetY, msg.targetZ);
            core.GetInterpolation().AddSnapshot(msg.entityId, now + 1.0f, target, Quat());
        }
    }

    // ── Combat Hit ──
    static void HandleCombatHit(PacketReader& reader) {
        MsgCombatHit msg;
        reader.ReadRaw(&msg, sizeof(msg));

        spdlog::debug("PacketHandler: Combat hit {} -> {} part={} dmg=({:.1f},{:.1f},{:.1f}) hp={:.1f}",
                      msg.attackerId, msg.targetId, msg.bodyPart,
                      msg.cutDamage, msg.bluntDamage, msg.pierceDamage, msg.resultHealth);

        auto& core = Core::Get();
        auto& registry = core.GetEntityRegistry();
        auto& funcs = core.GetGameFunctions();

        void* targetObj = registry.GetGameObject(msg.targetId);
        if (!targetObj) return;

        bool appliedViaFunction = false;

        // Try to apply damage via the game's native damage function.
        // Only safe to call when BOTH target and attacker are valid game objects —
        // ApplyDamage dereferences attacker and will crash on nullptr.
        if (funcs.ApplyDamage) {
            void* attackerObj = (msg.attackerId != INVALID_ENTITY)
                ? registry.GetGameObject(msg.attackerId) : nullptr;
            if (attackerObj) {
                auto damageFn = reinterpret_cast<ApplyDamageFn>(funcs.ApplyDamage);
                damageFn(targetObj, attackerObj, msg.bodyPart,
                         msg.cutDamage, msg.bluntDamage, msg.pierceDamage);
                appliedViaFunction = true;
            }
        }

        // Fallback: write health directly to character memory
        if (!appliedViaFunction) {
            auto& offsets = game::GetOffsets().character;
            uintptr_t charPtr = reinterpret_cast<uintptr_t>(targetObj);

            if (offsets.healthChain1 >= 0 && offsets.healthChain2 >= 0 && offsets.healthBase >= 0) {
                uintptr_t ptr1 = 0;
                if (Memory::Read(charPtr + offsets.healthChain1, ptr1) && ptr1 != 0) {
                    uintptr_t ptr2 = 0;
                    if (Memory::Read(ptr1 + offsets.healthChain2, ptr2) && ptr2 != 0) {
                        int partOffset = offsets.healthBase +
                            static_cast<int>(msg.bodyPart) * offsets.healthStride;
                        Memory::Write(ptr2 + partOffset, msg.resultHealth);
                    }
                }
            }
        }
    }

    // ── Combat Death ──
    static void HandleCombatDeath(PacketReader& reader) {
        MsgCombatDeath msg;
        reader.ReadRaw(&msg, sizeof(msg));

        spdlog::info("PacketHandler: Entity {} killed by {}", msg.entityId, msg.killerId);

        auto& core = Core::Get();
        auto& registry = core.GetEntityRegistry();
        auto& funcs = core.GetGameFunctions();

        void* entityObj = registry.GetGameObject(msg.entityId);
        if (entityObj && funcs.CharacterDeath) {
            void* killerObj = (msg.killerId != INVALID_ENTITY)
                ? registry.GetGameObject(msg.killerId) : nullptr;
            auto deathFn = reinterpret_cast<CharacterDeathFn>(funcs.CharacterDeath);
            deathFn(entityObj, killerObj);
        } else if (entityObj) {
            // Fallback: write death state directly
            auto& offsets = game::GetOffsets().character;
            if (offsets.isAlive >= 0) {
                uintptr_t charPtr = reinterpret_cast<uintptr_t>(entityObj);
                bool dead = false;
                Memory::Write(charPtr + offsets.isAlive, dead);
            }
        }
    }

    // ── Combat KO ──
    static void HandleCombatKO(PacketReader& reader) {
        MsgCombatKO msg;
        reader.ReadRaw(&msg, sizeof(msg));

        spdlog::info("PacketHandler: Entity {} KO'd by {} (part={}, hp={:.1f})",
                     msg.entityId, msg.attackerId, msg.bodyPart, msg.resultHealth);

        auto& core = Core::Get();
        auto& registry = core.GetEntityRegistry();
        auto& funcs = core.GetGameFunctions();

        void* entityObj = registry.GetGameObject(msg.entityId);
        if (entityObj && funcs.CharacterKO) {
            void* attackerObj = (msg.attackerId != INVALID_ENTITY)
                ? registry.GetGameObject(msg.attackerId) : nullptr;
            auto koFn = reinterpret_cast<game::func_types::CharacterKOFn>(funcs.CharacterKO);
            koFn(entityObj, attackerObj, static_cast<int>(msg.bodyPart));
        } else if (entityObj) {
            // Fallback: write the health value directly to trigger KO state
            auto& offsets = game::GetOffsets().character;
            uintptr_t charPtr = reinterpret_cast<uintptr_t>(entityObj);

            if (offsets.healthChain1 >= 0 && offsets.healthChain2 >= 0 && offsets.healthBase >= 0) {
                uintptr_t ptr1 = 0;
                if (Memory::Read(charPtr + offsets.healthChain1, ptr1) && ptr1 != 0) {
                    uintptr_t ptr2 = 0;
                    if (Memory::Read(ptr1 + offsets.healthChain2, ptr2) && ptr2 != 0) {
                        int partOffset = offsets.healthBase +
                            static_cast<int>(msg.bodyPart) * offsets.healthStride;
                        Memory::Write(ptr2 + partOffset, msg.resultHealth);
                    }
                }
            }
        }
    }

    // ── Stat Update ──
    static void HandleStatUpdate(PacketReader& reader) {
        MsgStatUpdate msg;
        if (!reader.ReadRaw(&msg, sizeof(msg))) return;

        spdlog::debug("PacketHandler: Stat update entity={} stat={} value={:.1f}",
                      msg.entityId, msg.statIndex, msg.statValue);

        auto& core = Core::Get();
        void* gameObj = core.GetEntityRegistry().GetGameObject(msg.entityId);
        if (!gameObj) return;

        // Write the stat value directly to the character's stats memory
        game::CharacterAccessor accessor(gameObj);
        uintptr_t statsPtr = accessor.GetStatsPtr();
        if (statsPtr == 0) return;

        // Each stat is a float at statsPtr + (statIndex * 4)
        // The stat index maps to the StatsOffsets fields
        auto& offsets = game::GetOffsets().stats;
        int statOffset = -1;

        switch (msg.statIndex) {
            case 0:  statOffset = offsets.meleeAttack;   break;
            case 1:  statOffset = offsets.meleeDefence;  break;
            case 2:  statOffset = offsets.dodge;         break;
            case 3:  statOffset = offsets.martialArts;   break;
            case 4:  statOffset = offsets.strength;      break;
            case 5:  statOffset = offsets.toughness;     break;
            case 6:  statOffset = offsets.dexterity;     break;
            case 7:  statOffset = offsets.athletics;     break;
            case 8:  statOffset = offsets.crossbows;     break;
            case 9:  statOffset = offsets.turrets;       break;
            case 10: statOffset = offsets.precision;     break;
            case 11: statOffset = offsets.stealth;       break;
            case 12: statOffset = offsets.assassination; break;
            case 13: statOffset = offsets.lockpicking;   break;
            case 14: statOffset = offsets.thievery;      break;
            case 15: statOffset = offsets.science;       break;
            case 16: statOffset = offsets.engineering;   break;
            case 17: statOffset = offsets.medic;         break;
            case 18: statOffset = offsets.farming;       break;
            case 19: statOffset = offsets.cooking;       break;
            case 20: statOffset = offsets.weaponsmith;   break;
            case 21: statOffset = offsets.armoursmith;   break;
            case 22: statOffset = offsets.labouring;     break;
            default:
                spdlog::warn("PacketHandler: Unknown stat index {}", msg.statIndex);
                return;
        }

        if (statOffset >= 0) {
            Memory::Write(statsPtr + statOffset, msg.statValue);
        }
    }

    // ── Time Sync ──
    static void HandleTimeSync(PacketReader& reader) {
        MsgTimeSync msg;
        reader.ReadRaw(&msg, sizeof(msg));

        spdlog::debug("PacketHandler: TimeSync tick={} tod={:.2f} speed={}",
                      msg.serverTick, msg.timeOfDay, msg.gameSpeed);

        // Apply time sync via the time hooks system
        time_hooks::SetServerTime(msg.timeOfDay, static_cast<float>(msg.gameSpeed));
    }

    // ── World Snapshot ──
    static void HandleWorldSnapshot(PacketReader& reader) {
        spdlog::info("PacketHandler: Receiving world snapshot...");

        auto& core = Core::Get();
        auto& registry = core.GetEntityRegistry();

        // Parse entity list from the snapshot
        // The server sends individual S2C_EntitySpawn packets for each entity,
        // so the world snapshot is essentially a batch of spawns.
        // If the server sends a bulk format, we parse it here:
        uint32_t entityCount = 0;
        if (!reader.ReadU32(entityCount)) {
            // If no entity count, this might be individual spawn packets following
            spdlog::info("PacketHandler: World snapshot contains individual spawn packets");
            return;
        }

        spdlog::info("PacketHandler: World snapshot with {} entities", entityCount);

        float now = static_cast<float>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count()) / 1000.f;

        auto& spawnMgr = core.GetSpawnManager();

        // Fixed fields per entity: u32+u8+u32+u32+f32*3+u32+u32 = 33 bytes (+ variable name)
        for (uint32_t i = 0; i < entityCount && reader.Remaining() >= 33; i++) {
            uint32_t entityId, templateId, factionId;
            uint8_t type;
            uint32_t ownerId;
            float px, py, pz;
            uint32_t compQuat;

            reader.ReadU32(entityId);
            reader.ReadU8(type);
            reader.ReadU32(ownerId);
            reader.ReadU32(templateId);
            reader.ReadVec3(px, py, pz);
            reader.ReadU32(compQuat);
            reader.ReadU32(factionId);

            // Read optional template name
            std::string templateName;
            uint16_t nameLen = 0;
            if (reader.Remaining() >= 2) {
                reader.ReadU16(nameLen);
                if (nameLen > 0 && nameLen <= 255 && reader.Remaining() >= nameLen) {
                    templateName.resize(nameLen);
                    reader.ReadRaw(templateName.data(), nameLen);
                }
            }

            Vec3 pos(px, py, pz);
            Quat rot = Quat::Decompress(compQuat);

            // Skip our own entities — they already exist in-game.
            // Remap local ID to server ID if needed.
            if (ownerId == core.GetLocalPlayerId()) {
                EntityID localId = registry.FindLocalEntityNear(pos, ownerId);
                if (localId != INVALID_ENTITY && localId != entityId) {
                    registry.RemapEntityId(localId, entityId);
                    spdlog::debug("PacketHandler: World snapshot remapped own entity {} -> {}", localId, entityId);
                }
                continue;
            }

            // Save host spawn point from first remote entity in world snapshot
            if (!core.HasHostSpawnPoint() && pos.x != 0.f && pos.z != 0.f) {
                core.SetHostSpawnPoint(pos);
                spdlog::info("PacketHandler: Host spawn point set from snapshot to ({:.1f}, {:.1f}, {:.1f})",
                             pos.x, pos.y, pos.z);
            }

            // Register as remote entity
            registry.RegisterRemote(entityId, static_cast<EntityType>(type), ownerId, pos);
            core.GetInterpolation().AddSnapshot(entityId, now, pos, rot);

            // ALWAYS queue spawn — even if SpawnManager isn't ready yet.
            // In-place replay doesn't need the template name; it replays the factory's
            // pre-call struct. Skipping here would create permanent ghost entities.
            {
                SpawnRequest req;
                req.netId        = entityId;
                req.owner        = ownerId;
                req.type         = static_cast<EntityType>(type);
                req.templateName = templateName;
                req.position     = pos;
                req.rotation     = rot;
                req.templateId   = templateId;
                req.factionId    = factionId;
                spawnMgr.QueueSpawn(req);
            }
        }

        spdlog::info("PacketHandler: World snapshot processed");

        // Notify sync orchestrator
        if (auto* so = core.GetSyncOrchestrator()) {
            so->GetPlayerEngine().OnWorldSnapshotReceived(static_cast<int>(entityCount));
        }
    }

    // ── Build Placed ──
    static void HandleBuildPlaced(PacketReader& reader) {
        MsgBuildPlaced msg;
        reader.ReadRaw(&msg, sizeof(msg));

        spdlog::info("PacketHandler: Building placed by player {} at ({:.1f}, {:.1f}, {:.1f})",
                     msg.builderId, msg.posX, msg.posY, msg.posZ);

        auto& core = Core::Get();
        auto& registry = core.GetEntityRegistry();
        auto& funcs = core.GetGameFunctions();

        // Register the building in the entity registry
        Vec3 pos(msg.posX, msg.posY, msg.posZ);
        registry.RegisterRemote(msg.entityId, EntityType::Building, msg.builderId, pos);

        // Note: We do NOT call the game's BuildingPlace function here because it
        // requires valid world and building template pointers we cannot construct
        // from network data alone. The building is tracked as a ghost entity.
    }

    // ── Health Update ──
    static void HandleHealthUpdate(PacketReader& reader) {
        MsgHealthUpdate msg;
        reader.ReadRaw(&msg, sizeof(msg));

        auto& core = Core::Get();
        void* entityObj = core.GetEntityRegistry().GetGameObject(msg.entityId);
        if (!entityObj) return;

        // Write health values directly to the character's health memory
        auto& offsets = game::GetOffsets().character;
        uintptr_t charPtr = reinterpret_cast<uintptr_t>(entityObj);

        if (offsets.healthChain1 >= 0 && offsets.healthChain2 >= 0 && offsets.healthBase >= 0) {
            uintptr_t ptr1 = 0;
            if (Memory::Read(charPtr + offsets.healthChain1, ptr1) && ptr1 != 0) {
                uintptr_t ptr2 = 0;
                if (Memory::Read(ptr1 + offsets.healthChain2, ptr2) && ptr2 != 0) {
                    for (int i = 0; i < static_cast<int>(BodyPart::Count); i++) {
                        int partOffset = offsets.healthBase + i * offsets.healthStride;
                        Memory::Write(ptr2 + partOffset, msg.health[i]);
                    }
                }
            }
        }
    }

    // ── Equipment Update ──
    static void HandleEquipmentUpdate(PacketReader& reader) {
        MsgEquipmentUpdate msg;
        reader.ReadRaw(&msg, sizeof(msg));

        spdlog::debug("PacketHandler: Equipment update entity={} slot={} item={}",
                      msg.entityId, msg.slot, msg.itemTemplateId);

        auto& core = Core::Get();
        void* gameObj = core.GetEntityRegistry().GetGameObject(msg.entityId);
        if (!gameObj) return;

        game::CharacterAccessor accessor(gameObj);
        uintptr_t invPtr = accessor.GetInventoryPtr();
        if (invPtr == 0) return;

        game::InventoryAccessor inventory(invPtr);
        if (msg.slot < static_cast<uint8_t>(EquipSlot::Count)) {
            inventory.SetEquipment(static_cast<EquipSlot>(msg.slot), msg.itemTemplateId);
        }
    }

    // ── Chat ──
    static void HandleChatMessage(PacketReader& reader) {
        uint32_t senderId;
        reader.ReadU32(senderId);
        std::string message;
        reader.ReadString(message);

        Core::Get().GetOverlay().AddChatMessage(senderId, message);

        // Also show on HUD overlay
        auto& pc = Core::Get().GetPlayerController();
        auto* remote = pc.GetRemotePlayer(senderId);
        std::string senderName = remote ? remote->playerName : ("Player_" + std::to_string(senderId));
        Core::Get().GetNativeHud().AddChatMessage(senderName, message);
    }

    static void HandleSystemMessage(PacketReader& reader) {
        uint32_t unused;
        reader.ReadU32(unused);
        std::string message;
        reader.ReadString(message);

        Core::Get().GetOverlay().AddSystemMessage(message);
        Core::Get().GetNativeHud().AddSystemMessage(message);
    }

    // ── Inventory / Trade ──

    static void HandleInventoryUpdate(PacketReader& reader) {
        MsgInventoryUpdate msg;
        if (!reader.ReadRaw(&msg, sizeof(msg))) return;

        spdlog::debug("PacketHandler: Inventory update entity={} action={} item={} qty={}",
                      msg.entityId, msg.action, msg.itemTemplateId, msg.quantity);

        auto& core = Core::Get();
        void* gameObj = core.GetEntityRegistry().GetGameObject(msg.entityId);
        if (!gameObj) return;

        game::CharacterAccessor accessor(gameObj);
        uintptr_t invPtr = accessor.GetInventoryPtr();
        if (invPtr == 0) return;

        game::InventoryAccessor inventory(invPtr);
        if (msg.action == 0) {
            // Add item - write directly to inventory memory
            inventory.AddItem(msg.itemTemplateId, msg.quantity);
        } else if (msg.action == 1) {
            // Remove item
            inventory.RemoveItem(msg.itemTemplateId, msg.quantity);
        }
    }

    static void HandleTradeResult(PacketReader& reader) {
        MsgTradeResult msg;
        if (!reader.ReadRaw(&msg, sizeof(msg))) return;

        spdlog::info("PacketHandler: Trade result buyer={} item={} qty={} success={}",
                     msg.buyerEntityId, msg.itemTemplateId, msg.quantity, msg.success);

        if (msg.success) {
            Core::Get().GetNativeHud().AddSystemMessage("Trade completed successfully");
        } else {
            Core::Get().GetNativeHud().AddSystemMessage("Trade denied by server");
        }
    }

    // ── Squad ──

    static void HandleSquadCreated(PacketReader& reader) {
        uint32_t creatorEntityId, squadNetId;
        reader.ReadU32(creatorEntityId);
        reader.ReadU32(squadNetId);
        std::string squadName;
        reader.ReadString(squadName);

        spdlog::info("PacketHandler: Squad '{}' created (netId={}, creator={})",
                     squadName, squadNetId, creatorEntityId);

        // Map the pending squad pointer to the server-assigned net ID
        squad_hooks::OnSquadNetIdAssigned(squadNetId);

        Core::Get().GetNativeHud().AddSystemMessage("Squad created: " + squadName);
    }

    static void HandleSquadMemberUpdate(PacketReader& reader) {
        MsgSquadMemberUpdate msg;
        if (!reader.ReadRaw(&msg, sizeof(msg))) return;

        spdlog::info("PacketHandler: Squad {} member {} action={}",
                     msg.squadNetId, msg.memberEntityId, msg.action);

        auto& core = Core::Get();
        auto& registry = core.GetEntityRegistry();

        // Update entity tracking: if a member was added to a squad, ensure
        // our registry knows this entity belongs to the squad's owner
        const auto* info = registry.GetInfo(msg.memberEntityId);
        if (info && info->isRemote) {
            if (msg.action == 0) {
                // Member added — entity is now active in this squad
                spdlog::debug("PacketHandler: Remote entity {} added to squad {}",
                              msg.memberEntityId, msg.squadNetId);
            } else if (msg.action == 1) {
                // Member removed — entity left squad (death, knockout, trade, etc.)
                spdlog::debug("PacketHandler: Remote entity {} removed from squad {}",
                              msg.memberEntityId, msg.squadNetId);
            }
        }
    }

    // ── Faction ──

    static void HandleFactionRelation(PacketReader& reader) {
        MsgFactionRelation msg;
        if (!reader.ReadRaw(&msg, sizeof(msg))) return;

        spdlog::info("PacketHandler: Faction relation {} <-> {} = {:.1f}",
                     msg.factionIdA, msg.factionIdB, msg.relation);

        auto& core = Core::Get();

        // Try to call the game's FactionRelation function via the hook's original pointer.
        // We need to find the faction objects by their IDs first.
        auto origFn = faction_hooks::GetOriginal();
        if (!origFn) {
            spdlog::debug("PacketHandler: No FactionRelation function — cannot apply relation");
            return;
        }

        // Find faction pointers by scanning remote player entities and the local player.
        // Each character has a faction pointer at character+0x10. Factions have an ID at +0x08.
        // We scan known entities to find factions matching the IDs.
        uintptr_t factionPtrA = 0, factionPtrB = 0;

        // Check local player's faction first
        uintptr_t localFaction = core.GetPlayerController().GetLocalFactionPtr();
        if (localFaction != 0) {
            uint32_t localFactionId = 0;
            Memory::Read(localFaction + 0x08, localFactionId);
            if (localFactionId == msg.factionIdA) factionPtrA = localFaction;
            if (localFactionId == msg.factionIdB) factionPtrB = localFaction;
        }

        // Scan all entities for faction pointers
        if (factionPtrA == 0 || factionPtrB == 0) {
            game::CharacterIterator iter;
            while (iter.HasNext() && (factionPtrA == 0 || factionPtrB == 0)) {
                game::CharacterAccessor character = iter.Next();
                if (!character.IsValid()) continue;

                uintptr_t fPtr = character.GetFactionPtr();
                if (fPtr == 0) continue;

                uint32_t fId = 0;
                Memory::Read(fPtr + 0x08, fId);
                if (fId == msg.factionIdA && factionPtrA == 0) factionPtrA = fPtr;
                if (fId == msg.factionIdB && factionPtrB == 0) factionPtrB = fPtr;
            }
        }

        if (factionPtrA == 0 || factionPtrB == 0) {
            spdlog::debug("PacketHandler: Could not find faction pointers for IDs {} and {}",
                         msg.factionIdA, msg.factionIdB);
            return;
        }

        // Call the game function with server-sourced guard to prevent feedback loop
        faction_hooks::SetServerSourced(true);
        __try {
            origFn(reinterpret_cast<void*>(factionPtrA),
                   reinterpret_cast<void*>(factionPtrB),
                   msg.relation);
            spdlog::info("PacketHandler: Applied faction relation {} <-> {} = {:.1f}",
                        msg.factionIdA, msg.factionIdB, msg.relation);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            spdlog::error("PacketHandler: FactionRelation call crashed");
        }
        faction_hooks::SetServerSourced(false);
    }

    // ── Building ──

    static void HandleBuildDestroyed(PacketReader& reader) {
        uint32_t buildingId;
        uint8_t reason;
        reader.ReadU32(buildingId);
        reader.ReadU8(reason);

        spdlog::info("PacketHandler: Building {} destroyed (reason={})", buildingId, reason);

        auto& core = Core::Get();
        core.GetEntityRegistry().Unregister(buildingId);

        const char* reasonStr = reason == 2 ? "dismantled" : "destroyed";
        core.GetNativeHud().AddSystemMessage(std::string("Building ") + reasonStr);
    }

    static void HandleBuildProgressUpdate(PacketReader& reader) {
        MsgBuildProgress msg;
        if (!reader.ReadRaw(&msg, sizeof(msg))) return;

        spdlog::debug("PacketHandler: Building {} progress={:.2f}", msg.entityId, msg.progress);

        // Update the entity registry with the build progress
        // This keeps our local state in sync so /entities can report accurately
        auto& core = Core::Get();
        const auto* info = core.GetEntityRegistry().GetInfo(msg.entityId);
        if (info && info->isRemote) {
            // Building progress is tracked — visual update would require
            // writing to the building's GameData (offset TBD from RE).
            // For now, log at info level when milestones are hit.
            if (msg.progress >= 1.0f) {
                spdlog::info("PacketHandler: Remote building {} construction complete!", msg.entityId);
                core.GetNativeHud().AddSystemMessage("Remote building completed.");
            } else if (msg.progress >= 0.5f) {
                static std::unordered_set<EntityID> notified50;
                if (notified50.insert(msg.entityId).second) {
                    spdlog::info("PacketHandler: Remote building {} 50% complete", msg.entityId);
                }
            }
        }
    }

    // ── Door State ──

    static void HandleDoorState(PacketReader& reader) {
        MsgDoorState msg;
        if (!reader.ReadRaw(&msg, sizeof(msg))) return;

        const char* stateNames[] = {"opened", "closed", "locked", "broken"};
        const char* stateName = (msg.state < 4) ? stateNames[msg.state] : "unknown";
        spdlog::info("PacketHandler: Door/gate {} state -> {}", msg.entityId, stateName);

        auto& core = Core::Get();
        void* gameObj = core.GetEntityRegistry().GetGameObject(msg.entityId);
        if (gameObj) {
            // Buildings in Kenshi have a functionality pointer that contains door state.
            // BuildingOffsets::functionality = 0xC0 → door state is at functionality+0x10
            auto& offsets = game::GetOffsets().building;
            uintptr_t bldPtr = reinterpret_cast<uintptr_t>(gameObj);
            uintptr_t funcPtr = 0;
            if (offsets.functionality >= 0) {
                Memory::Read(bldPtr + offsets.functionality, funcPtr);
            }
            if (funcPtr != 0 && funcPtr > 0x10000) {
                // Write door state at functionality + 0x10 (open/closed/locked flag)
                Memory::Write(funcPtr + 0x10, msg.state);
                spdlog::debug("PacketHandler: Wrote door state {} to building 0x{:X}", msg.state, bldPtr);
            } else {
                spdlog::debug("PacketHandler: No functionality ptr for building {} — door state not written", msg.entityId);
            }
        }
    }

    // ── Admin Response ──

    static void HandleAdminResponse(PacketReader& reader) {
        MsgAdminResponse msg;
        if (!reader.ReadRaw(&msg, sizeof(msg))) return;

        auto& core = Core::Get();
        std::string text = msg.responseText;
        if (msg.success) {
            spdlog::info("PacketHandler: Admin response: {}", text);
            core.GetNativeHud().AddSystemMessage("[Admin] " + text);
        } else {
            spdlog::warn("PacketHandler: Admin denied: {}", text);
            core.GetNativeHud().AddSystemMessage("[Admin Error] " + text);
        }
    }
};

// Called from core.cpp to initialize packet handling
void InitPacketHandler() {
    PacketHandler::Initialize();
}

} // namespace kmp
