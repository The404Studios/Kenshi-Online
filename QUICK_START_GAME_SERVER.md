# Kenshi Online - Quick Start: Game Server Hosting

## 🎮 What is Game Server Hosting?

The server now **runs an actual Kenshi game instance**! This means:
- ✅ Server hosts a real, live Kenshi world
- ✅ Players connect and spawn into the SAME running game
- ✅ All game logic runs server-side (authoritative)
- ✅ True multiplayer in the same world

---

## 🚀 Quick Start (5 Steps)

### Step 1: Build the Project

```bash
cd Kenshi-Online/Kenshi-Online
dotnet build --configuration Release
```

### Step 2: Copy the Mod DLL

Copy `KenshiOnlineMod.dll` to the server directory:

```bash
# After building the C++ mod
copy KenshiOnlineMod\bin\Release\KenshiOnlineMod.dll Kenshi-Online\bin\Release\net8.0\
```

### Step 3: Run the Server

```bash
cd bin/Release/net8.0
dotnet KenshiMultiplayer.dll
```

**Or use ServerProgram directly:**

```bash
dotnet run --project Kenshi-Online/ServerProgram.cs
```

### Step 4: Wait for Startup

The server will:
1. ✅ Launch Kenshi
2. ✅ Inject the mod
3. ✅ Load/create the world
4. ✅ Start network server
5. ✅ Ready for connections!

```
====================================
  Starting Kenshi Game Server
====================================

[1/5] Initializing IPC Bridge...
✓ IPC Bridge started

[2/5] Initializing Game Host...
✓ Game Host initialized

[3/5] Starting Kenshi game server...
Kenshi started (PID: 12345)
Mod injected successfully
✓ Game server started successfully

[4/5] Starting network server...
✓ Network server listening on port 5555

[5/5] Initializing game state synchronization...
✓ Synchronization ready

╔═══════════════════════════════════════════════════╗
║              SERVER READY & ONLINE                ║
╚═══════════════════════════════════════════════════╝

  Server Name:    Kenshi Online Server
  Network Port:   5555
  Max Players:    20
  World Name:     KenshiOnlineWorld

  Status:         🟢 ONLINE

  Players can now connect and join the game!
```

### Step 5: Connect from Client

Players can now connect using the client mod and join your hosted world!

---

## ⚙️ Configuration Options

### Command Line Arguments

```bash
dotnet KenshiMultiplayer.dll [options]
```

| Option | Description | Default |
|--------|-------------|---------|
| `--name "ServerName"` | Server display name | "Kenshi Online Server" |
| `--port 5555` | Network port | 5555 |
| `--max-players 20` | Max simultaneous players | 20 |
| `--world "WorldName"` | World save name | "KenshiOnlineWorld" |
| `--existing-save "SaveName"` | Use existing save | (creates new) |
| `--kenshi-path "C:\..."` | Kenshi install path | (auto-detect) |
| `--no-minimize` | Don't minimize game window | (minimized) |
| `--no-autosave` | Disable auto-save | (enabled) |

### Examples

**Start server with custom name:**
```bash
dotnet KenshiMultiplayer.dll --name "My Awesome Server" --port 7777
```

**Use existing save file:**
```bash
dotnet KenshiMultiplayer.dll --existing-save "my_world_save" --world "ServerWorld"
```

**Keep game window visible (for debugging):**
```bash
dotnet KenshiMultiplayer.dll --no-minimize
```

**Custom Kenshi path:**
```bash
dotnet KenshiMultiplayer.dll --kenshi-path "D:\Games\Kenshi"
```

---

## 🎛️ Server Console Commands

Once running, type these commands in the console:

| Command | Description |
|---------|-------------|
| `save` | Save the current world |
| `status` | Show server status and stats |
| `stop` | Stop the server gracefully |
| `help` | Show available commands |

**Example:**
```
> status

════════════════ SERVER STATUS ═════════════════
Game Server:    🟢 Running
Process ID:     12345
World:          KenshiOnlineWorld
Players:        3
Network:        🟢 Listening on port 5555
IPC Bridge:     🟢 Active
  Messages Rx:  1524
  Messages Tx:  3891
  Connections:  1
════════════════════════════════════════════════
```

---

## 🌍 World Management

### Starting Fresh (New World)

```bash
dotnet KenshiMultiplayer.dll --world "MyNewWorld"
```

This creates a brand new Kenshi world called "MyNewWorld".

### Using Existing Save

```bash
dotnet KenshiMultiplayer.dll --existing-save "my_awesome_save" --world "ServerWorld"
```

This:
1. Copies your save "my_awesome_save"
2. Renames it to "ServerWorld"
3. Uses it as the multiplayer world

**Tip:** Use your favorite singleplayer save with bases, characters, etc.!

---

## 👥 Player Spawning

When a player connects:
1. Client sends join request with character name
2. Server spawns them in the live game at The Hub (default)
3. Player loads into the world
4. Player can see and interact with others!

**Default Spawn:** The Hub (-4200, 150, 18500)

---

## 💾 Auto-Save

The server automatically saves every 10 minutes by default.

**Disable:**
```bash
dotnet KenshiMultiplayer.dll --no-autosave
```

**Manual save:**
```
> save
Saving world...
World save requested
```

---

## 🔧 Troubleshooting

### "Kenshi executable not found"

The server tries to auto-detect Kenshi. If it fails:

```bash
dotnet KenshiMultiplayer.dll --kenshi-path "C:\Program Files (x86)\Steam\steamapps\common\Kenshi"
```

### "Failed to inject mod"

1. Make sure `KenshiOnlineMod.dll` is in the same directory as the server
2. Run as Administrator
3. Check Windows Security isn't blocking the DLL

### "Mod DLL not found"

Copy the mod DLL to the server directory:
```bash
copy KenshiOnlineMod\bin\Release\KenshiOnlineMod.dll bin\Release\net8.0\
```

### Game window won't minimize

Use `--no-minimize` to keep it visible, or check if Kenshi is focused.

### Port already in use

Change the port:
```bash
dotnet KenshiMultiplayer.dll --port 7777
```

---

## 📊 System Requirements

### Minimum (Dedicated Server)
- **CPU:** Same as Kenshi + server overhead
- **RAM:** 8GB (Kenshi uses ~4GB, server ~1GB)
- **OS:** Windows 10/11 64-bit
- **Network:** 10 Mbps upload (for 10 players)

### Recommended (20+ players)
- **CPU:** Intel i7 / Ryzen 7
- **RAM:** 16GB
- **Network:** 50 Mbps upload

---

## 🔥 Pro Tips

1. **Headless Mode:** Use `--no-minimize` only for debugging. Minimized saves resources.

2. **Backup Saves:** The server auto-saves to `%APPDATA%/Kenshi/save/[WorldName]`

3. **Restart Clean:** Delete the world folder to start completely fresh

4. **Monitor Performance:** Use `status` command to check message counts and lag

5. **Graceful Shutdown:** Always use `stop` command instead of Ctrl+C to save properly

---

## 📁 File Locations

**Server Executable:**
```
Kenshi-Online/bin/Release/net8.0/KenshiMultiplayer.dll
```

**World Saves:**
```
%APPDATA%\Kenshi\save\[WorldName]\
```

**Mod DLL:**
```
Kenshi-Online/bin/Release/net8.0/KenshiOnlineMod.dll
```

**Server Logs:**
```
Console output (TODO: add file logging)
```

---

## 🎯 What's Next?

After your server is running:

1. **Build the client mod** (same `KenshiOnlineMod.dll`)
2. **Inject into player's Kenshi**
3. **Connect to your server IP:5555**
4. **Play together!**

See `TUTORIAL_ENGLISH.md` for complete client setup instructions.

---

## 🐛 Known Issues

- Manual save loading: You may need to manually load the save from Kenshi's menu the first time
- Windows Firewall: May need to allow Kenshi and the server through firewall
- Process cleanup: Kenshi may not close cleanly - use Task Manager if needed

---

## 💬 Need Help?

- **Issues:** https://github.com/The404Studios/Kenshi-Online/issues
- **Wiki:** https://github.com/The404Studios/Kenshi-Online/wiki

---

**Enjoy hosting your own Kenshi Online server!** 🎮🌍
