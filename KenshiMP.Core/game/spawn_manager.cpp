#include "spawn_manager.h"
#include "game_types.h"
#include "../core.h"
#include "../hooks/entity_hooks.h"
#include "../hooks/ai_hooks.h"
#include "../sync/pipeline_state.h"
#include "kmp/hook_manager.h"
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

void SpawnManager::SetSavedRequestStruct(const uint8_t* data, size_t size) {
    std::lock_guard lock(m_templateMutex);
    m_savedRequestStruct.assign(data, data + size);
    m_hasRequestStruct = true;
    spdlog::info("SpawnManager: Saved request struct ({} bytes)", size);
}

void SpawnManager::SetPreCallData(const uint8_t* data, size_t size, uintptr_t origAddr) {
    std::lock_guard lock(m_templateMutex);
    size_t copySize = (size < sizeof(m_preCallData)) ? size : sizeof(m_preCallData);
    memcpy(m_preCallData, data, copySize);
    m_preCallDataSize = copySize;
    m_preCallOrigAddr = origAddr;
    m_hasPreCallData = true;
    spdlog::info("SpawnManager: Saved pre-call data ({} bytes, origAddr=0x{:X})", copySize, origAddr);
}

// SEH wrapper for standalone factory call
static void* SEH_CallFactoryStandalone(FactoryProcessFn fn, void* factory, void* reqStruct) {
    __try {
        return fn(factory, reqStruct);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

// Write the local player's faction to a newly-spawned character to prevent
// use-after-free on faction+0x250. Mod template factions may not exist in
// the local save, but the local player's faction is always valid.
static void ApplyFactionFix(void* character) {
    if (!character) return;
    uintptr_t localFaction = entity_hooks::GetEarlyPlayerFaction();
    if (localFaction == 0) localFaction = entity_hooks::GetFallbackFaction();
    if (localFaction != 0) {
        game::CharacterAccessor accessor(character);
        accessor.WriteFaction(localFaction);
        spdlog::debug("SpawnManager: Faction fix applied — wrote 0x{:X} to char 0x{:X}",
                      localFaction, reinterpret_cast<uintptr_t>(character));
    } else {
        spdlog::warn("SpawnManager: No local faction available for faction fix on char 0x{:X}",
                     reinterpret_cast<uintptr_t>(character));
    }
}

void* SpawnManager::SpawnCharacterDirect(const Vec3* desiredPosition, int modSlot) {
    // ═══ SINGLE PATH: Mod template via FactoryCreate ═══
    // Mod templates are REAL persistent GameData from kenshi-online.mod.
    // FactoryCreate builds fresh internal state (faction, squad, AI) — no stale pointers.
    // REMOVED: createRandomChar fallback (wrong appearance, no mod data integration).
    if (m_modTemplateCount.load() <= 0 || !m_factory) {
        static int s_noTemplateLog = 0;
        if (++s_noTemplateLog <= 5 || s_noTemplateLog % 100 == 0) {
            spdlog::warn("SpawnManager: SpawnCharacterDirect not ready "
                         "(modTemplates={}, factory={})",
                         m_modTemplateCount.load(), m_factory != nullptr);
        }
        return nullptr;
    }

    // Clamp modSlot to valid range
    int templateCount = m_modTemplateCount.load();
    if (modSlot < 0 || modSlot >= templateCount) modSlot = modSlot % templateCount;
    if (modSlot < 0) modSlot = 0;

    Vec3 pos = desiredPosition ? *desiredPosition : Vec3{0, 0, 0};
    void* character = SpawnWithModTemplate(modSlot, pos);
    if (character) {
        spdlog::info("SpawnManager: SpawnCharacterDirect SUCCESS — char 0x{:X} (slot {})",
                     reinterpret_cast<uintptr_t>(character), modSlot);
        return character;
    }

    spdlog::warn("SpawnManager: SpawnCharacterDirect failed (slot={})", modSlot);
    return nullptr;
}

void SpawnManager::OnGameCharacterCreated(void* factory, void* gameData, void* character) {
    // Capture factory pointer on first call
    if (!m_factory && factory) {
        m_factory = factory;
        spdlog::info("SpawnManager: Captured RootObjectFactory at 0x{:X}",
                     reinterpret_cast<uintptr_t>(factory));
    }

    // ═══ PRIORITY 1: Extract GameData from the CHARACTER object (PREFERRED) ═══
    // The character stores a persistent reference to its GameData template,
    // managed by GameDataManager. This is safe to reuse for spawning.
    // The hook's `gameData` parameter may be a temporary/consumed request object
    // that becomes invalid after the factory call — DO NOT rely on it for spawning.
    if (character) {
        uintptr_t charPtr = reinterpret_cast<uintptr_t>(character);

        // Dump first few characters' raw memory for diagnostics
        static int s_charDumpCount = 0;
        if (s_charDumpCount < 2) {
            s_charDumpCount++;
            spdlog::info("SpawnManager: Character #{} at 0x{:X} (hookParam=0x{:X}):",
                         s_charDumpCount, charPtr,
                         gameData ? reinterpret_cast<uintptr_t>(gameData) : 0);
            for (int off = 0; off <= 0x80; off += 8) {
                uintptr_t val = 0;
                Memory::Read(charPtr + off, val);
                spdlog::info("  char+0x{:02X}: 0x{:016X}", off, val);
            }
        }

        // Scan pointer-aligned offsets for a GameData backpointer.
        // GameData has: +0x10 = GameDataManager*, +0x28 = name (Kenshi std::string)
        for (int offset = 0x08; offset <= 0x200; offset += 8) {
            // Skip offsets we KNOW are other things
            if (offset == 0x10) continue;  // Faction*
            if (offset >= 0x18 && offset < 0x38) continue; // name string (32 bytes)
            if (offset >= 0x48 && offset < 0x54) continue; // position Vec3
            if (offset >= 0x58 && offset < 0x68) continue; // rotation Quat

            uintptr_t candidateGD = 0;
            if (!Memory::Read(charPtr + offset, candidateGD) || candidateGD == 0) continue;
            if (candidateGD < 0x10000 || candidateGD > 0x00007FFFFFFFFFFF) continue;

            // Validate: candidate+0x28 should have a readable ASCII name
            std::string name = ReadKenshiString(candidateGD + 0x28);
            if (name.empty() || name.length() <= 1 || name.length() >= 100) continue;

            bool validName = true;
            for (char c : name) {
                if (c < 0x20 || c > 0x7E) { validName = false; break; }
            }
            if (!validName) continue;

            // Extra validation: candidate+0x10 should be a consistent pointer
            // (GameDataManager*). If we already know the manager, check it matches.
            uintptr_t candidateMgr = 0;
            Memory::Read(candidateGD + 0x10, candidateMgr);
            if (m_managerPointer != 0 && candidateMgr != m_managerPointer) continue;
            if (candidateMgr < 0x10000 || candidateMgr > 0x00007FFFFFFFFFFF) continue;

            // Found a valid GameData from character backpointer!
            {
                std::lock_guard lock(m_templateMutex);
                m_templates[name] = reinterpret_cast<void*>(candidateGD);

                // ALSO save as factory-validated template — these GameData objects
                // were used by the factory to create actual game objects.
                m_factoryInputTemplates[name] = reinterpret_cast<void*>(candidateGD);
                m_lastFactoryInput = reinterpret_cast<void*>(candidateGD);
                m_lastFactoryInputName = name;

                // Check if this is a CHARACTER (has faction pointer at char+0x10).
                // Objects with factions are actual characters (bandits, villagers, etc.)
                // Objects WITHOUT factions are buildings, items, food, etc.
                uintptr_t factionPtr = 0;
                Memory::Read(charPtr + 0x10, factionPtr);
                bool hasCharacterFaction = (factionPtr != 0 &&
                                            factionPtr > 0x10000 &&
                                            factionPtr < 0x00007FFFFFFFFFFF);

                if (hasCharacterFaction) {
                    m_characterTemplates[name] = reinterpret_cast<void*>(candidateGD);
                    m_lastCharacterTemplate = reinterpret_cast<void*>(candidateGD);
                    m_lastCharacterTemplateName = name;
                    spdlog::info("SpawnManager: CHARACTER template '{}' from char+0x{:X} = 0x{:X} "
                                 "(faction=0x{:X}, charTemplates={})",
                                 name, offset, candidateGD, factionPtr, m_characterTemplates.size());
                }

                if (!m_characterSourcedTemplate) {
                    m_characterSourcedTemplate = reinterpret_cast<void*>(candidateGD);
                    m_managerPointer = candidateMgr;
                    spdlog::info("SpawnManager: VALIDATED template '{}' from char+0x{:X} = 0x{:X} (mgr=0x{:X})",
                                 name, offset, candidateGD, candidateMgr);
                } else {
                    spdlog::debug("SpawnManager: Additional template '{}' at 0x{:X} (factoryTotal={}, charTotal={})",
                                  name, candidateGD, m_factoryInputTemplates.size(),
                                  m_characterTemplates.size());
                }
            }
            break; // Found one valid template from this character, move on
        }
    }

    // NOTE: The hook's gameData parameter is a STACK-allocated request struct
    // (addresses like 0xEFEA60), NOT a persistent GameData object.
    // We cannot read its name or reuse it for spawning.
    // Factory-validated templates are captured from the CHARACTER BACKPOINTER
    // (char+0x40 → GameData*) in the code above.
    if (gameData && !m_defaultTemplate) {
        m_defaultTemplate = gameData;
    }
}

void SpawnManager::QueueSpawn(const SpawnRequest& request) {
    std::lock_guard lock(m_queueMutex);
    m_spawnQueue.push(request);
    spdlog::info("SpawnManager: Queued spawn for entity {} (template: '{}')",
                 request.netId, request.templateName);
    Core::Get().GetPipelineOrch().RecordEvent(
        PipelineEventType::SpawnQueued, 0, request.netId, request.owner,
        "Queued: " + request.templateName);
}

void SpawnManager::ProcessSpawnQueue() {
    // ═══ DEPRECATED — DO NOT USE ═══
    // This function consumed spawn requests from the queue and attempted to call
    // the factory with CLONED request structs. The cloned structs had internal
    // pointers relocated to new addresses, creating characters that lacked proper
    // squad membership, AI state, and internal initialization — causing crashes.
    //
    // Spawn requests are now handled EXCLUSIVELY by the in-place replay mechanism
    // in entity_hooks.cpp (Hook_CharacterCreate). The in-place replay piggybacks
    // on natural game character creation events, using the ORIGINAL stack address
    // so all internal pointers remain valid. This creates fully-initialized characters
    // that the game treats as normal squad members.
    //
    // DO NOT call this function — it intentionally does nothing to preserve the
    // spawn queue for the in-place replay.
}

int SpawnManager::ProcessSpawnQueueFromHook(void* factory) {
    // Called from inside Hook_CharacterCreate while the hook is DISABLED.
    // We can safely call the factory function directly — no HookBypass needed.
    // This runs on the game thread in the correct context (during game logic phase).

    std::queue<SpawnRequest> toProcess;
    {
        std::lock_guard lock(m_queueMutex);
        if (m_spawnQueue.empty()) return 0;
        std::swap(toProcess, m_spawnQueue);
    }

    int spawned = 0;
    std::queue<SpawnRequest> retryQueue;

    auto origFn = reinterpret_cast<FactoryProcessFn>(m_origProcess);
    if (!origFn) {
        // Re-queue everything
        std::lock_guard lock(m_queueMutex);
        std::swap(m_spawnQueue, toProcess);
        return 0;
    }

    while (!toProcess.empty()) {
        SpawnRequest req = toProcess.front();
        toProcess.pop();

        // ═══ PRIORITY 0: MOD TEMPLATE SPAWN (preferred) ═══
        // If kenshi-online.mod is loaded and we have mod templates, use them.
        // Mod templates are REAL persistent GameData objects from the game's FCS database.
        // The factory creates fully-initialized characters from these.
        void* character = nullptr;
        bool usedModTemplate = false;

        // Map owner PlayerID to mod template slot (0-based).
        // PlayerIDs start at 1, so Player 1 → slot 0, Player 2 → slot 1, etc.
        // Wraps around if more players than templates (reuses templates).
        int templateCount = m_modTemplateCount.load();
        int modSlot = 0;
        if (templateCount > 0 && req.owner > 0) {
            modSlot = (static_cast<int>(req.owner) - 1) % templateCount;
        }
        if (modSlot < 0 || modSlot >= templateCount) modSlot = 0;

        if (modSlot >= 0 && modSlot < MAX_MOD_TEMPLATES && m_modPlayerTemplates[modSlot]) {
            spdlog::info("SpawnManager: [FROM HOOK] Attempting MOD TEMPLATE spawn for entity {} "
                         "(slot={}, modGD=0x{:X}, gdOffset={}, posOffset={})",
                         req.netId, modSlot,
                         reinterpret_cast<uintptr_t>(m_modPlayerTemplates[modSlot]),
                         m_gameDataOffsetInStruct, m_positionOffsetInStruct);

            character = SpawnWithModTemplate(modSlot, req.position);
            if (character) {
                usedModTemplate = true;
                spdlog::info("SpawnManager: [FROM HOOK] MOD TEMPLATE SUCCESS — char 0x{:X} for entity {}",
                             reinterpret_cast<uintptr_t>(character), req.netId);
            } else {
                spdlog::warn("SpawnManager: [FROM HOOK] MOD TEMPLATE failed for entity {}, falling back",
                             req.netId);
            }
        }

        // ═══ FALLBACK: Original template search and spawn ═══
        if (!character) {
            void* templateData = nullptr;
            std::string templateSource;

            {
                std::lock_guard lock(m_templateMutex);

                // Priority 1: CHARACTER template matching requested name
                if (!req.templateName.empty()) {
                    auto it = m_characterTemplates.find(req.templateName);
                    if (it != m_characterTemplates.end()) {
                        templateData = it->second;
                        templateSource = "char:'" + req.templateName + "'";
                    }
                }

                // Priority 2: ANY recent character template
                if (!templateData && m_lastCharacterTemplate) {
                    templateData = m_lastCharacterTemplate;
                    templateSource = "char-last:'" + m_lastCharacterTemplateName + "'";
                }

                // Priority 3: factory-input by name (may be building/item)
                if (!templateData && !req.templateName.empty()) {
                    auto it = m_factoryInputTemplates.find(req.templateName);
                    if (it != m_factoryInputTemplates.end()) {
                        templateData = it->second;
                        templateSource = "factory:'" + req.templateName + "'";
                    }
                }

                // Priority 4: ANY factory template (last resort)
                if (!templateData && m_lastFactoryInput) {
                    templateData = m_lastFactoryInput;
                    templateSource = "factory-last:'" + m_lastFactoryInputName + "'";
                }
            }

            if (!templateData) {
                req.retryCount++;
                if (req.retryCount < MAX_SPAWN_RETRIES) {
                    if (req.retryCount % 50 == 1) {
                        spdlog::warn("SpawnManager: [FROM HOOK] No template found for entity {} "
                                     "(requested='{}', retry={}/{}, charTemplates={}, factoryTemplates={}, modTemplates={})",
                                     req.netId, req.templateName, req.retryCount, MAX_SPAWN_RETRIES,
                                     m_characterTemplates.size(), m_factoryInputTemplates.size(), m_modTemplateCount.load());
                    }
                    retryQueue.push(req);
                } else {
                    spdlog::error("SpawnManager: [FROM HOOK] DROPPING entity {} — no template '{}' "
                                  "after {} retries", req.netId, req.templateName, MAX_SPAWN_RETRIES);
                }
                continue;
            }

            spdlog::info("SpawnManager: [FROM HOOK] Fallback spawn entity {} at ({:.0f},{:.0f},{:.0f}) "
                         "factory=0x{:X} template=0x{:X} source={}",
                         req.netId, req.position.x, req.position.y, req.position.z,
                         reinterpret_cast<uintptr_t>(factory),
                         reinterpret_cast<uintptr_t>(templateData),
                         templateSource);

            // Pass the GameData template directly to the factory.
            // STRUCT CLONE REMOVED — cloned structs had stale faction pointers and
            // broken self-references that caused AV crashes at game+0x927E94.
            // The factory should accept a GameData* for character creation.
            character = SEH_CallFactory(origFn, factory, templateData);
        }

        if (character) {
            spdlog::info("SpawnManager: [FROM HOOK] SUCCESS — character 0x{:X} for entity {} (modTemplate={})",
                         reinterpret_cast<uintptr_t>(character), req.netId, usedModTemplate);

            game::CharacterAccessor accessor(character);
            if (accessor.WritePosition(req.position)) {
                spdlog::info("SpawnManager: Position set to ({:.0f},{:.0f},{:.0f})",
                             req.position.x, req.position.y, req.position.z);
            }

            // Write the local player's faction to prevent use-after-free crash
            // on faction+0x250. The mod template may reference a faction that
            // doesn't exist in the local save, but the local player's faction is
            // guaranteed valid.
            ApplyFactionFix(character);

            if (m_onSpawned) {
                m_onSpawned(req.netId, character);
            }
            spawned++;
        } else {
            spdlog::error("SpawnManager: [FROM HOOK] Factory returned null for entity {}",
                         req.netId);
            req.retryCount++;
            if (req.retryCount < MAX_SPAWN_RETRIES) {
                retryQueue.push(req);
            }
        }
    }

    if (!retryQueue.empty()) {
        std::lock_guard lock(m_queueMutex);
        while (!retryQueue.empty()) {
            m_spawnQueue.push(retryQueue.front());
            retryQueue.pop();
        }
    }

    return spawned;
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
    // Get module image size from PE header for range checks
    size_t moduleSize = 0x4000000; // 64MB fallback
    {
        auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(moduleBase);
        if (dos->e_magic == IMAGE_DOS_SIGNATURE) {
            auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(moduleBase + dos->e_lfanew);
            if (nt->Signature == IMAGE_NT_SIGNATURE)
                moduleSize = nt->OptionalHeader.SizeOfImage;
        }
    }

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
            if (val > 0x10000 && val < 0x00007FFFFFFFFFFF &&
                (val & 0x7) == 0 &&
                !(val >= moduleBase && val < moduleBase + moduleSize)) {
                // Double-dereference: a real GameDataManager should be readable
                uintptr_t check = 0;
                if (Memory::Read(val, check) && check != 0) {
                    gdmAddress = candAddr;
                    gdmValue = val;
                    spdlog::info("SpawnManager: GameDataManager found via hardcoded offset 0x{:X} -> 0x{:X}", candAddr, val);
                    break;
                }
            }
        }
    }

    // ── Strategy 2: Derive from GameWorld singleton (works on Steam + GOG) ──
    if (gdmValue == 0) {
        auto& core = Core::Get();
        uintptr_t gwAddr = core.GetGameFunctions().GameWorldSingleton;
        if (gwAddr != 0) {
            uintptr_t gwPtr = 0;
            if (Memory::Read(gwAddr, gwPtr) && gwPtr != 0 &&
                !(gwPtr >= moduleBase && gwPtr < moduleBase + moduleSize)) {
                // GameWorld+0x20 = dataMgr1 (KenshiLib verified)
                uintptr_t val = 0;
                if (Memory::Read(gwPtr + 0x20, val) && val != 0 &&
                    val > 0x10000 && val < 0x00007FFFFFFFFFFF &&
                    !(val >= moduleBase && val < moduleBase + moduleSize)) {
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

    // ── Strategy 4: Use manager pointer from character backpointer extraction ──
    if (gdmValue == 0 && m_managerPointer != 0) {
        gdmValue = m_managerPointer;
        gdmAddress = 0; // Not from a specific address
        spdlog::info("SpawnManager: GameDataManager from character backpointer extraction = 0x{:X}",
                     m_managerPointer);
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
                        // Store ALL entries for mod player names so FindModTemplates
                        // can see every candidate (faction, character, squad) and pick
                        // the right one via the numId heuristic.
                        // Match "Player 1" through "Player 16" for 16-player support.
                        if (name.size() >= 8 && name.substr(0, 7) == "Player " &&
                            name.size() <= 9) {
                            // Verify the suffix is a valid player number (1-16)
                            std::string numStr = name.substr(7);
                            bool validNum = !numStr.empty() && numStr.size() <= 2;
                            if (validNum) {
                                for (char c : numStr) { if (c < '0' || c > '9') validNum = false; }
                            }
                            if (validNum) {
                                int num = std::stoi(numStr);
                                if (num >= 1 && num <= 16) {
                                    m_modCandidates.emplace_back(name, reinterpret_cast<void*>(gdPtr));
                                }
                            }
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

    // Set validated templates from heap scan results
    {
        std::lock_guard lock(m_templateMutex);
        const char* preferredTemplates[] = {
            "Greenlander", "Scorchlander", "Shek", "Hive Worker Drone",
            "greenlander", "scorchlander", "shek",
        };

        // Set character-sourced template from heap scan if not already set
        if (!m_characterSourcedTemplate) {
            for (auto tplName : preferredTemplates) {
                auto it = m_templates.find(tplName);
                if (it != m_templates.end()) {
                    m_characterSourcedTemplate = it->second;
                    spdlog::info("SpawnManager: Character-sourced template set from heap scan: '{}'", tplName);
                    break;
                }
            }
            if (!m_characterSourcedTemplate && !m_templates.empty()) {
                m_characterSourcedTemplate = m_templates.begin()->second;
                spdlog::info("SpawnManager: Character-sourced template set from heap scan: '{}' (first available)",
                             m_templates.begin()->first);
            }
        }

        // Also set default template as fallback
        if (!m_defaultTemplate && !m_templates.empty()) {
            m_defaultTemplate = m_templates.begin()->second;
        }
    }
}

void SpawnManager::SetGameDataOffset(int offset) {
    m_gameDataOffsetInStruct = offset;
    spdlog::info("SpawnManager: GameData offset in request struct = +0x{:X}", offset);
}

void SpawnManager::SetPositionOffset(int offset) {
    m_positionOffsetInStruct = offset;
    spdlog::info("SpawnManager: Position offset in request struct = +0x{:X}", offset);
}

void* SpawnManager::GetModTemplate(int playerSlot) const {
    if (playerSlot < 0 || playerSlot >= MAX_MOD_TEMPLATES) return nullptr;
    return m_modPlayerTemplates[playerSlot];
}

void SpawnManager::FindModTemplates() {
    std::lock_guard lock(m_templateMutex);

    // If we already found mod templates, don't re-search
    if (m_modTemplateCount.load() > 0) {
        return;
    }

    // If we have no mod candidates (heap scan hasn't run or found no Player entries), skip
    if (m_modCandidates.empty()) {
        spdlog::debug("SpawnManager: FindModTemplates — no mod candidates from heap scan, skipping");
        return;
    }

    // Look for "Player 1" through "Player 16" character templates from kenshi-online.mod.
    // m_modCandidates stores ALL heap-scan GameData entries named "Player N",
    // including factions, characters, and squads that share the same name.
    // We distinguish character templates by reading the numeric ID at GameData+0x08 —
    // character IDs are the highest per-player (e.g., 19/20 for Player 1/2 in the
    // original mod), so we prefer the candidate with the highest numId.
    int foundCount = 0;

    for (int slot = 0; slot < MAX_MOD_TEMPLATES; slot++) {
        std::string playerName = "Player " + std::to_string(slot + 1);
        struct Candidate { void* ptr; uint32_t numId; };
        std::vector<Candidate> candidates;

        for (auto& [name, gdPtr] : m_modCandidates) {
            if (name == playerName) {
                uint32_t numId = 0;
                Memory::Read(reinterpret_cast<uintptr_t>(gdPtr) + 0x08, numId);
                candidates.push_back({gdPtr, numId});
                spdlog::info("SpawnManager: Mod candidate '{}' at 0x{:X} numId={}",
                             playerName, reinterpret_cast<uintptr_t>(gdPtr), numId);
            }
        }

        if (candidates.empty()) {
            // Only warn for Player 1 and 2 — higher slots may not exist in smaller mods
            if (slot < 2) {
                spdlog::warn("SpawnManager: No GameData entry found for '{}' — kenshi-online.mod not loaded?",
                             playerName);
            }
            continue;
        }

        // Pick the candidate most likely to be a CHARACTER template.
        // Character IDs are the highest per-player, so prefer the highest numId.
        void* bestCandidate = nullptr;
        uint32_t bestId = 0;
        for (auto& c : candidates) {
            if (c.numId > bestId) {
                bestId = c.numId;
                bestCandidate = c.ptr;
            }
        }

        // Fallback: if all IDs are 0 (couldn't read), just use the first candidate
        if (!bestCandidate && !candidates.empty()) {
            bestCandidate = candidates[0].ptr;
            bestId = candidates[0].numId;
        }

        if (bestCandidate) {
            m_modPlayerTemplates[slot] = bestCandidate;
            foundCount++;
            spdlog::info("SpawnManager: MOD TEMPLATE slot {} = '{}' at 0x{:X} (id={})",
                         slot, playerName,
                         reinterpret_cast<uintptr_t>(bestCandidate), bestId);
        }
    }

    // Atomic store — safe for lockless reads from other threads
    m_modTemplateCount.store(foundCount);
    spdlog::info("SpawnManager: FindModTemplates complete — {} mod templates found (of {} max)",
                 foundCount, MAX_MOD_TEMPLATES);
}

void* SpawnManager::SpawnWithModTemplate(int playerSlot, const Vec3& position) {
    if (playerSlot < 0 || playerSlot >= MAX_MOD_TEMPLATES) return nullptr;
    void* modGD = m_modPlayerTemplates[playerSlot];
    if (!modGD) return nullptr;
    // Only m_factory is needed — CallFactoryCreate uses its own function pointer
    // (RVA 0x583400), not m_origProcess (the process trampoline).
    if (!m_factory) return nullptr;

    spdlog::info("SpawnManager: SpawnWithModTemplate slot={} factory=0x{:X} modGD=0x{:X} "
                 "pos=({:.0f},{:.0f},{:.0f})",
                 playerSlot, reinterpret_cast<uintptr_t>(m_factory),
                 reinterpret_cast<uintptr_t>(modGD),
                 position.x, position.y, position.z);

    // ═══ SINGLE PATH: RootObjectFactory::create ═══
    // The `create` function (RVA 0x583400) is the HIGH-LEVEL dispatcher called by
    // 11 game systems. It takes (factory, GameData*) and INTERNALLY builds a fresh
    // request struct with live pointers (faction, squad, AI), then calls process().
    // This completely bypasses the stale-pointer struct clone crash.
    //
    // REMOVED: Approaches 1-3 (raw GameData to process, struct clone, createRandomChar)
    // were crash-prone — stale faction pointers, broken self-refs, wrong appearance.
    // FactoryCreate is the ONLY safe path because it constructs fresh internal state.
    {
        void* character = entity_hooks::CallFactoryCreate(m_factory, modGD);
        if (character) {
            uintptr_t charAddr = reinterpret_cast<uintptr_t>(character);
            if (charAddr > 0x10000 && charAddr < 0x00007FFFFFFFFFFF && (charAddr & 0x7) == 0) {
                spdlog::info("SpawnManager: SpawnWithModTemplate SUCCESS — char 0x{:X}", charAddr);
                game::CharacterAccessor accessor(character);
                accessor.WritePosition(position);
                // DO NOT call ApplyFactionFix here — mod template characters have
                // persistent factions from kenshi-online.mod ("Player 1"/"Player 2").
                // Writing the LOCAL player's faction causes them to appear in the
                // squad panel, flooding it with remote characters and crashing.
                // Mod factions are always loaded (in GameDataManager), so no use-after-free.
                return character;
            }
        }
        spdlog::warn("SpawnManager: FactoryCreate returned null/invalid for slot {}", playerSlot);
    }

    spdlog::warn("SpawnManager: SpawnWithModTemplate failed for slot {}", playerSlot);
    return nullptr;
}

bool SpawnManager::VerifyReadiness() const {
    bool hasFactory = (m_factory != nullptr);
    bool hasOrigProcess = (m_origProcess != nullptr);
    bool hasPreCall = m_hasPreCallData;
    bool hasCharTemplates = false;
    bool hasAnyTemplates = false;
    size_t charCount = 0, factoryCount = 0, totalCount = 0;

    {
        std::lock_guard lock(m_templateMutex);
        charCount = m_characterTemplates.size();
        factoryCount = m_factoryInputTemplates.size();
        totalCount = m_templates.size();
        hasCharTemplates = (charCount > 0);
        hasAnyTemplates = (totalCount > 0);
    }

    spdlog::info("SpawnManager::VerifyReadiness:");
    spdlog::info("  Factory pointer:     {} (0x{:X})", hasFactory ? "YES" : "NO",
                 reinterpret_cast<uintptr_t>(m_factory));
    spdlog::info("  OrigProcess fn:      {} (0x{:X})", hasOrigProcess ? "YES" : "NO",
                 reinterpret_cast<uintptr_t>(m_origProcess));
    spdlog::info("  Pre-call data:       {} ({} bytes)", hasPreCall ? "YES" : "NO",
                 m_preCallDataSize);
    spdlog::info("  Character templates: {} ({})", hasCharTemplates ? "YES" : "NO", charCount);
    spdlog::info("  Factory templates:   {}", factoryCount);
    spdlog::info("  Total templates:     {}", totalCount);
    spdlog::info("  Manager pointer:     0x{:X}", m_managerPointer);
    int modCount = m_modTemplateCount.load();
    spdlog::info("  Mod templates:       {}", modCount);
    spdlog::info("  GameData offset:     {}", m_gameDataOffsetInStruct >= 0
                 ? ("0x" + std::to_string(m_gameDataOffsetInStruct)) : "NOT DETECTED");
    spdlog::info("  Position offset:     {}", m_positionOffsetInStruct >= 0
                 ? ("0x" + std::to_string(m_positionOffsetInStruct)) : "NOT DETECTED");

    // In-place replay path: needs factory + origProcess + preCallData
    bool inPlacePath = hasFactory && hasOrigProcess && hasPreCall;
    // Direct spawn path: needs origProcess + preCallData
    bool directPath = hasOrigProcess && hasPreCall;
    // Mod template path: needs mod templates + factory (GameData offset optional — has fallback)
    bool modTemplatePath = (modCount > 0) && hasFactory && hasOrigProcess;

    spdlog::info("  Spawn paths available:");
    spdlog::info("    Mod template (preferred): {}", modTemplatePath ? "READY" : "NOT READY");
    spdlog::info("    In-place replay (hook): {}", inPlacePath ? "READY" : "NOT READY");
    spdlog::info("    Direct spawn (fallback): {}", directPath ? "READY" : "NOT READY");

    if (!inPlacePath && !directPath && !modTemplatePath) {
        spdlog::warn("SpawnManager: NO spawn paths available! Remote characters cannot be created.");
        spdlog::warn("  This means the CharacterCreate hook did not fire during loading.");
    }

    return inPlacePath || directPath || modTemplatePath;
}

} // namespace kmp
