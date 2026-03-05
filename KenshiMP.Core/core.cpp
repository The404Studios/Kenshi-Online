#include "core.h"
#include "sys/command_registry.h"
#include "hooks/render_hooks.h"
#include "hooks/input_hooks.h"
#include "hooks/entity_hooks.h"
#include "hooks/movement_hooks.h"
#include "hooks/combat_hooks.h"
#include "hooks/world_hooks.h"
#include "hooks/save_hooks.h"
#include "hooks/time_hooks.h"
#include "hooks/game_tick_hooks.h"
#include "hooks/inventory_hooks.h"
#include "hooks/squad_hooks.h"
#include "hooks/faction_hooks.h"
#include "hooks/building_hooks.h"
#include "hooks/ai_hooks.h"
#include "hooks/resource_hooks.h"
#include "game/game_types.h"
#include "game/asset_facilitator.h"
#include "game/game_inventory.h"
#include "kmp/protocol.h"
#include "kmp/messages.h"
#include "kmp/constants.h"
#include "kmp/memory.h"
#include "kmp/function_analyzer.h"
#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <chrono>
#include <algorithm>
#include <Windows.h>

namespace kmp {

// Forward declaration
void InitPacketHandler();

// ── Crash diagnostics ──
// Tracks last completed step in OnGameTick so the crash handler can report it.
static volatile int g_lastTickStep = -1;
static volatile int g_tickNumber = 0;
static volatile const char* g_lastStepName = "init";

// Vectored exception handler — fires BEFORE Kenshi's frame-based SEH handlers.
// SetUnhandledExceptionFilter was being overridden by Kenshi, so we never saw crash info.
static PVOID g_vehHandle = nullptr;
static volatile bool g_vehFired = false;
static uintptr_t g_gameModuleBase = 0;
static uintptr_t g_gameModuleEnd  = 0;

// Exported counter — entity_hooks updates this so VEH can report which create crashed
volatile int g_lastCharacterCreateNum = 0;

// SEH-safe stack dump helper (no C++ objects with destructors allowed)
static int SEH_DumpStack(char* outBuf, int outBufSize, uint64_t rsp) {
    int pos = sprintf_s(outBuf, outBufSize, "  Stack at RSP:\n");
    __try {
        auto* sp = reinterpret_cast<const uint64_t*>(rsp);
        for (int i = 0; i < 16 && pos < outBufSize - 64; i++) {
            pos += sprintf_s(outBuf + pos, outBufSize - pos,
                "    [RSP+0x%02X] = 0x%016llX\n", i * 8, sp[i]);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        pos += sprintf_s(outBuf + pos, outBufSize - pos,
            "    (stack read failed)\n");
    }
    return pos;
}

static LONG CALLBACK VectoredCrashHandler(EXCEPTION_POINTERS* ep) {
    DWORD code = ep->ExceptionRecord->ExceptionCode;

    // Handle fatal exception types + heap/C++ exceptions for crash diagnosis.
    // 0xC0000374 = STATUS_HEAP_CORRUPTION, 0xC0000602 = STATUS_FAIL_FAST_EXCEPTION,
    // 0xE06D7363 = MSVC C++ exception (throw), 0xC0000409 = STATUS_STACK_BUFFER_OVERRUN
    if (code != EXCEPTION_ACCESS_VIOLATION &&
        code != EXCEPTION_STACK_OVERFLOW &&
        code != EXCEPTION_ILLEGAL_INSTRUCTION &&
        code != EXCEPTION_INT_DIVIDE_BY_ZERO &&
        code != EXCEPTION_PRIV_INSTRUCTION &&
        code != 0xC0000374 &&  // STATUS_HEAP_CORRUPTION
        code != 0xC0000602 &&  // STATUS_FAIL_FAST_EXCEPTION
        code != 0xC0000409 &&  // STATUS_STACK_BUFFER_OVERRUN (/GS check)
        code != 0xE06D7363) {  // MSVC C++ exception
        return EXCEPTION_CONTINUE_SEARCH;
    }

    // ── Filter by RIP location ──
    // System DLLs (ntdll, ucrtbase, etc.) trigger AVs during normal operation
    // (e.g., memory probing, guard page checks). Kenshi's SEH catches these.
    // Log crashes in: game module, our DLL, dynamically allocated hook stubs, or NULL.
    uintptr_t rip = reinterpret_cast<uintptr_t>(ep->ExceptionRecord->ExceptionAddress);
    bool inGame = (rip >= g_gameModuleBase && rip < g_gameModuleEnd);
    bool isNull = (rip < 0x10000);  // NULL or near-NULL pointer call

    // Also accept crashes in our DLL module (KenshiMP.Core.dll)
    static uintptr_t s_dllBase = 0, s_dllEnd = 0;
    if (s_dllBase == 0) {
        HMODULE ourDll = nullptr;
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                           reinterpret_cast<LPCSTR>(&VectoredCrashHandler), &ourDll);
        if (ourDll) {
            s_dllBase = reinterpret_cast<uintptr_t>(ourDll);
            auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(ourDll);
            auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(
                reinterpret_cast<const uint8_t*>(ourDll) + dos->e_lfanew);
            s_dllEnd = s_dllBase + nt->OptionalHeader.SizeOfImage;
        }
    }
    bool inOurDll = (s_dllBase && rip >= s_dllBase && rip < s_dllEnd);

    // Accept crashes in low-address VirtualAlloc'd pages (hook stubs at 0x01000000-0x7FFFFFFF)
    // Hook pages are allocated via VirtualAlloc with PAGE_EXECUTE_READWRITE at low addresses.
    // System DLLs are at 0x7FFE... which is above this range — no false positives.
    bool inHookStub = (rip >= 0x10000 && rip < 0x80000000);

    if (!inGame && !isNull && !inOurDll && !inHookStub) {
        return EXCEPTION_CONTINUE_SEARCH;  // System DLL exception — let SEH handle it
    }

    // Skip DLL-internal access violations that are benign SEH-protected probes.
    // Memory::Read probes trigger ~53 AVs per session (84% of all VEH entries).
    // These are caught by __except handlers and are completely harmless.
    // BUT we must NOT skip all DLL AVs — real hook bugs need to be logged.
    // Strategy: skip DLL AVs at known Memory::Read RVA ranges (dll+0x1B00..0x1C00)
    // and during loading (tick=0) when all the scanner probes happen.
    if (inOurDll && code == EXCEPTION_ACCESS_VIOLATION) {
        uintptr_t dllOffset = rip - s_dllBase;
        // Memory::Read<T> templates are at dll+0x1B00..0x1C00 (all builds)
        if (dllOffset >= 0x1A00 && dllOffset <= 0x1D00) {
            return EXCEPTION_CONTINUE_SEARCH;
        }
        // During loading (before first game tick), most DLL AVs are scanner probes
        if (g_tickNumber == 0) {
            return EXCEPTION_CONTINUE_SEARCH;
        }
    }

    // Deduplicate — don't flood the log with repeated identical crashes (same RIP)
    static volatile uintptr_t s_lastCrashRip = 0;
    static volatile int s_repeatCount = 0;
    if (rip == s_lastCrashRip) {
        if (++s_repeatCount > 2) return EXCEPTION_CONTINUE_SEARCH; // Log max 3 per RIP
    } else {
        s_lastCrashRip = rip;
        s_repeatCount = 0;
    }

    // Rate-limit crash logging — allow up to 20 entries per session.
    // (Old one-shot g_vehFired was consumed by SEH_MemcpySafe noise at startup,
    //  causing the REAL post-loading crash to go completely unlogged!)
    static volatile int s_vehEntryCount = 0;
    if (s_vehEntryCount >= 20) return EXCEPTION_CONTINUE_SEARCH;
    s_vehEntryCount++;
    g_vehFired = (s_vehEntryCount >= 20);

    // ── LIGHTWEIGHT LOGGING ONLY ──
    // NO spdlog calls here! spdlog acquires mutexes internally.
    // If the crash happened mid-spdlog-call, re-acquiring deadlocks.
    // Use only OutputDebugStringA + direct file write (no C++ objects).

    auto* ctx = ep->ContextRecord;
    uintptr_t ripOffset = inGame ? (rip - g_gameModuleBase) : 0;

    char buf[4096];
    int pos = sprintf_s(buf,
        "KMP VEH CRASH: ExceptionCode=0x%08lX at RIP=0x%016llX (game+0x%llX)\n"
        "  RAX=0x%016llX  RBX=0x%016llX  RCX=0x%016llX  RDX=0x%016llX\n"
        "  RSP=0x%016llX  RBP=0x%016llX  RSI=0x%016llX  RDI=0x%016llX\n"
        "  R8 =0x%016llX  R9 =0x%016llX  R10=0x%016llX  R11=0x%016llX\n"
        "  R12=0x%016llX  R13=0x%016llX  R14=0x%016llX  R15=0x%016llX\n"
        "  Last CharacterCreate: #%d, OnGameTick step: %d (%s), tick #%d\n"
        "  Filter: inGame=%d inNull=%d inDll=%d inStub=%d dllBase=0x%llX dllEnd=0x%llX\n",
        code, (unsigned long long)rip, (unsigned long long)ripOffset,
        ctx->Rax, ctx->Rbx, ctx->Rcx, ctx->Rdx,
        ctx->Rsp, ctx->Rbp, ctx->Rsi, ctx->Rdi,
        ctx->R8, ctx->R9, ctx->R10, ctx->R11,
        ctx->R12, ctx->R13, ctx->R14, ctx->R15,
        g_lastCharacterCreateNum,
        g_lastTickStep, g_lastStepName ? g_lastStepName : "?", g_tickNumber,
        (int)inGame, (int)isNull, (int)inOurDll, (int)inHookStub,
        (unsigned long long)s_dllBase, (unsigned long long)s_dllEnd);

    // Log access violation details with correct type classification
    if (code == EXCEPTION_ACCESS_VIOLATION &&
        ep->ExceptionRecord->NumberParameters >= 2) {
        ULONG_PTR avType = ep->ExceptionRecord->ExceptionInformation[0];
        const char* op = (avType == 0) ? "READ" : (avType == 1) ? "WRITE" : "DEP/EXECUTE";
        pos += sprintf_s(buf + pos, sizeof(buf) - pos,
            "  AV: %s at 0x%016llX\n", op,
            (unsigned long long)ep->ExceptionRecord->ExceptionInformation[1]);
    }

    // Stack dump: 16 qwords from RSP (in separate SEH-safe function)
    if (ctx->Rsp > 0x10000 && ctx->Rsp < 0x00007FFFFFFFFFFF) {
        char stackBuf[1024];
        SEH_DumpStack(stackBuf, sizeof(stackBuf), ctx->Rsp);
        strcat_s(buf, stackBuf);
    }

    OutputDebugStringA(buf);

    // Write to dedicated crash file (direct C I/O, no C++ objects)
    FILE* f = nullptr;
    fopen_s(&f, "KenshiOnline_CRASH.log", "a");
    if (f) {
        fprintf(f, "%s\n", buf);
        fclose(f);
    }

    g_vehFired = false; // Allow re-fire for subsequent crashes
    return EXCEPTION_CONTINUE_SEARCH; // Let Windows/Kenshi handle it
}

Core& Core::Get() {
    static Core instance;
    return instance;
}

bool Core::Initialize() {
    // Set up logging
    try {
        // PID-based log filename so multiple instances don't clobber each other
        DWORD pid = GetCurrentProcessId();
        std::string logFile = "KenshiOnline_" + std::to_string(pid) + ".log";
        auto logger = spdlog::basic_logger_mt("kenshi_online", logFile, true);
        spdlog::set_default_logger(logger);
        spdlog::set_level(spdlog::level::debug);
        spdlog::flush_on(spdlog::level::debug);
    } catch (...) {
        // Fallback: no file logging
    }

    // Capture game module range for VEH filtering
    HMODULE gameModule = GetModuleHandleA(nullptr);  // kenshi_x64.exe
    if (gameModule) {
        g_gameModuleBase = reinterpret_cast<uintptr_t>(gameModule);
        // Read PE header to get SizeOfImage
        auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(gameModule);
        auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(
            reinterpret_cast<const uint8_t*>(gameModule) + dos->e_lfanew);
        g_gameModuleEnd = g_gameModuleBase + nt->OptionalHeader.SizeOfImage;
    }

    // Register vectored exception handler — fires BEFORE Kenshi's SEH handlers.
    // SetUnhandledExceptionFilter gets overridden by Kenshi, so we use VEH instead.
    // Filtered to only log crashes in game module or NULL (skips system DLL noise).
    g_vehHandle = AddVectoredExceptionHandler(1, VectoredCrashHandler);

    // ── Last-resort crash detection ──
    // The post-loading "silent crash" isn't caught by VEH (not an AV/stack overflow).
    // Register atexit + SetUnhandledExceptionFilter to detect what kills us.
    atexit([]() {
        OutputDebugStringA("KMP: atexit() fired — process exiting\n");
        char buf[256];
        sprintf_s(buf, "KMP: atexit — LastCreate=#%d, LastTick=#%d step=%d (%s)\n",
                  g_lastCharacterCreateNum, g_tickNumber, g_lastTickStep,
                  g_lastStepName ? g_lastStepName : "?");
        OutputDebugStringA(buf);

        FILE* f = nullptr;
        fopen_s(&f, "C:\\Program Files (x86)\\Steam\\steamapps\\common\\Kenshi\\KenshiOnline_CRASH.log", "a");
        if (f) {
            fprintf(f, "\nKMP atexit: LastCreate=#%d, LastTick=#%d step=%d (%s)\n",
                    g_lastCharacterCreateNum, g_tickNumber, g_lastTickStep,
                    g_lastStepName ? g_lastStepName : "?");
            fclose(f);
        }
    });

    // Unhandled exception filter — catches exceptions that bypass both VEH and SEH
    SetUnhandledExceptionFilter([](EXCEPTION_POINTERS* ep) -> LONG {
        DWORD code = ep->ExceptionRecord->ExceptionCode;
        uintptr_t rip = reinterpret_cast<uintptr_t>(ep->ExceptionRecord->ExceptionAddress);

        char buf[512];
        sprintf_s(buf,
            "KMP UNHANDLED EXCEPTION: code=0x%08lX RIP=0x%016llX "
            "LastCreate=#%d LastTick=#%d step=%d (%s)\n",
            code, (unsigned long long)rip,
            g_lastCharacterCreateNum, g_tickNumber, g_lastTickStep,
            g_lastStepName ? g_lastStepName : "?");
        OutputDebugStringA(buf);

        // Write to crash log
        FILE* f = nullptr;
        fopen_s(&f, "C:\\Program Files (x86)\\Steam\\steamapps\\common\\Kenshi\\KenshiOnline_CRASH.log", "a");
        if (f) {
            fprintf(f, "\n%s", buf);
            auto* ctx = ep->ContextRecord;
            fprintf(f, "  RSP=0x%016llX RBP=0x%016llX\n",
                    (unsigned long long)ctx->Rsp, (unsigned long long)ctx->Rbp);
            fprintf(f, "  RAX=0x%016llX RBX=0x%016llX RCX=0x%016llX RDX=0x%016llX\n",
                    (unsigned long long)ctx->Rax, (unsigned long long)ctx->Rbx,
                    (unsigned long long)ctx->Rcx, (unsigned long long)ctx->Rdx);
            fclose(f);
        }
        return EXCEPTION_CONTINUE_SEARCH;
    });

    OutputDebugStringA("KMP: === Kenshi-Online v0.1.0 Initializing ===\n");
    spdlog::info("=== Kenshi-Online v{}.{}.{} Initializing ===", 0, 1, 0);

    // ── Steam vs GOG Detection ──
    // Check if steam_api64.dll is loaded (Steam version has it, GOG does not).
    {
        HMODULE steamApi = GetModuleHandleA("steam_api64.dll");
        bool isSteam = (steamApi != nullptr);
        m_isSteamVersion = isSteam;

        // Also get executable file size as a version fingerprint
        char exePath[MAX_PATH] = {};
        GetModuleFileNameA(nullptr, exePath, MAX_PATH);
        HANDLE hFile = CreateFileA(exePath, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        DWORD exeSize = 0;
        if (hFile != INVALID_HANDLE_VALUE) {
            exeSize = GetFileSize(hFile, nullptr);
            CloseHandle(hFile);
        }

        spdlog::info("Platform: {} | exe={} ({} bytes) | module base=0x{:X}",
                      isSteam ? "STEAM" : "GOG", exePath, exeSize, g_gameModuleBase);
        m_nativeHud.LogStep("INIT", isSteam ? "Platform: STEAM" : "Platform: GOG");
    }

    m_nativeHud.LogStep("INIT", "Kenshi Online v0.1.0 starting...");

    // Load config
    std::string configPath = ClientConfig::GetDefaultPath();
    m_config.Load(configPath);
    m_nativeHud.LogStep("INIT", "Config loaded");

    // Initialize game offsets (CE fallbacks)
    game::InitOffsetsFromScanner();
    m_nativeHud.LogStep("INIT", "Game offsets initialized");

    // Initialize pattern scanner
    m_nativeHud.LogStep("SCAN", "Pattern scanner starting...");
    if (!InitScanner()) {
        m_nativeHud.LogStep("ERR", "Pattern scanner FAILED");
    } else {
        m_nativeHud.LogStep("OK", "Pattern scanner resolved game functions");
    }

    // Initialize MinHook
    m_nativeHud.LogStep("HOOK", "MinHook initializing...");
    if (!HookManager::Get().Initialize()) {
        m_nativeHud.LogStep("ERR", "MinHook FAILED - cannot install hooks");
        return false;
    }
    m_nativeHud.LogStep("OK", "MinHook ready");

    // Loading guard starts FALSE — only set true when CharacterCreate burst
    // detects actual game loading (not at main menu).
    // m_isLoading = false; // already default
    m_nativeHud.LogStep("INIT", "Loading guard inactive (waiting for game load)");

    // Install hooks
    m_nativeHud.LogStep("HOOK", "Installing game hooks...");
    if (!InitHooks()) {
        m_nativeHud.LogStep("WARN", "Some hooks failed to install");
    } else {
        m_nativeHud.LogStep("OK", "All hooks installed");
    }

    // Initialize networking
    m_nativeHud.LogStep("NET", "Network (ENet) initializing...");
    if (!InitNetwork()) {
        m_nativeHud.LogStep("ERR", "Network init FAILED");
    } else {
        m_nativeHud.LogStep("OK", "Network ready (ENet)");
    }

    // Initialize packet handler
    InitPacketHandler();
    m_nativeHud.LogStep("NET", "Packet handler registered");

    // Set up SpawnManager callback
    m_spawnManager.SetOnSpawnedCallback(
        [this](EntityID netId, void* gameObject) {
            m_entityRegistry.SetGameObject(netId, gameObject);
            const auto* info = m_entityRegistry.GetInfo(netId);
            PlayerID owner = info ? info->ownerPlayerId : 0;
            m_playerController.OnRemoteCharacterSpawned(netId, gameObject, owner);
            game::CharacterAccessor accessor(gameObject);
            std::string charName = accessor.GetName();
            spdlog::info("Core: SpawnManager linked entity {} to game object 0x{:X} (name='{}')",
                         netId, reinterpret_cast<uintptr_t>(gameObject), charName);
            m_overlay.AddSystemMessage("Remote player spawned: " + charName);
        });
    m_nativeHud.LogStep("GAME", "Spawn manager callback set");

    if (!InitUI()) {
        m_nativeHud.LogStep("WARN", "UI init returned false (lazy init later)");
    }

    m_running = true;

    // Register slash commands
    CommandRegistry::Get().RegisterBuiltins();
    m_nativeHud.LogStep("CMD", "Slash commands registered");

    // Start background task orchestrator
    m_orchestrator.Start(2);
    m_nativeHud.LogStep("SYS", "Task orchestrator started (2 workers)");

    // Construct sync orchestrator (EntityResolver, ZoneEngine, PlayerEngine)
    m_syncOrchestrator = std::make_unique<SyncOrchestrator>(
        m_entityRegistry, m_playerController, m_interpolation,
        m_spawnManager, m_client, m_orchestrator);
    m_useSyncOrchestrator = m_config.useSyncOrchestrator;
    SyncFacilitator::Get().Bind(m_syncOrchestrator.get(), &m_entityRegistry,
                                 &m_interpolation, &m_spawnManager);
    AssetFacilitator::Get().Bind(&m_loadingOrch);
    m_pipelineOrch.Initialize(m_localPlayerId, m_entityRegistry, m_spawnManager,
                               m_loadingOrch, m_client, m_nativeHud);
    m_nativeHud.LogStep("SYS", m_useSyncOrchestrator
        ? "Sync orchestrator ACTIVE (new 7-stage pipeline)"
        : "Sync orchestrator STANDBY (legacy pipeline)");

    // Start network thread
    m_networkThread = std::thread(&Core::NetworkThreadFunc, this);
    m_nativeHud.LogStep("NET", "Network thread started");

    m_nativeHud.LogStep("OK", "=== Initialization complete ===");
    m_nativeHud.LogStep("INIT", "Waiting for game to load...");
    m_nativeHud.LogStep("INIT", "Press F1 for menu | Insert to toggle log");

    OutputDebugStringA("KMP: === Kenshi-Online Initialized Successfully ===\n");
    spdlog::info("=== Kenshi-Online Initialized Successfully ===");
    return true;
}

void Core::Shutdown() {
    spdlog::info("Kenshi-Online shutting down...");

    m_running = false;
    m_connected = false;

    // Shutdown pipeline debugger and facilitators
    m_pipelineOrch.Shutdown();
    AssetFacilitator::Get().Unbind();
    SyncFacilitator::Get().Unbind();
    if (m_syncOrchestrator) {
        m_syncOrchestrator->Shutdown();
    }

    // Stop task orchestrator before joining network thread
    m_orchestrator.Stop();

    if (m_networkThread.joinable()) {
        m_networkThread.join();
    }

    m_client.Disconnect();
    m_nativeHud.Shutdown();
    m_overlay.Shutdown();
    HookManager::Get().Shutdown();

    // Remove vectored exception handler
    if (g_vehHandle) {
        RemoveVectoredExceptionHandler(g_vehHandle);
        g_vehHandle = nullptr;
    }

    // Save config
    m_config.Save(ClientConfig::GetDefaultPath());

    spdlog::info("Kenshi-Online shutdown complete");
}

bool Core::InitScanner() {
    // ══════════════════════════════════════════════════════════════
    //  LEGACY SCANNER (kept for backward compatibility)
    // ══════════════════════════════════════════════════════════════
    if (!m_scanner.Init(nullptr)) {
        spdlog::error("Failed to init scanner for main executable");
        return false;
    }

    // ══════════════════════════════════════════════════════════════
    //  PATTERN ORCHESTRATOR — Enhanced multi-phase discovery
    // ══════════════════════════════════════════════════════════════
    //  7-phase pipeline:
    //    1. .pdata enumeration (every function in the exe)
    //    2. String discovery + cross-reference analysis
    //    3. VTable scanning + RTTI class hierarchy mapping
    //    4. SIMD batch pattern scan (all patterns in one pass)
    //    5. String xref fallback (for patterns that failed)
    //    6. Call graph analysis + label propagation
    //    7. Global pointer validation
    // ══════════════════════════════════════════════════════════════

    OrchestratorConfig orchConfig;
    orchConfig.enablePData     = true;
    orchConfig.enableStrings   = true;
    orchConfig.enableVTables   = true;
    orchConfig.enableCallGraph = true;
    orchConfig.enableBatchScan = true;
    orchConfig.enableLabelPropagation = true;
    orchConfig.stringMinLength = 4;
    orchConfig.callGraphDepth  = 2;
    orchConfig.fullCallGraph   = false; // Targeted graph for performance
    orchConfig.scanWideStrings = false;

    if (m_patternOrchestrator.Init(nullptr, orchConfig)) {
        // Register all built-in patterns (populates GameFunctions via target pointers)
        m_patternOrchestrator.RegisterBuiltinPatterns(m_gameFuncs);

        // Run the full 7-phase pipeline
        auto report = m_patternOrchestrator.Run();

        m_nativeHud.LogStep("SCAN",
            "Orchestrator: " + std::to_string(report.totalResolved) + "/" +
            std::to_string(report.totalEntries) + " resolved (" +
            std::to_string(report.pdataFunctions) + " .pdata funcs, " +
            std::to_string(report.vtablesFound) + " vtables, " +
            std::to_string(report.labeledFunctions) + " labeled)");
    }

    // Always run legacy scanner too — ensures PlayerBase discovery + proven-working path
    {
        bool resolved = ResolveGameFunctions(m_scanner, m_gameFuncs);
        if (!resolved) {
            spdlog::warn("Game functions minimally resolved: false - some features may not work");
        }
    }

    // ── Function Signature Analysis ──
    // Analyze prologues of all hooked functions to validate our signatures.
    {
        std::vector<FunctionSignature> sigs;

        struct HookSigCheck {
            const char* name;
            void* address;
            int hookParamCount; // params in our hook typedef
        };

        HookSigCheck checks[] = {
            {"CharacterSpawn",       m_gameFuncs.CharacterSpawn,       2}, // factory, templateData
            {"CharacterDestroy",     m_gameFuncs.CharacterDestroy,     1}, // character
            {"CharacterSetPosition", m_gameFuncs.CharacterSetPosition, 2}, // character, Vec3*
            {"CharacterMoveTo",      m_gameFuncs.CharacterMoveTo,      0}, // MID-FUNCTION — do not hook/call
            {"ApplyDamage",          m_gameFuncs.ApplyDamage,          6}, // target, attacker, bodyPart, cut, blunt, pierce
            {"CharacterDeath",       m_gameFuncs.CharacterDeath,       2}, // character, killer
            {"ZoneLoad",             m_gameFuncs.ZoneLoad,             3}, // zoneMgr, zoneX, zoneY
            {"ZoneUnload",           m_gameFuncs.ZoneUnload,           3}, // zoneMgr, zoneX, zoneY
            {"BuildingPlace",        m_gameFuncs.BuildingPlace,        5}, // world, building, x, y, z
            {"SaveGame",             m_gameFuncs.SaveGame,             2}, // saveManager, saveName
            {"LoadGame",             m_gameFuncs.LoadGame,             2}, // saveManager, saveName
        };

        // Map from check name to m_gameFuncs pointer so we can null mismatches
        std::unordered_map<std::string, void**> funcPtrs = {
            {"CharacterMoveTo",      &m_gameFuncs.CharacterMoveTo},
            {"BuildingPlace",        &m_gameFuncs.BuildingPlace},
            {"SaveGame",             &m_gameFuncs.SaveGame},
            {"LoadGame",             &m_gameFuncs.LoadGame},
        };

        for (auto& check : checks) {
            if (!check.address) continue;
            auto sig = FunctionAnalyzer::Analyze(
                reinterpret_cast<uintptr_t>(check.address), check.name);
            if (sig.IsValid()) {
                bool ok = FunctionAnalyzer::ValidateSignature(sig, check.hookParamCount);
                if (!ok) {
                    spdlog::warn("Core: SIGNATURE MISMATCH for '{}' — hook expects {} params, analysis suggests ~{} — DISABLING",
                                 check.name, check.hookParamCount, sig.estimatedParams);
                    // Null out the function pointer so it won't be hooked (prevents crashes)
                    auto it = funcPtrs.find(check.name);
                    if (it != funcPtrs.end() && it->second) {
                        *(it->second) = nullptr;
                        spdlog::warn("Core: Nulled '{}' to prevent crash from bad pattern match", check.name);
                    }
                }
                sigs.push_back(std::move(sig));
            }
        }

        FunctionAnalyzer::LogAnalysis(sigs);
    }

    // Bridge PlayerBase to the game_character module
    if (m_gameFuncs.PlayerBase != 0) {
        game::SetResolvedPlayerBase(m_gameFuncs.PlayerBase);
        spdlog::info("Core: PlayerBase bridged to game_character at 0x{:X}", m_gameFuncs.PlayerBase);
    }

    // Bridge CharacterSetPosition function to game_character module
    if (m_gameFuncs.CharacterSetPosition) {
        game::SetGameSetPositionFn(m_gameFuncs.CharacterSetPosition);
        spdlog::info("Core: SetPosition function bridged at 0x{:X}",
                     reinterpret_cast<uintptr_t>(m_gameFuncs.CharacterSetPosition));
    }

    return m_gameFuncs.IsMinimallyResolved();
}

// Forward declaration — defined below OnGameLoaded
static bool SEH_InstallEntityHooks();

bool Core::InitHooks() {
    bool allOk = true;

    // ══════════════════════════════════════════════════════════════
    // ImGui rendering REMOVED — Ogre3D/DX11 conflict.
    // Using Present hook for HWND/WndProc + OnGameTick only.
    // UI is native MyGUI menu.
    // ══════════════════════════════════════════════════════════════

    // D3D11 Present hook (WndProc input + OnGameTick fallback, NO ImGui rendering)
    m_nativeHud.LogStep("HOOK", "D3D11 Present hook...");
    if (!render_hooks::Install()) {
        m_nativeHud.LogStep("ERR", "Present hook FAILED");
        allOk = false;
    } else {
        m_nativeHud.LogStep("OK", "Present hook installed (WndProc + frame tick)");
    }

    // Input hooks: handled by WndProc in render_hooks now
    // input_hooks not needed

    // ═══════════════════════════════════════════════════════════════════
    // CharacterCreate hook installed IMMEDIATELY — it must be active during
    // game loading to capture the factory pointer and template database.
    // The hook has SEH protection and burst detection, making it safe during
    // loading. All OTHER gameplay hooks remain deferred to OnGameLoaded().
    // ═══════════════════════════════════════════════════════════════════
    if (m_gameFuncs.CharacterSpawn) {
        m_nativeHud.LogStep("HOOK", "Entity hooks (CharacterCreate)...");
        if (SEH_InstallEntityHooks()) {
            m_nativeHud.LogStep("OK", "CharacterCreate hook active (captures factory during loading)");
        } else {
            m_nativeHud.LogStep("ERR", "CharacterCreate hook FAILED");
            allOk = false;
        }
    } else {
        m_nativeHud.LogStep("ERR", "CharacterSpawn pattern NOT FOUND");
    }

    m_nativeHud.LogStep("HOOK", "Other gameplay hooks DEFERRED (after game loads)");

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

// ── SEH wrappers for OnGameLoaded sub-steps (no C++ objects allowed in __try) ──
static bool SEH_InstallEntityHooks() {
    __try {
        return entity_hooks::Install();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        OutputDebugStringA("KMP: SEH CRASH in entity_hooks::Install()\n");
        return false;
    }
}

static void SEH_PlayerControllerOnGameWorldLoaded(PlayerController& pc) {
    __try {
        pc.OnGameWorldLoaded();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        OutputDebugStringA("KMP: SEH CRASH in PlayerController::OnGameWorldLoaded()\n");
    }
}

static bool SEH_RetryGlobalDiscovery(PatternScanner& scanner, GameFunctions& funcs) {
    __try {
        return RetryGlobalDiscovery(scanner, funcs);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        OutputDebugStringA("KMP: SEH CRASH in RetryGlobalDiscovery()\n");
        return false;
    }
}

static void SEH_ScanGameDataHeap(SpawnManager& sm) {
    __try {
        sm.ScanGameDataHeap();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        OutputDebugStringA("KMP: SEH CRASH in ScanGameDataHeap()\n");
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  VTABLE-BASED RUNTIME DISCOVERY
// ═══════════════════════════════════════════════════════════════════════════
// When pattern scan + string fallback fail (common on Steam), discover
// critical function pointers from live game objects' vtables.
//
// CT research: activePlatoon vtable slot 2 (offset 0x10) = addMember(character*)
// Chain: character → GetSquadPtr() → platoon → +0x1D8 → activePlatoon → vtable+0x10
// Or:    character+0x658 → activePlatoon → vtable+0x10

static bool s_squadAddMemberDiscovered = false;

// SEH-protected pointer chain read — game objects may be freed or invalid
static uintptr_t SEH_ReadPtr(uintptr_t addr) {
    __try {
        return *reinterpret_cast<uintptr_t*>(addr);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

static uintptr_t SEH_ReadVTableSlot(uintptr_t objectPtr, int slotOffset) {
    __try {
        uintptr_t vtable = *reinterpret_cast<uintptr_t*>(objectPtr);
        if (vtable < 0x10000 || vtable >= 0x00007FFFFFFFFFFF) return 0;
        uintptr_t funcPtr = *reinterpret_cast<uintptr_t*>(vtable + slotOffset);
        return funcPtr;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

// Validate that a candidate function pointer is a real function entry in the module
static bool ValidateVTableCandidate(uintptr_t funcAddr, uintptr_t moduleBase, size_t moduleSize,
                                     const char* source) {
    if (funcAddr == 0) return false;

    // Must be within the module image
    if (funcAddr < moduleBase || funcAddr >= moduleBase + moduleSize) {
        spdlog::debug("VTableDiscovery: {} = 0x{:X} — outside module", source, funcAddr);
        return false;
    }

    // Must be a real function entry per .pdata
    DWORD64 imageBase = 0;
    auto* rtFunc = RtlLookupFunctionEntry(static_cast<DWORD64>(funcAddr), &imageBase, nullptr);
    if (rtFunc) {
        uintptr_t funcStart = static_cast<uintptr_t>(imageBase) + rtFunc->BeginAddress;
        if (funcStart != funcAddr) {
            spdlog::debug("VTableDiscovery: {} = 0x{:X} is +0x{:X} into 0x{:X} — not entry",
                          source, funcAddr, funcAddr - funcStart, funcStart);
            return false;
        }
    }

    // Function size should be reasonable for an addMember (>= 32 bytes)
    if (rtFunc) {
        uint32_t funcSize = rtFunc->EndAddress - rtFunc->BeginAddress;
        if (funcSize < 32) {
            spdlog::debug("VTableDiscovery: {} = 0x{:X} — too small ({} bytes)", source, funcAddr, funcSize);
            return false;
        }
    }

    return true;
}

// Discover SquadAddMember from live game objects' vtables.
// Tries multiple approaches to find the activePlatoon::addMember function.
static bool TryDiscoverSquadAddMemberFromVTable(GameFunctions& funcs,
                                                 uintptr_t moduleBase,
                                                 size_t moduleSize) {
    if (funcs.SquadAddMember != nullptr) return true; // Already have it
    if (s_squadAddMemberDiscovered) return false; // Already tried all approaches

    auto& core = Core::Get();
    void* primaryChar = core.GetPlayerController().GetPrimaryCharacter();
    if (!primaryChar) return false;

    uintptr_t charAddr = reinterpret_cast<uintptr_t>(primaryChar);
    game::CharacterAccessor accessor(primaryChar);

    // ── Approach 1: character+0x658 → activePlatoon → vtable+0x10 ──
    // Research data: CharacterHuman+0x658 = activePlatoon pointer
    {
        uintptr_t activePlatoon = SEH_ReadPtr(charAddr + 0x658);
        if (activePlatoon > 0x10000 && activePlatoon < 0x00007FFFFFFFFFFF) {
            uintptr_t funcAddr = SEH_ReadVTableSlot(activePlatoon, 0x10);
            if (ValidateVTableCandidate(funcAddr, moduleBase, moduleSize, "char+0x658→AP→vt+0x10")) {
                funcs.SquadAddMember = reinterpret_cast<void*>(funcAddr);
                s_squadAddMemberDiscovered = true;
                spdlog::info("VTableDiscovery: SquadAddMember FOUND! char+0x658 → activePlatoon 0x{:X} → vtable+0x10 = 0x{:X} (RVA 0x{:X})",
                              activePlatoon, funcAddr, funcAddr - moduleBase);
                goto install_hook;
            }
        }
    }

    // ── Approach 2: GetSquadPtr() → platoon → +0x1D8 → activePlatoon → vtable+0x10 ──
    // Research: platoon+0x1D8 = activePlatoon*
    {
        uintptr_t squadPtr = accessor.GetSquadPtr();
        if (squadPtr != 0) {
            uintptr_t activePlatoon = SEH_ReadPtr(squadPtr + 0x1D8);
            if (activePlatoon > 0x10000 && activePlatoon < 0x00007FFFFFFFFFFF) {
                uintptr_t funcAddr = SEH_ReadVTableSlot(activePlatoon, 0x10);
                if (ValidateVTableCandidate(funcAddr, moduleBase, moduleSize, "squad+0x1D8→AP→vt+0x10")) {
                    funcs.SquadAddMember = reinterpret_cast<void*>(funcAddr);
                    s_squadAddMemberDiscovered = true;
                    spdlog::info("VTableDiscovery: SquadAddMember FOUND! platoon 0x{:X} → +0x1D8 → activePlatoon 0x{:X} → vtable+0x10 = 0x{:X} (RVA 0x{:X})",
                                  squadPtr, activePlatoon, funcAddr, funcAddr - moduleBase);
                    goto install_hook;
                }
            }

            // ── Approach 3: GetSquadPtr() → vtable+0x10 directly ──
            // In case GetSquadPtr returns an activePlatoon (not platoon)
            {
                uintptr_t funcAddr = SEH_ReadVTableSlot(squadPtr, 0x10);
                if (ValidateVTableCandidate(funcAddr, moduleBase, moduleSize, "squad→vt+0x10 direct")) {
                    funcs.SquadAddMember = reinterpret_cast<void*>(funcAddr);
                    s_squadAddMemberDiscovered = true;
                    spdlog::info("VTableDiscovery: SquadAddMember FOUND! squad 0x{:X} → vtable+0x10 = 0x{:X} (RVA 0x{:X})",
                                  squadPtr, funcAddr, funcAddr - moduleBase);
                    goto install_hook;
                }
            }
        }
    }

    // No approach worked yet — will retry on next tick
    return false;

install_hook:
    // Install the squad hook with the discovered function
    if (squad_hooks::Install()) {
        spdlog::info("VTableDiscovery: Squad hooks installed successfully");
        core.GetNativeHud().LogStep("OK", "SquadAddMember discovered via vtable + hook installed");
    } else {
        // Hook install failed (maybe vtable func can't be MinHooked),
        // but the raw function pointer is still usable by AddCharacterToLocalSquad
        spdlog::warn("VTableDiscovery: Squad hook install failed, but raw function pointer available");
        core.GetNativeHud().LogStep("WARN", "SquadAddMember found (vtable) — raw ptr OK, hook failed");
    }

    return true;
}

void Core::OnGameLoaded() {
    if (m_gameLoaded.exchange(true)) return; // Only run once

    m_nativeHud.LogStep("GAME", "=== Game world loaded! ===");
    OutputDebugStringA("KMP: === Core::OnGameLoaded() START ===\n");

    // ── Clear loading guard ──
    m_isLoading = false;
    building_hooks::SetLoading(false);
    inventory_hooks::SetLoading(false);
    squad_hooks::SetLoading(false);
    faction_hooks::SetLoading(false);
    m_loadingOrch.OnGameLoaded();
    m_nativeHud.LogStep("GAME", "Loading guard cleared (hooks active)");

    // ═══ Install remaining gameplay hooks now that loading is complete ═══
    // (CharacterCreate was already installed in InitHooks to capture the loading burst)
    //
    // BISECT TEST: Enable hooks in groups to find which one causes the #314 crash.
    //   0 = skip all deferred hooks (PASSES — game loads to #410+)
    //   1 = Time + GameFrameUpdate only
    //   2 = + Movement only
    //   3 = + Squad only
    //   4 = + AI only
    //   5 = + Combat only
    //   6 = + Inventory only
    //   7 = + Faction + Resource (everything EXCEPT Building — disabled, see below)
    static constexpr int HOOK_PHASE = 7;
    {
        char buf[128];
        sprintf_s(buf, "KMP: Hook bisect phase %d\n", HOOK_PHASE);
        OutputDebugStringA(buf);
        spdlog::info("Core::OnGameLoaded — Hook bisect PHASE {}", HOOK_PHASE);
        m_nativeHud.LogStep("HOOK", ("Bisect phase " + std::to_string(HOOK_PHASE)).c_str());
    }
    if (HOOK_PHASE >= 1) {
    m_nativeHud.LogStep("HOOK", "Installing deferred gameplay hooks...");

    // ═══ MULTIPLAYER HOOKS ═══

    // Time hooks — ESSENTIAL (drives OnGameTick from the game's own time system)
    if (m_gameFuncs.TimeUpdate) {
        m_nativeHud.LogStep("HOOK", "Time hooks...");
        if (time_hooks::Install()) {
            m_nativeHud.LogStep("OK", "Time hooks installed");
        } else {
            m_nativeHud.LogStep("WARN", "Time hooks partial failure");
        }
    }

    // GameFrameUpdate — uses MovRaxRsp fix (auto-applied by HookManager)
    if (m_gameFuncs.GameFrameUpdate) {
        if (game_tick_hooks::Install()) {
            m_nativeHud.LogStep("OK", "GameFrameUpdate hook installed");
        } else {
            m_nativeHud.LogStep("WARN", "GameFrameUpdate hook FAILED");
        }
    }

    } // end phase 1
    if (HOOK_PHASE >= 2) {
    // Movement hooks — install if CharacterMoveTo resolved (it's null on Steam due to
    // mid-function pattern match). Movement sync works without it via position polling +
    // ai_hooks blocking, but the hook provides better AI movement blocking for remotes
    // and C2S_MoveCommand for precision movement sync.
    if (m_gameFuncs.CharacterMoveTo) {
        if (movement_hooks::Install()) {
            m_nativeHud.LogStep("OK", "Movement hooks installed");
        } else {
            m_nativeHud.LogStep("WARN", "Movement hooks FAILED");
        }
    } else {
        spdlog::info("Core::OnGameLoaded — Movement hooks SKIPPED (CharacterMoveTo not resolved)");
        m_nativeHud.LogStep("INFO", "Movement hooks skipped (position polling active)");
    }

    } // end phase 2 (Movement only)
    if (HOOK_PHASE >= 3) {
    // Squad hooks (SquadAddMember only — SquadCreate is skipped due to mov rax rsp)
    // If pattern scan failed, vtable discovery (above) may have already found it.
    // If not, OnGameTick will keep trying vtable discovery as a deferred fallback.
    if (m_gameFuncs.SquadAddMember) {
        if (squad_hooks::Install()) {
            m_nativeHud.LogStep("OK", "Squad hooks installed");
        } else {
            m_nativeHud.LogStep("WARN", "Squad hooks FAILED");
        }
    } else {
        m_nativeHud.LogStep("INFO", "SquadAddMember: deferred to vtable discovery in OnGameTick");
    }

    } // end phase 3 (Squad)
    if (HOOK_PHASE >= 4) {
    // AI hooks (AICreate + AIPackages — suppress decisions for remote characters)
    if (m_gameFuncs.AICreate) {
        if (ai_hooks::Install()) {
            m_nativeHud.LogStep("OK", "AI hooks installed");
        } else {
            m_nativeHud.LogStep("WARN", "AI hooks FAILED");
        }
    }

    } // end phase 4 (AI only)
    if (HOOK_PHASE >= 5) {
    // Combat hooks (ApplyDamage, CharacterDeath, CharacterKO)
    if (m_gameFuncs.ApplyDamage) {
        if (combat_hooks::Install()) {
            m_nativeHud.LogStep("OK", "Combat hooks installed");
        } else {
            m_nativeHud.LogStep("WARN", "Combat hooks FAILED");
        }
    }

    } // end phase 5 (Combat only)
    if (HOOK_PHASE >= 6) {
    // Inventory hooks (ItemPickup, ItemDrop, BuyItem)
    if (m_gameFuncs.ItemPickup) {
        if (inventory_hooks::Install()) {
            m_nativeHud.LogStep("OK", "Inventory hooks installed");
        } else {
            m_nativeHud.LogStep("WARN", "Inventory hooks FAILED");
        }
    }

    } // end phase 6 (Inventory only)

    // ═══ BUILDING HOOKS DISABLED ═══
    // All 5 building functions have `mov rax, rsp` prologues requiring the MovRaxRsp
    // naked detour fix. Installing them causes a deterministic crash during zone loading.
    // Root cause TBD — possibly the sheer volume of MovRaxRsp hooks (9 total) or
    // one of the building function addresses is wrong despite passing .pdata + alignment.
    // Building sync is non-critical for core multiplayer connectivity.
    spdlog::info("Core::OnGameLoaded — Building hooks SKIPPED (crash during zone loading)");
    m_nativeHud.LogStep("INFO", "Building hooks SKIPPED (crash investigation pending)");

    if (HOOK_PHASE >= 7) {
    // Faction hooks (FactionRelation)
    if (m_gameFuncs.FactionRelation) {
        if (faction_hooks::Install()) {
            m_nativeHud.LogStep("OK", "Faction hooks installed");
        } else {
            m_nativeHud.LogStep("WARN", "Faction hooks FAILED");
        }
    }

    // Resource hooks — attempt Ogre VTable discovery for asset loading control.
    // Non-critical: LoadingOrchestrator falls back to burst-detection timing if this fails.
    m_nativeHud.LogStep("HOOK", "Resource hooks (Ogre VTable discovery)...");
    if (resource_hooks::Install()) {
        m_nativeHud.LogStep("OK", "Resource hooks installed (Ogre monitoring active)");
    } else {
        m_nativeHud.LogStep("INFO", "Resource hooks: Ogre discovery deferred (burst-detection fallback)");
    }
    } // end phase 7 (Faction + Resource)

    // Run deferred global discovery — both PlayerBase and GameWorld.
    // During initial scan (before game loads), these globals are typically 0
    // because Kenshi hasn't initialized them yet. Now that the game is loaded,
    // we can find and validate them.
    m_nativeHud.LogStep("SCAN", "PlayerBase/GameWorld discovery...");

    // Validate that a global pointer contains a HEAP-allocated object (not a module-internal pointer).
    // Previous bug: .data addresses containing .text pointers passed validation,
    // causing CharacterIterator to read garbage and find 0 characters.
    uintptr_t moduleBase = m_scanner.GetBase();
    size_t moduleSize = m_scanner.GetSize();
    auto validateGlobal = [moduleBase, moduleSize](uintptr_t addr) -> bool {
        if (addr == 0) return false;
        uintptr_t val = 0;
        if (!Memory::Read(addr, val)) return false;
        if (val < 0x10000 || val >= 0x00007FFFFFFFFFFF) return false;
        // MUST be outside module image — real game objects are heap-allocated
        if (val >= moduleBase && val < moduleBase + moduleSize) return false;
        return true;
    };

    bool needsRetry = !validateGlobal(m_gameFuncs.PlayerBase) ||
                      !validateGlobal(m_gameFuncs.GameWorldSingleton);

    if (needsRetry) {
        m_nativeHud.LogStep("SCAN", "Retrying global discovery (game now loaded)...");
        SEH_RetryGlobalDiscovery(m_scanner, m_gameFuncs);

        if (m_gameFuncs.PlayerBase != 0) {
            game::SetResolvedPlayerBase(m_gameFuncs.PlayerBase);
            m_nativeHud.LogStep("OK", "PlayerBase discovered");
        } else {
            m_nativeHud.LogStep("WARN", "PlayerBase NOT found — faction capture will use entity_hooks bootstrap");
        }
        if (m_gameFuncs.GameWorldSingleton != 0) {
            m_nativeHud.LogStep("OK", "GameWorld discovered");
        } else {
            m_nativeHud.LogStep("WARN", "GameWorld NOT found — CharacterIterator may fail");
        }
    } else {
        m_nativeHud.LogStep("OK", "PlayerBase + GameWorld already valid");
    }

    // Always set GameWorld bridge (CharacterIterator uses it as fallback when PlayerBase fails)
    if (m_gameFuncs.GameWorldSingleton != 0) {
        game::SetResolvedGameWorld(m_gameFuncs.GameWorldSingleton);
        m_nativeHud.LogStep("OK", "GameWorld bridge set (fallback for CharacterIterator)");
    }

    // Now notify player controller (can use CharacterIterator if PlayerBase is resolved)
    m_nativeHud.LogStep("GAME", "Player controller: OnGameWorldLoaded...");
    SEH_PlayerControllerOnGameWorldLoaded(m_playerController);
    m_nativeHud.LogStep("OK", "Player controller ready");

    // ═══ Deferred vtable discovery for SquadAddMember ═══
    // If pattern scan + string fallback failed, try to discover from a live squad vtable.
    // CT data: "Squad vtable+0x10: adds character to squad"
    if (!m_gameFuncs.SquadAddMember) {
        m_nativeHud.LogStep("SCAN", "SquadAddMember: vtable discovery...");
        if (TryDiscoverSquadAddMemberFromVTable(m_gameFuncs, moduleBase, moduleSize)) {
            m_nativeHud.LogStep("OK", "SquadAddMember found via vtable!");
        } else {
            m_nativeHud.LogStep("INFO", "SquadAddMember: vtable discovery deferred (no squad yet)");
        }
    }

    // ═══ Verify spawn system readiness ═══
    m_nativeHud.LogStep("GAME", "Verifying spawn system...");
    bool spawnReady = m_spawnManager.VerifyReadiness();
    if (spawnReady) {
        m_nativeHud.LogStep("OK", "Spawn system ready");
    } else {
        m_nativeHud.LogStep("WARN", "Spawn system NOT ready — remote characters may fail");
    }

    // Early heap scan — try even without factory, as long as we have ANY data to bootstrap from
    if (m_spawnManager.GetManagerPointer() != 0 || m_spawnManager.GetTemplateCount() > 0) {
        m_nativeHud.LogStep("GAME", "Early heap scan for templates...");
        SEH_ScanGameDataHeap(m_spawnManager);
        m_nativeHud.LogStep("OK", "Heap scan done (" + std::to_string(m_spawnManager.GetTemplateCount()) + " templates)");
    } else if (m_spawnManager.IsReady()) {
        // Factory captured but no manager pointer yet — still try scan
        m_nativeHud.LogStep("GAME", "Heap scan (factory only, no manager ptr)...");
        SEH_ScanGameDataHeap(m_spawnManager);
        m_nativeHud.LogStep("OK", "Heap scan done (" + std::to_string(m_spawnManager.GetTemplateCount()) + " templates)");
    }

    // ═══ DISABLE CharacterCreate hook after loading ═══
    // The MovRaxRsp naked detour causes a silent crash ~3-5s after loading completes.
    // The hook is only needed during loading (factory capture + template collection).
    // Disable it now; re-enable when a multiplayer connection is established
    // (needed to register new characters created while connected).
    spdlog::info("Core::OnGameLoaded — About to disable CharacterCreate (m_connected={})", m_connected.load());
    if (!m_connected) {
        if (HookManager::Get().Disable("CharacterCreate")) {
            spdlog::info("Core::OnGameLoaded — CharacterCreate hook DISABLED (MovRaxRsp crash prevention)");
            m_nativeHud.LogStep("HOOK", "CharacterCreate disabled (re-enabled on connect)");
        } else {
            spdlog::error("Core::OnGameLoaded — CharacterCreate Disable() FAILED!");
            m_nativeHud.LogStep("ERR", "CharacterCreate disable FAILED");
        }
    } else {
        spdlog::warn("Core::OnGameLoaded — Skipping CharacterCreate disable (already connected)");
    }

    m_nativeHud.LogStep("GAME", "Ready! Press F1 for multiplayer menu");
    OutputDebugStringA("KMP: === Core::OnGameLoaded() COMPLETE ===\n");
}

void Core::NetworkThreadFunc() {
    spdlog::info("Network thread started");

    while (m_running) {
        // Always pump ENet events — handles async connect, receive, and disconnect
        m_client.Update();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    spdlog::info("Network thread stopped");
}

void Core::SendExistingEntitiesToServer() {
    // After connecting, scan existing characters and register+send them.
    // IMPORTANT: Only send characters from the player's faction (squad members).
    // Sending ALL characters would flood the server with hundreds of NPCs.
    //
    // TWO character sources are tried:
    //   1. CharacterIterator (PlayerBase/GameWorld — fast but may fail on Steam)
    //   2. entity_hooks loading cache (populated during game loading — always available)

    int count = 0;
    int skippedFaction = 0;

    // ── Resolve faction ──
    // Priority: PlayerController → entity_hooks captured faction
    uintptr_t playerFaction = m_playerController.GetLocalFactionPtr();
    if (playerFaction == 0) {
        playerFaction = entity_hooks::GetCapturedFaction();
        if (playerFaction != 0) {
            spdlog::info("Core: Using entity_hooks captured faction 0x{:X} (PlayerController had none)",
                         playerFaction);
            m_playerController.SetLocalFactionPtr(playerFaction);
        }
    }

    // ── Strategy 1: CharacterIterator ──
    game::CharacterIterator iter;
    bool iteratorWorked = (iter.Count() > 0);

    if (iteratorWorked && playerFaction != 0) {
        spdlog::info("Core: Scanning {} characters via CharacterIterator (faction=0x{:X})...",
                     iter.Count(), playerFaction);

        while (iter.HasNext()) {
            game::CharacterAccessor character = iter.Next();
            if (!character.IsValid()) continue;

            Vec3 pos = character.GetPosition();
            if (pos.x == 0.f && pos.y == 0.f && pos.z == 0.f) continue;

            uintptr_t charFaction = character.GetFactionPtr();
            if (charFaction != playerFaction) {
                skippedFaction++;
                continue;
            }

            void* gameObj = reinterpret_cast<void*>(character.GetPtr());
            if (m_entityRegistry.GetNetId(gameObj) != INVALID_ENTITY) continue;

            EntityID netId = m_entityRegistry.Register(gameObj, EntityType::NPC, m_localPlayerId);
            m_entityRegistry.UpdatePosition(netId, pos);

            Quat rot = character.GetRotation();
            m_entityRegistry.UpdateRotation(netId, rot);

            uintptr_t factionPtr = character.GetFactionPtr();
            uint32_t factionId = 0;
            if (factionPtr != 0) Memory::Read(factionPtr + 0x08, factionId);

            std::string charName = character.GetName();

            PacketWriter writer;
            writer.WriteHeader(MessageType::C2S_EntitySpawnReq);
            writer.WriteU32(netId);
            writer.WriteU8(static_cast<uint8_t>(EntityType::NPC));
            writer.WriteU32(m_localPlayerId);
            writer.WriteU32(0);
            writer.WriteF32(pos.x);
            writer.WriteF32(pos.y);
            writer.WriteF32(pos.z);
            writer.WriteU32(rot.Compress());
            writer.WriteU32(factionId);
            uint16_t nameLen = static_cast<uint16_t>(std::min<size_t>(charName.size(), 255));
            writer.WriteU16(nameLen);
            if (nameLen > 0) writer.WriteRaw(charName.data(), nameLen);

            m_client.SendReliable(writer.Data(), writer.Size());
            count++;
        }
    }

    // ── Strategy 2: entity_hooks loading cache fallback ──
    // If CharacterIterator found nothing OR faction was unknown, try the loading cache.
    // Characters were cached during Hook_CharacterCreate before connection.
    if (count == 0) {
        const auto& cached = entity_hooks::GetLoadingCharacters();

        if (!cached.empty()) {
            // If faction STILL unknown, take it from the most common faction in the cache
            if (playerFaction == 0 && !cached.empty()) {
                // Use first character's faction as player faction heuristic
                for (const auto& cc : cached) {
                    if (cc.factionPtr != 0) {
                        playerFaction = cc.factionPtr;
                        m_playerController.SetLocalFactionPtr(playerFaction);
                        spdlog::info("Core: FALLBACK — captured faction 0x{:X} from loading cache", playerFaction);
                        break;
                    }
                }
            }

            spdlog::info("Core: CharacterIterator {} — using loading cache ({} chars, faction=0x{:X})",
                         iteratorWorked ? "found no squad members" : "FAILED (0 chars)",
                         cached.size(), playerFaction);

            for (const auto& cc : cached) {
                if (cc.x == 0.f && cc.y == 0.f && cc.z == 0.f) continue;

                // Apply faction filter (with cache fallback: if NO faction, send first 4 chars)
                if (playerFaction != 0 && cc.factionPtr != playerFaction) {
                    skippedFaction++;
                    continue;
                }
                // If still no faction at all, cap at 4 characters to avoid NPC flood
                if (playerFaction == 0 && count >= 4) break;

                if (m_entityRegistry.GetNetId(cc.gameObj) != INVALID_ENTITY) continue;

                Vec3 pos(cc.x, cc.y, cc.z);
                EntityID netId = m_entityRegistry.Register(cc.gameObj, EntityType::NPC, m_localPlayerId);
                m_entityRegistry.UpdatePosition(netId, pos);

                // Read rotation from game object (may be stale but good enough)
                game::CharacterAccessor accessor(cc.gameObj);
                Quat rot = accessor.GetRotation();
                m_entityRegistry.UpdateRotation(netId, rot);

                std::string charName = accessor.GetName();

                PacketWriter writer;
                writer.WriteHeader(MessageType::C2S_EntitySpawnReq);
                writer.WriteU32(netId);
                writer.WriteU8(static_cast<uint8_t>(EntityType::NPC));
                writer.WriteU32(m_localPlayerId);
                writer.WriteU32(0);
                writer.WriteF32(pos.x);
                writer.WriteF32(pos.y);
                writer.WriteF32(pos.z);
                writer.WriteU32(rot.Compress());
                writer.WriteU32(cc.factionId);
                uint16_t nameLen = static_cast<uint16_t>(std::min<size_t>(charName.size(), 255));
                writer.WriteU16(nameLen);
                if (nameLen > 0) writer.WriteRaw(charName.data(), nameLen);

                m_client.SendReliable(writer.Data(), writer.Size());
                count++;
            }
        } else {
            spdlog::warn("Core: Both CharacterIterator AND loading cache empty — no characters to send");
        }
    }

    spdlog::info("Core: Sent {} squad characters to server (skipped {} non-squad NPCs, "
                 "iterator={}, cache={})",
                 count, skippedFaction,
                 iteratorWorked ? "ok" : "FAILED",
                 entity_hooks::GetLoadingCharacters().size());

    if (count > 0) {
        m_overlay.AddSystemMessage("Syncing " + std::to_string(count) + " squad characters...");
    }
}

void Core::OnGameTick(float deltaTime) {
    // ── Pre-check diagnostics ──
    static int s_preCheckCount = 0;
    s_preCheckCount++;
    if (s_preCheckCount <= 5 || s_preCheckCount % 3000 == 0) {
        char buf[128];
        sprintf_s(buf, "KMP: OnGameTick PRE-CHECK #%d connected=%d\n",
                  s_preCheckCount, m_connected.load() ? 1 : 0);
        OutputDebugStringA(buf);
    }

    if (!m_connected) return;

    // ── Per-frame dedup guard ──
    // OnGameTick is driven from BOTH time_hooks (TimeUpdate) and render_hooks (Present)
    // as a redundancy measure. But the pipeline (buffer swap, background work kick)
    // is NOT idempotent — double-swapping reverses the first swap. Use a minimum
    // interval to ensure only one full tick processes per frame (~6ms = 166fps max).
    {
        static auto s_lastTickTime = std::chrono::steady_clock::time_point{};
        auto now = std::chrono::steady_clock::now();
        auto sinceLast = std::chrono::duration_cast<std::chrono::microseconds>(now - s_lastTickTime);
        if (sinceLast.count() < 4000) return; // Skip if called within 4ms (same frame)
        s_lastTickTime = now;
    }

    // ── Game-loaded gate ──
    // Don't run game-world pipeline steps until the game has finished loading.
    // entity_hooks triggers OnGameLoaded once the factory is captured + enough creates.
    // Fallback: detect via PlayerBase becoming valid (for connect-before-load path
    // where entity_hooks hasn't captured the factory yet).
    // Without this gate, HandleSpawnQueue can call SpawnCharacterDirect during
    // loading — the factory creates a character before textures are ready → crash.
    if (!m_gameLoaded) {
        if (m_gameFuncs.PlayerBase != 0) {
            uintptr_t playerPtr = 0;
            uintptr_t modBase = m_scanner.GetBase();
            size_t modSize = m_scanner.GetSize();
            if (Memory::Read(m_gameFuncs.PlayerBase, playerPtr) && playerPtr != 0 &&
                playerPtr > 0x10000 && playerPtr < 0x00007FFFFFFFFFFF &&
                !(playerPtr >= modBase && playerPtr < modBase + modSize)) {
                spdlog::info("Core: Game loaded detected via PlayerBase fallback (0x{:X})", playerPtr);
                OnGameLoaded();
            }
        }
        return;
    }

    // ── Step tracking: member variable so SEH crash handler can report which step crashed ──
    static int s_tickCallCount = 0;
    s_tickCallCount++;
    g_tickNumber = s_tickCallCount;
    g_lastTickStep = 0;
    g_lastStepName = "tick_entry";

    // Log first few successful entries to confirm OnGameTick is running
    if (s_tickCallCount <= 5 || s_tickCallCount % 200 == 0) {
        char buf[128];
        sprintf_s(buf, "KMP: OnGameTick ENTERED #%d (dt=%.4f)\n", s_tickCallCount, deltaTime);
        OutputDebugStringA(buf);
        spdlog::info("Core::OnGameTick ENTERED (call #{}, dt={:.4f}, lastStep={})",
                     s_tickCallCount, deltaTime, m_lastCompletedStep.load());
    }
    SetLastCompletedStep(0);

    // ── Step 1: Entity scan with retry ──
    // SendExistingEntitiesToServer may fail on first attempt if CharacterIterator
    // can't resolve PlayerBase/GameWorld. Retry every 150 ticks (~1 second) for
    // up to 30 seconds until at least one character is sent.
    if (!m_initialEntityScanDone) {
        // Reset retry counter when starting fresh (after disconnect/reconnect).
        // Without this, the static counter persists across connections and may
        // exhaust retries prematurely on the second connection.
        static int s_entityScanRetries = 0;
        static bool s_wasScanning = false;
        if (!s_wasScanning) {
            s_entityScanRetries = 0;
            s_wasScanning = true;
        }
        static constexpr int MAX_ENTITY_SCAN_RETRIES = 45; // ~30 seconds at 150fps
        static constexpr int RETRY_INTERVAL_TICKS = 150;

        bool shouldAttempt = (s_entityScanRetries == 0) ||
                             (s_tickCallCount % RETRY_INTERVAL_TICKS == 0);

        if (shouldAttempt) {
            if (s_entityScanRetries == 0) {
                spdlog::info("Core::OnGameTick: Step 1 — SendExistingEntitiesToServer (first attempt)");
                m_nativeHud.LogStep("GAME", "Scanning local squad characters...");
            }

            SendExistingEntitiesToServer();
            auto localCount = m_entityRegistry.GetPlayerEntities(m_localPlayerId).size();
            s_entityScanRetries++;

            if (localCount > 0) {
                m_initialEntityScanDone = true;
                s_wasScanning = false; // Allow fresh retry on next connection
                m_nativeHud.LogStep("OK", "Sent " + std::to_string(localCount) + " characters to server");
                spdlog::info("Core: Entity scan succeeded on attempt {} — {} characters", s_entityScanRetries, localCount);
            } else if (s_entityScanRetries >= MAX_ENTITY_SCAN_RETRIES) {
                m_initialEntityScanDone = true;
                s_wasScanning = false; // Allow fresh retry on next connection
                m_nativeHud.LogStep("WARN", "Entity scan: no squad characters found after " +
                                    std::to_string(s_entityScanRetries) + " attempts");
                spdlog::warn("Core: Entity scan exhausted {} retries — 0 characters found", s_entityScanRetries);
            } else if (s_entityScanRetries <= 3 || s_entityScanRetries % 10 == 0) {
                spdlog::info("Core: Entity scan attempt {} found 0 chars — will retry", s_entityScanRetries);
            }
        }
    }

    // ── Step 1b: Deferred re-scan after faction bootstrap ──
    // entity_hooks bootstraps the faction from the first character it sees.
    // After that, we re-scan to register the player's existing squad members
    // (which loaded before hooks were installed).
    if (m_needsEntityRescan.exchange(false)) {
        spdlog::info("Core::OnGameTick: Faction bootstrap triggered — re-scanning existing characters");
        m_nativeHud.LogStep("GAME", "Faction bootstrapped — re-scanning squad...");
        SendExistingEntitiesToServer();
        auto localCount = m_entityRegistry.GetPlayerEntities(m_localPlayerId).size();
        m_nativeHud.LogStep("OK", "Re-scan found " + std::to_string(localCount) + " squad characters");
        if (localCount > 0) {
            m_initialEntityScanDone = true; // Stop retrying
        }
    }
    // ── Step 1c: Deferred vtable discovery for SquadAddMember ──
    // If pattern scan failed and OnGameLoaded didn't find a squad yet,
    // keep trying each tick until we have a squad to read the vtable from.
    if (!m_gameFuncs.SquadAddMember && !s_squadAddMemberDiscovered) {
        static int s_vtableRetries = 0;
        if (s_vtableRetries < 30 && s_tickCallCount % 100 == 0) { // Try every ~0.7s for ~21s
            s_vtableRetries++;
            TryDiscoverSquadAddMemberFromVTable(m_gameFuncs, m_scanner.GetBase(), m_scanner.GetSize());
        }
    }

    g_lastTickStep = 1; g_lastStepName = "entity_scan";
    SetLastCompletedStep(1);

    // ── Steps 2-9: Sync pipeline ──
    if (m_useSyncOrchestrator && m_syncOrchestrator) {
        g_lastTickStep = 2; g_lastStepName = "interpolation";
        m_interpolation.Update(deltaTime);
        SetLastCompletedStep(4);

        g_lastTickStep = 3; g_lastStepName = "sync_orch_tick";
        m_syncOrchestrator->Tick(deltaTime);
        SetLastCompletedStep(6);

        g_lastTickStep = 4; g_lastStepName = "loading_orch";
        m_loadingOrch.Tick();

        g_lastTickStep = 5; g_lastStepName = "host_teleport";
        HandleHostTeleport();
        SetLastCompletedStep(8);
        SetLastCompletedStep(9);
    } else {
        g_lastTickStep = 2; g_lastStepName = "wait_bg_work";
        if (m_pipelineStarted) {
            m_orchestrator.WaitForFrameWork();
        }
        SetLastCompletedStep(2);

        g_lastTickStep = 3; g_lastStepName = "swap_buffers";
        if (m_pipelineStarted) {
            std::swap(m_readBuffer, m_writeBuffer);
        }
        SetLastCompletedStep(3);

        g_lastTickStep = 4; g_lastStepName = "interpolation";
        m_interpolation.Update(deltaTime);
        SetLastCompletedStep(4);

        g_lastTickStep = 5; g_lastStepName = "apply_remote_pos";
        if (m_pipelineStarted) {
            ApplyRemotePositions();
        }
        SetLastCompletedStep(5);

        g_lastTickStep = 6; g_lastStepName = "poll_local_pos";
        PollLocalPositions();

        g_lastTickStep = 7; g_lastStepName = "send_packets";
        if (m_pipelineStarted) {
            SendCachedPackets();
        }
        SetLastCompletedStep(6);

        g_lastTickStep = 8; g_lastStepName = "loading_orch";
        m_loadingOrch.Tick();

        g_lastTickStep = 9; g_lastStepName = "handle_spawns";
        HandleSpawnQueue();
        SetLastCompletedStep(7);

        g_lastTickStep = 10; g_lastStepName = "host_teleport";
        HandleHostTeleport();
        SetLastCompletedStep(8);

        g_lastTickStep = 11; g_lastStepName = "kick_bg_work";
        m_frameData[m_writeBuffer].Clear();
        KickBackgroundWork();
        m_pipelineStarted = true;
        SetLastCompletedStep(9);
    }

    // ── Step 9b: Deferred probes — DISABLED ──
    // AnimClass probing was flooding the log with 5 "Method 2 unavailable" per tick
    // and never succeeding. PlayerControlled probing also never succeeds (CharacterIterator
    // always fails). Both are non-essential optimizations. Disabled to eliminate them
    // as potential crash contributors.
    g_lastTickStep = 12; g_lastStepName = "probes_skipped";

    // ── Step 10: Diagnostics ──
    g_lastTickStep = 13; g_lastStepName = "diagnostics";
    UpdateDiagnostics(deltaTime);
    SetLastCompletedStep(10);

    // ── Step 11: Pipeline debugger ──
    g_lastTickStep = 14; g_lastStepName = "pipeline_orch";
    m_pipelineOrch.Tick(deltaTime);
    SetLastCompletedStep(11);

    g_lastTickStep = 15; g_lastStepName = "tick_complete";
}

// ════════════════════════════════════════════════════════════════════════════
// Staged Pipeline Methods
// ════════════════════════════════════════════════════════════════════════════

// SEH-protected per-entity position/rotation write.
// WritePosition follows chains of game memory pointers (physics engine, Havok, etc.)
// that may become invalid if the character is freed or mid-transition.
// Without SEH, an AV here → exit(1) → atexit crash on freed trampolines.
// CharacterAccessor and Vec3/Quat are trivially destructible (safe with __try).
static bool SEH_WritePositionRotation(void* gameObj, Vec3 pos, Quat rot) {
    __try {
        game::CharacterAccessor accessor(gameObj);
        accessor.WritePosition(pos);

        auto& offsets = game::GetOffsets().character;
        if (offsets.rotation >= 0) {
            uintptr_t charPtr = reinterpret_cast<uintptr_t>(gameObj);
            Memory::Write(charPtr + offsets.rotation, rot);
        }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        static int s_writeAvCount = 0;
        if (++s_writeAvCount <= 10) {
            char buf[128];
            sprintf_s(buf, "KMP: SEH_WritePositionRotation CRASHED for gameObj 0x%p\n", gameObj);
            OutputDebugStringA(buf);
        }
        return false;
    }
}

// SEH-protected post-spawn setup for Core::HandleSpawnQueue's direct spawn fallback.
// Follows the same pattern as entity_hooks::SEH_PostSpawnSetup and
// game_tick_hooks::SEH_DirectSpawnPostSetup. All three spawn paths now have SEH.
// Only POD locals and trivially-destructible types (CharacterAccessor, Vec3) are
// created inside __try — safe with MSVC structured exception handling.
static bool SEH_FallbackPostSpawnSetup(void* character, EntityID netId,
                                        PlayerID owner, Vec3 pos) {
    __try {
        // 1. Teleport to desired position
        game::CharacterAccessor accessor(character);
        if (pos.x != 0.f || pos.y != 0.f || pos.z != 0.f) {
            accessor.WritePosition(pos);
        }

        // 2. Set name + faction
        Core::Get().GetPlayerController().OnRemoteCharacterSpawned(
            netId, character, owner);

        // 3. Mark as remote-controlled (AI decisions overridden)
        ai_hooks::MarkRemoteControlled(character);

        // 4. Squad injection (engine exploit — makes char selectable)
        squad_hooks::AddCharacterToLocalSquad(character);

        // 5. Set isPlayerControlled flag (engine exploit)
        game::WritePlayerControlled(
            reinterpret_cast<uintptr_t>(character), true);

        // 6. Schedule deferred AnimClass probe
        game::ScheduleDeferredAnimClassProbe(
            reinterpret_cast<uintptr_t>(character));

        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        static int s_crashCount = 0;
        if (++s_crashCount <= 10) {
            char buf[256];
            sprintf_s(buf, "KMP: SEH_FallbackPostSpawnSetup CRASHED for entity %u "
                      "char=0x%p (AV caught — entity linked, will receive updates)\n",
                      netId, character);
            OutputDebugStringA(buf);
        }
        spdlog::error("Core: Fallback post-spawn setup crashed for entity {} char=0x{:X}",
                      netId, reinterpret_cast<uintptr_t>(character));
        return false;
    }
}

void Core::ApplyRemotePositions() {
    auto& readFrame = m_frameData[m_readBuffer];
    if (!readFrame.ready) return;

    static int s_applyCount = 0;
    static int s_noObjCount = 0;
    static int s_avCount = 0;
    int applied = 0;
    int noObj = 0;
    int avErrors = 0;

    for (auto& result : readFrame.remoteResults) {
        if (!result.valid) continue;

        void* gameObj = m_entityRegistry.GetGameObject(result.netId);
        if (!gameObj) {
            noObj++;
            continue;
        }

        // SEH-protected write — catches AV from freed/invalid game objects
        if (SEH_WritePositionRotation(gameObj, result.position, result.rotation)) {
            // Update registry tracking (safe — our own code)
            m_entityRegistry.UpdatePosition(result.netId, result.position);
            m_entityRegistry.UpdateRotation(result.netId, result.rotation);
            applied++;
        } else {
            avErrors++;
            // Unlink the bad game object so we stop crashing every frame
            m_entityRegistry.SetGameObject(result.netId, nullptr);
        }
    }

    s_applyCount += applied;
    s_noObjCount += noObj;
    s_avCount += avErrors;

    // Log first few applications, then every 100th
    if (applied > 0 && (s_applyCount <= 5 || s_applyCount % 100 == 0)) {
        spdlog::info("Core::ApplyRemotePositions: applied {} this frame (total={}, noObj={}, avErrors={})",
                     applied, s_applyCount, s_noObjCount, s_avCount);
    }
}

// SEH-protected position read from a game character object.
// Returns false if the read crashes (freed object, invalid pointer chain).
static bool SEH_ReadPosition(void* gameObj, Vec3& outPos, Quat& outRot) {
    __try {
        game::CharacterAccessor accessor(gameObj);
        outPos = accessor.GetPosition();
        outRot = accessor.GetRotation();
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void Core::PollLocalPositions() {
    if (!m_connected || m_localPlayerId == 0) return;

    // Throttle to tick rate
    static auto s_lastPollTime = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - s_lastPollTime);
    if (elapsed.count() < KMP_TICK_INTERVAL_MS) return;
    s_lastPollTime = now;

    auto localEntities = m_entityRegistry.GetPlayerEntities(m_localPlayerId);
    if (localEntities.empty()) return;

    static int s_pollsSent = 0;

    for (EntityID netId : localEntities) {
        // Use GetInfoCopy for thread safety — avoids dangling pointer from GetInfo()
        auto infoCopy = m_entityRegistry.GetInfoCopy(netId);
        if (!infoCopy) continue;

        void* gameObj = m_entityRegistry.GetGameObject(netId);
        if (!gameObj) continue;

        Vec3 pos;
        Quat rotation;
        if (!SEH_ReadPosition(gameObj, pos, rotation)) {
            // Object freed or invalid — unlink
            m_entityRegistry.SetGameObject(netId, nullptr);
            continue;
        }

        // Skip if position hasn't changed enough
        if (pos.DistanceTo(infoCopy->lastPosition) < KMP_POS_CHANGE_THRESHOLD) continue;

        // Compute move speed from position delta
        float elapsedSec = elapsed.count() / 1000.f;
        float dist = pos.DistanceTo(infoCopy->lastPosition);
        float moveSpeed = (elapsedSec > 0.001f) ? dist / elapsedSec : 0.f;

        uint32_t compQuat = rotation.Compress();

        // Derive animation state from speed
        uint8_t animState = 0;
        if (moveSpeed > 5.0f) animState = 2; // running
        else if (moveSpeed > 0.5f) animState = 1; // walking

        uint8_t moveSpeedU8 = static_cast<uint8_t>(
            std::min(255.f, moveSpeed / 15.f * 255.f));

        uint16_t flags = 0;
        if (moveSpeed > 3.0f) flags |= 0x01; // running

        PacketWriter writer;
        writer.WriteHeader(MessageType::C2S_PositionUpdate);
        writer.WriteU8(1);
        writer.WriteU32(netId);
        writer.WriteF32(pos.x);
        writer.WriteF32(pos.y);
        writer.WriteF32(pos.z);
        writer.WriteU32(compQuat);
        writer.WriteU8(animState);
        writer.WriteU8(moveSpeedU8);
        writer.WriteU16(flags);

        m_client.SendUnreliable(writer.Data(), writer.Size());

        m_entityRegistry.UpdatePosition(netId, pos);
        m_entityRegistry.UpdateRotation(netId, rotation);

        s_pollsSent++;
        if (s_pollsSent <= 20 || s_pollsSent % 200 == 0) {
            spdlog::debug("Core::PollLocalPositions: sent #{} netId={} pos=({:.1f},{:.1f},{:.1f}) speed={:.1f}",
                          s_pollsSent, netId, pos.x, pos.y, pos.z, moveSpeed);
        }
    }
}

void Core::SendCachedPackets() {
    auto& readFrame = m_frameData[m_readBuffer];
    if (!readFrame.ready || readFrame.packetBytes.empty()) return;

    m_client.SendUnreliable(readFrame.packetBytes.data(), readFrame.packetBytes.size());
}

void Core::HandleHostTeleport() {
    if (!m_hasHostSpawnPoint || m_spawnTeleportDone || m_isHost) return;

    if (!m_hostTpTimerStarted) {
        m_hostTpTimerStarted = true;
        m_hostTpTimer = std::chrono::steady_clock::now();
        spdlog::info("Core: Host spawn point received ({:.1f}, {:.1f}, {:.1f}), "
                     "waiting 2s before teleport",
                     m_hostSpawnPoint.x, m_hostSpawnPoint.y, m_hostSpawnPoint.z);
        m_nativeHud.LogStep("GAME", "Host position received, teleporting in 2s...");
    }

    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - m_hostTpTimer);

    if (elapsed.count() < 2) return;

    auto localEntities = m_entityRegistry.GetPlayerEntities(m_localPlayerId);
    if (localEntities.empty()) {
        spdlog::debug("Core: Waiting for local entities to register before teleporting...");
        return;
    }

    int teleported = 0;
    for (EntityID netId : localEntities) {
        void* gameObj = m_entityRegistry.GetGameObject(netId);
        if (!gameObj) continue;

        game::CharacterAccessor accessor(gameObj);
        if (!accessor.IsValid()) continue;

        Vec3 spawnPos = m_hostSpawnPoint;
        spawnPos.x += static_cast<float>(teleported % 4) * 3.0f;
        spawnPos.z += static_cast<float>(teleported / 4) * 3.0f;

        if (accessor.WritePosition(spawnPos)) {
            m_entityRegistry.UpdatePosition(netId, spawnPos);
            teleported++;
        }
    }

    if (teleported > 0) {
        m_spawnTeleportDone = true;
        spdlog::info("Core: Teleported {} local characters to host at ({:.1f}, {:.1f}, {:.1f})",
                     teleported, m_hostSpawnPoint.x, m_hostSpawnPoint.y, m_hostSpawnPoint.z);
        m_overlay.AddSystemMessage("Teleported to host location!");
        m_nativeHud.AddSystemMessage("Teleported " + std::to_string(teleported)
                                     + " characters to host!");
        m_nativeHud.LogStep("OK", "Teleported to host location!");
    }
}

void Core::HandleSpawnQueue() {
    // Safety: don't process spawns until game world is fully loaded AND the
    // LoadingOrchestrator says it's safe (burst ended + cooldown elapsed + no
    // pending resources). This replaces the old 20-second fixed grace period
    // with resource-aware gating via AssetFacilitator.
    if (!m_gameLoaded) return;
    if (!AssetFacilitator::Get().CanSpawn()) return;

    // One-shot heap scan
    static bool heapScanned = false;
    if (!heapScanned && m_spawnManager.IsReady()) {
        if (m_spawnManager.GetManagerPointer() != 0 || m_spawnManager.GetTemplateCount() < 10) {
            spdlog::info("Core: Triggering GameData heap scan (manager=0x{:X}, templates={})...",
                         m_spawnManager.GetManagerPointer(), m_spawnManager.GetTemplateCount());
            m_spawnManager.ScanGameDataHeap();
            heapScanned = true;
            spdlog::info("Core: Heap scan complete, {} templates available",
                         m_spawnManager.GetTemplateCount());
        }
    }

    // Spawn queue fallback logic
    static auto s_lastSpawnLog = std::chrono::steady_clock::now();
    static auto s_firstPendingTime = std::chrono::steady_clock::time_point{};
    static bool s_hasPendingTimer = false;
    static int s_directSpawnAttempts = 0;
    static bool s_shownWaitingMsg = false;
    static bool s_shownTimeoutMsg = false;

    size_t pending = m_spawnManager.GetPendingSpawnCount();

    if (pending > 0 && !s_hasPendingTimer) {
        s_firstPendingTime = std::chrono::steady_clock::now();
        s_hasPendingTimer = true;
        s_shownWaitingMsg = false;
        s_shownTimeoutMsg = false;
        spdlog::info("Core: {} spawn(s) queued — waiting for game to create an NPC...", pending);
        m_nativeHud.LogStep("GAME", std::to_string(pending) + " spawn(s) queued");
        m_nativeHud.AddSystemMessage("Waiting for game to create an NPC for remote player...");
    } else if (pending == 0) {
        s_hasPendingTimer = false;
        s_directSpawnAttempts = 0;
        s_shownWaitingMsg = false;
        s_shownTimeoutMsg = false;
    }

    // Periodic status updates while waiting
    if (pending > 0 && s_hasPendingTimer) {
        auto pendingDuration = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - s_firstPendingTime);

        // At 10s, show encouraging message
        if (pendingDuration.count() >= 10 && !s_shownWaitingMsg) {
            s_shownWaitingMsg = true;
            m_nativeHud.AddSystemMessage("Still waiting for NPC creation... Walk near a town or camp.");
            m_nativeHud.LogStep("GAME", "Waiting for NPC spawn event (walk near town)...");
        }

        // At 30s, show timeout warning
        if (pendingDuration.count() >= 30 && !s_shownTimeoutMsg) {
            s_shownTimeoutMsg = true;
            spdlog::warn("Core: Spawn queue waiting 30s+ — no NPC creation events detected. "
                         "The CharacterCreate hook may not be firing.");
            m_nativeHud.AddSystemMessage("Spawn timeout! Try walking near NPCs or entering a town.");
            m_nativeHud.LogStep("WARN", "Spawn queue timeout (30s) — need NPC activity");
        }
    }

    auto sinceLog = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - s_lastSpawnLog);
    if (sinceLog.count() >= 5 && pending > 0) {
        s_lastSpawnLog = std::chrono::steady_clock::now();
        spdlog::info("Core::HandleSpawnQueue: {} pending spawns (inPlaceCount={}, charTemplates={}, "
                     "factoryReady={}, hasPreCall={})",
                     pending, entity_hooks::GetInPlaceSpawnCount(),
                     m_spawnManager.GetCharacterTemplateCount(),
                     m_spawnManager.IsReady(),
                     m_spawnManager.HasPreCallData());
    }

    if (pending > 0 && s_hasPendingTimer && m_spawnManager.HasPreCallData()) {
        auto pendingDuration = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - s_firstPendingTime);

        // Wait 10s before using direct spawn — gives in-place replay (entity_hooks)
        // time to handle spawns via the safer original-stack method.
        // In-place replay needs ~5s (loading burst end + 3s settle), so 10s
        // provides a comfortable margin before falling back to SpawnCharacterDirect.
        if (pendingDuration.count() >= 10 && s_directSpawnAttempts < 10) {
            static auto s_lastDirectAttempt = std::chrono::steady_clock::time_point{};
            auto sinceLast = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - s_lastDirectAttempt);

            if (s_directSpawnAttempts == 0 || sinceLast.count() >= 3) {
                s_lastDirectAttempt = std::chrono::steady_clock::now();
                s_directSpawnAttempts++;

                spdlog::info("Core: FALLBACK DIRECT SPAWN attempt #{} ({} pending, "
                             "{}s since queued)", s_directSpawnAttempts, pending,
                             pendingDuration.count());
                m_nativeHud.LogStep("GAME", "Fallback spawn attempt #" +
                              std::to_string(s_directSpawnAttempts) + "...");

                SpawnRequest req;
                if (m_spawnManager.PopNextSpawn(req)) {
                    void* character = m_spawnManager.SpawnCharacterDirect(&req.position);
                    if (character) {
                        spdlog::info("Core: FALLBACK SPAWN SUCCESS! entity={} char=0x{:X}",
                                     req.netId, reinterpret_cast<uintptr_t>(character));

                        // Link to EntityRegistry FIRST (safe — our own code, just a map insert).
                        // Even if post-spawn setup below crashes, the entity is linked and
                        // ApplyRemotePositions() will move it next frame.
                        m_entityRegistry.SetGameObject(req.netId, character);
                        m_entityRegistry.UpdatePosition(req.netId, req.position);

                        // SEH-protected post-spawn setup (WritePosition, name/faction, AI,
                        // squad injection, player controlled flag, AnimClass probe).
                        // All follow chains of game memory pointers that may be invalid
                        // for freshly-spawned characters from SpawnCharacterDirect.
                        if (SEH_FallbackPostSpawnSetup(character, req.netId,
                                                        req.owner, req.position)) {
                            m_nativeHud.LogStep("OK", "Remote player spawned!");
                            m_overlay.AddSystemMessage("Remote player character spawned!");
                            m_nativeHud.AddSystemMessage("Remote player spawned nearby!");
                        } else {
                            m_nativeHud.LogStep("WARN", "Spawn partial — setup crashed (SEH caught)");
                            m_nativeHud.AddSystemMessage("Remote player spawned (partial — setup error)");
                        }
                    } else {
                        spdlog::warn("Core: FALLBACK SPAWN FAILED for entity {} (attempt #{})",
                                     req.netId, s_directSpawnAttempts);
                        req.retryCount++;
                        if (req.retryCount < MAX_SPAWN_RETRIES) {
                            m_spawnManager.RequeueSpawn(req);
                        }
                        m_nativeHud.LogStep("WARN", "Spawn attempt failed, retrying...");
                    }
                }
            }
        }
    }
}

void Core::KickBackgroundWork() {
    m_orchestrator.PostFrameWork([this] { BackgroundReadEntities(); });
    m_orchestrator.PostFrameWork([this] { BackgroundInterpolate(); });
}

void Core::UpdateDiagnostics(float deltaTime) {
    static int s_tickCount = 0;
    static auto s_lastTickLog = std::chrono::steady_clock::now();
    s_tickCount++;
    auto tickNow = std::chrono::steady_clock::now();
    auto tickElapsed = std::chrono::duration_cast<std::chrono::seconds>(tickNow - s_lastTickLog);
    if (tickElapsed.count() >= 5) {
        spdlog::info("Core::OnGameTick: {} ticks in last {}s (dt={:.4f}), entities={}, remote={}",
                      s_tickCount, tickElapsed.count(), deltaTime,
                      m_entityRegistry.GetEntityCount(),
                      m_entityRegistry.GetRemoteCount());
        s_tickCount = 0;
        s_lastTickLog = tickNow;
    }
}

// ════════════════════════════════════════════════════════════════════════════
// Background Worker Methods (run on orchestrator threads)
// ════════════════════════════════════════════════════════════════════════════

// SEH-protected read of all character data needed for position sync.
// Runs on background worker thread — game can free characters at any time,
// turning valid pointers into dangling ones. Without SEH, an AV here kills
// the process. Only POD types in __try block (safe with MSVC SEH).
struct BGReadResult {
    Vec3 pos;
    Quat rot;
    float speed;
    uint8_t animState;
    bool valid;
};

static BGReadResult SEH_ReadCharacterBG(void* gameObj) {
    BGReadResult r = {};
    __try {
        game::CharacterAccessor character(gameObj);
        if (!character.IsValid()) return r;
        r.pos = character.GetPosition();
        r.rot = character.GetRotation();
        r.speed = character.GetMoveSpeed();
        r.animState = character.GetAnimState();
        r.valid = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        static int s_bgReadCrash = 0;
        if (++s_bgReadCrash <= 10) {
            char buf[128];
            sprintf_s(buf, "KMP: SEH caught AV in BGReadCharacter for gameObj 0x%p\n", gameObj);
            OutputDebugStringA(buf);
        }
        r.valid = false;
    }
    return r;
}

void Core::BackgroundReadEntities() {
    auto& writeFrame = m_frameData[m_writeBuffer];

    auto localEntities = m_entityRegistry.GetPlayerEntities(m_localPlayerId);

    static bool s_firstIterLog = true;
    if (s_firstIterLog && !localEntities.empty()) {
        spdlog::info("Core: Background read — {} local entities in registry", localEntities.size());
        s_firstIterLog = false;
    }

    struct PendingPos {
        CharacterPosition cp;
        EntityID netId;
        Vec3 pos;
        Quat rot;
    };
    std::vector<PendingPos> pendingPositions;

    for (EntityID netId : localEntities) {
        void* gameObj = m_entityRegistry.GetGameObject(netId);
        if (!gameObj) continue;

        // Use GetInfoCopy to avoid dangling pointer from GetInfo().
        // GetInfo() returns a raw pointer into the unordered_map — if another thread
        // modifies the registry (e.g., CharacterCreate hook Register() during zone load),
        // the map can rehash and invalidate the pointer → AV on worker thread → process exit.
        auto infoCopyOpt = m_entityRegistry.GetInfoCopy(netId);
        if (!infoCopyOpt || infoCopyOpt->isRemote) continue;
        EntityInfo infoCopy = *infoCopyOpt;

        // SEH-protected read: game can free characters during zone transitions
        // while this background thread is reading. Dangling pointer → AV.
        BGReadResult rd = SEH_ReadCharacterBG(gameObj);
        if (!rd.valid) continue;

        Vec3 pos = rd.pos;
        Quat rot = rd.rot;

        if (pos.x == 0.f && pos.y == 0.f && pos.z == 0.f) continue;

        float dist = pos.DistanceTo(infoCopy.lastPosition);
        if (dist < KMP_POS_CHANGE_THRESHOLD) continue;

        float computedSpeed = 0.f;
        if (infoCopy.lastUpdateTick > 0) {
            // Use a nominal deltaTime for background computation
            computedSpeed = dist / 0.016f; // ~60fps assumption
        }

        float speed = rd.speed;
        if (speed <= 0.f && computedSpeed > 0.f) {
            speed = computedSpeed;
        }

        uint8_t animState = rd.animState;
        if (animState == 0 && speed > 0.5f) {
            animState = (speed > 5.0f) ? 2 : 1;
        }

        // Store in frame data
        CachedEntityPos cached;
        cached.netId = netId;
        cached.position = pos;
        cached.rotation = rot;
        cached.speed = speed;
        cached.animState = animState;
        cached.dirty = true;
        writeFrame.localEntities.push_back(cached);

        PendingPos pp;
        pp.cp.entityId = netId;
        pp.cp.posX = pos.x;
        pp.cp.posY = pos.y;
        pp.cp.posZ = pos.z;
        pp.cp.compressedQuat = rot.Compress();
        pp.cp.animStateId = animState;
        pp.cp.moveSpeed = static_cast<uint8_t>(std::min(255.f, speed / 15.f * 255.f));
        pp.cp.flags = (speed > 3.0f) ? 0x01 : 0x00;
        pp.netId = netId;
        pp.pos = pos;
        pp.rot = rot;
        pendingPositions.push_back(pp);
    }

    // Build pre-serialized packet
    if (!pendingPositions.empty()) {
        PacketWriter writer;
        writer.WriteHeader(MessageType::C2S_PositionUpdate);
        writer.WriteU8(static_cast<uint8_t>(pendingPositions.size()));
        for (auto& pp : pendingPositions) {
            writer.WriteRaw(&pp.cp, sizeof(pp.cp));
            // Update registry tracking (shared_mutex inside)
            m_entityRegistry.UpdatePosition(pp.netId, pp.pos);
            m_entityRegistry.UpdateRotation(pp.netId, pp.rot);
        }
        writeFrame.packetBytes = std::move(writer.Buffer());
    }

    writeFrame.ready = true;
}

void Core::BackgroundInterpolate() {
    auto& writeFrame = m_frameData[m_writeBuffer];

    auto remoteEntities = m_entityRegistry.GetRemoteEntities();
    float now = static_cast<float>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count()) / 1000.f;

    static int s_interpCallCount = 0;
    int validCount = 0;
    s_interpCallCount++;

    for (EntityID remoteId : remoteEntities) {
        CachedRemoteResult result;
        result.netId = remoteId;

        uint8_t moveSpeed = 0;
        uint8_t animState = 0;
        if (m_interpolation.GetInterpolated(remoteId, now,
                                             result.position, result.rotation,
                                             moveSpeed, animState)) {
            result.moveSpeed = moveSpeed;
            result.animState = animState;
            result.valid = true;
            validCount++;
        }

        writeFrame.remoteResults.push_back(result);
    }

    // Log first few calls + every 200th
    if (!remoteEntities.empty() && (s_interpCallCount <= 5 || s_interpCallCount % 200 == 0)) {
        spdlog::info("Core::BackgroundInterpolate: {} remote entities, {} valid interp results (call #{})",
                     remoteEntities.size(), validCount, s_interpCallCount);
    }
}

bool Core::TeleportToNearestRemotePlayer() {
    if (!m_connected) {
        m_nativeHud.AddSystemMessage("Not connected to a server.");
        return false;
    }

    // Find the nearest remote entity with a valid game object and position
    auto remoteEntities = m_entityRegistry.GetRemoteEntities();
    if (remoteEntities.empty()) {
        m_nativeHud.AddSystemMessage("No remote players found.");
        spdlog::info("Core: TeleportToNearest — no remote entities");
        return false;
    }

    // Get our first local entity's position as reference
    auto localEntities = m_entityRegistry.GetPlayerEntities(m_localPlayerId);
    if (localEntities.empty()) {
        m_nativeHud.AddSystemMessage("No local characters registered.");
        return false;
    }

    Vec3 localPos(0, 0, 0);
    void* firstLocalObj = m_entityRegistry.GetGameObject(localEntities[0]);
    if (firstLocalObj) {
        game::CharacterAccessor localAccessor(firstLocalObj);
        localPos = localAccessor.GetPosition();
    }

    // Find the nearest remote entity with a valid position
    float bestDist = 1e18f;
    Vec3 bestPos(0, 0, 0);
    EntityID bestId = INVALID_ENTITY;
    PlayerID bestOwner = 0;

    for (EntityID remoteId : remoteEntities) {
        auto* info = m_entityRegistry.GetInfo(remoteId);
        if (!info) continue;

        Vec3 rPos = info->lastPosition;
        if (rPos.x == 0.f && rPos.y == 0.f && rPos.z == 0.f) continue;

        float dist = localPos.DistanceTo(rPos);
        if (dist < bestDist) {
            bestDist = dist;
            bestPos = rPos;
            bestId = remoteId;
            bestOwner = info->ownerPlayerId;
        }
    }

    if (bestId == INVALID_ENTITY) {
        m_nativeHud.AddSystemMessage("No remote players with valid positions found.");
        return false;
    }

    // Get the remote player's name for display
    auto* rp = m_playerController.GetRemotePlayer(bestOwner);
    std::string targetName = rp ? rp->playerName : ("Player_" + std::to_string(bestOwner));

    // Teleport all local entities to the target position
    int teleported = 0;
    for (EntityID netId : localEntities) {
        void* gameObj = m_entityRegistry.GetGameObject(netId);
        if (!gameObj) continue;

        game::CharacterAccessor accessor(gameObj);
        if (!accessor.IsValid()) continue;

        Vec3 tpPos = bestPos;
        tpPos.x += static_cast<float>(teleported % 4) * 3.0f;
        tpPos.z += static_cast<float>(teleported / 4) * 3.0f;

        if (accessor.WritePosition(tpPos)) {
            m_entityRegistry.UpdatePosition(netId, tpPos);
            teleported++;
        }
    }

    if (teleported > 0) {
        spdlog::info("Core: Teleported {} characters to {} at ({:.1f}, {:.1f}, {:.1f}) [dist={:.0f}]",
                     teleported, targetName, bestPos.x, bestPos.y, bestPos.z, bestDist);
        m_nativeHud.AddSystemMessage("Teleported to " + targetName + "!");
        m_overlay.AddSystemMessage("Teleported " + std::to_string(teleported) +
                                   " characters to " + targetName);
        return true;
    }

    m_nativeHud.AddSystemMessage("Teleport failed — no valid local characters.");
    return false;
}

} // namespace kmp
