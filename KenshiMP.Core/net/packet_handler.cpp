#include "../core.h"
#include "../game/game_types.h"
#include "../game/game_inventory.h"
#include "../game/spawn_manager.h"
#include "../hooks/entity_hooks.h"
#include "../hooks/time_hooks.h"
#include "kmp/protocol.h"
#include "kmp/messages.h"
#include "kmp/memory.h"
#include <spdlog/spdlog.h>

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

        switch (header.type) {
        // ── Connection ──
        case MessageType::S2C_HandshakeAck:
            HandleHandshakeAck(reader);
            break;
        case MessageType::S2C_HandshakeReject:
            HandleHandshakeReject(reader);
            break;
        case MessageType::S2C_PlayerJoined:
            HandlePlayerJoined(reader);
            break;
        case MessageType::S2C_PlayerLeft:
            HandlePlayerLeft(reader);
            break;

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
        case MessageType::S2C_HealthUpdate:
            HandleHealthUpdate(reader);
            break;
        case MessageType::S2C_EquipmentUpdate:
            HandleEquipmentUpdate(reader);
            break;

        // ── Chat ──
        case MessageType::S2C_ChatMessage:
            HandleChatMessage(reader);
            break;
        case MessageType::S2C_SystemMessage:
            HandleSystemMessage(reader);
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

        // Re-enable entity hooks that were suspended during game loading
        entity_hooks::ResumeForNetwork();

        spdlog::info("PacketHandler: Handshake accepted! Player ID: {}, Players: {}/{}",
                     msg.playerId, msg.currentPlayers, msg.maxPlayers);

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

        spdlog::warn("PacketHandler: Connection rejected: {}", msg.reasonText);
        Core::Get().GetOverlay().AddSystemMessage(
            std::string("Connection rejected: ") + msg.reasonText);
    }

    static void HandlePlayerJoined(PacketReader& reader) {
        MsgPlayerJoined msg;
        if (!reader.ReadRaw(&msg, sizeof(msg))) return;

        spdlog::info("PacketHandler: Player '{}' joined (ID: {})", msg.playerName, msg.playerId);
        Core::Get().GetOverlay().AddSystemMessage(
            std::string(msg.playerName) + " joined the game");
        Core::Get().GetOverlay().AddPlayer({msg.playerId, msg.playerName, 0, false});
    }

    static void HandlePlayerLeft(PacketReader& reader) {
        MsgPlayerLeft msg;
        if (!reader.ReadRaw(&msg, sizeof(msg))) return;

        spdlog::info("PacketHandler: Player {} left (reason: {})", msg.playerId, msg.reason);
        Core::Get().GetOverlay().RemovePlayer(msg.playerId);
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

        spdlog::info("PacketHandler: Entity spawn id={} type={} template='{}' at ({:.1f}, {:.1f}, {:.1f})",
                     entityId, type, templateName, px, py, pz);

        // Register in entity registry as remote (gameObject=nullptr until spawned)
        registry.RegisterRemote(entityId, static_cast<EntityType>(type), ownerId, spawnPos);

        // Add initial interpolation snapshot
        float now = static_cast<float>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count()) / 1000.f;
        core.GetInterpolation().AddSnapshot(entityId, now, spawnPos, rot);

        // Queue a real character spawn via SpawnManager.
        auto& spawnMgr = core.GetSpawnManager();
        if (spawnMgr.IsReady() || !templateName.empty()) {
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
        } else {
            spdlog::warn("PacketHandler: SpawnManager not ready and no template name — "
                         "entity {} will remain a ghost until factory is captured", entityId);
        }
    }

    static void HandleEntityDespawn(PacketReader& reader) {
        uint32_t entityId;
        uint8_t reason;
        reader.ReadU32(entityId);
        reader.ReadU8(reason);

        spdlog::debug("PacketHandler: Entity despawn id={} reason={}", entityId, reason);

        auto& core = Core::Get();
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
        if (targetObj && funcs.ApplyDamage) {
            // Apply damage via the game's damage function
            void* attackerObj = (msg.attackerId != INVALID_ENTITY)
                ? registry.GetGameObject(msg.attackerId) : nullptr;
            auto damageFn = reinterpret_cast<ApplyDamageFn>(funcs.ApplyDamage);
            damageFn(targetObj, attackerObj, msg.bodyPart,
                     msg.cutDamage, msg.bluntDamage, msg.pierceDamage);
        } else if (targetObj) {
            // Fallback: write health directly to character memory
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

            // Register as remote entity
            registry.RegisterRemote(entityId, static_cast<EntityType>(type), ownerId, pos);
            core.GetInterpolation().AddSnapshot(entityId, now, pos, rot);

            // Queue real spawn
            if (spawnMgr.IsReady() || !templateName.empty()) {
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
    }

    static void HandleSystemMessage(PacketReader& reader) {
        uint32_t unused;
        reader.ReadU32(unused);
        std::string message;
        reader.ReadString(message);

        Core::Get().GetOverlay().AddSystemMessage(message);
    }
};

// Called from core.cpp to initialize packet handling
void InitPacketHandler() {
    PacketHandler::Initialize();
}

} // namespace kmp
