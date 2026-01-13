# Quick Test Guide - Play in 10 Minutes

This guide gets you playing with a friend in the next 10 minutes.

## Prerequisites

- **Windows 10/11** (required for memory injection)
- **Kenshi 1.0.64** (64-bit version)
- **.NET 8.0 SDK** - [Download here](https://dotnet.microsoft.com/download/dotnet/8.0)
- **Visual Studio 2022** (optional, but makes building easier)

## Build (5 minutes)

### Option A: Command Line
```powershell
# Clone or download the repo
cd Kenshi-Online

# Build
cd Kenshi-Online
dotnet build -c Release

# The executable is at:
# Kenshi-Online/bin/Release/net8.0/KenshiOnline.exe
```

### Option B: Visual Studio
1. Open `Kenshi-Online.sln`
2. Set configuration to **Release**
3. Build > Build Solution (Ctrl+Shift+B)
4. Find `KenshiOnline.exe` in `bin/Release/net8.0/`

## Test with a Friend (5 minutes)

### Step 1: Host (You)

1. **Start Kenshi** and load any save
2. **Run as Administrator**: `KenshiOnline.exe`
3. Choose **1. Host Game (Quick Test Server)**
4. Server starts on port 7777
5. **Share your IP** with your friend
   - Find your IP: Open cmd and type `ipconfig`
   - Look for "IPv4 Address" (e.g., 192.168.1.100)
   - If on different networks, use your public IP from [whatismyip.com](https://www.whatismyip.com)

### Step 2: Friend (Them)

1. **Start Kenshi** and load any save
2. **Run as Administrator**: `KenshiOnline.exe`
3. Choose **2. Join Game (Quick Test Client)**
4. Enter your IP address when prompted
5. Enter a player name

### Step 3: Play!

Once connected, you should see:
- Your position syncing (shown in console)
- Other players count updating

Move around in Kenshi - your friend will see your position updates!

## Troubleshooting

### "Could not connect to Kenshi"
- Make sure Kenshi is running (kenshi_x64.exe)
- **Run KenshiOnline.exe as Administrator** (right-click > Run as administrator)

### "Could not connect to server"
- Check the IP address is correct
- Make sure **port 7777 is open** on the host's firewall
- If on different networks, host needs to **port forward 7777**

### "Position not updating"
- Make sure you're in-game (not in menus)
- Try selecting your character

## Port Forwarding (for playing over internet)

If you're on different networks (not same WiFi):

1. Host needs to access their router settings (usually 192.168.1.1)
2. Find "Port Forwarding" or "NAT"
3. Add a rule:
   - External Port: 7777
   - Internal Port: 7777
   - Protocol: TCP
   - Internal IP: Your computer's local IP

## Command Line Usage

```bash
# Quick test server (port 7777)
KenshiOnline.exe --test-server

# Quick test server on custom port
KenshiOnline.exe --test-server 8888

# Quick test client (localhost)
KenshiOnline.exe --test-client

# Quick test client (specific IP)
KenshiOnline.exe --test-client 192.168.1.100

# Quick test client (specific IP and port)
KenshiOnline.exe --test-client 192.168.1.100 8888
```

## What Works in Quick Test

- Position synchronization (your X, Y, Z coordinates)
- Multiple players connecting
- Basic server/client architecture

## What Doesn't Work Yet

- Seeing other players visually in Kenshi (positions sync, but no character rendering)
- Combat synchronization
- Inventory synchronization
- NPC synchronization

The Quick Test proves the network connection works. The full version (options 3/4) has more features but is more complex.

## Next Steps

Once Quick Test works:
1. Try the **Full Server** (option 3) for more features
2. Try the **Full Client** (option 4) for spawning, combat, etc.

## Getting Help

- Discord: [Join our server](https://discord.gg/W2K7GhmD)
- GitHub Issues: [Report bugs](https://github.com/The404Studios/Kenshi-Online/issues)
