#pragma once
#include <cstdint>
#include <vector>
#include <cstring>

namespace kmp {

// ── Message Types ──
enum class MessageType : uint8_t {
    // Connection (Channel 0 - Reliable Ordered)
    C2S_Handshake         = 0x01,
    S2C_HandshakeAck      = 0x02,
    S2C_HandshakeReject   = 0x03,
    C2S_Disconnect        = 0x04,
    S2C_PlayerJoined      = 0x05,
    S2C_PlayerLeft        = 0x06,
    C2S_Keepalive         = 0x07,
    S2C_KeepaliveAck      = 0x08,

    // World State (Channel 0)
    S2C_WorldSnapshot     = 0x10,
    S2C_TimeSync          = 0x11,
    S2C_ZoneData          = 0x12,

    // Entity Lifecycle (Channel 0)
    S2C_EntitySpawn       = 0x20,
    S2C_EntityDespawn     = 0x21,
    C2S_EntitySpawnReq    = 0x22,

    // Movement (Channel 2 - Unreliable Sequenced)
    C2S_PositionUpdate    = 0x30,
    S2C_PositionUpdate    = 0x31,
    C2S_MoveCommand       = 0x32,
    S2C_MoveCommand       = 0x33,

    // Combat (Channel 1 - Reliable Unordered)
    C2S_AttackIntent      = 0x40,
    S2C_CombatHit         = 0x41,
    S2C_CombatBlock       = 0x42,
    S2C_CombatDeath       = 0x43,
    S2C_CombatKO          = 0x44,
    C2S_CombatStance      = 0x45,

    // Stats (Channel 1)
    S2C_StatUpdate        = 0x50,
    S2C_HealthUpdate      = 0x51,
    S2C_EquipmentUpdate   = 0x52,

    // Inventory (Channel 1)
    C2S_ItemPickup        = 0x60,
    C2S_ItemDrop          = 0x61,
    C2S_ItemTransfer      = 0x62,
    S2C_InventoryUpdate   = 0x63,

    // Buildings (Channel 0)
    C2S_BuildRequest      = 0x70,
    S2C_BuildPlaced       = 0x71,
    S2C_BuildProgress     = 0x72,
    S2C_BuildDestroyed    = 0x73,
    C2S_DoorInteract      = 0x74,
    S2C_DoorState         = 0x75,

    // Chat (Channel 0)
    C2S_ChatMessage       = 0x80,
    S2C_ChatMessage       = 0x81,
    S2C_SystemMessage     = 0x82,

    // Admin (Channel 0)
    C2S_AdminCommand      = 0x90,
    S2C_AdminResponse     = 0x91,
};

// ── Packet Header ──
#pragma pack(push, 1)
struct PacketHeader {
    MessageType type;
    uint8_t     flags;     // Bit 0: compressed
    uint16_t    sequence;
    uint32_t    timestamp; // Server tick
};
#pragma pack(pop)

static_assert(sizeof(PacketHeader) == 8, "PacketHeader must be 8 bytes");

// ── Serialization Buffer ──
class PacketWriter {
public:
    PacketWriter() { m_data.reserve(256); }

    void WriteHeader(MessageType type, uint16_t seq = 0, uint32_t tick = 0) {
        PacketHeader h{};
        h.type = type;
        h.flags = 0;
        h.sequence = seq;
        h.timestamp = tick;
        WriteRaw(&h, sizeof(h));
    }

    void WriteU8(uint8_t v)   { WriteRaw(&v, 1); }
    void WriteU16(uint16_t v) { WriteRaw(&v, 2); }
    void WriteU32(uint32_t v) { WriteRaw(&v, 4); }
    void WriteI32(int32_t v)  { WriteRaw(&v, 4); }
    void WriteF32(float v)    { WriteRaw(&v, 4); }

    void WriteVec3(float x, float y, float z) {
        WriteF32(x); WriteF32(y); WriteF32(z);
    }

    void WriteString(const std::string& s) {
        uint16_t len = static_cast<uint16_t>(s.size());
        WriteU16(len);
        if (len > 0) WriteRaw(s.data(), len);
    }

    void WriteRaw(const void* data, size_t len) {
        size_t offset = m_data.size();
        m_data.resize(offset + len);
        std::memcpy(m_data.data() + offset, data, len);
    }

    const uint8_t* Data() const { return m_data.data(); }
    size_t Size() const { return m_data.size(); }
    std::vector<uint8_t>& Buffer() { return m_data; }

private:
    std::vector<uint8_t> m_data;
};

class PacketReader {
public:
    PacketReader(const uint8_t* data, size_t size)
        : m_data(data), m_size(size), m_pos(0) {}

    bool ReadHeader(PacketHeader& h) {
        return ReadRaw(&h, sizeof(h));
    }

    bool ReadU8(uint8_t& v)   { return ReadRaw(&v, 1); }
    bool ReadU16(uint16_t& v) { return ReadRaw(&v, 2); }
    bool ReadU32(uint32_t& v) { return ReadRaw(&v, 4); }
    bool ReadI32(int32_t& v)  { return ReadRaw(&v, 4); }
    bool ReadF32(float& v)    { return ReadRaw(&v, 4); }

    bool ReadVec3(float& x, float& y, float& z) {
        return ReadF32(x) && ReadF32(y) && ReadF32(z);
    }

    bool ReadString(std::string& s) {
        uint16_t len;
        if (!ReadU16(len)) return false;
        if (m_pos + len > m_size) return false;
        s.assign(reinterpret_cast<const char*>(m_data + m_pos), len);
        m_pos += len;
        return true;
    }

    bool ReadRaw(void* out, size_t len) {
        if (m_pos + len > m_size) return false;
        std::memcpy(out, m_data + m_pos, len);
        m_pos += len;
        return true;
    }

    size_t Remaining() const { return m_size - m_pos; }
    size_t Position() const { return m_pos; }
    const uint8_t* Current() const { return m_data + m_pos; }

private:
    const uint8_t* m_data;
    size_t m_size;
    size_t m_pos;
};

} // namespace kmp
