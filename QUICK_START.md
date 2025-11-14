# Re_Kenshi Multiplayer - 5 Minute Quick Start

Play Kenshi with your friends in under 5 minutes!

## First Time Setup (One-Time Only)

### Step 1: Build Everything

**Double-click:** `Setup_First_Time.bat`

This builds all components automatically. Just wait for it to finish (2-3 minutes).

✅ Requirements will be checked automatically
✅ Everything builds with one click
✅ Clear instructions shown at the end

## Playing With Friends

### Option A: You Host the Server

**1. Start the server:**
   - Double-click: `Host_Server.bat`
   - Server starts on port 7777

**2. Port forwarding (IMPORTANT!):**
   - Open your router admin page (usually http://192.168.1.1)
   - Forward port **7777** TCP to your PC's local IP
   - [How to port forward (video)](https://www.youtube.com/watch?v=jfSLxs40sIw)

**3. Get your public IP:**
   - Visit: https://whatismyipaddress.com
   - Copy your IP address (e.g., `203.0.113.42`)
   - Send this IP to your friend

**4. Start Kenshi:**
   - Launch Kenshi normally
   - Load or start a game

**5. Inject the plugin:**
   - Download DLL injector: [Extreme Injector](https://github.com/master131/ExtremeInjector/releases)
   - Select `kenshi_x64.exe` process
   - Inject `Re_Kenshi_Plugin\build\bin\Release\Re_Kenshi_Plugin.dll`
   - You'll see a success message!

**Your friend connects:**
   - Friend double-clicks: `Join_Friend.bat`
   - Friend enters your IP address
   - Friend follows steps 4-5 above

### Option B: Join a Friend's Server

**1. Get server IP from friend**
   - Ask your friend for their public IP
   - They get it from https://whatismyipaddress.com

**2. Connect to server:**
   - Double-click: `Join_Friend.bat`
   - Enter friend's IP address (e.g., `203.0.113.42`)
   - Press Enter

**3. Start Kenshi:**
   - Launch Kenshi normally
   - Load or start a game

**4. Inject the plugin:**
   - Use DLL injector to inject `Re_Kenshi_Plugin.dll` into Kenshi
   - You'll see a success message!

**Now playing together!** 🎉

## Testing Locally (Same PC/LAN)

**If you want to test on same computer or LAN:**

1. One person runs: `Host_Server.bat`
2. Other person runs: `Play_Localhost.bat`
3. Both inject plugin into Kenshi
4. Done!

## What You'll See

When everything is working:

**Server Console:**
```
Server listening on port 7777
[JOIN] Player 'Player_YourPC_1234' connected (1 players online)
[JOIN] Player 'Player_FriendPC_5678' connected (2 players online)
```

**Client Service Console:**
```
✓ Connected to game server
✓ C++ plugin connected via IPC
[SERVER] Player joined: Player_FriendPC_5678
```

**In Kenshi:**
- Message box: "Re_Kenshi Multiplayer Plugin loaded successfully!"
- Check `ReKenshi.log` in Kenshi folder for details

## Simple Flowchart

```
┌─────────────┐
│  HOST       │
│  Run:       │
│  Host_      │
│  Server.bat │
└──────┬──────┘
       │
       │ Gets IP: 203.0.113.42
       │ Sends to friend
       ▼
┌─────────────┐        ┌─────────────┐
│  FRIEND     │        │  HOST       │
│  Run:       │        │  Run:       │
│  Join_      │        │  Play_      │
│  Friend.bat │        │  Localhost  │
│  Enter IP   │        │  .bat       │
└──────┬──────┘        └──────┬──────┘
       │                      │
       │ ┌────────────────────┘
       │ │
       ▼ ▼
┌──────────────┐
│ BOTH         │
│ 1. Start     │
│    Kenshi    │
│ 2. Inject    │
│    Plugin    │
│ 3. Play!     │
└──────────────┘
```

## Troubleshooting

### "Failed to connect to server"
- ✅ Check server is running (`Host_Server.bat`)
- ✅ Check port 7777 is forwarded
- ✅ Check firewall allows port 7777
- ✅ Verify IP address is correct
- ✅ Try `telnet <ip> 7777` to test connection

### "Failed to connect to client service"
- ✅ Run `Join_Friend.bat` or `Play_Localhost.bat` BEFORE injecting plugin
- ✅ Client service must be running before you inject the DLL

### "Plugin won't inject"
- ✅ Run DLL injector as Administrator
- ✅ Make sure Kenshi is running
- ✅ Use 64-bit injector (Kenshi is 64-bit)
- ✅ Disable antivirus temporarily

### Can't see friend in-game
- ✅ Position sync works - you won't see models yet (future feature)
- ✅ Check server console - both players should show as connected
- ✅ Check `ReKenshi.log` for errors

## Server Commands

While server is running, type:

- `/list` - Show connected players
- `/kick <playerId>` - Kick a player
- `/stop` - Stop server

## Advanced: Port Forwarding Help

**Router Login:**
- Usually: http://192.168.1.1 or http://192.168.0.1
- Default logins: admin/admin, admin/password, or check router label

**Steps:**
1. Login to router
2. Find "Port Forwarding" or "Virtual Server"
3. Add new rule:
   - External Port: 7777
   - Internal Port: 7777
   - Internal IP: Your PC's local IP (find with `ipconfig`)
   - Protocol: TCP
4. Save and restart router

**Can't port forward?**
- Try Hamachi/ZeroTier for VPN (creates fake LAN)
- Use Ngrok for temporary tunnel
- Ask IT-savvy friend for help

## Quick Reference

| What | File | Purpose |
|------|------|---------|
| First setup | `Setup_First_Time.bat` | Build everything (once) |
| Host server | `Host_Server.bat` | Start server for friends |
| Join friend | `Join_Friend.bat` | Connect to friend's server |
| Test local | `Play_Localhost.bat` | Test on same PC/LAN |

| Port | Purpose |
|------|---------|
| 7777 | Game server (TCP) - needs port forwarding |

| File | Location |
|------|----------|
| Plugin DLL | `Re_Kenshi_Plugin\build\bin\Release\Re_Kenshi_Plugin.dll` |
| Server | `ReKenshi.Server\bin\Release\net8.0\ReKenshiServer.exe` |
| Client | `ReKenshi.ClientService\bin\Release\net8.0\ReKenshiClientService.exe` |
| Logs | `ReKenshi.log` (in Kenshi folder) |

## Tips for Best Experience

✅ **Use Discord/TeamSpeak** - Talk while playing!
✅ **Same game version** - Make sure both have same Kenshi version
✅ **Same mods** - Use same mod loadout for consistency
✅ **Wired connection** - Better than WiFi for stability
✅ **Close bandwidth hogs** - Pause downloads/streaming
✅ **Start in same location** - Meet up in-game first

## What Works Now

✅ Real-time position synchronization (10 Hz)
✅ Health synchronization
✅ Player state (alive/dead)
✅ Multiple players (unlimited)
✅ Automatic reconnection
✅ Server admin commands

## Coming Soon

⏳ Remote player models (you'll see friends)
⏳ Combat synchronization
⏳ Inventory sync
⏳ In-game UI (F1 menu)
⏳ Chat system

## Need More Help?

📖 **Full Documentation:**
- `MULTIPLAYER_SETUP.md` - Detailed setup guide
- `KSERVERMOD_INTEGRATION.md` - Advanced features

🐛 **Found a bug?**
- Check `ReKenshi.log` for errors
- Report issues with log attached

🎮 **Happy multiplayer!**

---

**Made with ❤️ for the Kenshi community**
