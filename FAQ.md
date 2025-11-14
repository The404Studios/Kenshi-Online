# Frequently Asked Questions (FAQ)

## 📖 Table of Contents

- [General Questions](#general-questions)
- [Installation & Setup](#installation--setup)
- [Gameplay Questions](#gameplay-questions)
- [Technical Issues](#technical-issues)
- [Multiplayer Features](#multiplayer-features)
- [Performance](#performance)
- [Security & Cheating](#security--cheating)
- [Contributing](#contributing)

---

## General Questions

### What is Kenshi Online?

Kenshi Online is a multiplayer modification for Kenshi that allows you to play with friends in real-time. It features server-authoritative gameplay, entity synchronization, chat systems, and social features.

### Is Kenshi Online free?

Yes! Kenshi Online is completely free and open-source under the MIT license. However, you need to own Kenshi to play.

### Does this work with the Steam version of Kenshi?

Yes! Kenshi Online works with both Steam and GOG versions of Kenshi.

### How many players can play together?

Currently, up to 32 players can play on a single server. Performance depends on server hardware.

### Is this compatible with other Kenshi mods?

Partial compatibility. Kenshi Online works best with vanilla Kenshi. Some mods may work, but:
- Gameplay mods: May cause desync issues
- Visual mods: Usually work fine
- Script mods: May conflict with multiplayer

**Recommendation:** Use the same mod setup on all clients for best results.

### Is voice chat supported?

Not yet. Voice chat is planned for v2.1. Currently, use Discord or similar for voice communication.

---

## Installation & Setup

### What do I need to play?

**Requirements:**
- Windows 10/11 (64-bit)
- Kenshi (Steam or GOG version)
- .NET 8.0 Runtime (downloads automatically)
- DLL Injector (Process Hacker or Extreme Injector)

**For building C++ plugin:**
- Visual Studio 2022 with C++ support
- CMake 3.20+
- .NET 8.0 SDK

### How do I install?

**Super simple - 2 steps:**

```batch
# Step 1: Build C++ plugin (first time only)
Build_Plugin.bat

# Step 2: Launch (every time)
PLAY.bat
```

Then inject the plugin and play! See [EASY_START.md](EASY_START.md) for details.

### Do I need to build anything manually?

No! `PLAY.bat` auto-builds the C# launcher. `Build_Plugin.bat` handles the C++ plugin with automated dependency downloads.

### The build fails. What do I do?

**Common fixes:**

1. **"CMake not found"** - Install CMake from https://cmake.org/download/
2. **"Visual Studio not found"** - Install VS 2022 with C++ workload
3. **".NET SDK not found"** - Install .NET 8.0 SDK from https://dotnet.microsoft.com/download
4. **"json.hpp too small"** - Run `Re_Kenshi_Plugin/Download_Dependencies.bat` manually

### How do I update to a new version?

```batch
git pull
Build_Plugin.bat   # Rebuild C++ plugin
PLAY.bat           # New version ready!
```

---

## Gameplay Questions

### How do Solo Mode, Host Mode, and Join Mode work?

- **Solo Mode**: Runs a local server automatically. Perfect for testing or playing alone with multiplayer features.
- **Host Mode**: Runs a dedicated server. Share your connection string with friends.
- **Join Mode**: Connects to a friend's server using their connection string.

### How do I invite friends?

**As Host:**
1. Run `PLAY.bat` → Select `[2] Host Server`
2. Copy the connection string (e.g., `kenshi://192.168.1.100:7777`)
3. Send to friends via Discord/chat

**As Client:**
1. Run `PLAY.bat` → Select `[3] Join Server`
2. Paste friend's connection string
3. Done!

### Can I run a dedicated server?

Yes! Run `KenshiOnline.exe host 7777` from command line. It runs in the background without the game.

### Do I need to forward ports?

**If hosting:** Yes, forward port 7777 (or your chosen port) in your router.

**If joining:** No port forwarding needed.

### How does saving work in multiplayer?

Currently, the server is the authoritative state. Save systems are per-session. Persistent world saves are planned for v2.1.

### What happens if I disconnect?

Your character freezes in-game for 5 minutes (timeout period). Reconnect within that time to resume playing. After timeout, you'll be kicked.

### Can I play with mods?

Best practice: All players should use the **same mod loadout** to avoid desync. Visual-only mods usually work fine.

---

## Technical Issues

### "Failed to inject DLL"

**Solutions:**
1. Run injector as **Administrator**
2. Make sure Kenshi is running
3. Check antivirus isn't blocking the DLL
4. Verify DLL exists: `bin/Release/Plugin/Re_Kenshi_Plugin.dll`
5. Try a different injector (Process Hacker vs Extreme Injector)

### "Can't connect to server"

**Host side:**
1. Check port is forwarded in router (port 7777)
2. Check Windows Firewall allows the port
3. Verify server is running (`PLAY.bat` → Host Mode)
4. Use your **public IP** for friends (https://whatismyip.com)

**Client side:**
1. Check IP address is correct
2. Check port is correct (default: 7777)
3. Try pinging the server IP first
4. Check your firewall

### "Server crashes immediately"

**Common causes:**
1. Port already in use - try a different port
2. Corrupted config - delete `kenshi_online.json` and restart
3. Missing dependencies - run `Build_Plugin.bat` again
4. Check logs in `bin/Release/` for error messages

### "High lag / desync issues"

**Optimization tips:**
1. Lower `SyncRadius` in config (default: 100m)
2. Increase `TickRate` if server can handle it (max: 60)
3. Enable `DeltaSyncEnabled` (default: true)
4. Check network ping: High ping = lag
5. Reduce `MaxPlayers` if server is struggling

### "Plugin doesn't seem to work"

**Verify:**
1. DLL actually injected (check injector confirmation)
2. Check `KenshiOnline.log` for errors
3. Client service is running (should see in PLAY.bat output)
4. Server is running and accepting connections
5. No antivirus blocking

---

## Multiplayer Features

### How does chat work?

**5 chat channels:**
- **Global** - Everyone on server
- **Squad** - Your squad members only
- **Proximity** - Players within 50m
- **Whisper** - Private message to one player
- **System** - Server announcements

Type `/help` in chat for commands.

### How do squads work?

- Maximum 8 members per squad
- Leader can invite/kick members
- Squad chat channel
- Shared quest markers (planned)

**Commands:**
- `/squad create <name>` - Create squad
- `/squad invite <player>` - Invite player
- `/squad leave` - Leave squad

### How does trading work?

1. Right-click player → Initiate Trade
2. Both players add items
3. Both players click "Accept"
4. Server validates (prevents duping)
5. Trade completes or cancels after 5 min

### What admin commands are available?

**20+ admin commands** (requires admin password):

```
/admin <password>        - Authenticate as admin
/kick <player>           - Kick player
/ban <player>            - Ban player
/unban <player>          - Unban player
/teleport <player> <x> <y> <z> - Teleport player
/setspeed <multiplier>   - Set game speed
/weather <type>          - Change weather
/time <hour>             - Set time of day
/announce <message>      - Server announcement
/list                    - List all players
/stats                   - Show server stats
```

See [KENSHI_ONLINE_V2.md](KENSHI_ONLINE_V2.md) for full list.

---

## Performance

### What are the system requirements?

**Minimum:**
- Windows 10 (64-bit)
- 8 GB RAM
- Dual-core CPU
- 100 MB disk space
- 1 Mbps upload (hosting)

**Recommended:**
- Windows 11 (64-bit)
- 16 GB RAM
- Quad-core CPU
- 500 MB disk space
- 5+ Mbps upload (hosting)

### How much bandwidth does it use?

**Per player:**
- ~5-10 KB/s with delta sync enabled (recommended)
- ~30-50 KB/s without delta sync

**For 10 players:** ~50-100 KB/s total

Delta sync reduces bandwidth by 80%!

### Server performance tips?

1. Enable delta sync (`DeltaSyncEnabled: true`)
2. Reduce sync radius (`SyncRadius: 100`)
3. Lower max players if struggling
4. Use SSD for server files
5. Close unnecessary programs
6. Dedicated server: Run headless (no game client)

### Client performance tips?

1. Lower game graphics settings
2. Enable interpolation for smooth movement
3. Reduce culling distance in config
4. Close background programs
5. Use wired ethernet vs WiFi if possible

---

## Security & Cheating

### Is there anti-cheat?

Yes! **Server-authoritative design** means:
- Server validates all combat actions
- Server validates inventory changes
- Server checks position validity
- Speed hack detection
- Item duplication prevention

### Can players cheat?

The server-authoritative architecture prevents most cheating:
- ❌ Can't dupe items (server validates)
- ❌ Can't instant-kill (server checks damage)
- ❌ Can't teleport (position validation)
- ❌ Can't speed hack (speed threshold checking)

However:
- ⚠️ Visual mods might give unfair advantage
- ⚠️ Macro/automation tools might work

**Report exploits** to GitHub Issues!

### How do I report a cheater?

1. Note their player ID/name
2. Record evidence (screenshot/video)
3. Report to server admin
4. Admin can use `/ban <player>` command

### Is my data safe?

- Passwords: Never send passwords to server
- Personal info: Only player name is shared
- IP addresses: Visible to server host only
- Encryption: Planned for v2.1

**Privacy tip:** Use a username, not real name!

---

## Contributing

### How can I contribute?

See [CONTRIBUTING.md](CONTRIBUTING.md) for complete guide!

**Quick ways to help:**
- Report bugs on GitHub
- Suggest features
- Write documentation
- Translate to other languages
- Create tutorials/videos
- Help others in Discord
- Submit code improvements

### I found a bug. What do I do?

1. Check if already reported: https://github.com/The404Studios/Kenshi-Online/issues
2. If new, create issue with:
   - Description of bug
   - Steps to reproduce
   - Your system info
   - Log files if applicable
   - Screenshots/videos

### Can I fork this project?

Yes! It's open-source under MIT license. Fork away!

### How do I test my changes?

```batch
# Make changes to code
Build_Plugin.bat   # Rebuild if C++ changes
PLAY.bat           # Launch to test

# Use Solo Mode for quick testing
# No need for multiple computers!
```

---

## Still Have Questions?

- **GitHub Discussions**: https://github.com/The404Studios/Kenshi-Online/discussions
- **GitHub Issues**: https://github.com/The404Studios/Kenshi-Online/issues
- **Discord**: (Add your Discord invite)
- **Email**: (Your contact email)

---

*Last updated: 2025-01-14 | Version 2.0.0*
