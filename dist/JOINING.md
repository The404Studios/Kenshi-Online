# KenshiMP - Multiplayer for Kenshi
### made with love by fourzerofour

---

## Installation

1. Extract the KenshiMP zip into any folder
2. Run `install.bat` as Administrator
3. The installer auto-detects your Kenshi folder and sets everything up
4. To remove, run `uninstall.bat`

---

## Hosting a Game

Anyone can host! No port forwarding needed (UPnP handles it automatically).

1. Launch Kenshi normally
2. Click **MULTIPLAYER** on the main menu
3. Click **HOST GAME**
   - The dedicated server starts automatically
   - Port 27800 is auto-forwarded via UPnP on your router
4. Click **NEW GAME** to load the world
5. Share your IP address with friends
   - Your external IP is shown in the server console window
   - Or check https://whatismyip.com

The server auto-saves every 60 seconds.

### If UPnP doesn't work

Some routers have UPnP disabled. You'll need to forward port **27800 UDP** manually:
1. Open your router settings (usually 192.168.1.1)
2. Find Port Forwarding / NAT settings
3. Add: External port 27800, Internal port 27800, Protocol UDP, to your PC's local IP

---

## Joining a Game

1. Launch Kenshi normally
2. Click **MULTIPLAYER** on the main menu
3. Click **JOIN GAME**
4. Enter the host's IP address and port (default: 27800)
5. Click **CONNECT**
6. Click **NEW GAME** to load the world
7. You'll auto-connect when the game loads!

### Using the Server Browser

1. Click **MULTIPLAYER** > **Server Browser**
2. The browser queries known servers and shows player counts in real-time
3. Click **Join** next to any online server
4. Or type an IP directly in the "Direct IP" box (format: `ip:port`)

---

## In-Game Controls

| Key | Action |
|-----|--------|
| F1 | Open/close multiplayer menu |
| Tab | Toggle player list |
| Enter | Open chat |
| ` (backtick) | Debug overlay |
| Escape | Close current panel / go back |

---

## Leaving a Game

- Press **F1** to open the menu, then click **Disconnect**
- Or just close Kenshi normally (the server keeps running)
- The server auto-saves your position

---

## Troubleshooting

**Can't connect?**
- Make sure the host's server is running (console window should be open)
- Check that port 27800 UDP is forwarded (or UPnP is enabled on the router)
- Try pinging the host's IP to verify network connectivity

**Server browser shows 0/0?**
- The server might not be running
- Make sure you're querying the correct IP and port

**Game crashes on join?**
- Make sure both players have KenshiMP installed (run `install.bat`)
- Always click **NEW GAME** when joining, not Load Game

**Other player is invisible?**
- Entity spawning is still in development
- You should see each other's names and positions in the player list (Tab)

---

## Server Console Commands

When hosting, the server console window supports:

| Command | Description |
|---------|-------------|
| `status` | Show server status |
| `players` | List connected players |
| `kick <id>` | Kick a player by ID |
| `say <msg>` | Broadcast system message |
| `save` | Manual save |
| `stop` | Shutdown server |

---

## Technical Details

- Protocol: ENet over UDP, port 27800
- Max players: 16
- Tick rate: 20 Hz
- Auto-save: every 60 seconds
- UPnP: automatic port mapping (no manual forwarding needed)
- Save location: `kenshi_mp_world.json` in the server directory
