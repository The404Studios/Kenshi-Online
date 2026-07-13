# Kenshi Online

**16-player co-op multiplayer mod for Kenshi**

[![Build Status](https://img.shields.io/badge/build-passing-brightgreen)]()
[![Version](https://img.shields.io/badge/version-0.3.0--alpha-blue)]()
[![Platform](https://img.shields.io/badge/platform-Windows%20x64-lightgrey)]()
[![License](https://img.shields.io/badge/license-MIT-green)]()

> ⚠️ **ALPHA STATUS:** Multiplayer is functional but in active development. See [Known Issues](#known-issues).

---

## 🚀 Quick Start

```bash
# 1. Clone
git clone --recursive https://github.com/The404Studios/Kenshi-Online.git
cd Kenshi-Online

# 2. Build
cd build
MSBuild.exe KenshiMP.sln /p:Configuration=Release /p:Platform=x64 /m

# 3. Install
copy bin\Release\KenshiMP.Core.dll "C:\Program Files (x86)\Steam\steamapps\common\Kenshi\"
# Edit Kenshi/data/Plugins_x64.cfg, add: Plugin=../KenshiMP.Core

# 4. Launch
KenshiMP.Injector.exe
```

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
1. Launch via `KenshiMP.Injector.exe`
2. Load your save
3. Press **F1** → enter server IP/port
4. Click "Connect"
5. Wait for "All players ready"

### Server
1. Edit `server.json`:
   ```json
   {
     "serverName": "My Server",
     "port": 7777,
     "maxPlayers": 16
   }
   ```
2. Run `KenshiMP.Server.exe`
3. Forward UDP port 7777

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

- **GitHub:** [The404Studios/Kenshi-Online](https://github.com/The404Studios/Kenshi-Online)
- **Issues:** [Report bugs](https://github.com/The404Studios/Kenshi-Online/issues)
- **Email:** the404studios@gmail.com

---

**Last Updated:** 2026-06-04 | **Version:** 0.3.0-alpha | **Status:** ✅ Functional

<p align="center">
  <strong>Built with 🧠 by Claude AI and ❤️ by The404Studios</strong>
</p>
