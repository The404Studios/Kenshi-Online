# Kenshi Online

**16-player co-op multiplayer mod for Kenshi**

[![Build Status](https://img.shields.io/badge/build-passing-brightgreen)]()
[![Version](https://img.shields.io/badge/version-0.3.0--alpha-blue)]()
[![Platform](https://img.shields.io/badge/platform-Windows%20x64-lightgrey)]()
[![License](https://img.shields.io/badge/license-MIT-green)]()

> ⚠️ **ALPHA STATUS:** Multiplayer is functional but in active development. See [Known Issues](#known-issues).

---

## 🚀 Quick Start

### 1. Download

Download the current Windows prerelease:

- [KenshiMP v0.3.0-alpha-installer.2](https://github.com/Dedstate/Kenshi-Online/releases/tag/v0.3.0-alpha-installer.2)
- [Direct download: KenshiMP-Install.zip](https://github.com/Dedstate/Kenshi-Online/releases/download/v0.3.0-alpha-installer.2/KenshiMP-Install.zip)
- [SHA-256 checksums](https://github.com/Dedstate/Kenshi-Online/releases/download/v0.3.0-alpha-installer.2/SHA256SUMS.txt)

Extract the entire ZIP into a separate folder. Do not run the installer from
inside the archive.

### 2. Install

1. Close Kenshi if it is running.
2. Run `install.bat` from the extracted folder.
3. The installer detects the standard Steam and GOG locations. If Kenshi is
   installed elsewhere, enter the folder containing `kenshi_x64.exe`.
4. If Windows denies write access to the Kenshi folder, run `install.bat` as
   Administrator.
5. Wait for `KenshiMP installation completed and verified`.

The installer copies the plugin, server, UI layouts and multiplayer mod; adds
`Plugin=KenshiMP.Core` to `Plugins_x64.cfg`; enables `kenshi-online` in the mod
list; and stores backups in `<Kenshi>\.KenshiMP-install-state`.

### 3. Connect

The quickest method is the included launcher:

1. Run `KenshiMP.Injector.exe` from the extracted folder.
2. Confirm the Kenshi path.
3. Enter your player name, server IP address and port (`27800` by default).
4. Click **PLAY**.
5. Load or start a game. The launcher enables auto-connect, so synchronization
   begins after the world is loaded.

You can also launch Kenshi normally, open **MULTIPLAYER → JOIN GAME**, enter the
same address, port and player name, then click **CONNECT**. Press **F1** to open
or close the multiplayer menu while in game.

---

## 📋 What Works

✅ **2-16 player co-op** - Connect to dedicated server  
✅ **Real-time position sync** - See other players move  
✅ **Combat sync** - Death/KO events synchronized  
✅ **Building/Squad/Faction sync** - World state shared  
✅ **Authority validation** - Prevents cheating (Phases 1-6 complete)  
✅ **Late join fixed** - Players joining during loading now appear correctly  
✅ **Steam deadlock fixed** - 90s timeout prevents infinite loading  

---

## 📚 Documentation

- **[Architecture Overview](docs/AUTHORITY-IMPLEMENTATION-COMPLETE.md)** - Complete authority system (Phases 1-8)
- **[Recent Fixes](docs/MULTIPLAYER-FIXES-2026-06-04.md)** - Spawn queue + timeout fixes
- **[Build Guide](#building-from-source)** - Compilation instructions below
- **[API Reference](docs/API.md)** - Protocol messages (TODO)
- **[Hook Reference](docs/HOOKS.md)** - 14 hook modules (TODO)

---

## 🏗️ Architecture

```
┌─────────── Kenshi Process ───────────┐
│  KenshiMP.Core.dll                   │
│  ┌──────────┐  ┌─────────────────┐  │
│  │ 14 Hooks │→ │ EntityRegistry  │  │
│  └──────────┘  └─────────────────┘  │
│  ┌──────────┐  ┌─────────────────┐  │
│  │NetClient │→ │ AuthValidator   │  │
│  └──────────┘  └─────────────────┘  │
└──────────────────────────────────────┘
           ↕ ENet (3 channels)
┌─────────── Dedicated Server ─────────┐
│  KenshiMP.Server.exe                 │
│  ┌──────────┐  ┌─────────────────┐  │
│  │NetServer │→ │ AuthValidator   │  │
│  └──────────┘  └─────────────────┘  │
│  ┌──────────┐  ┌─────────────────┐  │
│  │ GameState│  │WorldPersistence │  │
│  └──────────┘  └─────────────────┘  │
└──────────────────────────────────────┘
```

**Authority Model:**
- Server owns truth, clients own input
- 8-way validation decision tree (Phase 2)
- Generation tracking prevents ghost control (Phase 6)
- Server validates all commands (Phase 5)

---

## 🔨 Building from Source

### Prerequisites
- Visual Studio 2022
- Windows 10+ SDK
- CMake 3.20+

### Build
```bash
cd build
cmake ..  # Generate solution
MSBuild.exe KenshiMP.sln /p:Configuration=Release /p:Platform=x64 /m
```

### Output
- `bin/Release/KenshiMP.Core.dll` (1.4MB) - Client
- `bin/Release/KenshiMP.Server.exe` (515KB) - Server
- `bin/Release/KenshiMP.Injector.exe` (99KB) - Launcher

---

## 🎮 How to Play

### Client

1. Install the complete release package with `install.bat`.
2. Launch through `KenshiMP.Injector.exe` for automatic connection, or launch
   Kenshi normally and use **MULTIPLAYER → JOIN GAME**.
3. Enter the host IP, UDP port and your player name.
4. Click **CONNECT**, then load or start the world if it is not already loaded.
5. Wait for the connection/synchronization message before playing.

For a server on the same PC use `127.0.0.1:27800`. For LAN use the host's local
IP address. For Internet play use its public IP or DNS name; the host must allow
UDP traffic on the configured port.

### Server

1. Edit `server.json` next to `KenshiMP.Server.exe`:
   ```json
   {
     "serverName": "My Server",
     "port": 27800,
     "maxPlayers": 16
   }
   ```
2. Run `KenshiMP.Server.exe`, or use **MULTIPLAYER → HOST GAME**.
3. Allow `KenshiMP.Server.exe` through Windows Firewall.
4. For Internet hosting, forward UDP port `27800` to the server PC if UPnP does
   not configure the router automatically.

The server must remain running while clients are connected. If you change the
`port` value, every client must use the same port.

### Updating and uninstalling

- To update, extract the newer complete ZIP and run its `install.bat` again.
  Existing original backups are preserved.
- To remove KenshiMP safely, run `uninstall.bat` from the same release package.
- Do not delete `<Kenshi>\.KenshiMP-install-state` before uninstalling; it
  contains the information needed to restore the original files.

---

## 🐛 Known Issues

### Fixed (v0.3.0)
✅ Players joining during loading invisible → **FIXED** (DeferredSpawnQueue)  
✅ Steam deadlock on loading → **FIXED** (90s hard timeout)  

### Current
❌ Combat damage bars don't sync (ApplyDamage hook crash)  
⚠️ ReconcileLocal stub (Phase 7 prediction not impl)  
⚠️ AI not synchronized (local AI decisions)  

See [docs/MULTIPLAYER-FIXES-2026-06-04.md](docs/MULTIPLAYER-FIXES-2026-06-04.md) for details.

---

## 🤝 Contributing

1. Fork the repo
2. Create a feature branch
3. Commit your changes
4. Push and open a PR

**Areas needing help:**
- Combat damage sync (fix ApplyDamage hook)
- Client prediction (Phase 7)
- Inventory sync
- Documentation/wiki

---

## 📊 Project Stats

- **Projects:** 7 (Core, Server, Common, Scanner, Injector, Tests)
- **Source Files:** ~90 C++ files
- **Lines of Code:** ~35,000
- **Hooks:** 14 modules (entity, combat, time, movement, etc.)
- **Protocol Messages:** 40+ types
- **Functions Reversed:** 20+ verified offsets

---

## 📜 License

MIT License - See [LICENSE](LICENSE)

---

## 📞 Contact

- **GitHub:** [Dedstate/Kenshi-Online](https://github.com/Dedstate/Kenshi-Online)
- **Issues:** [Report bugs](https://github.com/Dedstate/Kenshi-Online/issues)
- **Email:** the404studios@gmail.com

---

**Last Updated:** 2026-07-14 | **Version:** v0.3.0-alpha-installer.2 | **Status:** Prerelease

<p align="center">
  <strong>Built with 🧠 by Claude AI and ❤️ by The404Studios</strong>
</p>
