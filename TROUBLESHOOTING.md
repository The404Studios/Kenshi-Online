# 🔧 Kenshi Online - Troubleshooting Guide

Comprehensive solutions for technical issues. For quick answers, see [FAQ.md](FAQ.md).

---

## 📋 Table of Contents

- [Installation Issues](#installation-issues)
- [Build Errors](#build-errors)
- [DLL Injection Problems](#dll-injection-problems)
- [Connection Issues](#connection-issues)
- [Server Crashes](#server-crashes)
- [Performance Problems](#performance-problems)
- [Synchronization Issues](#synchronization-issues)
- [Game Crashes](#game-crashes)
- [Advanced Debugging](#advanced-debugging)

---

## Installation Issues

### ❌ ".NET SDK not found" when running PLAY.bat

**Symptoms:**
```
'dotnet' is not recognized as an internal or external command
```

**Solution:**
1. Download .NET 8.0 SDK from https://dotnet.microsoft.com/download
2. Run the installer (choose "SDK" not "Runtime")
3. Restart your command prompt/terminal
4. Verify installation:
   ```batch
   dotnet --version
   ```
   Should show: `8.0.x`

**If still not working:**
- Check Environment Variables: `PATH` should include `C:\Program Files\dotnet\`
- Reboot computer after installation
- Try running as Administrator

---

### ❌ "CMake not found" when building plugin

**Symptoms:**
```
'cmake' is not recognized as an internal or external command
```

**Solution:**
1. Download CMake from https://cmake.org/download/
2. **IMPORTANT:** During installation, select "Add CMake to system PATH for all users"
3. Restart command prompt
4. Verify:
   ```batch
   cmake --version
   ```

**Manual PATH setup (if needed):**
1. Open System Properties → Environment Variables
2. Edit `PATH` variable
3. Add: `C:\Program Files\CMake\bin`
4. Click OK, restart terminal

---

### ❌ "Visual Studio not found" during C++ build

**Symptoms:**
```
CMake Error: Could not find Visual Studio 17 2022
```

**Solutions:**

**Option 1: Install Visual Studio 2022**
1. Download from https://visualstudio.microsoft.com/downloads/
2. Run installer, select **"Desktop development with C++"**
3. Ensure these components are checked:
   - MSVC v143 - VS 2022 C++ x64/x86 build tools
   - Windows 10/11 SDK
   - C++ CMake tools for Windows
4. Install (requires ~7 GB disk space)

**Option 2: Use Build Tools (lighter)**
1. Download "Build Tools for Visual Studio 2022"
2. Select "C++ build tools" workload
3. Install

**Verification:**
```batch
"C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\devenv.exe"
```

---

### ❌ Directory structure is wrong

**Symptoms:**
```
Could not find file 'KenshiOnline.Launcher\KenshiOnline.Launcher.csproj'
```

**Solution:**
Ensure you're running from the repository root:
```batch
cd Kenshi-Online
dir
```

You should see:
```
KenshiOnline.Launcher/
KenshiOnline.Core/
Re_Kenshi_Plugin/
PLAY.bat
Build_Plugin.bat
```

**If structure is wrong:**
```batch
git pull origin main
git checkout main
```

---

## Build Errors

### ❌ "nlohmann/json.hpp too small" error

**Symptoms:**
```
[WARN] nlohmann/json is stub file (1234 bytes)
```

**Solution:**
```batch
cd Re_Kenshi_Plugin
Download_Dependencies.bat
```

**If download fails:**

**Manual download:**
1. Go to: https://github.com/nlohmann/json/releases/latest
2. Download `json.hpp`
3. Place in: `Re_Kenshi_Plugin\vendor\nlohmann\json.hpp`
4. Verify file size > 100 KB

**If behind firewall/proxy:**
```batch
REM Edit Download_Dependencies.bat, add proxy settings
set HTTP_PROXY=http://proxy.company.com:8080
set HTTPS_PROXY=http://proxy.company.com:8080
```

---

### ❌ CMake generation fails

**Symptoms:**
```
CMake Error: The source directory does not exist
```

**Solution:**
```batch
cd Re_Kenshi_Plugin
mkdir build
cd build
cmake .. -G "Visual Studio 17 2022" -A x64
```

**If still fails:**

Check CMakeLists.txt exists:
```batch
dir Re_Kenshi_Plugin\CMakeLists.txt
```

**Clean rebuild:**
```batch
rmdir /s /q Re_Kenshi_Plugin\build
Build_Plugin.bat
```

---

### ❌ C++ compilation errors

**Symptoms:**
```
error C2065: 'nlohmann': undeclared identifier
error C2039: 'json': is not a member of 'nlohmann'
```

**Solution:**

**1. Check include paths in CMakeLists.txt:**
```cmake
target_include_directories(${PROJECT_NAME}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/vendor
)
```

**2. Verify includes in source files:**
```cpp
#include "../vendor/nlohmann/json.hpp"  // Correct
#include "json.hpp"                      // Wrong
```

**3. Clean and rebuild:**
```batch
cd Re_Kenshi_Plugin\build
cmake --build . --config Release --clean-first
```

---

### ❌ Launcher build fails

**Symptoms:**
```
error MSB4057: The target "Publish" does not exist
```

**Solution:**

**Check .NET SDK version:**
```batch
dotnet --version
```
Must be 8.0 or higher.

**Manual build:**
```batch
cd KenshiOnline.Launcher
dotnet restore
dotnet build -c Release
dotnet publish -c Release -r win-x64 --self-contained true /p:PublishSingleFile=true
```

**If "project not found":**
```batch
dir KenshiOnline.Launcher\KenshiOnline.Launcher.csproj
```
Ensure .csproj exists.

---

## DLL Injection Problems

### ❌ "Failed to inject DLL" with Process Hacker

**Symptoms:**
- No error message, but injection doesn't work
- Kenshi doesn't show multiplayer features

**Solutions:**

**1. Run Process Hacker as Administrator**
- Right-click Process Hacker → "Run as administrator"

**2. Check DLL path is correct:**
```
<Your-Repo-Path>\Kenshi-Online\bin\Release\Plugin\Re_Kenshi_Plugin.dll
```

**3. Verify DLL is built:**
```batch
dir bin\Release\Plugin\Re_Kenshi_Plugin.dll
```
Size should be > 100 KB.

**4. Disable antivirus temporarily:**
- Windows Defender might block DLL injection
- Add exception for Re_Kenshi_Plugin.dll

**5. Check Kenshi is running:**
- Task Manager → Details → `kenshi_x64.exe`
- Must be 64-bit version

---

### ❌ "Injection failed: Access Denied"

**Symptoms:**
```
Error: Unable to inject DLL (Access Denied)
```

**Solutions:**

**1. Anti-cheat software blocking:**
- Disable any anti-cheat software
- Close programs like: RivaTuner, MSI Afterburner, Discord overlay

**2. Windows Defender Application Control:**
```batch
REM Check if WDAC is enabled
Get-CimInstance -ClassName Win32_DeviceGuard -Namespace root\Microsoft\Windows\DeviceGuard
```
If Code Integrity is enabled, disable it or add exception.

**3. Use different injector:**

**Try Extreme Injector:**
1. Download from https://github.com/master131/ExtremeInjector
2. Select `kenshi_x64.exe`
3. Add `Re_Kenshi_Plugin.dll`
4. Method: "Manual Map" or "LoadLibrary"
5. Click "Inject"

---

### ❌ DLL injected but no multiplayer features appear

**Symptoms:**
- Injection succeeds
- No errors
- But multiplayer UI doesn't show

**Debugging Steps:**

**1. Check logs:**
```batch
type bin\Release\KenshiOnline.log
```
Look for:
```
[INFO] Plugin initialized
[INFO] Connected to client service
```

**2. Verify client service is running:**
```batch
tasklist | findstr KenshiOnline
```
Should show: `KenshiOnline.exe`

**3. Check named pipe connection:**
```batch
REM In PowerShell
[System.IO.Directory]::GetFiles("\\.\\pipe\\")
```
Should list: `KenshiOnlinePipe`

**4. Check game version:**
- Only tested on Kenshi 1.0.68 (GOG)
- Steam version should work but may have compatibility issues

**5. Reinstall clean:**
```batch
rmdir /s /q bin
Build_Plugin.bat
PLAY.bat
```

---

## Connection Issues

### ❌ "Can't connect to server" as client

**Symptoms:**
```
[ERROR] Connection refused
[ERROR] Timeout while connecting
```

**Host-side checklist:**

**1. Verify server is running:**
```batch
tasklist | findstr KenshiOnline
```

**2. Check server is listening:**
```batch
netstat -an | findstr 7777
```
Should show: `0.0.0.0:7777   LISTENING`

**3. Port forwarding (if playing over internet):**
- Router → Port Forwarding
- Add rule:
  - External Port: 7777
  - Internal Port: 7777
  - Internal IP: [Your PC IP]
  - Protocol: TCP

**4. Windows Firewall:**
```batch
REM Allow KenshiOnline through firewall
netsh advfirewall firewall add rule name="Kenshi Online" dir=in action=allow protocol=TCP localport=7777
```

**5. Verify public IP (for internet play):**
- Visit: https://whatismyip.com
- Give friends your public IP: `123.45.67.89:7777`

**Client-side checklist:**

**1. Verify IP address format:**
```
Correct: 192.168.1.100:7777
Wrong: 192.168.1.100.7777
Wrong: http://192.168.1.100:7777
```

**2. Ping test:**
```batch
ping 192.168.1.100
```
Should get replies. If timeout, network issue.

**3. Telnet test:**
```batch
telnet 192.168.1.100 7777
```
If "Could not open connection", port is blocked.

**4. Try local connection first:**
```batch
REM Test with localhost
kenshi://127.0.0.1:7777
```

---

### ❌ Connection drops frequently

**Symptoms:**
- Connect successfully
- After 1-5 minutes, disconnect
- "Connection lost" message

**Solutions:**

**1. Check network stability:**
```batch
ping -t [server-ip]
```
Watch for packet loss or high latency (>200ms).

**2. Increase timeout settings:**

Edit `bin\Release\kenshi_online.json`:
```json
{
  "ConnectionSettings": {
    "HeartbeatInterval": 30,
    "ConnectionTimeout": 120
  }
}
```

**3. Router QoS settings:**
- Enable QoS (Quality of Service)
- Prioritize gaming traffic
- Reserve bandwidth for Kenshi Online

**4. Disable power saving on network adapter:**
- Device Manager → Network adapters
- Right-click adapter → Properties
- Power Management → Uncheck "Allow computer to turn off this device"

**5. Use wired ethernet:**
- WiFi packet loss can cause disconnects
- Switch to ethernet cable

---

### ❌ High ping / lag

**Symptoms:**
- 500+ ms latency
- Players teleporting
- Actions delayed

**Diagnostics:**

**1. Check actual network latency:**
```batch
ping [server-ip]
```
If ping > 150ms, network issue not game issue.

**2. Check server load:**
```batch
REM On server machine
tasklist /fi "imagename eq KenshiOnline.exe" /v
```
CPU usage > 90% = server overloaded.

**3. Check bandwidth:**
```batch
REM Windows Resource Monitor
resmon.exe
```
Network tab → Check if upload/download saturated.

**Optimizations:**

**Reduce sync radius** (server config):
```json
{
  "SyncSettings": {
    "SyncRadius": 50.0
  }
}
```

**Enable delta sync** (should be default):
```json
{
  "SyncSettings": {
    "DeltaSyncEnabled": true
  }
}
```

**Lower tick rate if server weak:**
```json
{
  "SyncSettings": {
    "TickRate": 10
  }
}
```

---

## Server Crashes

### ❌ Server crashes immediately on startup

**Symptoms:**
```
KenshiOnline.exe has stopped working
```

**Check logs first:**
```batch
type bin\Release\KenshiOnline.log
```

**Common causes:**

**1. Port already in use:**
```
[ERROR] Failed to bind to port 7777: Address already in use
```

**Solution:**
```batch
REM Find what's using the port
netstat -ano | findstr :7777

REM Kill the process (replace PID)
taskkill /PID [PID] /F

REM Or use different port
KenshiOnline.exe host 7778
```

**2. Corrupted configuration:**
```
[ERROR] Failed to parse configuration
```

**Solution:**
```batch
REM Delete and regenerate
del bin\Release\kenshi_online.json
KenshiOnline.exe host 7777
```

**3. Missing dependencies:**
```
[ERROR] Could not load file or assembly
```

**Solution:**
```batch
REM Rebuild everything
rmdir /s /q bin
PLAY.bat
```

---

### ❌ Server crashes during gameplay

**Symptoms:**
- Server runs fine initially
- Crashes after 5-30 minutes
- No error message

**Debugging:**

**1. Enable crash dumps:**

Create `bin\Release\crash-handler.bat`:
```batch
@echo off
:loop
KenshiOnline.exe host 7777
echo Server crashed! Restarting in 10 seconds...
timeout /t 10
goto loop
```

**2. Check memory usage:**
```batch
REM If memory grows continuously = memory leak
tasklist /fi "imagename eq KenshiOnline.exe" /fo table
```

**3. Check event logs:**
```batch
eventvwr.msc
```
Windows Logs → Application → Look for KenshiOnline errors.

**4. Reproduce crash:**
- Note what was happening when crash occurred
- Report to GitHub with steps to reproduce

---

## Performance Problems

### ❌ Low FPS in multiplayer (but solo is fine)

**Symptoms:**
- 60 FPS in single player
- 20-30 FPS in multiplayer
- Stuttering

**Solutions:**

**1. Disable name tags / health bars:**
```json
{
  "GraphicsSettings": {
    "ShowPlayerNames": false,
    "ShowHealthBars": false
  }
}
```

**2. Reduce sync radius:**
```json
{
  "SyncSettings": {
    "SyncRadius": 50.0
  }
}
```

**3. Lower game graphics:**
- Kenshi Options → Graphics
- Reduce shadow quality
- Disable SSAO
- Lower texture quality

**4. Close background programs:**
- Discord overlay (disable)
- Browser tabs
- Streaming software

**5. Dedicated GPU:**
- Right-click kenshi_x64.exe → Run with graphics processor → High-performance NVIDIA/AMD

---

### ❌ Server using 100% CPU

**Symptoms:**
- Server machine fans loud
- Task Manager shows KenshiOnline.exe at 100% CPU
- Players experiencing lag

**Solutions:**

**1. Reduce tick rate:**
```json
{
  "SyncSettings": {
    "TickRate": 10
  }
}
```

**2. Limit max players:**
```json
{
  "ServerInfo": {
    "MaxPlayers": 16
  }
}
```

**3. Increase thread count (if you have cores available):**
```json
{
  "PerformanceSettings": {
    "WorkerThreads": 4
  }
}
```

**4. Disable expensive features:**
```json
{
  "SyncSettings": {
    "SyncNPCs": false,
    "SyncAnimals": false
  }
}
```

**5. Use dedicated server (headless):**
- Don't run Kenshi game on server machine
- Just run `KenshiOnline.exe host 7777`
- Saves GPU and RAM

---

## Synchronization Issues

### ❌ Players see each other in different positions

**Symptoms:**
- Player A sees Player B at position X
- Player B sees themselves at position Y
- Position desync

**Causes:**
- High latency (>300ms)
- Packet loss
- Interpolation disabled

**Solutions:**

**1. Enable interpolation:**
```json
{
  "SyncSettings": {
    "InterpolationEnabled": true,
    "InterpolationTime": 0.1
  }
}
```

**2. Check network quality:**
```batch
ping -n 100 [server-ip]
```
Packet loss > 5% = bad network.

**3. Force position sync:**
Type in chat: `/admin [password]` then `/teleport [player] [x] [y] [z]`

---

### ❌ Combat damage not syncing

**Symptoms:**
- Hit enemy, no damage
- Enemy hits you, delayed damage
- Health desynced

**Cause:**
Server-authoritative system - all damage validated server-side.

**Solutions:**

**1. Check server logs:**
```batch
type bin\Release\KenshiOnline.log | findstr "Combat"
```

**2. Verify combat sync enabled:**
```json
{
  "CombatSettings": {
    "ServerAuthoritativeCombat": true,
    "SyncCombatStats": true
  }
}
```

**3. Reduce latency** (see performance section)

---

### ❌ Inventory items duplicating/disappearing

**Symptoms:**
- Pick up item, it appears then disappears
- Drop item, it duplicates
- Trading items lost

**Cause:**
Server-authoritative inventory prevents duping, but lag can cause visual issues.

**Solutions:**

**1. Wait for server confirmation:**
- After picking up item, wait 1-2 seconds
- Don't spam pickup button

**2. Check server validation:**
```json
{
  "InventorySettings": {
    "ServerAuthoritativeInventory": true,
    "ValidateItemTransfers": true
  }
}
```

**3. Relog to resync:**
- Disconnect and reconnect
- Server state is correct, client was desynced

---

## Game Crashes

### ❌ Kenshi crashes when plugin loads

**Symptoms:**
- Inject DLL
- Kenshi closes immediately
- No error message

**Solutions:**

**1. Check DLL is correct version:**
```batch
dumpbin /headers bin\Release\Plugin\Re_Kenshi_Plugin.dll | findstr machine
```
Should say: `x64`

**2. Verify game is unmodded:**
- Disable all other mods
- Verify game files (Steam: Right-click → Properties → Verify)

**3. Check for conflicting software:**
- Disable: RivaTuner, MSI Afterburner, Fraps, OBS hooks
- These can conflict with DLL injection

**4. Rebuild plugin clean:**
```batch
rmdir /s /q Re_Kenshi_Plugin\build
rmdir /s /q bin\Release\Plugin
Build_Plugin.bat
```

---

### ❌ Kenshi crashes during gameplay (with plugin)

**Symptoms:**
- Game runs fine for a while
- Crashes after 10-60 minutes
- Crash to desktop, no error

**Debugging:**

**1. Check crash logs:**
- Kenshi creates logs in: `%APPDATA%\Kenshi\`
- Look for errors near crash time

**2. Disable plugin features incrementally:**

Edit plugin config:
```json
{
  "PluginSettings": {
    "EnableEntitySync": false
  }
}
```

Test if crash still occurs. Re-enable one by one to find culprit.

**3. Memory leak check:**
- Use Process Explorer
- Graph memory usage over time
- If grows continuously = leak, report bug

---

## Advanced Debugging

### Enabling Debug Logging

**Server-side:**
```json
{
  "LogSettings": {
    "LogLevel": "Debug",
    "LogToFile": true,
    "LogFilePath": "kenshi_server_debug.log"
  }
}
```

**Client-side:**
```json
{
  "LogSettings": {
    "LogLevel": "Trace",
    "LogToFile": true
  }
}
```

**Plugin debug logging:**
Edit `Re_Kenshi_Plugin/src/Logger.cpp`:
```cpp
#define LOG_LEVEL LOG_LEVEL_TRACE
```
Rebuild plugin.

---

### Network Traffic Analysis

**Capture packets with Wireshark:**
1. Install Wireshark: https://www.wireshark.org/
2. Start capture on your network interface
3. Filter: `tcp.port == 7777`
4. Analyze KenshiOnline protocol messages

**Check bandwidth usage:**
```batch
REM Windows Resource Monitor
resmon.exe
```
Network → TCP Connections → Find KenshiOnline.exe

---

### Memory Dump Analysis

**If server crashes:**
```batch
REM Enable automatic crash dumps
reg add "HKLM\SOFTWARE\Microsoft\Windows\Windows Error Reporting\LocalDumps\KenshiOnline.exe" /v DumpFolder /t REG_EXPAND_SZ /d "%LOCALAPPDATA%\CrashDumps" /f
reg add "HKLM\SOFTWARE\Microsoft\Windows\Windows Error Reporting\LocalDumps\KenshiOnline.exe" /v DumpType /t REG_DWORD /d 2 /f
```

Crash dumps will be saved to: `%LOCALAPPDATA%\CrashDumps\`

Analyze with Visual Studio or WinDbg.

---

### Performance Profiling

**Profile server:**
1. Install dotTrace: https://www.jetbrains.com/profiler/
2. Attach to KenshiOnline.exe process
3. Record performance snapshot
4. Analyze CPU hotspots

**Profile client FPS:**
```json
{
  "GraphicsSettings": {
    "ShowFPS": true,
    "ShowNetworkStats": true
  }
}
```

---

## Still Need Help?

**Before reporting a bug:**
1. Run `Verify_Installation.bat`
2. Check [FAQ.md](FAQ.md)
3. Search existing GitHub issues
4. Collect logs:
   ```batch
   copy bin\Release\KenshiOnline.log issue-logs.txt
   ```

**Report bug:**
- GitHub Issues: https://github.com/The404Studios/Kenshi-Online/issues
- Include:
  - OS version (Windows 10/11)
  - Kenshi version (Steam/GOG)
  - Log files
  - Steps to reproduce
  - Screenshots/video if applicable

**Community help:**
- Discord: (Your Discord link)
- GitHub Discussions: https://github.com/The404Studios/Kenshi-Online/discussions

---

*Last updated: 2025-01-14 | Version 2.0.0*
