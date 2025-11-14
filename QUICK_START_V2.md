# Kenshi Online v2.0 - Quick Start Guide

## 5-Minute Setup

### Prerequisites (Install These First)
1. **Download .NET 8.0 SDK**: https://dotnet.microsoft.com/download
2. **Download Kenshi**: Steam or GOG version
3. **Download DLL Injector**: Extreme Injector or similar

### Step 1: Build (2 minutes)

```batch
# Run the build script
Build_KenshiOnline.bat
```

Wait for "Build Successful!" message.

### Step 2: Host or Join

#### To Host a Server:

```batch
# Run host script
Host_KenshiOnline.bat

# Enter port (or press Enter for default 7777)
```

Keep this window open!

#### To Join a Server:

```batch
# Run join script
Join_KenshiOnline.bat

# Enter server IP address
# Enter port (or press Enter for default 7777)
```

Keep this window open!

### Step 3: Inject Plugin

1. **Launch Kenshi**
2. **Wait for main menu** to fully load
3. **Open DLL Injector**
4. **Select Process**: `kenshi_x64.exe`
5. **Select DLL**: `Re_Kenshi_Plugin/build/Release/Re_Kenshi_Plugin.dll`
6. **Click Inject**

### Step 4: Play!

1. Load a save game
2. Your position will sync automatically
3. See other players in the world

## Troubleshooting

### "Build failed"
- Install .NET 8.0 SDK
- Restart your terminal

### "Plugin won't inject"
- Wait for main menu to load
- Try a different injector
- Check if DLL is 64-bit

### "Can't connect to server"
- Make sure server is running
- Check firewall settings
- Verify IP and port are correct

### "Client service won't start"
- Check if .NET 8.0 is installed
- Run `dotnet --version` to verify

## What's Working

✅ Player position sync
✅ Health synchronization
✅ Player list
✅ Server commands
✅ Basic combat sync

## What's In Progress

⏳ Inventory synchronization
⏳ NPC synchronization
⏳ Building synchronization
⏳ Full combat system

## Server Commands

Open server console and type:

```
status    - Show server status
players   - List connected players
help      - Show all commands
```

## Admin Commands

Set yourself as admin in server, then use:

```
settime 12       - Set noon
setspeed 2       - 2x game speed
teleport player1 0 0 0
heal player1
```

## Configuration

Edit `kenshi_online_config.json`:

```json
{
  "player": {
    "player_name": "YourName"
  },
  "server": {
    "address": "127.0.0.1",
    "port": 7777
  }
}
```

## Performance Tips

1. **Reduce sync radius** if laggy:
   ```json
   "sync_radius": 50.0
   ```

2. **Lower update rate** if bandwidth limited:
   ```json
   "update_rate_hz": 5
   ```

3. **Disable features** you don't need:
   ```json
   "npc_sync": false,
   "building_sync": false
   ```

## Port Forwarding (For Hosting)

To let friends connect from internet:

1. **Open Router Settings** (usually 192.168.1.1)
2. **Find Port Forwarding** section
3. **Forward port 7777** (or your chosen port)
4. **Protocol**: TCP
5. **Internal IP**: Your PC's local IP
6. **External Port**: 7777
7. **Internal Port**: 7777

Give friends your **external IP** (google "what's my IP").

## Security Note

⚠️ Only play with people you trust! This is a experimental mod.

## Need Help?

1. Check `KenshiOnline.log` for errors
2. Read full docs: `KENSHI_ONLINE_V2.md`
3. Report issues on GitHub

## What Next?

- Try admin commands
- Explore with friends
- Report bugs you find
- Request features

---

**Have fun!** 🎮
