#include "../core.h"
#include "../game/game_types.h"
#include "../hooks/time_hooks.h"
#include "kmp/protocol.h"
#include "kmp/messages.h"
#include "kmp/memory.h"
#include <spdlog/spdlog.h>

namespace kmp {

// Forward declarations for game function call types
using CharacterSpawnFn = void*(__fastcall*)(void* factory, void* templateData);
using CharacterMoveToFn = void(__fastcall*)(void* character, float x, float y, float z, int moveType);
using ApplyDamageFn = void(__fastcall*)(void* target, void* attacker,
                                         int bodyPart, float cut, float blunt, float pierce);
using CharacterDeathFn = void(__fastcall*)(void* character, void* killer);
using BuildingPlaceFn = void(__fastcall*)(void* world, void* building, float x, float y, float z);

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

        spdlog::debug("PacketHandler: Entity spawn id={} type={} at ({:.1f}, {:.1f}, {:.1f})",
                      entityId, type, px, py, pz);

        auto& core = Core::Get();
        auto& registry = core.GetEntityRegistry();
        auto& funcs = core.GetGameFunctions();

        // Try to create the character using the game's spawn function
        void* gameObject = nullptr;
        if (funcs.CharacterSpawn && static_cast<EntityType>(type) != EntityType::Building) {
            // Call the game's character factory
            // Note: we pass nullptr for templateData since we don't have the full
            // game template structure. The character will spawn as a default entity.
            auto spawnFn = reinterpret_cast<CharacterSpawnFn>(funcs.CharacterSpawn);
            gameObject = spawnFn(nullptr, nullptr);

            if (gameObject) {
                // Set the spawned character's position
                game::CharacterAccessor accessor(gameObject);
                auto& offsets = game::GetOffsets();
                if (offsets.character.position >= 0) {
                    uintptr_t ptr = reinterpret_cast<uintptr_t>(gameObject);
                    Memory::Write(ptr + offsets.character.position, px);
                    Memory::Write(ptr + offsets.character.position + 4, py);
                    Memory::Write(ptr + offsets.character.position + 8, pz);
                }

                // Set rotation
                Quat rot = Quat::Decompress(compQuat);
                if (offsets.character.rotation >= 0) {
                    uintptr_t ptr = reinterpret_cast<uintptr_t>(gameObject);
                    Memory::Write(ptr + offsets.character.rotation, rot);
                }

                spdlog::info("PacketHandler: Spawned remote entity {} via game function", entityId);
            }
        }

        // Register in entity registry as a remote entity
        registry.RegisterRemote(entityId, static_cast<EntityType>(type), ownerId, Vec3(px, py, pz));

        // If we got a game object, link it to the registry entry
        if (gameObject) {
            // The registry's RegisterRemote already added the entry.
            // We need to update the gameObject pointer.
            // For now, the entity is tracked as a "ghost" for position interpolation.
        }

        // Add initial interpolation snapshot
        float now = static_cast<float>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count()) / 1000.f;
        Quat rot = Quat::Decompress(compQuat);
        core.GetInterpolation().AddSnapshot(entityId, now, Vec3(px, py, pz), rot);
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

            interp.AddSnapshot(pos.entityId, now, position, rotation);
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

        // Look up the entity's game object
        void* gameObj = registry.GetGameObject(msg.entityId);
        if (gameObj && funcs.CharacterMoveTo) {
            // Call the game's MoveTo function to make the character walk to the target
            auto moveToFn = reinterpret_cast<CharacterMoveToFn>(funcs.CharacterMoveTo);
            moveToFn(gameObj, msg.targetX, msg.targetY, msg.targetZ, msg.moveType);
        } else {
            // No game object or no MoveTo function — just update position tracking
            // The interpolation system will handle smooth movement
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

        for (uint32_t i = 0; i < entityCount && reader.Remaining() >= 28; i++) {
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

            // Register as remote entity
            registry.RegisterRemote(entityId, static_cast<EntityType>(type), ownerId, Vec3(px, py, pz));

            // Add initial position snapshot for interpolation
            Quat rot = Quat::Decompress(compQuat);
            core.GetInterpolation().AddSnapshot(entityId, now, Vec3(px, py, pz), rot);
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

        // Try to create the building in the local game world
        if (funcs.BuildingPlace) {
            auto buildFn = reinterpret_cast<BuildingPlaceFn>(funcs.BuildingPlace);
            // We pass nullptr for building object since we're creating from network data
            buildFn(nullptr, nullptr, msg.posX, msg.posY, msg.posZ);
        }
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

        auto& core = Core::Get();
        void* entityObj = core.GetEntityRegistry().GetGameObject(msg.entityId);
        if (!entityObj) return;

        // Write equipment template ID to the character's equipment slot
        auto& offsets = game::GetOffsets().character;
        uintptr_t charPtr = reinterpret_cast<uintptr_t>(entityObj);

        if (offsets.inventory >= 0) {
            uintptr_t invPtr = 0;
            Memory::Read(charPtr + offsets.inventory, invPtr);
            if (invPtr != 0) {
                // Equipment array at inventory+0x40, each entry is a pointer to item
                uintptr_t equipArray = invPtr + 0x40;
                int slot = static_cast<int>(msg.slot);
                uintptr_t itemPtr = 0;
                Memory::Read(equipArray + slot * sizeof(uintptr_t), itemPtr);
                if (itemPtr != 0) {
                    Memory::Write(itemPtr + 0x08, msg.itemTemplateId);
                }
            }
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
