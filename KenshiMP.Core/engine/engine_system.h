#pragma once
// ═══════════════════════════════════════════════════════════════════════════
//  KMP Engine System — Pluggable game system interface
// ═══════════════════════════════════════════════════════════════════════════
// Each game system (combat, inventory, movement, etc.) implements this
// interface and registers with the SystemRegistry.
//
// Systems are:
//   - Initialized in dependency order
//   - Ticked in registration order
//   - Shut down in reverse order
//   - Individually enable/disable-able at runtime
//
// This allows the engine to be composed of independent, testable systems
// that communicate only through the EventBus.

#include "engine_events.h"
#include "kmp/assert.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <algorithm>
#include <spdlog/spdlog.h>

namespace kmp::engine {

// ── System Interface ──
class IEngineSystem {
public:
    virtual ~IEngineSystem() = default;

    // Called once during initialization. Return false to indicate failure.
    virtual bool Init() = 0;

    // Called every tick. deltaTime in seconds.
    virtual void Tick(float deltaTime) = 0;

    // Called once during shutdown.
    virtual void Shutdown() = 0;

    // System identity
    virtual const char* GetName() const = 0;

    // Dependencies: names of systems that must initialize before this one
    virtual std::vector<std::string> GetDependencies() const { return {}; }

    // Runtime enable/disable
    bool IsEnabled() const { return m_enabled; }
    void SetEnabled(bool enabled) {
        if (m_enabled != enabled) {
            spdlog::info("System[{}]: {}", GetName(), enabled ? "ENABLED" : "DISABLED");
            m_enabled = enabled;
        }
    }

    // Whether this system requires an active server connection
    virtual bool RequiresConnection() const { return false; }

    // Whether this system requires the game world to be loaded
    virtual bool RequiresGameLoaded() const { return false; }

private:
    bool m_enabled = true;
};

// ── System Registry ──
// Owns all systems, manages their lifecycle.

class SystemRegistry {
public:
    static SystemRegistry& Get() {
        static SystemRegistry instance;
        return instance;
    }

    // Register a system (takes ownership)
    template<typename T, typename... Args>
    T* Register(Args&&... args) {
        auto system = std::make_unique<T>(std::forward<Args>(args)...);
        T* ptr = system.get();
        const char* name = ptr->GetName();

        KMP_VERIFY_MSG(m_systemMap.find(name) == m_systemMap.end(),
                       "Duplicate system registration");

        spdlog::info("SystemRegistry: Registered '{}'", name);
        m_systemMap[name] = ptr;
        m_systems.push_back(std::move(system));
        m_initOrder.clear(); // Invalidate cached order
        return ptr;
    }

    // Initialize all systems in dependency order
    bool InitAll() {
        ComputeInitOrder();

        for (const auto& name : m_initOrder) {
            auto* sys = m_systemMap[name];
            spdlog::info("SystemRegistry: Initializing '{}'", name);

            if (!sys->Init()) {
                spdlog::error("SystemRegistry: Failed to initialize '{}'", name);
                return false;
            }
        }

        m_initialized = true;
        spdlog::info("SystemRegistry: All {} systems initialized", m_initOrder.size());
        return true;
    }

    // Tick all enabled systems
    void TickAll(float deltaTime, bool isConnected, bool isGameLoaded) {
        for (auto& sys : m_systems) {
            if (!sys->IsEnabled()) continue;
            if (sys->RequiresConnection() && !isConnected) continue;
            if (sys->RequiresGameLoaded() && !isGameLoaded) continue;

            __try {
                sys->Tick(deltaTime);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                spdlog::error("SystemRegistry: SEH exception in '{}'", sys->GetName());
                sys->SetEnabled(false); // Disable faulting system
            }
        }
    }

    // Shutdown all systems in reverse init order
    void ShutdownAll() {
        for (auto it = m_initOrder.rbegin(); it != m_initOrder.rend(); ++it) {
            auto* sys = m_systemMap[*it];
            spdlog::info("SystemRegistry: Shutting down '{}'", *it);
            sys->Shutdown();
        }
        m_initialized = false;
    }

    // Get a system by name
    IEngineSystem* GetSystem(const std::string& name) {
        auto it = m_systemMap.find(name);
        return it != m_systemMap.end() ? it->second : nullptr;
    }

    // Get a system by type
    template<typename T>
    T* GetSystem() {
        for (auto& sys : m_systems) {
            if (auto* typed = dynamic_cast<T*>(sys.get())) {
                return typed;
            }
        }
        return nullptr;
    }

    bool IsInitialized() const { return m_initialized; }
    size_t SystemCount() const { return m_systems.size(); }

    // Reset (for testing or reconnect)
    void Clear() {
        ShutdownAll();
        m_systems.clear();
        m_systemMap.clear();
        m_initOrder.clear();
    }

private:
    SystemRegistry() = default;

    void ComputeInitOrder() {
        if (!m_initOrder.empty()) return;

        // Topological sort by dependencies
        std::unordered_map<std::string, int> visited; // 0=unvisited, 1=visiting, 2=visited
        std::vector<std::string> order;

        for (auto& sys : m_systems) {
            visited[sys->GetName()] = 0;
        }

        std::function<bool(const std::string&)> visit = [&](const std::string& name) -> bool {
            if (visited[name] == 2) return true;  // Already processed
            if (visited[name] == 1) {
                spdlog::error("SystemRegistry: Circular dependency involving '{}'", name);
                KMP_FAIL("Circular system dependency");
                return false;
            }

            visited[name] = 1;

            auto* sys = m_systemMap[name];
            if (sys) {
                for (const auto& dep : sys->GetDependencies()) {
                    if (m_systemMap.find(dep) == m_systemMap.end()) {
                        spdlog::error("SystemRegistry: '{}' depends on unknown system '{}'", name, dep);
                        KMP_FAIL("Missing system dependency");
                        return false;
                    }
                    if (!visit(dep)) return false;
                }
            }

            visited[name] = 2;
            order.push_back(name);
            return true;
        };

        for (auto& sys : m_systems) {
            visit(sys->GetName());
        }

        m_initOrder = std::move(order);
    }

    std::vector<std::unique_ptr<IEngineSystem>> m_systems;
    std::unordered_map<std::string, IEngineSystem*> m_systemMap;
    std::vector<std::string> m_initOrder;
    bool m_initialized = false;
};

} // namespace kmp::engine
