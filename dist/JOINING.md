# Kenshi-Online - Multiplayer for Kenshi
### made with love by fourzerofour

---

## What's in the zip

| File | Purpose |
|------|---------|
| `KenshiMP.Injector.exe` | Launcher - sets up everything and starts Kenshi |
| `KenshiMP.Core.dll` | Multiplayer plugin loaded by Kenshi's Ogre engine |
| `KenshiMP.Server.exe` | Dedicated server (run on your PC or a VPS) |
| `kenshi-online.mod` | Game mod with player character templates |
| `install.bat` | One-click installer (alternative to the Injector) |
| `uninstall.bat` | Clean removal - restores vanilla Kenshi |

---

## Installation

### Option A: Use the Injector (recommended)

1. Extract the zip anywhere
2. Run `KenshiMP.Injector.exe`
3. Set your **player name** and **server address**
4. Click **PLAY**

The injector automatically:
- Copies `KenshiMP.Core.dll` into your Kenshi folder
- Adds the plugin to `Plugins_x64.cfg`
- Installs `kenshi-online.mod` (player templates)
- Adds it to your mod load list
- Launches Kenshi

### Option B: Use install.bat

1. Extract the zip into any folder
2. Run `install.bat` as Administrator
3. It auto-detects your Kenshi folder and installs everything
4. Launch Kenshi normally after installation

---

## Hosting a Game

Anyone can host. No port forwarding needed if your router supports UPnP.

1. Run `KenshiMP.Server.exe` on your PC (or copy it to a VPS)
2. Launch Kenshi with multiplayer (Injector or install.bat)
3. Click **MULTIPLAYER** on the main menu
4. Click **HOST GAME** or connect to `localhost:27800`
5. Click **NEW GAME** to load the world
6. Share your IP with friends (check https://whatismyip.com)

The server auto-saves every 60 seconds. Port **27800 UDP**.

### If UPnP doesn't work

Forward port **27800 UDP** on your router:
1. Open router settings (usually 192.168.1.1)
2. Find Port Forwarding / NAT
3. Add: External 27800, Internal 27800, Protocol UDP, to your PC's local IP

### Server on a VPS

1. Copy `KenshiMP.Server.exe` and optionally `server.json` to your VPS
2. Edit `server.json`:
```json
{
  "serverName": "My Kenshi Server",
  "port": 27800,
  "maxPlayers": 16,
  "pvpEnabled": true,
  "gameSpeed": 1.0
}
```
3. Run: `./KenshiMP.Server.exe`
4. Open port **27800 UDP** in your firewall

---

## Joining a Game

1. Launch Kenshi with multiplayer enabled
2. Click **MULTIPLAYER** on the main menu
3. Click **JOIN GAME**
4. Enter the host's IP address and port (default: 27800)
5. Click **CONNECT**
6. Click **NEW GAME** to load the world
7. You auto-connect when the game loads

### Server Browser

1. Click **MULTIPLAYER** > **Server Browser**
2. Click **Join** next to any online server
3. Or type an IP directly in the **Direct IP** box (`ip:port`)

---

## In-Game Controls

| Key | Action |
|-----|--------|
| F1 | Open/close multiplayer menu |
| Tab | Toggle player list |
| Enter | Open/close chat |
| Insert | Toggle debug/loading log |
| ` (backtick) | Toggle debug overlay |
| Escape | Close current panel |

---

## Client Commands (type in chat)

| Command | Description |
|---------|-------------|
| `/connect <ip:port>` | Connect to a server |
| `/disconnect` | Disconnect from server |
| `/players` | List connected players |
| `/tp <x> <y> <z>` | Teleport to coordinates |
| `/pos` | Show your current position |
| `/ping` | Show latency to server |
| `/status` | Show connection status |
| `/entities` | List synced entities |
| `/time` | Show game time |
| `/debug` | Toggle debug info |
| `/help` | Show all commands |

---

## Server Console Commands

| Command | Description |
|---------|-------------|
| `status` | Show server status |
| `players` | List connected players |
| `kick <id>` | Kick a player by ID |
| `say <msg>` | Broadcast system message |
| `save` | Manual world save |
| `stop` | Shutdown server gracefully |

---

## Leaving a Game

- Press **F1** > **Disconnect**
- Or just close Kenshi (the server keeps running, your position is saved)

---

## Troubleshooting

**Can't connect?**
- Make sure the server is running (console window open)
- Check port 27800 UDP is forwarded / UPnP enabled
- Verify the IP is correct (external IP for internet, local IP for LAN)

**Game crashes on join?**
- Make sure both players have Kenshi-Online installed
- Always click **NEW GAME** when joining, not Load Game
- Check that `kenshi-online.mod` is installed (the Injector does this automatically)

**Other player is invisible?**
- Remote players spawn automatically using the `kenshi-online.mod` character templates
- Make sure `kenshi-online` appears in your mod list (the Injector handles this)
- If spawning takes more than 15 seconds, try walking near a town or NPC camp
- Check the debug log (Insert key) for "MOD TEMPLATE" or "NPC HIJACK" messages

**Server shows "0 players" but you connected?**
- The connection happens after the world loads — click NEW GAME first
- Check the debug log for connection status

**Mod conflicts?**
- Kenshi-Online is compatible with most mods
- Load order doesn't matter for `kenshi-online.mod`
- If you have issues, try disabling other mods temporarily

**Running two instances on one PC (for testing)?**
- Launch Kenshi normally for instance 1
- Open a command prompt in your Kenshi folder and run: `start "" "kenshi_x64.exe"`
- Each instance gets its own config file automatically

---

## Uninstalling

Run `uninstall.bat` — it removes all files and restores vanilla Kenshi.

Or manually:
1. Delete `KenshiMP.Core.dll` from your Kenshi folder
2. Remove `Plugin=KenshiMP.Core` from `Plugins_x64.cfg`
3. Delete `data/kenshi-online.mod` and `mods/kenshi-online/`
4. Remove `kenshi-online` from `data/__mods.list`

---

## Technical Details

- Protocol: ENet over UDP, port 27800
- Max players: 16
- Tick rate: 20 Hz (50ms)
- Auto-save: every 60 seconds
- Sync: positions, combat, items, buildings, squads, chat
