# 🎮 Kenshi Online v2.0

**Play Kenshi with your friends in real-time multiplayer!**

[![Build Status](https://img.shields.io/badge/build-ready-brightgreen.svg)]()
[![Version](https://img.shields.io/badge/version-2.0-blue.svg)]()
[![Platform](https://img.shields.io/badge/platform-Windows-lightgrey.svg)]()
[![License](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![.NET 8.0](https://img.shields.io/badge/.NET-8.0-purple.svg)](https://dotnet.microsoft.com/download/dotnet/8.0)
[![C++17](https://img.shields.io/badge/C++-17-blue.svg)](https://en.cppreference.com/w/cpp/17)

---

## ⚡ Quick Start (5 Minutes!)

### Step 1: Build C++ Plugin (One Time)
```batch
Build_Plugin.bat
```
This downloads dependencies and builds the C++ plugin.

### Step 2: Launch (Every Time)
```batch
PLAY.bat
```
Choose your mode: Solo, Host, or Join!

### Step 3: Play Kenshi
1. Start Kenshi
2. Inject `bin/Release/Plugin/Re_Kenshi_Plugin.dll` into kenshi_x64.exe
3. Play with friends! 🎉

**📖 Full guide:** [EASY_START.md](EASY_START.md) | [EASY_START_RU.md](EASY_START_RU.md)

---

## 🌟 What is Kenshi Online?

**Kenshi Online v2.0** is a complete multiplayer modification for Kenshi, built from the ground up with:

- **True multiplayer** - Play with up to 32 friends
- **Server-authoritative** - No cheating, fair gameplay
- **Real-time sync** - See your friends' actions instantly
- **Easy setup** - One-click launcher, automated builds
- **Production-ready** - Professional architecture, fully documented

---

## ✨ Features

### 🎮 Game Modes

#### Solo Mode
- **One-click local multiplayer** for testing
- Server and client run automatically
- Perfect for mod development

#### Host Mode
- **Share with friends** using connection strings
- Auto-detects your IP address
- Configurable ports and player limits
- Up to 32 simultaneous players

#### Join Mode
- **Paste friend's connection string** and connect
- Saves server history (last 10 servers)
- Quick-join from recent servers
- Auto-reconnect support

### 🌐 Multiplayer Features

- **Entity Synchronization** - Players, NPCs, items, all synced
- **Combat System** - Real-time battles with friends
- **Inventory Management** - Share items, trade gear
- **World State** - Time, weather, game speed synchronized
- **Chat System** - Global, squad, proximity, whisper channels
- **Squad System** - Form parties up to 8 members
- **Friend System** - Manage up to 100 friends
- **Trading System** - Secure player-to-player trading
- **Admin Commands** - 20+ commands for server management

### ⚙️ Technical Features

- **20 Hz Server Updates** - Smooth, responsive gameplay
- **Delta Synchronization** - Only send changed data (80% bandwidth reduction)
- **Spatial Grid Optimization** - 100m cells for efficient range queries
- **Priority System** - Important entities update first
- **Session Management** - Automatic heartbeat and timeout detection
- **Persistent Settings** - Player name, preferences saved automatically

---

## 📦 What's Inside

```
Kenshi-Online/
├── PLAY.bat                    ← START HERE! One-click launcher
├── Build_Plugin.bat            ← Build C++ plugin (run once)
│
├── KenshiOnline.Launcher/      ← Unified C# launcher
│   ├── Program.cs             (Solo/Host/Join modes, all-in-one)
│   └── KenshiOnline.Launcher.csproj
│
├── KenshiOnline.Core/          ← Core game logic
│   ├── Entities/              (Player, NPC, Item entities)
│   ├── Synchronization/       (Combat, Inventory, World sync)
│   ├── Chat/                  (Multi-channel chat)
│   ├── Squad/                 (Party system)
│   ├── Social/                (Friends)
│   ├── Trading/               (Player trading)
│   └── Admin/                 (Server commands)
│
├── KenshiOnline.Server/        ← Standalone server (deprecated, use launcher)
├── KenshiOnline.ClientService/ ← Standalone client (deprecated, use launcher)
│
├── Re_Kenshi_Plugin/           ← C++ game plugin
│   ├── include/               (Headers)
│   ├── src/                   (Source files)
│   ├── vendor/                (Third-party libraries)
│   ├── Download_Dependencies.bat
│   └── CMakeLists.txt
│
└── Documentation/
    ├── EASY_START.md          ← Ultra-simple 5-minute guide
    ├── EASY_START_RU.md       ← Russian version
    ├── QUICK_START_V2.md      ← Detailed guide
    ├── QUICK_START_V2_RU.md   ← Russian detailed guide
    ├── KENSHI_ONLINE_V2.md    ← Full technical documentation
    ├── KENSHI_ONLINE_V2_RU.md ← Russian technical docs
    └── FEATURES.md            ← Complete feature list
```

---

## 🛠️ Requirements

### For Playing (Minimum)
- **Windows 10/11** (64-bit)
- **Kenshi** (Steam version recommended)
- **.NET 8.0 Runtime** - [Download](https://dotnet.microsoft.com/download/dotnet/8.0)
- **DLL Injector** - Process Hacker or Extreme Injector

### For Building C++ Plugin (One Time)
- **Visual Studio 2022** with C++ support - [Download](https://visualstudio.microsoft.com/)
- **CMake** 3.20+ - [Download](https://cmake.org/download/)
- **.NET 8.0 SDK** - [Download](https://dotnet.microsoft.com/download/dotnet/8.0)

The build scripts check for everything and guide you!

---

## 📚 Documentation

| Document | Description | Language |
|----------|-------------|----------|
| [EASY_START.md](EASY_START.md) | 3-minute quick start guide | 🇺🇸 English |
| [EASY_START_RU.md](EASY_START_RU.md) | Быстрый старт за 3 минуты | 🇷🇺 Russian |
| [QUICK_START_V2.md](QUICK_START_V2.md) | Detailed setup guide | 🇺🇸 English |
| [QUICK_START_V2_RU.md](QUICK_START_V2_RU.md) | Подробное руководство | 🇷🇺 Russian |
| [KENSHI_ONLINE_V2.md](KENSHI_ONLINE_V2.md) | Full technical documentation | 🇺🇸 English |
| [KENSHI_ONLINE_V2_RU.md](KENSHI_ONLINE_V2_RU.md) | Полная техническая документация | 🇷🇺 Russian |
| [FEATURES.md](FEATURES.md) | Complete feature list | 🇺🇸 English |
| [Re_Kenshi_Plugin/vendor/README.md](Re_Kenshi_Plugin/vendor/README.md) | C++ dependencies | 🇺🇸 English |

---

## 🚀 Architecture

### Three-Tier System

```
┌─────────────────────────────────────────────────────────────┐
│                       KENSHI GAME                           │
│                    (kenshi_x64.exe)                         │
└──────────────┬──────────────────────────────────────────────┘
               │ DLL Injection
               ↓
┌─────────────────────────────────────────────────────────────┐
│                  C++ PLUGIN (Tier 1)                        │
│               Re_Kenshi_Plugin.dll                          │
│  - Hooks into game memory                                   │
│  - Reads player/entity data                                 │
│  - Sends to Client Service via Named Pipes (IPC)           │
└──────────────┬──────────────────────────────────────────────┘
               │ Named Pipe IPC
               ↓
┌─────────────────────────────────────────────────────────────┐
│           C# CLIENT SERVICE (Tier 2)                        │
│            KenshiOnline.exe (Join mode)                     │
│  - Bridges between plugin and server                        │
│  - Manages network connection                               │
│  - Handles protocol translation                             │
└──────────────┬──────────────────────────────────────────────┘
               │ TCP Socket
               ↓
┌─────────────────────────────────────────────────────────────┐
│             C# GAME SERVER (Tier 3)                         │
│            KenshiOnline.exe (Host mode)                     │
│  - Server-authoritative game state                          │
│  - Entity management (players, NPCs, items)                 │
│  - Combat validation                                        │
│  - Inventory management                                     │
│  - World state synchronization                              │
│  - Admin commands                                           │
│  - Session management                                       │
└─────────────────────────────────────────────────────────────┘
```

### Unified Launcher

**NEW in v2.0!** Everything is now in **one executable**:

```
KenshiOnline.exe
├─ Solo Mode     → Runs local server + client automatically
├─ Host Mode     → Runs game server (Tier 3)
└─ Join Mode     → Runs client service (Tier 2)
```

No more separate server/client executables!

---

## 💻 Development

### Project Structure

- **KenshiOnline.Launcher** - C# unified launcher (new in v2.0)
- **KenshiOnline.Core** - Shared game logic library
- **KenshiOnline.Server** - Standalone server (legacy, use launcher)
- **KenshiOnline.ClientService** - Standalone client (legacy, use launcher)
- **Re_Kenshi_Plugin** - C++ game plugin

### Building from Source

```batch
# 1. Build C++ plugin
Build_Plugin.bat

# 2. Build C# projects (automatic via PLAY.bat)
# Or manually:
dotnet build KenshiOnline.sln -c Release

# 3. Run
PLAY.bat
```

### Development Tools

- **Visual Studio 2022** - Recommended IDE
- **VS Code** - Alternative for C# development
- **Visual Studio Code + C++ extension** - For plugin development
- **CMake GUI** - Visual CMake configuration

---

## 🤝 Contributing

We welcome contributions! See [CONTRIBUTING.md](CONTRIBUTING.md) for guidelines.

### Quick Contribution Guide

1. Fork the repository
2. Create a feature branch (`git checkout -b feature/amazing-feature`)
3. Commit your changes (`git commit -m 'Add amazing feature'`)
4. Push to the branch (`git push origin feature/amazing-feature`)
5. Open a Pull Request

---

## 📋 Roadmap

### v2.0 (Current) ✅
- [x] Unified launcher application
- [x] Solo/Host/Join modes
- [x] Server-authoritative combat
- [x] Inventory synchronization
- [x] World state sync
- [x] Chat system (5 channels)
- [x] Squad system (8 members)
- [x] Friend system (100 friends)
- [x] Trading system
- [x] Admin commands (20+)
- [x] Automated build system
- [x] Complete documentation (EN + RU)

### v2.1 (Planned) 🔮
- [ ] Voice chat integration
- [ ] Persistent world saves
- [ ] Building synchronization
- [ ] Full NPC AI synchronization
- [ ] Advanced admin web panel
- [ ] Steam integration
- [ ] Server browser
- [ ] Mod compatibility layer

### v3.0 (Future) 🌟
- [ ] MMO-style servers (100+ players)
- [ ] Dedicated server hosting service
- [ ] In-game marketplace
- [ ] Custom game modes
- [ ] Replay system
- [ ] Anti-cheat system

---

## 🐛 Troubleshooting

### Common Issues

**"PLAY.bat doesn't work"**
- Install .NET 8.0 SDK: https://dotnet.microsoft.com/download

**"Build_Plugin.bat fails"**
- Install Visual Studio 2022 with C++ support
- Install CMake 3.20+
- Run as Administrator

**"Can't connect to server"**
- Host must forward port in router
- Check firewall settings
- Verify IP address is correct

**"Plugin won't inject"**
- Run injector as Administrator
- Ensure Kenshi is running
- Check antivirus isn't blocking

**"Server crashes"**
- Check logs in bin/Release/
- Verify all players have same mod version
- Report issue with logs on GitHub

### Getting Help

- **GitHub Issues**: https://github.com/The404Studios/Kenshi-Online/issues
- **Discussions**: https://github.com/The404Studios/Kenshi-Online/discussions
- **Discord**: (Add your Discord invite link)

---

## 📜 License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

### Third-Party Licenses

- **nlohmann/json** - MIT License
- **.NET Runtime** - MIT License
- **CMake** - BSD 3-Clause License

---

## 🙏 Acknowledgments

- **Kenshi** by Lo-Fi Games
- **nlohmann/json** by Niels Lohmann
- **The Kenshi modding community**
- **All contributors and testers**

---

## 📊 Statistics

- **Lines of Code**: 15,000+
- **Files**: 50+
- **Documentation Pages**: 8
- **Supported Languages**: English, Russian
- **Build Time**: ~2 minutes
- **Setup Time**: ~5 minutes

---

## ⭐ Star History

If you find this project useful, please consider giving it a star on GitHub!

[![Star History Chart](https://api.star-history.com/svg?repos=The404Studios/Kenshi-Online&type=Date)](https://star-history.com/#The404Studios/Kenshi-Online&Date)

---

<div align="center">

**Made with ❤️ by the Kenshi community**

[Report Bug](https://github.com/The404Studios/Kenshi-Online/issues) · [Request Feature](https://github.com/The404Studios/Kenshi-Online/issues) · [Join Discord](#)

</div>
