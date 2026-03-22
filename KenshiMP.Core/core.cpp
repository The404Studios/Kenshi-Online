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
#include "hooks/squad_spawn_hooks.h"
#include "hooks/char_tracker_hooks.h"
#include "game/game_types.h"
#include "game/asset_facilitator.h"
#include "game/shared_save_sync.h"
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
#include <cmath>
#include <unordered_map>
#include <csignal>
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

// ── Breadcrumb trail for crash diagnosis ──
// Writes to a file BEFORE each dangerous operation. If the process dies from
// __fastfail, TerminateProcess, or any uncatchable exception, the file shows
// the last known step. Uses direct C I/O with fflush for immediate write.
static void WriteBreadcrumb(const char* step, int tickNum = 0, int extra = 0) {
    // Write every Nth tick to avoid excessive I/O, PLUS always write the first 50
    static int s_writeCount = 0;
    if (tickNum > 50 && tickNum % 10 != 0) return; // Skip most ticks after warmup
    s_writeCount++;

    FILE* f = nullptr;
    fopen_s(&f, "C:\\Program Files (x86)\\Steam\\steamapps\\common\\Kenshi\\KenshiOnline_BREADCRUMB.txt", "w");
    if (f) {
        fprintf(f, "tick=%d step=%s extra=%d charCreate=#%d writes=%d\n",
                tickNum, step, extra, g_lastCharacterCreateNum, s_writeCount);
        fflush(f);
        fclose(f);
    }
}

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

    // Also detect crashes in Ogre/MyGUI DLLs — our code interacts with these,
    // and crashes there are invisible without explicit tracking.
    // PhysX is resolved to EXCLUDE it from inHookStub — PhysXCore64.dll loads at
    // ~0x70800000 which falls inside the 0x10000..0x80000000 hook stub range.
    // PhysX crashes aren't caused by our code and should not be logged.
    static uintptr_t s_ogreBase = 0, s_ogreEnd = 0;
    static uintptr_t s_myguiBase = 0, s_myguiEnd = 0;
    static uintptr_t s_physxBase = 0, s_physxEnd = 0;
    static uintptr_t s_physx3Base = 0, s_physx3End = 0;
    static bool s_gameDllsResolved = false;
    if (!s_gameDllsResolved) {
        s_gameDllsResolved = true;
        auto resolveModule = [](const char* name, uintptr_t& base, uintptr_t& end) {
            HMODULE h = GetModuleHandleA(name);
            if (h) {
                base = reinterpret_cast<uintptr_t>(h);
                auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(h);
                auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(
                    reinterpret_cast<const uint8_t*>(h) + dos->e_lfanew);
                end = base + nt->OptionalHeader.SizeOfImage;
            }
        };
        resolveModule("OgreMain_x64.dll", s_ogreBase, s_ogreEnd);
        resolveModule("MyGUIEngine_x64.dll", s_myguiBase, s_myguiEnd);
        resolveModule("PhysXCore64.dll", s_physxBase, s_physxEnd);
        resolveModule("PhysX3_x64.dll", s_physx3Base, s_physx3End);
    }
    bool inOgre = (s_ogreBase && rip >= s_ogreBase && rip < s_ogreEnd);
    bool inMyGUI = (s_myguiBase && rip >= s_myguiBase && rip < s_myguiEnd);
    bool inPhysX = (s_physxBase && rip >= s_physxBase && rip < s_physxEnd) ||
                   (s_physx3Base && rip >= s_physx3Base && rip < s_physx3End);

    // Exclude PhysX from hook stub detection — PhysX crashes are not our fault
    if (inPhysX) inHookStub = false;

    if (!inGame && !isNull && !inOurDll && !inHookStub && !inOgre && !inMyGUI) {
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
        // SEH-protected functions: SEH_ReadPointer, SEH_ReadKenshiStringRaw,
        // SEH_CallFactory, ScanGameDataHeap, and other SEH helpers in spawn_manager.
        // These intentionally trigger AVs while probing memory — caught by __except.
        // Range 0x2BD00..0x2D000 covers all spawn_manager SEH functions.
        if (dllOffset >= 0x2BD00 && dllOffset <= 0x2D000) {
            return EXCEPTION_CONTINUE_SEARCH;
        }
        // SEH helpers in entity_hooks (SEH_ReadCharacterData, SEH_ReadFactionPtr, etc.)
        // Range 0x21000..0x22200 covers all entity_hooks SEH functions.
        if (dllOffset >= 0x21000 && dllOffset <= 0x22200) {
            return EXCEPTION_CONTINUE_SEARCH;
        }
        // SEH helpers in sync_orchestrator (SEH_WritePositionRotation, SEH_ReadPosition, etc.)
        // These probe game objects that may be freed. Range 0x0..0x2000 from sync_orch start.
        // Broad skip: any DLL AV in an __except-protected function is benign.
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
        "  Filter: inGame=%d inNull=%d inDll=%d inStub=%d inOgre=%d inMyGUI=%d dllBase=0x%llX dllEnd=0x%llX\n",
        code, (unsigned long long)rip, (unsigned long long)ripOffset,
        ctx->Rax, ctx->Rbx, ctx->Rcx, ctx->Rdx,
        ctx->Rsp, ctx->Rbp, ctx->Rsi, ctx->Rdi,
        ctx->R8, ctx->R9, ctx->R10, ctx->R11,
        ctx->R12, ctx->R13, ctx->R14, ctx->R15,
        g_lastCharacterCreateNum,
        g_lastTickStep, g_lastStepName ? g_lastStepName : "?", g_tickNumber,
        (int)inGame, (int)isNull, (int)inOurDll, (int)inHookStub,
        (int)inOgre, (int)inMyGUI,
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

    // ── Session header in CRASH.log ──
    // Mark session boundaries so crash analysis can tell which entries belong together.
    {
        FILE* cf = nullptr;
        fopen_s(&cf, "KenshiOnline_CRASH.log", "a");
        if (cf) {
            SYSTEMTIME st;
            GetLocalTime(&st);
            fprintf(cf, "\n═══ KMP SESSION START %04d-%02d-%02d %02d:%02d:%02d PID=%lu "
                        "gameBase=0x%llX ═══\n",
                    st.wYear, st.wMonth, st.wDay,
                    st.wHour, st.wMinute, st.wSecond,
                    GetCurrentProcessId(),
                    (unsigned long long)g_gameModuleBase);
            fclose(cf);
        }
    }

    // ── Breadcrumb file for crash diagnosis ──
    // Opened once at startup, written before every dangerous operation, fflush'd.
    // Even if VEH and atexit fail (e.g., __fastfail), the file shows last known step.
    {
        FILE* bcf = nullptr;
        fopen_s(&bcf, "C:\\Program Files (x86)\\Steam\\steamapps\\common\\Kenshi\\KenshiOnline_BREADCRUMB.txt", "w");
        if (bcf) {
            fprintf(bcf, "KMP breadcrumb file created — monitoring for crash location\n");
            fflush(bcf);
            fclose(bcf);
        }
    }

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

    // ── Abort signal handler ──
    // C runtime abort() (e.g., from failed assertions or heap corruption) doesn't
    // call atexit. This handler logs the last known state before termination.
    signal(SIGABRT, [](int) {
        char buf[256];
        sprintf_s(buf, "KMP SIGABRT: LastCreate=#%d, LastTick=#%d step=%d (%s)\n",
                  g_lastCharacterCreateNum, g_tickNumber, g_lastTickStep,
                  g_lastStepName ? g_lastStepName : "?");
        OutputDebugStringA(buf);

        FILE* f = nullptr;
        fopen_s(&f, "C:\\Program Files (x86)\\Steam\\steamapps\\common\\Kenshi\\KenshiOnline_CRASH.log", "a");
        if (f) {
            fprintf(f, "\nKMP SIGABRT: LastCreate=#%d, LastTick=#%d step=%d (%s)\n",
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
            auto infoCopy = m_entityRegistry.GetInfoCopy(netId);
            PlayerID owner = infoCopy ? infoCopy->ownerPlayerId : 0;
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

// SEH wrapper for shutdown steps — no C++ objects with destructors allowed inside __try
static bool SEH_ShutdownStep(void (*fn)()) {
    __try {
        fn();
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Forward declaration — defined after OnGameTick in "Staged Pipeline Methods" section
static int SEH_ValidateEntityFaction(void* gameObj, uintptr_t goodFaction,
                                      uintptr_t modBase, size_t modSize);

// SEH-protected sync orchestrator tick. If the game crashes inside
// position reads/writes (e.g., freed character objects), catch it
// instead of killing the process.
static bool SEH_SyncOrchestratorTick(SyncOrchestrator* orch, float dt) {
    __try {
        orch->Tick(dt);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        static int s_syncCrashCount = 0;
        if (++s_syncCrashCount <= 10) {
            char buf[128];
            sprintf_s(buf, "KMP: SEH_SyncOrchestratorTick CRASHED (count=%d)\n", s_syncCrashCount);
            OutputDebugStringA(buf);
        }
        return false;
    }
}

static bool SEH_InterpolationUpdate(Interpolation* interp, float dt) {
    __try {
        interp->Update(dt);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static void SEH_LoadingOrchTick(LoadingOrchestrator* orch) {
    __try {
        orch->Tick();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        static int s_count = 0;
        if (++s_count <= 5) {
            char buf[128];
            sprintf_s(buf, "KMP: SEH_LoadingOrchTick CRASHED (count=%d)\n", s_count);
            OutputDebugStringA(buf);
        }
    }
}

void Core::Shutdown() {
    spdlog::info("Kenshi-Online shutting down...");

    m_running = false;
    m_connected = false;

    // Each shutdown step is SEH-protected individually — MyGUI widgets may already be
    // freed by Kenshi, causing AVs in setVisible/setCaption calls during teardown.
    m_pipelineOrch.Shutdown();
    AssetFacilitator::Get().Unbind();
    SyncFacilitator::Get().Unbind();
    if (m_syncOrchestrator) {
        m_syncOrchestrator->Shutdown();
    }

    m_orchestrator.Stop();

    if (m_networkThread.joinable()) {
        m_networkThread.join();
    }

    m_client.Disconnect();

    // Uninstall inline hooks before HookManager (which only handles MinHook hooks)
    squad_spawn_hooks::Uninstall();
    char_tracker_hooks::Uninstall();

    // These touch MyGUI — most likely to crash during Kenshi's own shutdown
    if (!SEH_ShutdownStep([](){ Core::Get().GetNativeHud().Shutdown(); })) {
        OutputDebugStringA("KMP: SEH — NativeHud::Shutdown crashed\n");
    }
    if (!SEH_ShutdownStep([](){ Core::Get().GetOverlay().Shutdown(); })) {
        OutputDebugStringA("KMP: SEH — Overlay::Shutdown crashed\n");
    }
    if (!SEH_ShutdownStep([](){ HookManager::Get().Shutdown(); })) {
        OutputDebugStringA("KMP: SEH — HookManager::Shutdown crashed\n");
    }

    // Remove vectored exception handler
    if (g_vehHandle) {
        RemoveVectoredExceptionHandler(g_vehHandle);
        g_vehHandle = nullptr;
    }

    // Save config to PID-specific path (avoids stomping another instance's config)
    m_config.Save(ClientConfig::GetInstancePath());

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

    // ── Steam singleton validation ──
    // Hardcoded RVAs (PlayerBase=0x01AC8A90, GameWorld=0x02133040) are GOG 1.0.68 only.
    // On Steam, these addresses contain unrelated data — dereferencing reads garbage
    // pointers that crash downstream systems (CharacterIterator, SpawnManager, etc.).
    // Validate by reading the value and checking it looks like a real heap pointer.
    {
        uintptr_t base = m_scanner.GetBase();
        size_t size = m_scanner.GetSize();
        auto validateSingleton = [base, size](uintptr_t addr, const char* name) -> bool {
            if (addr == 0) return true; // Not resolved, nothing to validate
            uintptr_t val = 0;
            if (!Memory::Read(addr, val)) {
                spdlog::warn("Core: {} at 0x{:X} — unreadable, clearing", name, addr);
                return false;
            }
            // Must be in valid userspace heap range and NOT inside the module image
            if (val == 0) return true; // NULL is OK (game not loaded yet)
            if (val < 0x10000 || val >= 0x00007FFFFFFFFFFF) {
                spdlog::warn("Core: {} at 0x{:X} reads garbage 0x{:X} — clearing (Steam offset mismatch)",
                             name, addr, val);
                return false;
            }
            if (val >= base && val < base + size) {
                spdlog::warn("Core: {} at 0x{:X} reads module-internal ptr 0x{:X} — clearing",
                             name, addr, val);
                return false;
            }
            // Double-dereference: a real singleton should point to a readable object
            uintptr_t check = 0;
            if (!Memory::Read(val, check)) {
                spdlog::warn("Core: {} at 0x{:X} -> 0x{:X} not dereferenceable — clearing",
                             name, addr, val);
                return false;
            }
            return true;
        };

        if (!validateSingleton(m_gameFuncs.PlayerBase, "PlayerBase")) {
            m_gameFuncs.PlayerBase = 0;
        }
        if (!validateSingleton(m_gameFuncs.GameWorldSingleton, "GameWorldSingleton")) {
            m_gameFuncs.GameWorldSingleton = 0;
        }

        if (m_isSteamVersion && m_gameFuncs.PlayerBase == 0 && m_gameFuncs.GameWorldSingleton == 0) {
            spdlog::info("Core: Steam version — singletons cleared (will use loading cache + entity_hooks fallback)");
            m_nativeHud.LogStep("INFO", "Steam: using entity_hooks fallback (no hardcoded singletons)");
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
                    // Only null functions that are known to be dangerous with wrong signatures
                    auto it = funcPtrs.find(check.name);
                    if (it != funcPtrs.end() && it->second) {
                        *(it->second) = nullptr;
                        spdlog::warn("Core: SIGNATURE MISMATCH for '{}' — hook expects {} params, analysis suggests ~{} — NULLED",
                                     check.name, check.hookParamCount, sig.estimatedParams);
                    } else {
                        // MovRaxRsp functions confuse param detection — warn but don't disable
                        spdlog::warn("Core: Signature analysis for '{}' — hook expects {} params, analysis suggests ~{} (MovRaxRsp may confuse analyzer — keeping)",
                                     check.name, check.hookParamCount, sig.estimatedParams);
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
    // CharacterCreate hook is INSTALLED but DISABLED immediately.
    // It must NOT be active during game loading — the 130+ rapid-fire
    // CharacterCreate calls through the MovRaxRsp naked detour corrupt
    // the heap. The hook is only enabled when connecting to a multiplayer
    // server (via ResumeForNetwork). Character discovery during/after
    // loading uses CharacterIterator instead.
    // ═══════════════════════════════════════════════════════════════════
    if (m_gameFuncs.CharacterSpawn) {
        m_nativeHud.LogStep("HOOK", "Entity hooks (CharacterCreate)...");
        if (SEH_InstallEntityHooks()) {
            m_nativeHud.LogStep("OK", "CharacterCreate installed (captures 2, then bypasses)");
        } else {
            m_nativeHud.LogStep("ERR", "CharacterCreate hook FAILED");
            allOk = false;
        }
    } else {
        m_nativeHud.LogStep("ERR", "CharacterSpawn pattern NOT FOUND");
    }

    // ═══════════════════════════════════════════════════════════════════
    // Combat, Inventory, and Faction hooks installed EARLY.
    // These fire on individual game events (not loading bursts), so the
    // MovRaxRsp wrapper handles them fine. Each hook has SEH protection
    // and checks IsConnected()/loading guards before sending packets.
    // Previously deferred to OnGameLoaded which often never fired due
    // to a timing bug in the loading gap detection during Startup phase.
    // ═══════════════════════════════════════════════════════════════════

    // Set loading guards so hooks don't send packets during save load
    inventory_hooks::SetLoading(true);
    faction_hooks::SetLoading(true);

    // Combat hooks (ApplyDamage, CharacterDeath, CharacterKO)
    if (m_gameFuncs.ApplyDamage) {
        m_nativeHud.LogStep("HOOK", "Combat hooks...");
        if (combat_hooks::Install()) {
            m_nativeHud.LogStep("OK", "Combat hooks installed");
        } else {
            m_nativeHud.LogStep("WARN", "Combat hooks FAILED");
        }
    } else {
        m_nativeHud.LogStep("WARN", "ApplyDamage not resolved — combat hooks skipped");
    }

    // Squad spawn bypass hooks (for reliable remote character spawning)
    {
        m_nativeHud.LogStep("HOOK", "Squad spawn bypass...");
        if (squad_spawn_hooks::Install()) {
            m_nativeHud.LogStep("OK", "Squad spawn bypass installed");
        } else {
            m_nativeHud.LogStep("WARN", "Squad spawn bypass not available (pattern not found)");
        }
    }

    // Character tracker hooks (animation update — tracks all chars by name)
    {
        m_nativeHud.LogStep("HOOK", "Character tracker...");
        if (char_tracker_hooks::Install()) {
            m_nativeHud.LogStep("OK", "Character tracker installed");
        } else {
            m_nativeHud.LogStep("WARN", "Character tracker not available (pattern not found)");
        }
    }

    // Inventory hooks (ItemPickup, ItemDrop, BuyItem)
    if (m_gameFuncs.ItemPickup) {
        m_nativeHud.LogStep("HOOK", "Inventory hooks...");
        if (inventory_hooks::Install()) {
            m_nativeHud.LogStep("OK", "Inventory hooks installed");
        } else {
            m_nativeHud.LogStep("WARN", "Inventory hooks FAILED");
        }
    } else {
        m_nativeHud.LogStep("WARN", "ItemPickup not resolved — inventory hooks skipped");
    }

    // Faction hooks (FactionRelation)
    if (m_gameFuncs.FactionRelation) {
        m_nativeHud.LogStep("HOOK", "Faction hooks...");
        if (faction_hooks::Install()) {
            m_nativeHud.LogStep("OK", "Faction hooks installed");
        } else {
            m_nativeHud.LogStep("WARN", "Faction hooks FAILED");
        }
    } else {
        m_nativeHud.LogStep("WARN", "FactionRelation not resolved — faction hooks skipped");
    }

    // Time hooks — drives OnGameTick (essential for game loop)
    if (m_gameFuncs.TimeUpdate) {
        m_nativeHud.LogStep("HOOK", "Time hooks...");
        if (time_hooks::Install()) {
            m_nativeHud.LogStep("OK", "Time hooks installed");
        } else {
            m_nativeHud.LogStep("WARN", "Time hooks FAILED");
        }
    }

    // AI hooks (AICreate + AIPackages)
    if (m_gameFuncs.AICreate) {
        m_nativeHud.LogStep("HOOK", "AI hooks...");
        if (ai_hooks::Install()) {
            m_nativeHud.LogStep("OK", "AI hooks installed");
        } else {
            m_nativeHud.LogStep("WARN", "AI hooks FAILED");
        }
    }

    m_nativeHud.LogStep("OK", "All hooks installed");

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

// ── Client Phase State Machine ──

void Core::TransitionTo(ClientPhase newPhase) {
    ClientPhase old = m_clientPhase.exchange(newPhase, std::memory_order_release);
    if (old != newPhase) {
        spdlog::info("Core: Phase {} -> {}", ClientPhaseToString(old), ClientPhaseToString(newPhase));
        m_nativeHud.LogStep("PHASE", std::string(ClientPhaseToString(old)) + " -> " + ClientPhaseToString(newPhase));
    }
}

void Core::OnLoadingGapDetected() {
    ClientPhase current = m_clientPhase.load(std::memory_order_acquire);

    // Accept from MainMenu (normal flow) or GameReady (in-game Load button,
    // detected by render_hooks as >10s gap). NOT from Startup — engine initialization
    // gaps are not save game loads.
    if (current == ClientPhase::MainMenu || current == ClientPhase::GameReady) {
        // Reset game-loaded state so OnGameLoaded() can fire again for the new save.
        // Without this, a second load would never trigger OnGameLoaded because
        // m_gameLoaded is already true from the first load.
        m_gameLoaded = false;
        m_initialEntityScanDone = false;
        m_spawnTeleportDone = false;
        m_needPollReset = true; // Reset PollForGameLoad statics on next call

        // If we were in GameReady, all cached game pointers (local AND remote)
        // are now stale. Clear everything to prevent UAF on freed game objects.
        if (current == ClientPhase::GameReady) {
            spdlog::info("Core: In-game save load — clearing ALL stale state");
            m_entityRegistry.Clear();
            m_interpolation.Clear();
            m_playerController.Reset();
            m_frameData[0].Clear();
            m_frameData[1].Clear();
            m_pipelineStarted = false;
            game::ResetProbeState();  // animClassOffset may differ between saves
        }

        // Apply faction patch BEFORE loading starts — this determines which
        // faction the player controls when the save loads.
        // Research mod approach: patch the 17-byte faction string in .rdata
        // so the game assigns us the correct player faction.
        if (m_lobbyManager.HasFaction()) {
            if (m_lobbyManager.ApplyFactionPatch()) {
                spdlog::info("Core: Faction string patched before save load");
                m_nativeHud.LogStep("LOBBY", "Faction patched: " + m_lobbyManager.GetFactionString());
            } else {
                spdlog::warn("Core: Faction patch FAILED — player may get wrong faction");
            }
        }

        // Disable CharacterCreate hook BEFORE loading starts.
        // The 130+ character creates during load would fire through the
        // MovRaxRsp wrapper and corrupt its global RSP slots (DEP crash).
        // The hook is re-enabled in OnGameLoaded() after the burst completes.
        entity_hooks::SuspendForDisconnect();

        TransitionTo(ClientPhase::Loading);
    }
}

// SEH-protected CharacterIterator count (no C++ objects with destructors in __try)
static int SEH_CharacterIteratorCount() {
    __try {
        game::CharacterIterator iter;
        return iter.Count();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        static int s_crashCount = 0;
        if (++s_crashCount <= 5) {
            OutputDebugStringA("KMP: SEH_CharacterIteratorCount crashed (SEH caught)\n");
        }
        return -1;
    }
}

void Core::PollForGameLoad() {
    if (m_gameLoaded) return;
    // Poll during Loading (normal), but also Startup/MainMenu as fallback.
    // Loading gap detection can miss fast loads (user clicks Continue immediately),
    // but globals + CharacterIterator still detect game loaded from any phase.
    ClientPhase phase = m_clientPhase.load(std::memory_order_acquire);
    if (phase == ClientPhase::Connected || phase == ClientPhase::Connecting) return;

    // Try to resolve PlayerBase/GameWorld globals.
    // During initial scan (before game loads), these point into the module or are 0.
    // After the game loads, they contain heap pointers to live game objects.
    SEH_RetryGlobalDiscovery(m_scanner, m_gameFuncs);

    // Validate: must point to heap-allocated objects (outside module image)
    uintptr_t moduleBase = m_scanner.GetBase();
    size_t moduleSize = m_scanner.GetSize();
    auto validateGlobal = [moduleBase, moduleSize](uintptr_t addr) -> bool {
        if (addr == 0) return false;
        uintptr_t val = 0;
        if (!Memory::Read(addr, val)) return false;
        if (val < 0x10000 || val >= 0x00007FFFFFFFFFFF) return false;
        if (val >= moduleBase && val < moduleBase + moduleSize) return false;
        return true;
    };

    bool playerBaseValid = validateGlobal(m_gameFuncs.PlayerBase);
    bool gameWorldValid = validateGlobal(m_gameFuncs.GameWorldSingleton);

    // Reset statics on second load (flag set by OnLoadingGapDetected)
    static int s_pollCount = 0;
    static int s_noCharCount = 0;
    if (m_needPollReset) {
        m_needPollReset = false;
        s_pollCount = 0;
        s_noCharCount = 0;
        spdlog::info("Core::PollForGameLoad — reset statics for new load");
    }

    if (!playerBaseValid && !gameWorldValid) {
        if (++s_pollCount <= 5 || s_pollCount % 10 == 0) {
            spdlog::debug("Core::PollForGameLoad — globals not ready yet (poll #{})", s_pollCount);
        }
        return;
    }

    // Set bridges so CharacterIterator can find characters
    if (playerBaseValid) game::SetResolvedPlayerBase(m_gameFuncs.PlayerBase);
    if (gameWorldValid) game::SetResolvedGameWorld(m_gameFuncs.GameWorldSingleton);

    // Try CharacterIterator — if it finds characters, the game is loaded
    int charCount = SEH_CharacterIteratorCount();
    if (charCount > 0) {
        spdlog::info("Core::PollForGameLoad — {} characters detected! Triggering OnGameLoaded",
                     charCount);
        m_nativeHud.LogStep("GAME", "Characters detected (" + std::to_string(charCount) + ") — game loaded!");
        OnGameLoaded();
    } else {
        s_noCharCount++;
        if (s_noCharCount <= 5 || s_noCharCount % 10 == 0) {
            spdlog::debug("Core::PollForGameLoad — globals valid but {} characters (poll #{})",
                         charCount, s_noCharCount);
        }

        // Fallback: if globals are valid for 60+ seconds (30+ polls at 2s interval)
        // but CharacterIterator still returns 0, assume game is loaded anyway.
        // CharacterIterator may fail to find the character list structure on Steam.
        // Raised from 5 to 30 to avoid false triggering on the character creation screen
        // (which has valid-looking globals but no actual game world).
        if (s_noCharCount >= 30) {
            spdlog::warn("Core::PollForGameLoad — CharacterIterator returned 0 for {} polls with valid globals. "
                         "Assuming game loaded (fallback).", s_noCharCount);
            m_nativeHud.LogStep("GAME", "Game assumed loaded (CharacterIterator fallback after 60s)");
            OnGameLoaded();
        }
    }
}

void Core::OnGameLoaded() {
    if (m_gameLoaded.exchange(true)) return; // Only run once

    // Transition to GameReady phase
    TransitionTo(ClientPhase::GameReady);

    m_nativeHud.LogStep("GAME", "=== Game world loaded! ===");
    OutputDebugStringA("KMP: === Core::OnGameLoaded() START ===\n");

    // ── Clear loading guard ──
    building_hooks::SetLoading(false);
    inventory_hooks::SetLoading(false);
    squad_hooks::SetLoading(false);
    faction_hooks::SetLoading(false);
    m_loadingOrch.OnGameLoaded();
    m_nativeHud.LogStep("GAME", "Loading guard cleared (hooks active)");

    // ═══ Install remaining hooks that were deferred to post-load ═══
    // Combat, inventory, faction, time, and AI hooks are already installed
    // in InitHooks. Only movement, squad, and resource hooks remain deferred.
    m_nativeHud.LogStep("HOOK", "Installing post-load hooks...");
    OutputDebugStringA("KMP: OnGameLoaded — installing post-load hooks\n");

    // Movement hooks (CharacterMoveTo — null on Steam, position polling works instead)
    if (m_gameFuncs.CharacterMoveTo) {
        if (movement_hooks::Install()) {
            m_nativeHud.LogStep("OK", "Movement hooks installed");
        } else {
            m_nativeHud.LogStep("WARN", "Movement hooks FAILED");
        }
    } else {
        m_nativeHud.LogStep("INFO", "Movement hooks skipped (position polling active)");
    }

    // Squad hooks
    if (m_gameFuncs.SquadAddMember) {
        if (squad_hooks::Install()) {
            m_nativeHud.LogStep("OK", "Squad hooks installed");
        } else {
            m_nativeHud.LogStep("WARN", "Squad hooks FAILED");
        }
    }

    // Resource hooks (Ogre VTable discovery)
    if (resource_hooks::Install()) {
        m_nativeHud.LogStep("OK", "Resource hooks installed");
    } else {
        m_nativeHud.LogStep("INFO", "Resource hooks deferred (burst-detection fallback)");
    }
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

        // Re-validate after retry — SEH_RetryGlobalDiscovery may re-resolve to same garbage
        if (m_gameFuncs.PlayerBase != 0 && !validateGlobal(m_gameFuncs.PlayerBase)) {
            spdlog::warn("Core: PlayerBase still garbage after retry — clearing");
            m_gameFuncs.PlayerBase = 0;
        }
        if (m_gameFuncs.GameWorldSingleton != 0 && !validateGlobal(m_gameFuncs.GameWorldSingleton)) {
            spdlog::warn("Core: GameWorldSingleton still garbage after retry — clearing");
            m_gameFuncs.GameWorldSingleton = 0;
        }

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
        m_spawnManager.FindModTemplates();
        if (m_spawnManager.GetModTemplateCount() > 0) {
            m_nativeHud.LogStep("MOD", "Found " + std::to_string(m_spawnManager.GetModTemplateCount()) + " mod player templates");
        }
    } else if (m_spawnManager.IsReady()) {
        // Factory captured but no manager pointer yet — still try scan
        m_nativeHud.LogStep("GAME", "Heap scan (factory only, no manager ptr)...");
        SEH_ScanGameDataHeap(m_spawnManager);
        m_nativeHud.LogStep("OK", "Heap scan done (" + std::to_string(m_spawnManager.GetTemplateCount()) + " templates)");
        m_spawnManager.FindModTemplates();
        if (m_spawnManager.GetModTemplateCount() > 0) {
            m_nativeHud.LogStep("MOD", "Found " + std::to_string(m_spawnManager.GetModTemplateCount()) + " mod player templates");
        }
    }

    // CharacterCreate hook was installed DISABLED to survive the 130+ loading burst.
    // Now that loading is complete, enable it so runtime NPC spawns capture
    // factory data + pre-call struct for the spawn system.
    if (HookManager::Get().Enable("CharacterCreate")) {
        spdlog::info("Core::OnGameLoaded — CharacterCreate hook ENABLED (safe for runtime spawns)");
        m_nativeHud.LogStep("HOOK", "CharacterCreate enabled (post-load)");
    } else {
        spdlog::warn("Core::OnGameLoaded — CharacterCreate Enable() returned false");
        m_nativeHud.LogStep("WARN", "CharacterCreate enable failed");
    }

    // ═══ DUMP ALL FUNCTIONS AND OFFSETS ═══
    {
        uintptr_t base = m_scanner.GetBase();
        auto fmtPtr = [base](const char* name, void* ptr) {
            if (ptr)
                spdlog::info("  FUNC  {:24s} = 0x{:X}  (RVA 0x{:X})", name,
                             reinterpret_cast<uintptr_t>(ptr),
                             reinterpret_cast<uintptr_t>(ptr) - base);
            else
                spdlog::info("  FUNC  {:24s} = NULL", name);
        };
        spdlog::info("=== FUNCTION DUMP (base=0x{:X}) ===", base);
        fmtPtr("CharacterSpawn",       m_gameFuncs.CharacterSpawn);
        fmtPtr("CharacterDestroy",     m_gameFuncs.CharacterDestroy);
        fmtPtr("CreateRandomSquad",    m_gameFuncs.CreateRandomSquad);
        fmtPtr("CharacterSerialise",   m_gameFuncs.CharacterSerialise);
        fmtPtr("CharacterKO",          m_gameFuncs.CharacterKO);
        fmtPtr("CharacterSetPosition", m_gameFuncs.CharacterSetPosition);
        fmtPtr("CharacterMoveTo",      m_gameFuncs.CharacterMoveTo);
        fmtPtr("ApplyDamage",          m_gameFuncs.ApplyDamage);
        fmtPtr("StartAttack",          m_gameFuncs.StartAttack);
        fmtPtr("CharacterDeath",       m_gameFuncs.CharacterDeath);
        fmtPtr("HealthUpdate",         m_gameFuncs.HealthUpdate);
        fmtPtr("CutDamageMod",         m_gameFuncs.CutDamageMod);
        fmtPtr("UnarmedDamage",        m_gameFuncs.UnarmedDamage);
        fmtPtr("MartialArtsCombat",    m_gameFuncs.MartialArtsCombat);
        fmtPtr("ZoneLoad",             m_gameFuncs.ZoneLoad);
        fmtPtr("ZoneUnload",           m_gameFuncs.ZoneUnload);
        fmtPtr("BuildingPlace",        m_gameFuncs.BuildingPlace);
        fmtPtr("BuildingDestroyed",    m_gameFuncs.BuildingDestroyed);
        fmtPtr("Navmesh",             m_gameFuncs.Navmesh);
        fmtPtr("SpawnCheck",           m_gameFuncs.SpawnCheck);
        fmtPtr("GameFrameUpdate",      m_gameFuncs.GameFrameUpdate);
        fmtPtr("TimeUpdate",           m_gameFuncs.TimeUpdate);
        fmtPtr("SaveGame",             m_gameFuncs.SaveGame);
        fmtPtr("LoadGame",             m_gameFuncs.LoadGame);
        fmtPtr("ImportGame",           m_gameFuncs.ImportGame);
        fmtPtr("CharacterStats",       m_gameFuncs.CharacterStats);
        fmtPtr("InputKeyPressed",      m_gameFuncs.InputKeyPressed);
        fmtPtr("InputMouseMoved",      m_gameFuncs.InputMouseMoved);
        fmtPtr("SquadCreate",          m_gameFuncs.SquadCreate);
        fmtPtr("SquadAddMember",       m_gameFuncs.SquadAddMember);
        fmtPtr("ItemPickup",           m_gameFuncs.ItemPickup);
        fmtPtr("ItemDrop",             m_gameFuncs.ItemDrop);
        fmtPtr("BuyItem",              m_gameFuncs.BuyItem);
        fmtPtr("FactionRelation",      m_gameFuncs.FactionRelation);
        fmtPtr("AICreate",             m_gameFuncs.AICreate);
        fmtPtr("AIPackages",           m_gameFuncs.AIPackages);
        fmtPtr("GunTurret",            m_gameFuncs.GunTurret);
        fmtPtr("GunTurretFire",        m_gameFuncs.GunTurretFire);
        fmtPtr("BuildingDismantle",    m_gameFuncs.BuildingDismantle);
        fmtPtr("BuildingConstruct",    m_gameFuncs.BuildingConstruct);
        fmtPtr("BuildingRepair",       m_gameFuncs.BuildingRepair);
        spdlog::info("  SGLTN PlayerBase           = 0x{:X} (val=0x{:X})",
                     m_gameFuncs.PlayerBase,
                     m_gameFuncs.PlayerBase ? SEH_ReadPtr(m_gameFuncs.PlayerBase) : 0);
        spdlog::info("  SGLTN GameWorldSingleton   = 0x{:X} (val=0x{:X})",
                     m_gameFuncs.GameWorldSingleton,
                     m_gameFuncs.GameWorldSingleton ? SEH_ReadPtr(m_gameFuncs.GameWorldSingleton) : 0);
        spdlog::info("  Count: {}/{} resolved", m_gameFuncs.CountResolved(), GameFunctions::TotalFunctions());

        auto& co = game::GetOffsets().character;
        auto& wo = game::GetOffsets().world;
        spdlog::info("=== OFFSET DUMP ===");
        auto fmtOff = [](const char* name, int val) {
            if (val >= 0)
                spdlog::info("  OFF   {:24s} = 0x{:03X}", name, val);
            else
                spdlog::info("  OFF   {:24s} = -1 (UNKNOWN)", name);
        };
        fmtOff("name", co.name);
        fmtOff("faction", co.faction);
        fmtOff("position", co.position);
        fmtOff("rotation", co.rotation);
        fmtOff("gameDataPtr", co.gameDataPtr);
        fmtOff("inventory", co.inventory);
        fmtOff("stats", co.stats);
        fmtOff("animClassOffset", co.animClassOffset);
        fmtOff("charMovementOffset", co.charMovementOffset);
        fmtOff("writablePosOffset", co.writablePosOffset);
        fmtOff("writablePosVecOffset", co.writablePosVecOffset);
        fmtOff("squad", co.squad);
        fmtOff("equipment", co.equipment);
        fmtOff("isPlayerControlled", co.isPlayerControlled);
        fmtOff("health", co.health);
        fmtOff("healthChain1", co.healthChain1);
        fmtOff("healthChain2", co.healthChain2);
        fmtOff("healthBase", co.healthBase);
        fmtOff("moneyChain1", co.moneyChain1);
        fmtOff("moneyChain2", co.moneyChain2);
        fmtOff("moneyBase", co.moneyBase);
        fmtOff("sceneNode", co.sceneNode);
        fmtOff("aiPackage", co.aiPackage);
        fmtOff("world.gameSpeed", wo.gameSpeed);
        fmtOff("world.characterList", wo.characterList);
        fmtOff("world.zoneManager", wo.zoneManager);
        fmtOff("world.timeOfDay", wo.timeOfDay);
        spdlog::info("=== END DUMP ===");
    }

    m_nativeHud.LogStep("GAME", "Ready! Press F1 for multiplayer menu");
    OutputDebugStringA("KMP: === Core::OnGameLoaded() COMPLETE ===\n");
}

// SEH wrapper for network thread update — must be a plain function (no C++ objects with destructors)
static int SEH_NetworkUpdate(NetworkClient* client) {
    __try {
        client->Update();
        return 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return GetExceptionCode();
    }
}

void Core::NetworkThreadFunc() {
    spdlog::info("Network thread started");
    int consecutiveCrashes = 0;

    while (m_running) {
        int exCode = SEH_NetworkUpdate(&m_client);
        if (exCode != 0) {
            consecutiveCrashes++;
            if (consecutiveCrashes <= 20) {
                spdlog::error("NetworkThread: SEH crash #{} in Update() — exception 0x{:08X}",
                              consecutiveCrashes, static_cast<unsigned>(exCode));
                OutputDebugStringA("KMP: NetworkThread SEH crash in Update()\n");
            }
            // Back off briefly after a crash to avoid tight crash loops
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        } else {
            consecutiveCrashes = 0;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    spdlog::info("Network thread stopped");
}

void Core::SendExistingEntitiesToServer() {
    // After connecting, scan existing characters and register+send them.
    // IMPORTANT: Only send characters from the player's faction (squad members).
    // Uses CharacterIterator which was already validated by PollForGameLoad.

    int count = 0;
    int skippedFaction = 0;

    // ── Resolve faction ──
    uintptr_t playerFaction = m_playerController.GetLocalFactionPtr();

    // ── CharacterIterator ──
    game::CharacterIterator iter;
    bool iteratorWorked = (iter.Count() > 0);

    uintptr_t firstPlayerCharPtr = 0;
    uintptr_t firstNpcCharPtr = 0;

    if (iteratorWorked) {
        // If faction unknown, discover it from the first character with isPlayerControlled
        if (playerFaction == 0) {
            // First pass: find a character that looks like it belongs to the player
            // (heuristic: first character in the list is often the player's)
            game::CharacterIterator factionIter;
            while (factionIter.HasNext()) {
                game::CharacterAccessor ch = factionIter.Next();
                if (!ch.IsValid()) continue;
                uintptr_t fPtr = ch.GetFactionPtr();
                if (fPtr > 0x10000 && fPtr < 0x00007FFFFFFFFFFF && (fPtr & 0x7) == 0) {
                    // Reject if within module image (not a real heap-allocated faction)
                    uintptr_t modBase = Memory::GetModuleBase();
                    if (fPtr >= modBase && fPtr < modBase + 0x4000000) continue;
                    playerFaction = fPtr;
                    m_playerController.SetLocalFactionPtr(playerFaction);
                    spdlog::info("Core: Faction discovered from CharacterIterator: 0x{:X}", playerFaction);
                    break;
                }
            }
        }

        if (playerFaction != 0) {
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
                    if (firstNpcCharPtr == 0) firstNpcCharPtr = character.GetPtr();
                    continue;
                }

                if (firstPlayerCharPtr == 0) firstPlayerCharPtr = character.GetPtr();

                void* gameObj = reinterpret_cast<void*>(character.GetPtr());
                if (m_entityRegistry.GetNetId(gameObj) != INVALID_ENTITY) continue;

                EntityID netId = m_entityRegistry.Register(gameObj, EntityType::NPC, m_localPlayerId);
                m_entityRegistry.UpdatePosition(netId, pos);

                Quat rot = character.GetRotation();
                m_entityRegistry.UpdateRotation(netId, rot);

                uintptr_t factionPtr = character.GetFactionPtr();
                uint32_t factionId = 0;
                if (factionPtr != 0) Memory::Read(factionPtr + game::GetOffsets().faction.id, factionId);

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

                // Extended state: per-limb health + alive flag
                // This ensures the server starts with correct health state rather
                // than defaulting to 100% for all limbs.
                writer.WriteU8(1); // hasExtendedState flag
                for (int bp = 0; bp < 7; bp++) {
                    float h = character.GetHealth(static_cast<BodyPart>(bp));
                    writer.WriteF32(h);
                }
                writer.WriteU8(character.IsAlive() ? 1 : 0);

                m_client.SendReliable(writer.Data(), writer.Size());
                count++;
            }
        }
    }

    // Discover isPlayerControlled offset by comparing a player char vs NPC char
    if (firstPlayerCharPtr != 0 && firstNpcCharPtr != 0) {
        game::ProbePlayerControlledOffset(firstPlayerCharPtr, firstNpcCharPtr);
    }

    if (count == 0) {
        spdlog::warn("Core: CharacterIterator found 0 squad characters (iterator={}, faction=0x{:X})",
                     iteratorWorked ? "ok" : "FAILED", playerFaction);
    }

    spdlog::info("Core: Sent {} squad characters to server (skipped {} non-squad NPCs)",
                 count, skippedFaction);

    if (count > 0) {
        m_overlay.AddSystemMessage("Syncing " + std::to_string(count) + " squad characters...");
    }
}

// ── Entity scan statics (file scope for cross-function reset) ──
// Used in OnGameTick for retry logic, reset in HandleSpawnQueue on reconnect.
static int s_entityScanRetries = 0;
static bool s_wasScanning = false;

// Keepalive timer — reset on reconnect via ResetKeepaliveTimer().
static auto s_lastKeepalive = std::chrono::steady_clock::now();

void ResetKeepaliveTimer() {
    s_lastKeepalive = std::chrono::steady_clock::now();
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

    // ── Keepalive ──
    // The server has a 10-second idle timeout. When a player stands still and
    // stops sending position updates, the server disconnects them. Send a
    // keepalive every 5 seconds (well under the timeout) to prevent this.
    // Placed before the game-loaded gate so keepalives fire even during loading.
    // s_lastKeepalive is file-scope so it can be reset on reconnect.
    {
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(now - s_lastKeepalive).count() >= 5) {
            s_lastKeepalive = now;
            PacketWriter writer;
            writer.WriteHeader(MessageType::C2S_Keepalive);
            m_client.SendReliable(writer.Data(), writer.Size());
        }
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
    WriteBreadcrumb("tick_entry", s_tickCallCount);
    SetLastCompletedStep(0);

    // ── Step 0: Force unpause ──
    // Kenshi allows pausing (Space key) which freezes the game world. In multiplayer
    // this must be prevented — the server keeps ticking regardless. Every tick, force
    // GameWorld.paused = false and write server game speed to prevent local overrides.
    {
        uintptr_t gwSingleton = game::GetResolvedGameWorld();
        if (gwSingleton != 0) {
            uintptr_t gwPtr = 0;
            if (Memory::Read(gwSingleton, gwPtr) && gwPtr > 0x10000 && gwPtr < 0x00007FFFFFFFFFFF) {
                auto& offsets = game::GetOffsets();
                // Force unpause
                uint8_t paused = 0;
                Memory::Read(gwPtr + offsets.world.paused, paused);
                if (paused != 0) {
                    uint8_t unpaused = 0;
                    Memory::Write(gwPtr + offsets.world.paused, unpaused);
                    static int s_unpauseCount = 0;
                    if (++s_unpauseCount <= 5) {
                        spdlog::info("Core: Forced unpause (multiplayer — pause disabled)");
                    }
                }
                // Force game speed to 1.0 (server-controlled; prevents local speed changes)
                float currentSpeed = 0.f;
                Memory::Read(gwPtr + offsets.world.gameSpeed, currentSpeed);
                if (currentSpeed < 0.5f || currentSpeed > 3.5f) {
                    float normalSpeed = 1.0f;
                    Memory::Write(gwPtr + offsets.world.gameSpeed, normalSpeed);
                }
            }
        }
    }

    // ── Step 1: Entity scan with retry ──
    // SendExistingEntitiesToServer may fail on first attempt if CharacterIterator
    // can't resolve PlayerBase/GameWorld. Retry every 150 ticks (~1 second) for
    // up to 30 seconds until at least one character is sent.
    if (!m_initialEntityScanDone) {
        // Reset retry counter when starting fresh (after disconnect/reconnect).
        // Without this, the static counter persists across connections and may
        // exhaust retries prematurely on the second connection.
        // s_entityScanRetries and s_wasScanning are file-scope statics
        // (declared above OnGameTick) so HandleSpawnQueue can reset them.
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
        WriteBreadcrumb("interpolation", s_tickCallCount, 2);
        SEH_InterpolationUpdate(&m_interpolation, deltaTime);
        SetLastCompletedStep(4);

        g_lastTickStep = 3; g_lastStepName = "sync_orch_tick";
        WriteBreadcrumb("sync_orch_tick", s_tickCallCount, 3);
        SEH_SyncOrchestratorTick(m_syncOrchestrator.get(), deltaTime);
        SetLastCompletedStep(6);

        g_lastTickStep = 4; g_lastStepName = "loading_orch";
        WriteBreadcrumb("loading_orch", s_tickCallCount, 4);
        SEH_LoadingOrchTick(&m_loadingOrch);

        g_lastTickStep = 5; g_lastStepName = "handle_spawns";
        WriteBreadcrumb("handle_spawns_sync", s_tickCallCount, 5);
        HandleSpawnQueue();

        // Process deferred AnimClass probes (discovers physics position chain)
        game::ProcessDeferredAnimClassProbes();

        // Process deferred combat events (death/KO queued from hook context)
        combat_hooks::ProcessDeferredEvents();

        // Process deferred character discoveries (new chars found by animation hook)
        char_tracker_hooks::ProcessDeferredDiscovery();

        // Process deferred zone events (load/unload queued from hook context)
        world_hooks::ProcessDeferredZoneEvents();

        // Shared-save sync: discover characters by name, sync positions
        shared_save_sync::Update(deltaTime);

        g_lastTickStep = 6; g_lastStepName = "host_teleport";
        WriteBreadcrumb("host_teleport", s_tickCallCount, 6);
        HandleHostTeleport();
        SetLastCompletedStep(8);
        SetLastCompletedStep(9);
    } else {
        g_lastTickStep = 2; g_lastStepName = "wait_bg_work";
        WriteBreadcrumb("wait_bg_work", s_tickCallCount, 2);
        if (m_pipelineStarted) {
            m_orchestrator.WaitForFrameWork();
        }
        SetLastCompletedStep(2);

        g_lastTickStep = 3; g_lastStepName = "swap_buffers";
        WriteBreadcrumb("swap_buffers", s_tickCallCount, 3);
        if (m_pipelineStarted) {
            // WaitForFrameWork() guarantees workers are done — safe to swap
            int w = m_writeBuffer.load(std::memory_order_relaxed);
            int r = m_readBuffer.load(std::memory_order_relaxed);
            m_writeBuffer.store(r, std::memory_order_release);
            m_readBuffer.store(w, std::memory_order_release);
        }
        SetLastCompletedStep(3);

        g_lastTickStep = 4; g_lastStepName = "interpolation";
        WriteBreadcrumb("interpolation_alt", s_tickCallCount, 4);
        SEH_InterpolationUpdate(&m_interpolation, deltaTime);
        SetLastCompletedStep(4);

        g_lastTickStep = 5; g_lastStepName = "apply_remote_pos";
        WriteBreadcrumb("apply_remote_pos", s_tickCallCount, 5);
        // Direct interpolation → game character write. Always runs (no double-buffer dependency).
        // The old ApplyRemotePositions() required BackgroundInterpolate to have filled
        // the read buffer — this direct path reads interpolation results immediately.
        ApplyRemotePositionsDirect();
        SetLastCompletedStep(5);

        g_lastTickStep = 6; g_lastStepName = "poll_local_pos";
        WriteBreadcrumb("poll_local_pos", s_tickCallCount, 6);
        PollLocalPositions();

        g_lastTickStep = 7; g_lastStepName = "send_packets";
        // NOTE: SendCachedPackets() removed — PollLocalPositions() already
        // sends fresh per-entity position updates on the main thread.
        // The background thread's cached packetBytes is a duplicate; sending
        // both doubled outbound bandwidth.
        SetLastCompletedStep(6);

        g_lastTickStep = 8; g_lastStepName = "loading_orch";
        WriteBreadcrumb("loading_orch_alt", s_tickCallCount, 8);
        SEH_LoadingOrchTick(&m_loadingOrch);

        g_lastTickStep = 9; g_lastStepName = "handle_spawns";
        WriteBreadcrumb("handle_spawns", s_tickCallCount, 9);
        HandleSpawnQueue();

        // Process deferred AnimClass probes (discovers physics position chain)
        game::ProcessDeferredAnimClassProbes();

        // Process deferred combat events (death/KO queued from hook context)
        combat_hooks::ProcessDeferredEvents();

        // Process deferred character discoveries (new chars found by animation hook)
        char_tracker_hooks::ProcessDeferredDiscovery();

        // Process deferred zone events (load/unload queued from hook context)
        world_hooks::ProcessDeferredZoneEvents();

        // Shared-save sync: discover characters by name, sync positions
        shared_save_sync::Update(deltaTime);
        SetLastCompletedStep(7);

        g_lastTickStep = 10; g_lastStepName = "host_teleport";
        WriteBreadcrumb("host_teleport_alt", s_tickCallCount, 10);
        HandleHostTeleport();
        SetLastCompletedStep(8);

        g_lastTickStep = 11; g_lastStepName = "kick_bg_work";
        WriteBreadcrumb("kick_bg_work", s_tickCallCount, 11);
        m_frameData[m_writeBuffer.load()].Clear();
        KickBackgroundWork();
        m_pipelineStarted = true;
        SetLastCompletedStep(9);
    }

    // ── Step 9b: Deferred probes — DISABLED ──
    g_lastTickStep = 12; g_lastStepName = "probes_skipped";

    // ── Step 9c: Periodic faction validation for remote entities ──
    // The game reads faction+0x250 on EVERY character update tick (game+0x927E94).
    // If a remote character's faction is freed (zone unload), the game crashes.
    // Fix: periodically validate and repair faction pointers for all remote entities.
    if (s_tickCallCount % 50 == 0) { // Every ~0.5 seconds
        g_lastTickStep = 12; g_lastStepName = "faction_validate";
        auto remoteEntities = m_entityRegistry.GetRemoteEntities();
        if (!remoteEntities.empty()) {
            void* primaryChar = m_playerController.GetPrimaryCharacter();
            uintptr_t goodFaction = 0;
            if (primaryChar) {
                uintptr_t primaryPtr = reinterpret_cast<uintptr_t>(primaryChar);
                Memory::Read(primaryPtr + 0x10, goodFaction);
                if (goodFaction < 0x10000 || goodFaction >= 0x00007FFFFFFFFFFF ||
                    (goodFaction & 0x7) != 0) {
                    goodFaction = 0;
                } else {
                    uintptr_t modBase = m_scanner.GetBase();
                    size_t modSize = m_scanner.GetSize();
                    if (goodFaction >= modBase && goodFaction < modBase + modSize) {
                        goodFaction = 0;
                    }
                }
            }
            if (goodFaction != 0) {
                uintptr_t modBase = m_scanner.GetBase();
                size_t modSize = m_scanner.GetSize();
                for (EntityID eid : remoteEntities) {
                    void* gameObj = m_entityRegistry.GetGameObject(eid);
                    if (!gameObj) continue;
                    uintptr_t charPtr = reinterpret_cast<uintptr_t>(gameObj);
                    if (charPtr < 0x10000 || charPtr >= 0x00007FFFFFFFFFFF) continue;

                    int result = SEH_ValidateEntityFaction(gameObj, goodFaction, modBase, modSize);
                    if (result == 1) {
                        static int s_factionFixCount = 0;
                        if (++s_factionFixCount <= 20) {
                            spdlog::warn("Core: Fixed stale faction for remote entity {} "
                                         "(char=0x{:X})", eid, charPtr);
                        }
                    } else if (result == -1) {
                        m_entityRegistry.SetGameObject(eid, nullptr);
                    }
                }
            }
        }
    }

    // ── Step 10: Diagnostics ──
    g_lastTickStep = 13; g_lastStepName = "diagnostics";
    WriteBreadcrumb("diagnostics", s_tickCallCount, 13);
    UpdateDiagnostics(deltaTime);
    SetLastCompletedStep(10);

    // ── Step 11: Pipeline debugger ──
    g_lastTickStep = 14; g_lastStepName = "pipeline_orch";
    WriteBreadcrumb("pipeline_orch", s_tickCallCount, 14);
    m_pipelineOrch.Tick(deltaTime);
    SetLastCompletedStep(11);

    g_lastTickStep = 15; g_lastStepName = "tick_complete";
    WriteBreadcrumb("tick_complete", s_tickCallCount, 15);
}

// ════════════════════════════════════════════════════════════════════════════
// Staged Pipeline Methods
// ════════════════════════════════════════════════════════════════════════════

// SEH-protected faction validation for a single remote entity.
// Returns: 0=ok, 1=fixed, -1=freed (should unlink).
// Only POD types inside __try — safe with MSVC structured exceptions.
//
// IMPORTANT: Only fixes truly stale/freed faction pointers. Does NOT overwrite
// valid factions with the local player's faction. Remote characters need to be
// on a DIFFERENT faction for PvP combat and theft to work. If we assign the
// local player's faction, the game treats them as allies and crashes on
// "attack unprovoked" and theft interactions.
static int SEH_ValidateEntityFaction(void* gameObj, uintptr_t goodFaction,
                                      uintptr_t modBase, size_t modSize) {
    __try {
        uintptr_t charPtr = reinterpret_cast<uintptr_t>(gameObj);
        uintptr_t faction = 0;
        Memory::Read(charPtr + 0x10, faction);

        bool isStale = false;
        if (faction == 0 || faction < 0x10000 || faction >= 0x00007FFFFFFFFFFF ||
            (faction & 0x7) != 0) {
            isStale = true;
        } else {
            // Check if faction object's vtable is still valid (not freed)
            uintptr_t vtable = 0;
            Memory::Read(faction, vtable);
            // Vtable should point into .rdata (within the game module)
            if (vtable == 0 || vtable < modBase || vtable >= modBase + modSize) {
                isStale = true;
            }
        }
        if (isStale) {
            // Only fix if the pointer is truly invalid (freed/null/out-of-range).
            // Use goodFaction as a last resort to prevent crash on faction+0x250 read.
            Memory::Write(charPtr + 0x10, goodFaction);
            return 1;
        }
        return 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1; // Character was freed
    }
}

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
            // Validate quaternion before writing — prevents Ogre crash where
            // a quaternion w=1.0 (0x3F800000) was read as a pointer.
            // NaN/Inf check + magnitude sanity check.
            bool quatValid = true;
            float magSq = rot.w * rot.w + rot.x * rot.x + rot.y * rot.y + rot.z * rot.z;
            if (std::isnan(magSq) || std::isinf(magSq) || magSq < 0.5f || magSq > 1.5f) {
                quatValid = false;
            }
            if (std::isnan(rot.w) || std::isnan(rot.x) || std::isnan(rot.y) || std::isnan(rot.z) ||
                std::isinf(rot.w) || std::isinf(rot.x) || std::isinf(rot.y) || std::isinf(rot.z)) {
                quatValid = false;
            }

            if (quatValid) {
                uintptr_t charPtr = reinterpret_cast<uintptr_t>(gameObj);

                // Safety: read current value first — if the existing value at +0x58
                // looks like a pointer (>0x10000), DON'T overwrite it. It might be
                // a SceneNode* or other Ogre pointer, not a quaternion.
                uintptr_t existingVal = 0;
                Memory::Read(charPtr + offsets.rotation, existingVal);
                bool looksLikePointer = (existingVal > 0x10000 && existingVal < 0x00007FFFFFFFFFFF
                                          && (existingVal & 0x3) == 0);
                if (!looksLikePointer) {
                    Memory::Write(charPtr + offsets.rotation, rot);
                } else {
                    static int s_skipCount = 0;
                    if (++s_skipCount <= 5) {
                        spdlog::warn("SEH_WritePositionRotation: SKIPPED rotation write — "
                                     "char+0x{:X} = 0x{:X} looks like pointer, not quaternion",
                                     offsets.rotation, existingVal);
                    }
                }
            }
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

// Validate faction pointer on a spawned character. Only writes if the existing
// faction is STALE (null, freed, or invalid vtable). Does NOT overwrite valid
// factions — remote characters must stay on a different faction from the local
// player so that PvP combat ("attack unprovoked") and theft mechanics work.
// The game crashes at game+0x927E94 if faction+0x250 is unreadable.
static bool SEH_FixUpFaction_Core(void* spawnedChar) {
    __try {
        uintptr_t spawnedPtr = reinterpret_cast<uintptr_t>(spawnedChar);

        // First check: is the character's CURRENT faction valid?
        uintptr_t currentFaction = 0;
        Memory::Read(spawnedPtr + 0x10, currentFaction);

        bool currentIsValid = false;
        if (currentFaction != 0 && currentFaction >= 0x10000 &&
            currentFaction < 0x00007FFFFFFFFFFF && (currentFaction & 0x7) == 0) {
            uintptr_t vtable = 0;
            Memory::Read(currentFaction, vtable);
            uintptr_t modBase = Core::Get().GetScanner().GetBase();
            size_t modSize = Core::Get().GetScanner().GetSize();
            if (vtable != 0 && vtable >= modBase && vtable < modBase + modSize) {
                currentIsValid = true;
            }
        }

        // If current faction is valid, leave it alone. The mod template's faction
        // or the NPC's original faction is fine — it gives remote characters a
        // different allegiance so combat/theft interactions work properly.
        if (currentIsValid) {
            return false; // No fix needed
        }

        // Current faction is stale/freed — need a replacement to prevent crash.
        // Use fallback faction (any valid faction seen during character creation).
        uintptr_t faction = entity_hooks::GetFallbackFaction();

        if (faction == 0 || faction < 0x10000 || faction > 0x00007FFFFFFFFFFF) {
            faction = entity_hooks::GetEarlyPlayerFaction();
        }

        if (faction == 0 || faction < 0x10000 || faction > 0x00007FFFFFFFFFFF) {
            faction = Core::Get().GetPlayerController().GetLocalFactionPtr();
        }

        if (faction == 0 || faction < 0x10000 || faction > 0x00007FFFFFFFFFFF) {
            return false; // No valid faction available, leave as-is
        }

        // Validate the replacement faction
        uintptr_t vtable = 0;
        Memory::Read(faction, vtable);
        if (vtable == 0) return false;

        Memory::Write(spawnedPtr + 0x10, faction);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
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
    bool allOk = true;

    // 1. Teleport to desired position
    __try {
        game::CharacterAccessor accessor(character);
        if (pos.x != 0.f || pos.y != 0.f || pos.z != 0.f) {
            accessor.WritePosition(pos);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        OutputDebugStringA("KMP: FallbackPostSpawn — WritePosition AV\n");
        allOk = false;
    }

    // 2. Set name + faction
    __try {
        Core::Get().GetPlayerController().OnRemoteCharacterSpawned(
            netId, character, owner);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        OutputDebugStringA("KMP: FallbackPostSpawn — OnRemoteCharacterSpawned AV\n");
        allOk = false;
    }

    // 3. Mark as remote-controlled (AI decisions overridden)
    __try {
        ai_hooks::MarkRemoteControlled(character);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        OutputDebugStringA("KMP: FallbackPostSpawn — MarkRemoteControlled AV\n");
        allOk = false;
    }

    // 4. Squad injection DISABLED — activePlatoon resolution picks up code-section
    //    pointers on Steam builds → WRITE AV at game+0xE85340 → cascading crash.
    //    Remote characters are visible in the world without squad injection.

    // 5. WritePlayerControlled DISABLED — not needed for visibility, and can crash
    //    if the character's internal state isn't fully initialized.

    // 6. Schedule deferred AnimClass probe
    __try {
        game::ScheduleDeferredAnimClassProbe(
            reinterpret_cast<uintptr_t>(character));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        OutputDebugStringA("KMP: FallbackPostSpawn — AnimClassProbe AV\n");
        allOk = false;
    }

    if (!allOk) {
        static int s_crashCount = 0;
        if (++s_crashCount <= 10) {
            char buf[256];
            sprintf_s(buf, "KMP: SEH_FallbackPostSpawnSetup partial failure for entity %u "
                      "char=0x%p (surviving steps completed)\n",
                      netId, character);
            OutputDebugStringA(buf);
        }
        spdlog::error("Core: Fallback post-spawn setup partial failure for entity {} char=0x{:X}",
                      netId, reinterpret_cast<uintptr_t>(character));
    }
    return allOk;
}

void Core::ApplyRemotePositions() {
    auto& readFrame = m_frameData[m_readBuffer.load()];
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

    // CRITICAL: Log when we have remote entities with positions but no game objects.
    // This means SpawnCharacterDirect hasn't run yet or failed for these entities.
    // Without this warning, "players can't see each other" is impossible to diagnose.
    static int s_noObjWarnings = 0;
    static auto s_lastNoObjLog = std::chrono::steady_clock::time_point{};
    if (noObj > 0 && applied == 0) {
        auto now = std::chrono::steady_clock::now();
        auto sinceLog = std::chrono::duration_cast<std::chrono::seconds>(now - s_lastNoObjLog);
        if (s_noObjWarnings < 10 || sinceLog.count() >= 5) {
            spdlog::warn("Core::ApplyRemotePositions: {} remote entities have NO game object — "
                         "positions arriving but characters not spawned yet! "
                         "(spawns pending={}, spawnReady={}, gate={})",
                         noObj, m_spawnManager.GetPendingSpawnCount(),
                         m_spawnManager.IsReady() && m_spawnManager.HasPreCallData(),
                         m_loadingOrch.GetSpawnBlockReason());
            s_noObjWarnings++;
            s_lastNoObjLog = now;
        }
    }
}

// ── Direct remote position application (bypass double-buffer pipeline) ──
// Reads interpolation results directly on the main thread and writes them
// to game characters. This eliminates timing issues with the double-buffer
// swap and ensures remote characters are updated every frame.
void Core::ApplyRemotePositionsDirect() {
    auto remoteEntities = m_entityRegistry.GetRemoteEntities();
    if (remoteEntities.empty()) return;

    float now = SessionTime();
    static int s_directApplyTotal = 0;
    static int s_directNoObjTotal = 0;
    int applied = 0;
    int noObj = 0;

    for (EntityID remoteId : remoteEntities) {
        void* gameObj = m_entityRegistry.GetGameObject(remoteId);
        if (!gameObj) {
            noObj++;
            continue;
        }

        // Validate gameObj pointer
        uintptr_t objAddr = reinterpret_cast<uintptr_t>(gameObj);
        if (objAddr < 0x10000 || objAddr >= 0x00007FFFFFFFFFFF || (objAddr & 0x7) != 0) {
            m_entityRegistry.SetGameObject(remoteId, nullptr);
            continue;
        }

        Vec3 pos;
        Quat rot;
        uint8_t moveSpeed = 0, animState = 0;
        if (m_interpolation.GetInterpolated(remoteId, now, pos, rot, moveSpeed, animState)) {
            if (SEH_WritePositionRotation(gameObj, pos, rot)) {
                m_entityRegistry.UpdatePosition(remoteId, pos);
                m_entityRegistry.UpdateRotation(remoteId, rot);
                applied++;
            } else {
                // Bad game object — unlink so we stop crashing
                m_entityRegistry.SetGameObject(remoteId, nullptr);
            }
        }
    }

    s_directApplyTotal += applied;
    s_directNoObjTotal += noObj;

    if (applied > 0 && (s_directApplyTotal <= 5 || s_directApplyTotal % 200 == 0)) {
        spdlog::info("Core::ApplyRemotePositionsDirect: applied {} this frame (total={}, noObj={})",
                     applied, s_directApplyTotal, s_directNoObjTotal);
    }

    // Warn when positions are arriving but characters aren't spawned
    static int s_noObjWarnings = 0;
    static auto s_lastNoObjLog = std::chrono::steady_clock::time_point{};
    if (noObj > 0 && applied == 0) {
        auto nowClock = std::chrono::steady_clock::now();
        auto sinceLog = std::chrono::duration_cast<std::chrono::seconds>(nowClock - s_lastNoObjLog);
        if (s_noObjWarnings < 10 || sinceLog.count() >= 5) {
            spdlog::warn("Core::ApplyRemotePositionsDirect: {} remote entities have NO game object — "
                         "spawns pending={}, factory={}",
                         noObj, m_spawnManager.GetPendingSpawnCount(),
                         m_spawnManager.IsReady());
            s_noObjWarnings++;
            s_lastNoObjLog = nowClock;
        }
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

        // Validate gameObj pointer: must be aligned and in reasonable user-space range
        uintptr_t objAddr = reinterpret_cast<uintptr_t>(gameObj);
        if (objAddr < 0x10000 || objAddr >= 0x00007FFFFFFFFFFF || (objAddr & 0x7) != 0) {
            m_entityRegistry.SetGameObject(netId, nullptr);
            spdlog::warn("PollLocalPositions: Invalid gameObj 0x{:X} for entity {} — unlinked", objAddr, netId);
            continue;
        }

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
    auto& readFrame = m_frameData[m_readBuffer.load()];
    if (!readFrame.ready || readFrame.packetBytes.empty()) return;

    m_client.SendUnreliable(readFrame.packetBytes.data(), readFrame.packetBytes.size());
}

void Core::HandleHostTeleport() {
    if (!HasHostSpawnPoint() || m_spawnTeleportDone || m_isHost) return;

    // Snapshot host spawn point (mutex-protected)
    Vec3 hostPos = GetHostSpawnPoint();

    if (!m_hostTpTimerStarted) {
        m_hostTpTimerStarted = true;
        m_hostTpTimer = std::chrono::steady_clock::now();
        spdlog::info("Core: Host spawn point received ({:.1f}, {:.1f}, {:.1f}), "
                     "waiting 2s before teleport",
                     hostPos.x, hostPos.y, hostPos.z);
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

        Vec3 spawnPos = hostPos;
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
                     teleported, hostPos.x, hostPos.y, hostPos.z);
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
    if (!AssetFacilitator::Get().CanSpawn() && !m_forceSpawnBypass.load()) {
        // Log gate reason once per second when spawns are pending
        static auto s_lastGateLog = std::chrono::steady_clock::time_point{};
        static auto s_gateBlockStart = std::chrono::steady_clock::time_point{};
        static bool s_gateLoggedOnce = false;
        static bool s_gateTimerStarted = false;
        size_t queueSize = m_spawnManager.GetPendingSpawnCount();
        if (queueSize > 0) {
            auto now = std::chrono::steady_clock::now();
            if (!s_gateTimerStarted) {
                s_gateBlockStart = now;
                s_gateTimerStarted = true;
            }
            auto sinceLastLog = std::chrono::duration_cast<std::chrono::seconds>(now - s_lastGateLog);
            auto gateBlockDuration = std::chrono::duration_cast<std::chrono::seconds>(now - s_gateBlockStart);
            if (!s_gateLoggedOnce || sinceLastLog.count() >= 5) {
                std::string reason = m_loadingOrch.GetSpawnBlockReason();
                spdlog::warn("Core: HandleSpawnQueue BLOCKED — {} pending, gate: {} (blocked {}s)",
                             queueSize, reason, gateBlockDuration.count());
                m_nativeHud.LogStep("SPAWN", "Gate blocked: " + reason);
                s_lastGateLog = now;
                s_gateLoggedOnce = true;
            }
            // PLAYTEST BYPASS: After 8 seconds of blocking, force through.
            // The game is loaded — the gate is being too conservative.
            if (gateBlockDuration.count() >= 8) {
                spdlog::warn("Core: FORCE BYPASS — spawn gate blocked for {}s, forcing spawn through!",
                             gateBlockDuration.count());
                m_nativeHud.AddSystemMessage("Spawn gate bypassed (timeout) — forcing spawn...");
                s_gateTimerStarted = false;
                // Fall through to spawn logic below
            } else {
                return;
            }
        } else {
            s_gateTimerStarted = false;
        }
        if (queueSize == 0) return;
    }

    // Reset statics on reconnect (flag set by SetConnected(false))
    static bool heapScanned = false;
    static int heapScanAttempts = 0;
    static auto s_lastHeapScan = std::chrono::steady_clock::time_point{};
    static auto s_lastSpawnLog = std::chrono::steady_clock::now();
    static auto s_firstPendingTime = std::chrono::steady_clock::time_point{};
    static bool s_hasPendingTimer = false;
    static int s_directSpawnAttempts = 0;
    static bool s_shownWaitingMsg = false;
    static bool s_shownTimeoutMsg = false;
    static bool s_retriedHookEnable = false;
    static int64_t s_lastNotReadyLog = 0;
    static auto s_lastDirectAttempt = std::chrono::steady_clock::time_point{};

    if (m_needSpawnQueueReset) {
        m_needSpawnQueueReset = false;
        heapScanned = false;
        heapScanAttempts = 0;
        s_lastHeapScan = std::chrono::steady_clock::time_point{};
        s_hasPendingTimer = false;
        s_directSpawnAttempts = 0;
        s_shownWaitingMsg = false;
        s_shownTimeoutMsg = false;
        s_retriedHookEnable = false;
        s_lastNotReadyLog = 0;
        s_wasScanning = false;
        s_entityScanRetries = 0;
        s_lastDirectAttempt = std::chrono::steady_clock::time_point{};
        spdlog::info("Core::HandleSpawnQueue: Reset statics for new connection");
    }

    // Heap scan — runs once, then retries if mod templates weren't found.
    // The first scan may fail if GameDataManager wasn't captured yet (hook was
    // disabled during loading). After ResumeForNetwork + first NPC creation,
    // m_managerPointer gets set, enabling a successful re-scan.
    bool needsScan = !heapScanned ||
        (heapScanned && m_spawnManager.GetModTemplateCount() == 0 && heapScanAttempts < 5);
    // Cooldown: don't re-scan more than once every 5 seconds
    auto timeSinceLastScan = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - s_lastHeapScan);
    if (needsScan && m_spawnManager.IsReady() && timeSinceLastScan.count() >= 5) {
        if (m_spawnManager.GetManagerPointer() != 0 || m_spawnManager.GetTemplateCount() < 10) {
            heapScanAttempts++;
            s_lastHeapScan = std::chrono::steady_clock::now();
            spdlog::info("Core: Triggering GameData heap scan #{} (manager=0x{:X}, templates={})...",
                         heapScanAttempts, m_spawnManager.GetManagerPointer(),
                         m_spawnManager.GetTemplateCount());
            SEH_ScanGameDataHeap(m_spawnManager);
            m_spawnManager.FindModTemplates();
            heapScanned = true;
            spdlog::info("Core: Heap scan #{} complete, {} templates available ({} mod templates)",
                         heapScanAttempts, m_spawnManager.GetTemplateCount(),
                         m_spawnManager.GetModTemplateCount());
        }
    }

    // Spawn queue fallback logic
    size_t pending = m_spawnManager.GetPendingSpawnCount();

    if (pending > 0 && !s_hasPendingTimer) {
        s_firstPendingTime = std::chrono::steady_clock::now();
        s_hasPendingTimer = true;
        s_shownWaitingMsg = false;
        s_shownTimeoutMsg = false;
        spdlog::info("Core: {} spawn(s) queued — spawning remote player(s)...", pending);
        m_nativeHud.LogStep("GAME", std::to_string(pending) + " spawn(s) queued");
        m_nativeHud.AddSystemMessage("Spawning remote player...");
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

        // At 10s, show helpful message (direct spawn should work by 2s)
        if (pendingDuration.count() >= 10 && !s_shownWaitingMsg) {
            s_shownWaitingMsg = true;
            m_nativeHud.AddSystemMessage("Spawning taking longer than expected...");
            m_nativeHud.LogStep("GAME", "Spawn delayed 10s+");
        }

        // At 30s, show timeout warning
        if (pendingDuration.count() >= 30 && !s_shownTimeoutMsg) {
            s_shownTimeoutMsg = true;
            spdlog::warn("Core: Spawn queue waiting 30s+ — check kenshi-online.mod is in load order.");
            m_nativeHud.AddSystemMessage("Spawn timeout! Ensure kenshi-online.mod is loaded.");
            m_nativeHud.LogStep("WARN", "Spawn timeout (30s) — check mod load order");
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

    // ═══ DIRECT SPAWN (mod template + createRandomChar immediate fallback) ═══
    // Try mod template spawn ASAP (2s for heap scan). If that fails, immediately
    // try createRandomChar as fallback (wrong appearance but functional).
    // No more "walk near NPCs" — direct spawn is the primary path.
    if (pending > 0 && s_hasPendingTimer) {
        auto pendingDuration = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - s_firstPendingTime);

        bool hasModTemplates = m_spawnManager.GetModTemplateCount() > 0;
        bool hasFactory = m_spawnManager.IsReady(); // factory captured

        // Try direct spawn after 2s (gives heap scan time to find mod templates).
        // CallFactoryCreate uses RootObjectFactory::create which builds a fresh
        // request struct from the mod GameData — no stale pointers.
        auto timeSinceLastAttempt = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - s_lastDirectAttempt);
        if (hasFactory && pendingDuration.count() >= 2
            && timeSinceLastAttempt.count() >= 1) {
            s_lastDirectAttempt = std::chrono::steady_clock::now();
            SpawnRequest spawnReq;
            if (m_spawnManager.PopNextSpawn(spawnReq)) {
                // ── Per-player spawn cap ──
                // Only spawn 1 character per remote player to prevent squad panel flooding.
                // The remote player's primary character is sufficient for co-op gameplay.
                // Additional squad members are tracked as registry-only ghosts (position synced
                // but no game object) to keep the world clean.
                static constexpr int MAX_DIRECT_SPAWNS_PER_PLAYER = 1;
                static std::unordered_map<PlayerID, int> s_directSpawnsPerPlayer;
                if (s_directSpawnsPerPlayer[spawnReq.owner] >= MAX_DIRECT_SPAWNS_PER_PLAYER) {
                    // Already spawned enough for this player — drop remaining requests silently.
                    // The entity stays in the registry (position tracked) but has no game object.
                    spdlog::debug("Core: Skipping spawn for entity {} — player {} already has {} char(s)",
                                  spawnReq.netId, spawnReq.owner, s_directSpawnsPerPlayer[spawnReq.owner]);
                    // Don't requeue — just let it go
                } else {

                // Map owner PlayerID to mod template slot (0-based).
                int templateCount = m_spawnManager.GetModTemplateCount();
                int modSlot = 0;
                if (templateCount > 0 && spawnReq.owner > 0) {
                    modSlot = (static_cast<int>(spawnReq.owner) - 1) % templateCount;
                }
                if (modSlot < 0 || modSlot >= templateCount) modSlot = 0;

                void* newChar = nullptr;
                bool usedModTemplate = false;

                // ── PATH 1: Mod template (preferred — correct appearance) ──
                if (hasModTemplates) {
                    spdlog::info("Core: MOD TEMPLATE SPAWN for entity {} owner={} slot={} "
                                 "pos=({:.1f},{:.1f},{:.1f})",
                                 spawnReq.netId, spawnReq.owner, modSlot,
                                 spawnReq.position.x, spawnReq.position.y, spawnReq.position.z);

                    newChar = m_spawnManager.SpawnCharacterDirect(&spawnReq.position, modSlot);
                    if (newChar) usedModTemplate = true;
                }

                // ── PATH 2: createRandomChar (immediate fallback — wrong appearance) ──
                if (!newChar) {
                    spdlog::info("Core: createRandomChar FALLBACK for entity {} owner={} "
                                 "(modTemplate {})",
                                 spawnReq.netId, spawnReq.owner,
                                 hasModTemplates ? "failed" : "not available");

                    newChar = entity_hooks::CallFactoryCreateRandom(m_spawnManager.GetFactory());
                }

                uintptr_t newCharAddr = reinterpret_cast<uintptr_t>(newChar);
                if (newChar && newCharAddr > 0x10000 && newCharAddr < 0x00007FFFFFFFFFFF
                    && (newCharAddr & 0x7) == 0) {
                    spdlog::info("Core: Spawn SUCCESS — char 0x{:X} for entity {} (modTemplate={})",
                                 newCharAddr, spawnReq.netId, usedModTemplate);

                    // Only apply faction fix for non-mod-template spawns (random chars).
                    // Mod template characters have persistent factions from kenshi-online.mod
                    // that are always loaded. Writing the LOCAL player's faction causes them
                    // to appear in the squad panel, flooding it and crashing the game.
                    if (!usedModTemplate) {
                        SEH_FixUpFaction_Core(newChar);
                    }
                    m_entityRegistry.SetGameObject(spawnReq.netId, newChar);
                    m_entityRegistry.UpdatePosition(spawnReq.netId, spawnReq.position);
                    SEH_FallbackPostSpawnSetup(newChar, spawnReq.netId, spawnReq.owner, spawnReq.position);

                    s_directSpawnAttempts++;
                    s_directSpawnsPerPlayer[spawnReq.owner]++;
                    if (usedModTemplate) {
                        m_nativeHud.AddSystemMessage("Remote player spawned!");
                    } else {
                        m_nativeHud.AddSystemMessage("Remote player spawned (fallback appearance)");
                    }
                    m_nativeHud.LogStep("OK", "Entity " + std::to_string(spawnReq.netId) + " spawned");
                } else {
                    spdlog::warn("Core: Both spawn paths returned null for entity {} (attempt {})",
                                 spawnReq.netId, spawnReq.retryCount);
                    spawnReq.retryCount++;
                    if (spawnReq.retryCount < MAX_SPAWN_RETRIES) {
                        m_spawnManager.RequeueSpawn(spawnReq);
                    } else {
                        spdlog::error("Core: Entity {} exceeded max retries ({}), removing",
                                      spawnReq.netId, MAX_SPAWN_RETRIES);
                        m_entityRegistry.Unregister(spawnReq.netId);
                        m_nativeHud.AddSystemMessage("Failed to spawn remote player after retries.");
                    }
                }

                } // end spawn cap else
            }
        } else if (!hasFactory) {
            // Factory not captured yet — try re-enabling CharacterCreate hook.
            if (!s_retriedHookEnable && pendingDuration.count() >= 5) {
                s_retriedHookEnable = true;
                spdlog::warn("Core: Factory not captured after 5s — re-enabling CharacterCreate hook");
                entity_hooks::ResumeForNetwork();
                m_nativeHud.LogStep("SPAWN", "Re-enabling CharacterCreate hook...");
            }
            if (pendingDuration.count() / 5 != s_lastNotReadyLog) {
                s_lastNotReadyLog = pendingDuration.count() / 5;
                spdlog::warn("Core: {} spawns pending for {}s but factory not ready "
                             "(factory={}, templates={}, modTemplates={})",
                             pending, pendingDuration.count(),
                             m_spawnManager.IsReady(),
                             m_spawnManager.GetTemplateCount(), m_spawnManager.GetModTemplateCount());
            }
        }
    }

    // Clear force bypass flag after processing
    if (m_forceSpawnBypass.load()) {
        m_forceSpawnBypass.store(false);
        spdlog::info("Core: Force spawn bypass cleared");
    }

    // ═══ STUCK ENTITY CLEANUP ═══
    // Detect remote entities registered but never spawned (stuck in Spawning state).
    // This catches entities that fell through the cracks — e.g., spawn queue drained
    // but SetGameObject was never called.
    static auto s_lastStuckCheck = std::chrono::steady_clock::now();
    auto sinceStuckCheck = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - s_lastStuckCheck);
    if (sinceStuckCheck.count() >= 10) {
        s_lastStuckCheck = std::chrono::steady_clock::now();
        auto remoteIds = m_entityRegistry.GetRemoteEntities();
        for (EntityID rid : remoteIds) {
            auto info = m_entityRegistry.GetInfoCopy(rid);
            if (info && info->state == EntityState::Spawning && info->gameObject == nullptr) {
                // If spawn queue is empty, this entity is truly orphaned
                if (m_spawnManager.GetPendingSpawnCount() == 0) {
                    spdlog::warn("Core: Stuck entity {} (owner={}) in Spawning state with no "
                                 "game object and empty spawn queue — unregistering",
                                 rid, info->ownerPlayerId);
                    m_entityRegistry.Unregister(rid);
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
    auto& writeFrame = m_frameData[m_writeBuffer.load()];

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
    auto& writeFrame = m_frameData[m_writeBuffer.load()];

    auto remoteEntities = m_entityRegistry.GetRemoteEntities();
    float now = SessionTime();

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

void Core::ForceSpawnRemotePlayers() {
    if (!m_connected) {
        m_nativeHud.AddSystemMessage("Not connected to a server.");
        return;
    }
    size_t pending = m_spawnManager.GetPendingSpawnCount();
    if (pending == 0) {
        m_nativeHud.AddSystemMessage("No pending spawns in queue.");
        // Check for remote entities without game objects
        auto remoteEntities = m_entityRegistry.GetRemoteEntities();
        int noObj = 0;
        for (auto eid : remoteEntities) {
            if (!m_entityRegistry.GetGameObject(eid)) noObj++;
        }
        if (noObj > 0) {
            m_nativeHud.AddSystemMessage(std::to_string(noObj) + " remote entities without game objects — re-queuing...");
            for (auto eid : remoteEntities) {
                if (!m_entityRegistry.GetGameObject(eid)) {
                    auto infoCopy = m_entityRegistry.GetInfoCopy(eid);
                    if (infoCopy) {
                        SpawnRequest req;
                        req.netId = eid;
                        req.owner = infoCopy->ownerPlayerId;
                        req.type = infoCopy->type;
                        req.position = infoCopy->lastPosition;
                        m_spawnManager.QueueSpawn(req);
                    }
                }
            }
        }
        return;
    }
    spdlog::info("Core::ForceSpawnRemotePlayers: {} pending spawns, forcing bypass", pending);
    m_nativeHud.AddSystemMessage("Force-spawning " + std::to_string(pending) + " remote player(s)...");
    m_forceSpawnBypass.store(true);
    // HandleSpawnQueue will run next tick with the bypass active
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
        auto infoCopy = m_entityRegistry.GetInfoCopy(remoteId);
        if (!infoCopy) continue;

        Vec3 rPos = infoCopy->lastPosition;
        if (rPos.x == 0.f && rPos.y == 0.f && rPos.z == 0.f) continue;

        float dist = localPos.DistanceTo(rPos);
        if (dist < bestDist) {
            bestDist = dist;
            bestPos = rPos;
            bestId = remoteId;
            bestOwner = infoCopy->ownerPlayerId;
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
