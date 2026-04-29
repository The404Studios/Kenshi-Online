#pragma once
// ═══════════════════════════════════════════════════════════════════════════
//  KMP Event System — Typed event dispatch for the engine pipeline
// ═══════════════════════════════════════════════════════════════════════════
// Decouples game systems from each other. Instead of direct calls between
// hooks and managers, systems publish events and subscribe to them.
//
// Features:
//   - Type-safe event dispatch (no string keys, no dynamic_cast)
//   - Priority ordering (higher priority handlers run first)
//   - Thread-safe publish/subscribe
//   - Event consumption (handler can stop propagation)
//   - Deferred dispatch (queue events for processing in a specific stage)
//
// Usage:
//   // Subscribe:
//   EventBus::Get().Subscribe<EntitySpawnedEvent>([](const EntitySpawnedEvent& e) {
//       spdlog::info("Entity {} spawned at ({}, {}, {})", e.entityId, e.pos.x, e.pos.y, e.pos.z);
//   });
//
//   // Publish:
//   EventBus::Get().Publish(EntitySpawnedEvent{entityId, pos, type, owner});

#include "kmp/types.h"
#include "kmp/assert.h"
#include <functional>
#include <vector>
#include <unordered_map>
#include <typeindex>
#include <mutex>
#include <atomic>
#include <queue>
#include <memory>

namespace kmp::engine {

// ═══════════════════════════════════════════════════════════════════════════
//  Event Types — All game events defined as structs
// ═══════════════════════════════════════════════════════════════════════════

// ── Connection Events ──
struct PlayerConnectedEvent {
    PlayerID playerId;
    std::string playerName;
    bool isHost;
};

struct PlayerDisconnectedEvent {
    PlayerID playerId;
    std::string reason;
};

struct ConnectionStateChangedEvent {
    bool connected;
    std::string serverAddress;
};

// ── Entity Lifecycle Events ──
struct EntitySpawnedEvent {
    EntityID   entityId;
    Vec3       position;
    EntityType type;
    PlayerID   owner;
    bool       isRemote;
};

struct EntityDespawnedEvent {
    EntityID   entityId;
    EntityType type;
    PlayerID   owner;
};

struct EntityOwnerChangedEvent {
    EntityID entityId;
    PlayerID oldOwner;
    PlayerID newOwner;
};

// ── Movement Events ──
struct EntityMovedEvent {
    EntityID entityId;
    Vec3     oldPos;
    Vec3     newPos;
    Quat     rotation;
    bool     isTeleport; // True if distance > threshold (snap, not interpolate)
};

struct ZoneChangedEvent {
    EntityID  entityId;
    ZoneCoord oldZone;
    ZoneCoord newZone;
};

// ── Combat Events ──
struct CombatStartedEvent {
    EntityID attacker;
    EntityID target;
};

struct DamageDealtEvent {
    EntityID attacker;
    EntityID target;
    BodyPart bodyPart;
    float    cutDamage;
    float    bluntDamage;
    float    pierceDamage;
    float    resultingHealth;
};

struct EntityDeathEvent {
    EntityID victim;
    EntityID killer;
};

struct EntityKnockoutEvent {
    EntityID victim;
    EntityID attacker;
};

// ── Inventory Events ──
struct ItemPickedUpEvent {
    EntityID entityId;
    uint32_t itemTemplateId;
    int      quantity;
};

struct ItemDroppedEvent {
    EntityID entityId;
    uint32_t itemTemplateId;
    int      quantity;
    Vec3     dropPosition;
};

struct TradeCompletedEvent {
    EntityID buyer;
    EntityID seller;
    uint32_t itemTemplateId;
    int      quantity;
    int      price;
};

// ── Building Events ──
struct BuildingPlacedEvent {
    EntityID entityId;
    Vec3     position;
    Quat     rotation;
    PlayerID owner;
};

struct BuildingDestroyedEvent {
    EntityID entityId;
    EntityID attacker;
};

struct BuildingProgressEvent {
    EntityID entityId;
    float    progress; // 0.0 - 1.0
};

// ── Squad Events ──
struct SquadCreatedEvent {
    EntityID   squadId;
    PlayerID   owner;
    std::string name;
};

struct SquadMemberAddedEvent {
    EntityID squadId;
    EntityID memberId;
};

struct SquadMemberRemovedEvent {
    EntityID squadId;
    EntityID memberId;
};

// ── World Events ──
struct GameLoadedEvent {
    bool isNewGame;
};

struct TimeChangedEvent {
    float oldTime;
    float newTime; // 0.0 - 1.0
};

struct WeatherChangedEvent {
    int oldWeather;
    int newWeather;
};

// ── Chat Events ──
struct ChatMessageEvent {
    PlayerID sender;
    std::string senderName;
    std::string message;
    bool isSystem;
};

// ── Diagnostics Events ──
struct PipelineStallEvent {
    int   stageIndex;
    float durationMs;
};

struct MemoryAccessFailedEvent {
    uintptr_t address;
    std::string context;
    bool isRead; // true = read, false = write
};

// ═══════════════════════════════════════════════════════════════════════════
//  Event Bus — Central dispatch hub
// ═══════════════════════════════════════════════════════════════════════════

// Handler priority (higher = runs first)
enum class EventPriority : int {
    First   = 1000,   // System-critical handlers
    High    = 500,    // Important handlers
    Normal  = 0,      // Default
    Low     = -500,   // Cleanup/logging handlers
    Last    = -1000,  // Final handlers (e.g., diagnostics)
};

class EventBus {
public:
    static EventBus& Get() {
        static EventBus instance;
        return instance;
    }

    // Subscribe to an event type
    template<typename EventT>
    int Subscribe(std::function<void(const EventT&)> handler,
                  EventPriority priority = EventPriority::Normal) {
        int id = m_nextHandlerId.fetch_add(1, std::memory_order_relaxed);

        auto wrapper = [handler = std::move(handler)](const void* event) {
            handler(*static_cast<const EventT*>(event));
        };

        std::lock_guard lock(m_mutex);
        auto& handlers = m_handlers[std::type_index(typeid(EventT))];
        handlers.push_back({id, static_cast<int>(priority), std::move(wrapper)});

        // Sort by priority (descending — higher priority first)
        std::sort(handlers.begin(), handlers.end(),
                  [](const HandlerEntry& a, const HandlerEntry& b) {
                      return a.priority > b.priority;
                  });

        return id;
    }

    // Unsubscribe a handler by ID
    void Unsubscribe(int handlerId) {
        std::lock_guard lock(m_mutex);
        for (auto& [type, handlers] : m_handlers) {
            handlers.erase(
                std::remove_if(handlers.begin(), handlers.end(),
                               [handlerId](const HandlerEntry& h) { return h.id == handlerId; }),
                handlers.end());
        }
    }

    // Publish an event immediately (synchronous dispatch)
    template<typename EventT>
    void Publish(const EventT& event) {
        std::vector<HandlerEntry> snapshot;
        {
            std::lock_guard lock(m_mutex);
            auto it = m_handlers.find(std::type_index(typeid(EventT)));
            if (it == m_handlers.end()) return;
            snapshot = it->second; // Copy to avoid holding lock during dispatch
        }

        for (auto& entry : snapshot) {
            __try {
                entry.handler(&event);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                spdlog::error("EventBus: SEH exception in handler {} for event {}",
                              entry.id, typeid(EventT).name());
            }
        }
    }

    // Queue an event for deferred dispatch (call FlushDeferred to process)
    template<typename EventT>
    void Defer(EventT event) {
        std::lock_guard lock(m_deferredMutex);
        m_deferred.push_back({
            std::type_index(typeid(EventT)),
            std::make_shared<EventT>(std::move(event)),
            [this](const void* e) { Publish(*static_cast<const EventT*>(e)); }
        });
    }

    // Process all deferred events (call from a specific pipeline stage)
    void FlushDeferred() {
        std::vector<DeferredEvent> batch;
        {
            std::lock_guard lock(m_deferredMutex);
            batch.swap(m_deferred);
        }

        for (auto& entry : batch) {
            __try {
                entry.dispatch(entry.data.get());
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                spdlog::error("EventBus: SEH exception flushing deferred event {}", entry.type.name());
            }
        }
    }

    // Clear all handlers (on shutdown)
    void Clear() {
        std::lock_guard lock(m_mutex);
        m_handlers.clear();
        {
            std::lock_guard dLock(m_deferredMutex);
            m_deferred.clear();
        }
    }

    // Get handler count for a specific event type (for diagnostics)
    template<typename EventT>
    size_t HandlerCount() const {
        std::lock_guard lock(m_mutex);
        auto it = m_handlers.find(std::type_index(typeid(EventT)));
        return it != m_handlers.end() ? it->second.size() : 0;
    }

private:
    EventBus() = default;

    struct HandlerEntry {
        int id;
        int priority;
        std::function<void(const void*)> handler;
    };

    struct DeferredEvent {
        std::type_index type;
        std::shared_ptr<void> data;
        std::function<void(const void*)> dispatch;
    };

    mutable std::mutex m_mutex;
    std::unordered_map<std::type_index, std::vector<HandlerEntry>> m_handlers;

    std::mutex m_deferredMutex;
    std::vector<DeferredEvent> m_deferred;

    std::atomic<int> m_nextHandlerId{1};
};

} // namespace kmp::engine
