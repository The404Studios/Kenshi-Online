# Kenshi Online - Complete Setup and Usage Tutorial

## Table of Contents
1. [Introduction](#introduction)
2. [System Requirements](#system-requirements)
3. [Installation](#installation)
4. [Building the Project](#building-the-project)
5. [Running the Server](#running-the-server)
6. [Installing the Mod](#installing-the-mod)
7. [Connecting to Multiplayer](#connecting-to-multiplayer)
8. [Using the UI Features](#using-the-ui-features)
9. [Troubleshooting](#troubleshooting)
10. [Advanced Configuration](#advanced-configuration)

---

## Introduction

Kenshi Online is a multiplayer mod for Kenshi that allows you to play with friends in the same world. The system consists of three main components:

1. **The Middleware Server** (C# .NET 8.0) - Handles networking, player synchronization, and game state
2. **The Mod** (C++ DLL) - Injects into Kenshi and communicates with the middleware
3. **The ImGui UI** - Provides a modern interface for server browsing, friends, and lobbies

---

## System Requirements

### For Server Host:
- **OS**: Windows 10/11 or Linux (with Mono)
- **CPU**: Intel Core i5 or equivalent
- **RAM**: 4GB minimum, 8GB recommended
- **Network**: Open port 5555 (TCP) for game server, port 8080 (TCP) for WebUI
- **.NET Runtime**: .NET 8.0 SDK

### For Players:
- **OS**: Windows 10/11 (64-bit)
- **Kenshi**: Version 0.98.50 or compatible
- **RAM**: Same as Kenshi requirements
- **Network**: Stable internet connection

---

## Installation

### Step 1: Clone the Repository

```bash
git clone https://github.com/The404Studios/Kenshi-Online.git
cd Kenshi-Online
```

### Step 2: Install Dependencies

#### For C# Middleware:
```bash
cd Kenshi-Online
dotnet restore
```

#### For C++ Mod (Windows):
You need:
- Visual Studio 2022 (Community Edition is fine)
- CMake 3.15 or higher
- Windows SDK 10.0

---

## Building the Project

### Building the C# Middleware Server

```bash
cd Kenshi-Online
dotnet build --configuration Release
```

The compiled server will be in: `bin/Release/net8.0/KenshiMultiplayer.dll`

### Building the C++ Mod

#### Option 1: Using Visual Studio

1. Open `Kenshi-Online.sln` in Visual Studio
2. Right-click on `KenshiOnlineMod` project → Build
3. The DLL will be in: `bin/Release/KenshiOnlineMod.dll`

#### Option 2: Using CMake

```bash
cd KenshiOnlineMod
mkdir build
cd build
cmake .. -G "Visual Studio 17 2022" -A x64
cmake --build . --config Release
```

The DLL will be in: `build/Release/KenshiOnlineMod.dll`

#### Option 3: Using MSBuild (vcxproj)

```bash
cd KenshiOnlineMod
msbuild KenshiOnlineMod.vcxproj /p:Configuration=Release /p:Platform=x64
```

---

## Running the Server

### Step 1: Configure the Server

Edit `bin/Release/net8.0/server_config.json` (create if it doesn't exist):

```json
{
  "ServerName": "My Kenshi Server",
  "Port": 5555,
  "MaxPlayers": 20,
  "WebUIPort": 8080,
  "EnableEncryption": true,
  "TickRate": 20,
  "EnableCompression": true,
  "EnableInterpolation": true
}
```

### Step 2: Start the Server

```bash
cd bin/Release/net8.0
dotnet KenshiMultiplayer.dll --server
```

You should see:
```
====================================
  Kenshi Online Server Starting...
====================================
Server Name: My Kenshi Server
Port: 5555
WebUI: http://localhost:8080
====================================
Server started successfully!
Waiting for connections...
```

### Step 3: Verify Server is Running

Open a web browser and go to: `http://localhost:8080`

You should see the Kenshi Online WebUI.

---

## Installing the Mod

### Step 1: Locate Kenshi Directory

Default location: `C:\Program Files (x86)\Steam\steamapps\common\Kenshi`

### Step 2: Install the Mod DLL

1. Copy `KenshiOnlineMod.dll` to your Kenshi directory
2. Create a folder called `mods` in Kenshi directory if it doesn't exist
3. Move `KenshiOnlineMod.dll` into the `mods` folder

### Step 3: Inject the Mod

There are two methods:

#### Method A: Automatic Injection (Recommended)

Run the injector tool:

```bash
cd bin/Release/net8.0
dotnet KenshiMultiplayer.dll --inject
```

This will automatically find Kenshi and inject the mod.

#### Method B: Manual Injection

1. Download a DLL injector (e.g., Process Hacker, Xenos Injector)
2. Start Kenshi
3. Use the injector to inject `KenshiOnlineMod.dll` into `kenshi_x64.exe`

### Step 4: Verify Mod is Loaded

When you start Kenshi, you should see:
- A console window appear with "Kenshi Online Mod Loaded"
- An ImGui menu overlay (press `F1` to toggle)

---

## Connecting to Multiplayer

### Method 1: Using the Server Browser

1. Launch Kenshi with the mod installed
2. Press `F1` to open the Kenshi Online menu
3. Click **"Server Browser"**
4. Click **"Refresh"** to get the list of available servers
5. Select a server from the list
6. Click **"Join Server"**

The game will:
- Create a multiplayer save game
- Load you into the server
- Spawn your character at the designated spawn point

### Method 2: Direct Connect

1. Press `F1` to open the menu
2. In the main menu, enter the server address (e.g., `192.168.1.100`)
3. Enter the port (default: `5555`)
4. Click **"Connect"**

### Method 3: Join via Friend

1. Press `F1` to open the menu
2. Click **"Friends List"**
3. Find your online friend
4. Click **"Join Server"** next to their name

---

## Using the UI Features

### Server Browser

**Features:**
- Filter servers by name
- See player count, ping, map, game mode
- Sort by columns
- Show password-protected servers
- Join with one click

**How to use:**
1. Press `F1` → **Server Browser**
2. Use the filter box to search for specific servers
3. Click on a server row to select it
4. Click **"Join Server"** at the bottom

### Friends System

**Adding Friends:**
1. Press `F1` → **Friends List**
2. Click **"Add Friend"**
3. Enter their username
4. Send request

**Managing Friends:**
- See online/offline status
- See what server they're on
- Invite them to your lobby
- Join their server directly

**Friend List Features:**
- Search bar to find friends quickly
- Level display
- Last seen timestamp for offline friends
- Online indicator

### Lobby Invite System

**Creating a Lobby:**
1. Press `F1` → **Main Menu**
2. Click **"Create Lobby"**
3. Invite friends from your friends list

**Joining a Lobby:**
1. Press `F1` → **Lobby Invites**
2. See all pending invites
3. Click **"Accept"** to join

**Lobby Features:**
- Chat with lobby members
- Set lobby to private/public
- Start the game together when everyone is ready

### In-Game UI

**Connection Status Window** (top-left corner):
- Current server name
- Player count
- Your ping
- Disconnect button

**Hotkeys:**
- `F1`: Toggle main menu
- `F2`: Toggle server browser
- `F3`: Toggle friends list
- `F4`: Toggle lobby invites
- `ESC`: Close all windows

---

## Troubleshooting

### Problem: "Failed to connect to server"

**Solutions:**
1. Check server is running: `netstat -an | findstr 5555`
2. Check firewall settings - allow port 5555
3. Verify server IP address is correct
4. Try direct connect with IP instead of hostname

### Problem: "Mod not loading"

**Solutions:**
1. Ensure DLL is 64-bit (x64)
2. Check Kenshi version is 0.98.50
3. Run Kenshi as Administrator
4. Check console output for error messages
5. Verify `ws2_32.lib` is available

### Problem: "Black screen after joining server"

**Solutions:**
1. Wait 30 seconds for save game to load
2. Check server logs for errors
3. Restart Kenshi and try again
4. Verify save game was created in `%APPDATA%/Kenshi/save/multiplayer`

### Problem: "High ping / lag"

**Solutions:**
1. Server: Increase tick rate in config (max 60)
2. Enable compression in server config
3. Reduce player count if server is overloaded
4. Check your internet connection
5. Choose a server closer to your location

### Problem: "Players not syncing correctly"

**Solutions:**
1. Restart the server
2. All players should disconnect and reconnect
3. Check server tick rate (should be at least 20)
4. Enable interpolation in config
5. Check network scheduler is running

---

## Advanced Configuration

### Server Configuration

Edit `server_config.json`:

```json
{
  "ServerName": "Advanced Server",
  "Port": 5555,
  "MaxPlayers": 50,
  "WebUIPort": 8080,

  // Network Settings
  "TickRate": 60,
  "InterpolationDelayMs": 100,
  "CompressionStrategy": "DeltaGZip",

  // Scheduler Settings
  "ClientTickRateMs": 50,
  "ServerTickRateMs": 20,
  "MiddlewareTickRateMs": 100,

  // Security
  "EnableEncryption": true,
  "RequireAuthentication": true,
  "EnableAntiCheat": true,

  // Features
  "EnableInterpolation": true,
  "EnableCompression": true,
  "EnableSaveGameLoading": true
}
```

### Client Configuration

Edit `%APPDATA%/KenshiOnline/client_config.json`:

```json
{
  "AutoConnect": false,
  "LastServer": "127.0.0.1:5555",
  "Username": "YourUsername",
  "EnableUI": true,
  "UIHotkey": "F1"
}
```

### Performance Tuning

**For Low-End Servers:**
```json
{
  "TickRate": 20,
  "MaxPlayers": 10,
  "CompressionStrategy": "DeltaGZip",
  "EnableInterpolation": false
}
```

**For High-Performance Servers:**
```json
{
  "TickRate": 60,
  "MaxPlayers": 100,
  "CompressionStrategy": "Delta",
  "EnableInterpolation": true,
  "InterpolationDelayMs": 50
}
```

### Hosting a Public Server

1. **Port Forwarding:**
   - Forward port 5555 (TCP) in your router
   - Forward port 8080 (TCP) for WebUI

2. **Firewall Rules:**
   ```bash
   # Windows Firewall
   netsh advfirewall firewall add rule name="Kenshi Server" dir=in action=allow protocol=TCP localport=5555
   netsh advfirewall firewall add rule name="Kenshi WebUI" dir=in action=allow protocol=TCP localport=8080
   ```

3. **Get Your Public IP:**
   ```bash
   curl ifconfig.me
   ```

4. **Share with Players:**
   - Give them your public IP and port (e.g., `123.45.67.89:5555`)
   - Or use a domain name

---

## Technical Architecture

### Data Flow:

```
Kenshi Game (C++)
    ↕ (IPC Named Pipes)
Mod DLL (C++)
    ↕ (IPC Bridge)
Middleware (C#)
    ↕ (TCP + Encryption)
Server (C#)
    ↕ (TCP + Encryption)
Other Clients
```

### Key Components:

1. **InterpolationEngine**: Smooths player movement
2. **CompressionEngine**: Reduces bandwidth usage by 60-80%
3. **NetworkScheduler**: Prioritizes messages (Critical → High → Normal → Low)
4. **EnhancedIPCBridge**: Handles game ↔ middleware communication
5. **SaveGameLoader**: Creates multiplayer save games for joining servers

---

## FAQ

**Q: Can I play with friends on different continents?**
A: Yes, but expect higher ping. The system has interpolation to help smooth lag.

**Q: How many players can join one server?**
A: Depends on your hardware. Tested up to 50 players on a good server.

**Q: Is this compatible with other mods?**
A: Generally yes, but some mods that heavily modify game mechanics may conflict.

**Q: Can I host a server on Linux?**
A: Yes! The middleware is .NET 8.0 and runs on Linux. Just use `dotnet` command.

**Q: Is the mod VAC-safe?**
A: Kenshi doesn't use VAC, so this is not a concern.

**Q: Can I use this for a dedicated server?**
A: Yes! Run the server without the game client.

---

## Credits

- **Kenshi**: Lo-Fi Games
- **Kenshi Online**: The 404 Studios
- **Contributors**: See GitHub repository

## Support

- **Issues**: https://github.com/The404Studios/Kenshi-Online/issues
- **Discord**: [Join our community]
- **Wiki**: https://github.com/The404Studios/Kenshi-Online/wiki

---

**Enjoy playing Kenshi with your friends!** 🎮
