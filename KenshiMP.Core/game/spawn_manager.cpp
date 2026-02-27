#include "spawn_manager.h"
#include "game_types.h"
#include "../core.h"
#include <spdlog/spdlog.h>
#include <Windows.h>

namespace kmp {

// ── SEH helper functions ──
// MSVC forbids __try in functions with C++ objects that need unwinding.
// These thin wrappers contain ONLY the SEH-protected code, no std:: objects.

// Reads raw string data from a Kenshi std::string at `addr` into `outBuf`.
// Returns the number of bytes copied, or 0 on failure.
static size_t SEH_ReadKenshiStringRaw(uintptr_t addr, char* outBuf, size_t bufSize) {
    __try {
        uint64_t length = 0;
        uint64_t capacity = 0;
        Memory::Read(addr + 0x10, length);
        Memory::Read(addr + 0x18, capacity);

        if (length == 0 || length > 4096) return 0;

        const char* strData = nullptr;
        if (capacity < 16) {
            strData = reinterpret_cast<const char*>(addr);
        } else {
            uintptr_t heapPtr = 0;
            Memory::Read(addr, heapPtr);
            if (heapPtr == 0) return 0;
            strData = reinterpret_cast<const char*>(heapPtr);
        }

        size_t copyLen = (length < bufSize - 1) ? static_cast<size_t>(length) : bufSize - 1;
        __try {
            memcpy(outBuf, strData, copyLen);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return 0;
        }
        outBuf[copyLen] = '\0';
        return copyLen;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

// Calls the factory function under SEH protection.
// Returns the created character pointer, or nullptr on exception.
static void* SEH_CallFactory(FactoryProcessFn fn, void* factory, void* templateData) {
    __try {
        return fn(factory, templateData);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

// Reads a uintptr_t-sized value from memory under SEH protection.
// Returns true if the read succeeded.
static bool SEH_ReadPointer(const uintptr_t* src, uintptr_t& out) {
    __try {
        out = *src;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        out = 0;
        return false;
    }
}

// ── Kenshi std::string layout (MSVC x64) ──
// Offset +0x00: union { char buf[16]; char* ptr; }  (small string or heap pointer)
// Offset +0x10: size_t length
// Offset +0x18: size_t capacity
// If capacity < 16, the string is stored inline in buf[16].
// If capacity >= 16, ptr points to heap allocation.
std::string SpawnManager::ReadKenshiString(uintptr_t addr) {
    char buf[256] = {};
    size_t len = SEH_ReadKenshiStringRaw(addr, buf, sizeof(buf));
    if (len == 0) return "";
    return std::string(buf, len);
}

void SpawnManager::OnGameCharacterCreated(void* factory, void* gameData, void* character) {
    // Capture factory pointer on first call
    if (!m_factory && factory) {
        m_factory = factory;
        spdlog::info("SpawnManager: Captured RootObjectFactory at 0x{:X}",
                     reinterpret_cast<uintptr_t>(factory));
    }

    // Capture template from the GameData
    if (gameData) {
        // GameData name is at offset 0x28 (from KServerMod structs)
        uintptr_t gdPtr = reinterpret_cast<uintptr_t>(gameData);
        std::string name = ReadKenshiString(gdPtr + 0x28);

        if (!name.empty()) {
            std::lock_guard lock(m_templateMutex);
            if (m_templates.find(name) == m_templates.end()) {
                m_templates[name] = gameData;
                spdlog::debug("SpawnManager: Captured template '{}' at 0x{:X}",
                             name, gdPtr);
            }

            // Store the first player-type template as default
            if (!m_defaultTemplate) {
                m_defaultTemplate = gameData;
                spdlog::info("SpawnManager: Default template set to '{}' at 0x{:X}",
                            name, gdPtr);
            }
        }
    }
}

void SpawnManager::QueueSpawn(const SpawnRequest& request) {
    std::lock_guard lock(m_queueMutex);
    m_spawnQueue.push(request);
    spdlog::info("SpawnManager: Queued spawn for entity {} (template: '{}')",
                 request.netId, request.templateName);
}

void SpawnManager::ProcessSpawnQueue() {
    if (!m_factory || !m_origProcess) return;

    std::queue<SpawnRequest> toProcess;
    {
        std::lock_guard lock(m_queueMutex);
        std::swap(toProcess, m_spawnQueue);
    }

    while (!toProcess.empty()) {
        SpawnRequest req = toProcess.front();
        toProcess.pop();

        // Find the template
        void* templateData = nullptr;
        if (!req.templateName.empty()) {
            templateData = FindTemplate(req.templateName);
        }
        if (!templateData) {
            templateData = GetDefaultTemplate();
        }
        if (!templateData) {
            spdlog::warn("SpawnManager: No template available for entity {}, skipping", req.netId);
            continue;
        }

        spdlog::info("SpawnManager: Spawning entity {} using factory at 0x{:X}, template at 0x{:X}",
                     req.netId,
                     reinterpret_cast<uintptr_t>(m_factory),
                     reinterpret_cast<uintptr_t>(templateData));

        // Call the game's character creation function via SEH wrapper
        void* character = SEH_CallFactory(m_origProcess, m_factory, templateData);

        if (character) {
            spdlog::info("SpawnManager: Character created at 0x{:X} for entity {}",
                         reinterpret_cast<uintptr_t>(character), req.netId);

            // Set position using CharacterAccessor (writes to physics chain + cached pos)
            game::CharacterAccessor accessor(character);
            if (!accessor.WritePosition(req.position)) {
                spdlog::warn("SpawnManager: Failed to write position for entity {}", req.netId);
            }

            // Notify the entity registry
            if (m_onSpawned) {
                m_onSpawned(req.netId, character);
            }
        } else {
            spdlog::error("SpawnManager: Factory returned null or exception for entity {}", req.netId);
        }
    }
}

void* SpawnManager::FindTemplate(const std::string& name) const {
    std::lock_guard lock(m_templateMutex);
    auto it = m_templates.find(name);
    return (it != m_templates.end()) ? it->second : nullptr;
}

void* SpawnManager::GetDefaultTemplate() const {
    std::lock_guard lock(m_templateMutex);
    return m_defaultTemplate;
}

size_t SpawnManager::GetTemplateCount() const {
    std::lock_guard lock(m_templateMutex);
    return m_templates.size();
}

void SpawnManager::ScanGameDataHeap() {
    // Scan the process heap for GameData objects.
    // GameData has a GameDataManager* at offset +0x10.
    // We look for the main GameDataManager pointer in memory.

    uintptr_t moduleBase = Memory::GetModuleBase();

    // ── Strategy 1: Try hardcoded offsets (fast, works if version matches) ──
    uintptr_t gdmAddress = 0;
    uintptr_t gdmValue = 0;

    uintptr_t hardcodedCandidates[] = {
        moduleBase + 0x2133060,          // GOG GameDataManagerMain
        moduleBase + 0x2133040 + 0x20,   // GOG GameWorld + dataMgr1 offset
    };

    for (auto candAddr : hardcodedCandidates) {
        uintptr_t val = 0;
        if (Memory::Read(candAddr, val) && val != 0) {
            if (val > moduleBase && val < moduleBase + 0x10000000) {
                gdmAddress = candAddr;
                gdmValue = val;
                spdlog::info("SpawnManager: GameDataManager found via hardcoded offset 0x{:X}", candAddr);
                break;
            }
        }
    }

    // ── Strategy 2: Derive from GameWorld singleton (works on Steam + GOG) ──
    if (gdmValue == 0) {
        auto& core = Core::Get();
        uintptr_t gwAddr = core.GetGameFunctions().GameWorldSingleton;
        if (gwAddr != 0) {
            uintptr_t gwPtr = 0;
            if (Memory::Read(gwAddr, gwPtr) && gwPtr != 0) {
                // GameWorld+0x20 = dataMgr1 (KenshiLib verified)
                uintptr_t val = 0;
                if (Memory::Read(gwPtr + 0x20, val) && val != 0 && val > moduleBase) {
                    gdmValue = val;
                    gdmAddress = gwPtr + 0x20;
                    spdlog::info("SpawnManager: GameDataManager found via GameWorld+0x20 = 0x{:X}", val);
                }
            }
        }
    }

    // ── Strategy 3: Scan from captured template's manager pointer ──
    if (gdmValue == 0 && !m_templates.empty()) {
        // We already have some templates. Read the manager pointer from one.
        // GameData+0x10 = GameDataManager* (KServerMod verified)
        auto it = m_templates.begin();
        uintptr_t gdPtr = reinterpret_cast<uintptr_t>(it->second);
        uintptr_t mgrPtr = 0;
        if (Memory::Read(gdPtr + 0x10, mgrPtr) && mgrPtr != 0 && mgrPtr > moduleBase) {
            gdmValue = mgrPtr;
            gdmAddress = gdPtr + 0x10;
            spdlog::info("SpawnManager: GameDataManager found via existing template '{}' = 0x{:X}",
                         it->first, mgrPtr);
        }
    }

    if (gdmValue == 0) {
        spdlog::warn("SpawnManager: Could not find GameDataManager, skipping heap scan");
        return;
    }

    spdlog::info("SpawnManager: GameDataManager at 0x{:X} (value 0x{:X}), scanning heap...",
                 gdmAddress, gdmValue);

    // Scan writable memory regions for pointers to gdmValue
    MEMORY_BASIC_INFORMATION mbi;
    uintptr_t scanAddr = 0;
    int found = 0;
    auto startTime = GetTickCount64();

    while (VirtualQuery(reinterpret_cast<void*>(scanAddr), &mbi, sizeof(mbi))) {
        if (mbi.State == MEM_COMMIT &&
            (mbi.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE)) &&
            !(mbi.Protect & PAGE_GUARD) &&
            mbi.RegionSize > 0 && mbi.RegionSize < 0x10000000) {

            auto* base = reinterpret_cast<const uintptr_t*>(mbi.BaseAddress);
            size_t count = mbi.RegionSize / sizeof(uintptr_t);

            for (size_t i = 0; i < count; i++) {
                uintptr_t val = 0;
                if (!SEH_ReadPointer(&base[i], val)) break; // Region became unreadable
                if (val == gdmValue) {
                    // Found a pointer to GameDataManager
                    // The GameData object starts 0x10 bytes before this
                    uintptr_t gdPtr = reinterpret_cast<uintptr_t>(&base[i]) - 0x10;

                    // Read the name from GameData+0x28
                    std::string name = ReadKenshiString(gdPtr + 0x28);
                    if (!name.empty() && name.length() > 1 && name.length() < 200) {
                        std::lock_guard lock(m_templateMutex);
                        if (m_templates.find(name) == m_templates.end()) {
                            m_templates[name] = reinterpret_cast<void*>(gdPtr);
                        }
                        found++;
                    }
                }
            }
        }

        scanAddr = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
        if (scanAddr < reinterpret_cast<uintptr_t>(mbi.BaseAddress)) break; // Overflow
    }

    auto elapsed = (GetTickCount64() - startTime) / 1000.0;
    spdlog::info("SpawnManager: Heap scan found {} GameData entries ({} unique templates) in {:.1f}s",
                 found, m_templates.size(), elapsed);

    // Set default template if we found any player-like templates
    if (!m_defaultTemplate) {
        std::lock_guard lock(m_templateMutex);
        const char* preferredTemplates[] = {
            "Greenlander", "Scorchlander", "Shek", "Hive Worker Drone",
            "greenlander", "scorchlander", "shek",
        };
        for (auto tplName : preferredTemplates) {
            auto it = m_templates.find(tplName);
            if (it != m_templates.end()) {
                m_defaultTemplate = it->second;
                spdlog::info("SpawnManager: Default template set to '{}' from heap scan", tplName);
                break;
            }
        }
        if (!m_defaultTemplate && !m_templates.empty()) {
            m_defaultTemplate = m_templates.begin()->second;
            spdlog::info("SpawnManager: Default template set to '{}' (first available)",
                        m_templates.begin()->first);
        }
    }
}

} // namespace kmp
