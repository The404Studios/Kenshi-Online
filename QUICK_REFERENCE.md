# 🎮 Kenshi Online - Quick Reference Card

**One-page cheat sheet for common tasks and commands** | [Full Documentation](KENSHI_ONLINE_V2.md) | [FAQ](FAQ.md) | [Troubleshooting](TROUBLESHOOTING.md)

---

## ⚡ Quick Start (3 Minutes!)

```batch
# First time setup
Build_Plugin.bat           # Build C++ plugin (once)

# Launch (every time)
PLAY.bat                   # Auto-builds launcher, shows menu
```

**Game Modes:**
- `[1] Solo Mode` - Test locally (easiest)
- `[2] Host Server` - Play with friends
- `[3] Join Server` - Join friend's server

---

## 🎯 Essential Commands

### Launcher Commands (Command Line)

```batch
KenshiOnline.exe solo                    # Solo mode (local server + client)
KenshiOnline.exe host 7777               # Host server on port 7777
KenshiOnline.exe join 192.168.1.100:7777 # Join server
KenshiOnline.exe help                    # Show all commands
```

### In-Game Chat Commands

**Basic Chat:**
```
/global <message>    # Send to everyone
/squad <message>     # Send to squad members
/proximity <message> # Send to nearby players (50m)
/whisper <player> <message>  # Private message
```

**Squad Management:**
```
/squad create <name>      # Create new squad
/squad invite <player>    # Invite player to squad
/squad kick <player>      # Remove player from squad
/squad leave              # Leave current squad
/squad list               # List squad members
```

**Friend System:**
```
/friend add <player>      # Send friend request
/friend remove <player>   # Remove friend
/friend list              # Show friend list
/friend online            # Show online friends
```

**Admin Commands** (requires admin password):
```
/admin <password>         # Authenticate as admin
/kick <player>            # Kick player from server
/ban <player>             # Ban player
/unban <player>           # Unban player
/teleport <player> <x> <y> <z>  # Teleport player
/setspeed <multiplier>    # Set game speed (0.5 - 5.0)
/weather <type>           # Change weather (clear/rain/storm)
/time <hour>              # Set time (0-24)
/announce <message>       # Server-wide announcement
/list                     # List all connected players
/stats                    # Show server statistics
```

---

## 📁 Important File Locations

```
Kenshi-Online/
├── PLAY.bat                           # ⭐ CLICK THIS TO START
├── Build_Plugin.bat                   # ⭐ RUN ONCE FIRST TIME
├── Verify_Installation.bat            # Check if setup is correct
│
├── bin/Release/
│   ├── KenshiOnline.exe              # Main launcher (auto-built)
│   ├── kenshi_online.json            # Your settings (auto-created)
│   ├── KenshiOnline.log              # Log file (check for errors)
│   └── Plugin/
│       └── Re_Kenshi_Plugin.dll      # ⭐ INJECT THIS INTO KENSHI
│
└── config-examples/
    ├── server-config-example.json    # All server settings reference
    └── client-config-example.json    # All client settings reference
```

---

## 🔧 DLL Injection (Process Hacker)

**Steps:**
1. **Run Kenshi** first (wait for main menu)
2. **Run Process Hacker** as Administrator
3. Find `kenshi_x64.exe` in process list
4. Right-click → **Miscellaneous** → **Inject DLL**
5. Browse to: `bin\Release\Plugin\Re_Kenshi_Plugin.dll`
6. Click **Inject**

**Success indicators:**
- No error message
- Check in-game for multiplayer UI elements

---

## ⚙️ Key Configuration Settings

### Server Settings (`kenshi_online.json`)

```json
{
  "ServerInfo": {
    "ServerName": "My Server",      # Server name in browser
    "MaxPlayers": 32,                # 1-64 players
    "Port": 7777,                    # TCP port (must forward!)
    "Password": "",                  # Leave empty for public
    "AdminPassword": "admin123"      # For admin commands
  },
  "SyncSettings": {
    "TickRate": 20,                  # Updates/sec (10-60)
    "SyncRadius": 100.0,             # Meters (50-500)
    "DeltaSyncEnabled": true         # Saves 80% bandwidth!
  },
  "ChatSettings": {
    "MaxMessageLength": 256,
    "SpamProtectionEnabled": true,
    "MessageCooldown": 1.0           # Seconds between messages
  }
}
```

### Client Settings

```json
{
  "PlayerInfo": {
    "PlayerName": "YourName"         # Change this!
  },
  "ConnectionSettings": {
    "DefaultPort": 7777,
    "AutoConnect": false,
    "ConnectionTimeout": 30
  },
  "GraphicsSettings": {
    "ShowFPS": true,
    "ShowPlayerNames": true,
    "ShowHealthBars": true,
    "ShowSquadMarkers": true
  }
}
```

---

## 🌐 Port Forwarding (For Hosting)

**Router Setup:**
1. Find your **local IP**: `ipconfig` (e.g., 192.168.1.100)
2. Access router admin: Usually http://192.168.1.1
3. Find **Port Forwarding** section
4. Add rule:
   - **External Port:** 7777
   - **Internal Port:** 7777
   - **Internal IP:** [Your local IP]
   - **Protocol:** TCP
5. Save and restart router

**Give friends your public IP:**
- Visit: https://whatismyip.com
- Connection string: `kenshi://123.45.67.89:7777`

**Windows Firewall:**
```batch
netsh advfirewall firewall add rule name="Kenshi Online" dir=in action=allow protocol=TCP localport=7777
```

---

## 🐛 Common Issues - Quick Fixes

| Problem | Quick Fix |
|---------|-----------|
| **"DLL inject failed"** | Run injector as Administrator |
| **"Can't connect"** | Check IP format: `192.168.1.100:7777` (not `http://`) |
| **"Server won't start"** | Port 7777 in use? Try `KenshiOnline.exe host 7778` |
| **"CMake not found"** | Download CMake, select "Add to PATH" during install |
| **".NET not found"** | Download .NET 8.0 SDK (not Runtime) |
| **"High lag"** | Reduce `SyncRadius` to 50, enable `DeltaSyncEnabled` |
| **"FPS drop"** | Disable `ShowPlayerNames` and `ShowHealthBars` |
| **"Desync issues"** | All players must use same mod loadout |
| **"Plugin not working"** | Check `KenshiOnline.log` for errors |
| **"nlohmann/json error"** | Run `Re_Kenshi_Plugin\Download_Dependencies.bat` |

---

## 📊 Performance Optimization

### Low-End Server Optimization
```json
{
  "SyncSettings": {
    "TickRate": 10,           # Half speed = half CPU
    "SyncRadius": 50.0,       # Sync less = faster
    "SyncNPCs": false,        # Disable NPC sync
    "SyncAnimals": false      # Disable animal sync
  },
  "ServerInfo": {
    "MaxPlayers": 8           # Fewer players = better performance
  }
}
```

### Low-End Client Optimization
```json
{
  "GraphicsSettings": {
    "ShowPlayerNames": false,
    "ShowHealthBars": false,
    "ShowSquadMarkers": false,
    "InterpolationEnabled": true  # Smoother movement
  },
  "SyncSettings": {
    "ClientUpdateRate": 10         # Less frequent updates
  }
}
```

### Bandwidth Saving
```json
{
  "SyncSettings": {
    "DeltaSyncEnabled": true,      # 80% less bandwidth
    "CompressionEnabled": true,    # Compress packets
    "SyncRadius": 75.0             # Don't sync distant players
  }
}
```

---

## 🔍 Diagnostic Commands

```batch
# Verify installation
Verify_Installation.bat

# Check server is listening
netstat -an | findstr 7777

# Check if plugin DLL exists
dir bin\Release\Plugin\Re_Kenshi_Plugin.dll

# View logs
type bin\Release\KenshiOnline.log

# Test network connection
ping <server-ip>
telnet <server-ip> 7777

# Check .NET version
dotnet --version

# Check CMake version
cmake --version
```

---

## 📞 Get Help

**Documentation:**
- 📖 [Full Docs](KENSHI_ONLINE_V2.md) - Complete feature documentation
- ❓ [FAQ](FAQ.md) - Frequently asked questions
- 🔧 [Troubleshooting](TROUBLESHOOTING.md) - Detailed problem solving
- 🚀 [Easy Start](EASY_START.md) - Beginner's guide

**Support:**
- **GitHub Issues:** https://github.com/The404Studios/Kenshi-Online/issues
- **Discussions:** https://github.com/The404Studios/Kenshi-Online/discussions
- **Discord:** (Add your Discord link)

**Before asking for help:**
1. Run `Verify_Installation.bat`
2. Check `bin\Release\KenshiOnline.log`
3. Search existing GitHub issues

---

## 📝 Hotkey Reference (Default)

| Key | Action |
|-----|--------|
| `F1` | Toggle multiplayer overlay |
| `F2` | Show/hide player names |
| `F3` | Show/hide FPS counter |
| `F4` | Show/hide network stats |
| `T` | Open chat (Global) |
| `Y` | Open chat (Squad) |
| `U` | Open chat (Proximity) |
| `Enter` | Send chat message |
| `Esc` | Close chat |

*Customize in `kenshi_online.json` → HotkeySettings*

---

## 🎯 Pro Tips

**🚀 Performance:**
- Enable `DeltaSyncEnabled` - saves 80% bandwidth
- Use `SyncRadius: 100` - don't sync whole world
- Dedicated servers: run headless (no game), just `KenshiOnline.exe host`

**🛡️ Security:**
- Set strong `AdminPassword` if public server
- Use `ServerPassword` to make server private
- Ban griefers: `/admin <pass>` then `/ban <player>`

**🤝 Better Multiplayer:**
- Same mod loadout for all players = less desync
- Voice chat: Use Discord, built-in coming in v2.1
- Squads: Max 8 players, great for coordinated play

**💡 Troubleshooting:**
- 90% of issues: Run as Administrator
- Can't connect? Check firewall, not antivirus
- Check logs first: `KenshiOnline.log`

---

**Version 2.0.0** | Last Updated: 2025-01-14 | [GitHub](https://github.com/The404Studios/Kenshi-Online) | MIT License
