#pragma once
#include <cstdint>

namespace kmp {

// Protocol
constexpr uint32_t KMP_PROTOCOL_VERSION = 1;
constexpr uint16_t KMP_DEFAULT_PORT     = 27800;
constexpr int      KMP_MAX_PLAYERS      = 16;
constexpr int      KMP_MAX_NAME_LENGTH  = 31;

// Tick rates
constexpr int   KMP_TICK_RATE           = 20;      // 20 Hz state sync
constexpr int   KMP_TICK_INTERVAL_MS    = 1000 / KMP_TICK_RATE; // 50ms
constexpr float KMP_TICK_INTERVAL_SEC   = 1.0f / KMP_TICK_RATE;

// Interpolation
constexpr float KMP_INTERP_DELAY_SEC    = 0.1f;    // 100ms interpolation buffer
constexpr int   KMP_MAX_SNAPSHOTS       = 20;       // Snapshot buffer per entity

// Networking
constexpr int   KMP_CHANNEL_COUNT       = 3;
constexpr int   KMP_CHANNEL_RELIABLE_ORDERED   = 0;
constexpr int   KMP_CHANNEL_RELIABLE_UNORDERED = 1;
constexpr int   KMP_CHANNEL_UNRELIABLE_SEQ     = 2;

// Bandwidth limits
constexpr uint32_t KMP_UPSTREAM_LIMIT   = 128 * 1024; // 128 KB/s
constexpr uint32_t KMP_DOWNSTREAM_LIMIT = 256 * 1024; // 256 KB/s

// Timeouts
constexpr uint32_t KMP_CONNECT_TIMEOUT_MS  = 5000;
constexpr uint32_t KMP_KEEPALIVE_INTERVAL  = 1000;  // 1 second
constexpr uint32_t KMP_TIMEOUT_MS          = 10000;  // 10 seconds

// Zone system
constexpr float KMP_ZONE_SIZE           = 750.f;   // Meters per zone (estimated)
constexpr int   KMP_INTEREST_RADIUS     = 1;       // ±1 zone (3x3 grid)

// Position sync thresholds
constexpr float KMP_POS_CHANGE_THRESHOLD = 0.1f;   // Minimum movement to send update
constexpr float KMP_ROT_CHANGE_THRESHOLD = 0.01f;  // Minimum rotation change

// Entity limits
constexpr int KMP_MAX_ENTITIES_PER_ZONE  = 512;
constexpr int KMP_MAX_SYNC_ENTITIES      = 2048;   // Total synced entities per client

} // namespace kmp
