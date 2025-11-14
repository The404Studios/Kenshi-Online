# 🎮 KENSHI ONLINE - Easy Start Guide

## ONE-CLICK SETUP

Just double-click **`PLAY.bat`** - that's it!

The launcher will:
1. ✅ Build itself automatically (first time only)
2. ✅ Launch with a simple menu
3. ✅ Let you choose: Solo, Host, or Join

---

## HOW TO PLAY

### Option 1: SOLO MODE (Easiest - Play Alone)

1. Run `PLAY.bat`
2. Select **[1] Solo Mode**
3. Start Kenshi
4. Inject `bin/Release/Plugin/Re_Kenshi_Plugin.dll` into kenshi_x64.exe
5. Play!

### Option 2: PLAY WITH FRIENDS

**To Host:**
1. Run `PLAY.bat`
2. Select **[2] Host Server**
3. Share the connection string with your friend
4. Start Kenshi and inject plugin

**To Join:**
1. Run `PLAY.bat`
2. Select **[3] Join Server**
3. Paste your friend's connection string
4. Start Kenshi and inject plugin

---

## PLUGIN INJECTION

You need a DLL injector to inject the plugin into Kenshi:

**Recommended Injectors:**
- **Process Hacker** - https://processhacker.sourceforge.io/
  - Open Process Hacker as Admin
  - Find `kenshi_x64.exe`
  - Right-click → Miscellaneous → Inject DLL
  - Select `Re_Kenshi_Plugin.dll`

- **Extreme Injector** - https://github.com/master131/ExtremeInjector
  - Select `kenshi_x64.exe`
  - Add `Re_Kenshi_Plugin.dll`
  - Click Inject

**Plugin Location:**
```
bin/Release/Plugin/Re_Kenshi_Plugin.dll
```

---

## COMMAND LINE (Optional)

You can also run directly from command line:

```batch
# Solo mode
KenshiOnline.exe solo

# Host server on port 7777
KenshiOnline.exe host 7777

# Join server
KenshiOnline.exe join 192.168.1.100:7777
```

---

## FEATURES

### 🎮 Solo Mode
- **One-click** local multiplayer testing
- Server and client run automatically
- Perfect for testing mods

### 🌐 Host Server
- **Auto-detects** your local IP
- **Generates** shareable connection string
- Supports **up to 32 players**
- Configurable port

### 🤝 Join Server
- **Paste** connection string from friend
- **Saves** server history
- **Auto-reconnect** support
- Quick join from recent servers

### ⚙️ Settings
- Player name
- Default port
- Max players
- Auto-connect
- FPS overlay

---

## TROUBLESHOOTING

**"PLAY.bat doesn't work"**
- Install .NET 8.0 SDK: https://dotnet.microsoft.com/download

**"Can't connect to server"**
- Make sure host forwarded the port in their router
- Check firewall settings
- Verify IP address is correct

**"Plugin won't inject"**
- Run injector as Administrator
- Make sure Kenshi is running
- Check antivirus isn't blocking it

**"Server won't start"**
- Check if port is already in use
- Try a different port in settings

---

## FILE STRUCTURE

```
Kenshi-Online/
├── PLAY.bat                      ← START HERE!
├── bin/
│   └── Release/
│       ├── KenshiOnline.exe     ← Main launcher (auto-built)
│       ├── kenshi_online.json   ← Your settings (auto-created)
│       └── Plugin/
│           └── Re_Kenshi_Plugin.dll  ← Inject this into Kenshi
└── ...
```

---

## SUPPORT

- **GitHub Issues**: https://github.com/The404Studios/Kenshi-Online/issues
- **Discord**: (Add your Discord link)
- **Documentation**: See KENSHI_ONLINE_V2.md for full documentation

---

**Enjoy multiplayer Kenshi! 🎮**
