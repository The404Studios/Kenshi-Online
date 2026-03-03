#include "injector.h"
#include "process.h"
#include <Windows.h>
#include <CommCtrl.h>
#include <shlobj.h>
#include <shellapi.h>
#include <Shlwapi.h>
#include <string>
#include <sstream>

#pragma comment(lib, "shlwapi.lib")

#pragma comment(lib, "comctl32.lib")
#pragma comment(linker, "\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

// ── Window controls ──
static HWND g_hwnd;
static HWND g_editGamePath;
static HWND g_editServerAddr;
static HWND g_editServerPort;
static HWND g_editPlayerName;
static HWND g_btnPlay;
static HWND g_btnBrowse;
static HWND g_statusText;

#define IDC_GAMEPATH    101
#define IDC_SERVERADDR  102
#define IDC_SERVERPORT  103
#define IDC_PLAYERNAME  104
#define IDC_PLAY        105
#define IDC_BROWSE      106
#define IDC_STATUS      107

static std::wstring s_gamePath;

void UpdateStatus(const wchar_t* text) {
    SetWindowTextW(g_statusText, text);
}

void OnPlay() {
    wchar_t gamePath[MAX_PATH], serverAddr[128], serverPort[8], playerName[32];
    GetWindowTextW(g_editGamePath, gamePath, MAX_PATH);
    GetWindowTextW(g_editServerAddr, serverAddr, 128);
    GetWindowTextW(g_editServerPort, serverPort, 8);
    GetWindowTextW(g_editPlayerName, playerName, 32);

    std::wstring gameDir(gamePath);

    // Validate game path
    std::wstring exeCheck = gameDir + L"\\kenshi_x64.exe";
    if (!PathFileExistsW(exeCheck.c_str())) {
        UpdateStatus(L"Error: kenshi_x64.exe not found at the specified path");
        return;
    }

    // 1. Copy KenshiMP.Core.dll to the Kenshi directory
    UpdateStatus(L"Copying KenshiMP.Core.dll...");
    if (!kmp::CopyPluginDll(gameDir)) {
        // Check if it already exists (maybe from a previous install)
        std::wstring existingDll = gameDir + L"\\KenshiMP.Core.dll";
        if (!PathFileExistsW(existingDll.c_str())) {
            UpdateStatus(L"Error: KenshiMP.Core.dll not found. Place it next to the injector.");
            return;
        }
        // DLL already exists, continue
    }

    // 2. Install the plugin line in Plugins_x64.cfg
    UpdateStatus(L"Installing Ogre plugin...");
    if (!kmp::InstallOgrePlugin(gameDir)) {
        UpdateStatus(L"Failed to modify Plugins_x64.cfg");
        return;
    }

    // 3. Write connection config (writes to %APPDATA%/KenshiMP/client.json)
    char addrA[128], portA[8], nameA[32];
    WideCharToMultiByte(CP_UTF8, 0, serverAddr, -1, addrA, sizeof(addrA), nullptr, nullptr);
    WideCharToMultiByte(CP_UTF8, 0, serverPort, -1, portA, sizeof(portA), nullptr, nullptr);
    WideCharToMultiByte(CP_UTF8, 0, playerName, -1, nameA, sizeof(nameA), nullptr, nullptr);
    kmp::WriteConnectConfig(addrA, portA, nameA);

    // 4. Launch the game
    UpdateStatus(L"Launching Kenshi via Steam...");
    if (!kmp::LaunchKenshi(gameDir)) {
        UpdateStatus(L"Failed to launch Kenshi");
        return;
    }

    UpdateStatus(L"Kenshi launched! Click OK in Kenshi's settings, then the mod loads automatically.");
}

void OnBrowse() {
    wchar_t path[MAX_PATH] = {};
    BROWSEINFOW bi = {};
    bi.hwndOwner = g_hwnd;
    bi.lpszTitle = L"Select Kenshi game directory";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_USENEWUI;

    LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
    if (pidl) {
        SHGetPathFromIDListW(pidl, path);
        SetWindowTextW(g_editGamePath, path);
        CoTaskMemFree(pidl);
    }
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_PLAY: OnPlay(); break;
        case IDC_BROWSE: OnBrowse(); break;
        }
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    return 0;
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow) {
    InitCommonControls();

    // Register window class
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW);
    wc.lpszClassName = L"KenshiOnlineLauncher";
    RegisterClassExW(&wc);

    // Create window
    g_hwnd = CreateWindowExW(0, L"KenshiOnlineLauncher",
        L"Kenshi-Online Launcher",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 480, 340,
        nullptr, nullptr, hInstance, nullptr);

    int y = 15;
    auto label = [&](const wchar_t* text, int x, int w) {
        CreateWindowW(L"STATIC", text, WS_CHILD | WS_VISIBLE,
                     x, y, w, 20, g_hwnd, nullptr, hInstance, nullptr);
    };

    // Game path
    label(L"Kenshi Path:", 15, 80);
    y += 22;

    std::wstring defaultPath = kmp::FindKenshiPath();
    g_editGamePath = CreateWindowW(L"EDIT", defaultPath.c_str(),
        WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
        15, y, 350, 24, g_hwnd, (HMENU)IDC_GAMEPATH, hInstance, nullptr);
    g_btnBrowse = CreateWindowW(L"BUTTON", L"...",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        372, y, 80, 24, g_hwnd, (HMENU)IDC_BROWSE, hInstance, nullptr);
    y += 35;

    // Player name
    label(L"Player Name:", 15, 85);
    y += 22;
    g_editPlayerName = CreateWindowW(L"EDIT", L"Player",
        WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
        15, y, 435, 24, g_hwnd, (HMENU)IDC_PLAYERNAME, hInstance, nullptr);
    y += 35;

    // Server address
    label(L"Server Address:", 15, 100);
    label(L"Port:", 340, 40);
    y += 22;
    g_editServerAddr = CreateWindowW(L"EDIT", L"127.0.0.1",
        WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
        15, y, 315, 24, g_hwnd, (HMENU)IDC_SERVERADDR, hInstance, nullptr);
    g_editServerPort = CreateWindowW(L"EDIT", L"27800",
        WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER,
        340, y, 112, 24, g_hwnd, (HMENU)IDC_SERVERPORT, hInstance, nullptr);
    y += 40;

    // Play button
    g_btnPlay = CreateWindowW(L"BUTTON", L"PLAY",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_CENTER,
        15, y, 435, 40, g_hwnd, (HMENU)IDC_PLAY, hInstance, nullptr);
    y += 50;

    // Status
    g_statusText = CreateWindowW(L"STATIC", L"Ready. Press PLAY to launch Kenshi-Online.",
        WS_CHILD | WS_VISIBLE,
        15, y, 435, 20, g_hwnd, (HMENU)IDC_STATUS, hInstance, nullptr);

    ShowWindow(g_hwnd, nCmdShow);
    UpdateWindow(g_hwnd);

    // Message loop
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return (int)msg.wParam;
}
