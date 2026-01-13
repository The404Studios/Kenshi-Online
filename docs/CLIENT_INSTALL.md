# Client Installation Guide

## Requirements

### System Requirements
- Windows 10/11 (64-bit)
- Kenshi installed (Steam or GOG version)
- 4GB RAM minimum
- Stable internet connection

### Kenshi Version Compatibility
- **Supported**: Kenshi 1.0.55 and later
- **Recommended**: Latest stable version
- **Not Supported**: Experimental branches

## Installation Methods

### Method 1: Release Download (Recommended)

1. **Download the latest release**
   - Go to Releases page
   - Download `KenshiOnline-Client-vX.X.X.zip`

2. **Extract files**
   ```
   Extract to: C:\KenshiOnline\
   ```

3. **Verify files exist**
   ```
   C:\KenshiOnline\
   ├── KenshiMultiplayer.exe
   ├── KenshiMultiplayer.dll
   └── client_config.json
   ```

### Method 2: Build from Source

```bash
# Clone repository
git clone https://github.com/The404Studios/Kenshi-Online.git
cd Kenshi-Online

# Build client
dotnet build -c Release

# Copy output
# From: bin/Release/net8.0/
# To: Your preferred location
```

## Configuration

### Create client_config.json
```json
{
  "ServerAddress": "your-server-ip",
  "ServerPort": 5555,
  "Username": "YourUsername",
  "KenshiPath": "C:\\Program Files (x86)\\Steam\\steamapps\\common\\Kenshi",
  "AutoConnect": false,
  "Language": "en"
}
```

### Configuration Options

| Option | Description | Default |
|--------|-------------|---------|
| ServerAddress | Server IP or hostname | localhost |
| ServerPort | Server TCP port | 5555 |
| Username | Display name | Player |
| KenshiPath | Kenshi install directory | Auto-detect |
| AutoConnect | Connect on launch | false |
| Language | UI language | en |

### Finding Kenshi Path

**Steam:**
```
C:\Program Files (x86)\Steam\steamapps\common\Kenshi
```

**GOG:**
```
C:\GOG Games\Kenshi
```

**Custom:** Right-click Kenshi in Steam → Properties → Local Files → Browse

## Connecting to a Server

### Step 1: Start Kenshi
Launch Kenshi normally through Steam or GOG.

### Step 2: Start Client
```bash
# Run the client application
./KenshiMultiplayer.exe
```

### Step 3: Connect
1. Client will detect running Kenshi
2. Enter server address if not in config
3. Click "Connect" or use auto-connect

### Step 4: Authenticate
- First time: Register new account
- Returning: Login with credentials

### Step 5: Play
Once connected:
- Load your save or create new character
- Other players will appear in your game
- Actions sync automatically

## First-Time Setup

### 1. Account Registration
```
Username: [your username]
Password: [secure password]
Confirm:  [repeat password]
```

### 2. Character Creation
- Create character in Kenshi as normal
- Character data syncs to server
- Server saves your progress

### 3. Initial Sync
First connection may take longer as server sends world state.

## Overlay Interface

Once connected, an overlay shows:
- Connected players
- Your connection status
- Quick chat
- Server info

### Hotkeys
| Key | Action |
|-----|--------|
| F9 | Toggle overlay |
| Enter | Open chat |
| Tab | Player list |
| Esc | Close menus |

## Troubleshooting Installation

### "Kenshi not found"
**Cause:** Incorrect KenshiPath in config

**Fix:**
1. Find your Kenshi installation
2. Update client_config.json:
```json
{
  "KenshiPath": "C:\\Path\\To\\Your\\Kenshi"
}
```

### "Connection refused"
**Cause:** Server not running or wrong address

**Fix:**
1. Verify server is running
2. Check ServerAddress in config
3. Check ServerPort matches server
4. Verify firewall allows connection

### "Authentication failed"
**Cause:** Wrong credentials or account issue

**Fix:**
1. Check username/password
2. Try registering new account
3. Contact server admin if banned

### "Game crashed on inject"
**Causes:** Version mismatch, overlays, antivirus

**Fix:**
1. Verify Kenshi version is supported
2. Disable Steam overlay (see below)
3. Disable Discord overlay (see below)
4. Add exception to antivirus
5. Run as administrator

### Disabling Steam Overlay
1. Right-click Kenshi in Steam
2. Properties
3. General tab
4. Uncheck "Enable the Steam Overlay"

### Disabling Discord Overlay
1. Open Discord Settings
2. Game Activity
3. Disable overlay for Kenshi

### "DLL not found"
**Cause:** Missing .NET runtime

**Fix:**
1. Install .NET 8.0 Runtime
2. Download from: https://dotnet.microsoft.com/download/dotnet/8.0

### "Access denied"
**Cause:** Insufficient permissions

**Fix:**
1. Right-click KenshiMultiplayer.exe
2. Run as Administrator
3. Or: Right-click → Properties → Compatibility → Run as administrator

## Antivirus Configuration

Some antivirus software may flag the client due to memory injection.

### Windows Defender
1. Open Windows Security
2. Virus & threat protection
3. Manage settings
4. Add exclusion → Folder
5. Select KenshiOnline folder

### Common Antivirus
Add exclusions for:
- `KenshiMultiplayer.exe`
- `KenshiOnline` folder
- Kenshi game folder

## Performance Tips

### Reduce Network Lag
- Use wired connection instead of WiFi
- Close bandwidth-heavy applications
- Choose servers geographically close

### Reduce Game Lag
- Lower Kenshi graphics settings
- Disable unnecessary overlays
- Close background applications

### Memory Usage
- Restart client periodically on long sessions
- Keep Kenshi save file size reasonable

## Updating

### Automatic Updates
Client checks for updates on launch.
- Prompted to update when available
- Updates download automatically

### Manual Update
1. Download new release
2. Close client and Kenshi
3. Replace old files with new
4. Keep client_config.json (merge if needed)

## Uninstalling

1. Close Kenshi and client
2. Delete KenshiOnline folder
3. (Optional) Remove from antivirus exclusions

**Note:** Server keeps your save data. Reinstalling client won't lose progress.

## Getting Help

### Before Asking for Help
1. Check [Troubleshooting](TROUBLESHOOTING.md)
2. Read error messages carefully
3. Check server is online

### Reporting Issues
Include:
- Client version
- Kenshi version
- Windows version
- Error messages
- Steps to reproduce

### Community Resources
- GitHub Issues
- Discord server
- Wiki documentation

## Next Steps

- [Troubleshooting](TROUBLESHOOTING.md) - Detailed problem solving
- [Architecture](ARCHITECTURE.md) - How it works
- [Server Setup](SERVER_SETUP.md) - Host your own server
