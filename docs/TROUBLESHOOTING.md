# Troubleshooting Guide

This guide covers common issues, their causes, and solutions. Issues are organized by category and severity.

## Quick Diagnostics

Before diving into specific issues, run these checks:

```
1. Is the server running?
2. Is Kenshi running?
3. Is the client running?
4. Are you connected to the internet?
5. Is your firewall blocking connections?
```

## Client Issues

### Game crashes on inject

**Symptoms:**
- Kenshi crashes when client connects
- Game freezes then closes
- Error message about memory access

**Causes & Solutions:**

1. **Verify Kenshi version**
   ```
   Supported: 1.0.55+
   Check: Kenshi main menu shows version
   ```
   If version is wrong, update Kenshi through Steam/GOG.

2. **Run injector as administrator**
   - Right-click KenshiMultiplayer.exe
   - Select "Run as administrator"
   - Or set permanently: Properties → Compatibility → Run as administrator

3. **Disable overlays**

   **Steam Overlay:**
   - Steam → Right-click Kenshi → Properties
   - Uncheck "Enable Steam Overlay while in-game"

   **Discord Overlay:**
   - Discord → Settings → Game Activity
   - Disable overlay for Kenshi

   **NVIDIA GeForce Experience:**
   - GeForce Experience → Settings → In-Game Overlay → Off

   **AMD ReLive:**
   - Radeon Settings → ReLive → Off

4. **Antivirus interference**
   - Add exclusion for KenshiOnline folder
   - Add exclusion for Kenshi folder
   - Temporarily disable real-time scanning to test

5. **Conflicting mods**
   - Disable all Kenshi mods
   - Test with vanilla Kenshi
   - Re-enable mods one by one to find conflict

6. **Corrupted game files**
   ```
   Steam: Right-click Kenshi → Properties → Local Files → Verify integrity
   GOG: Use GOG Galaxy repair feature
   ```

### Cannot connect to server

**Symptoms:**
- "Connection refused" error
- "Connection timed out"
- Stuck on "Connecting..."

**Solutions:**

1. **Verify server address**
   ```json
   // client_config.json
   {
     "ServerAddress": "correct-ip-or-hostname",
     "ServerPort": 5555
   }
   ```

2. **Check server is running**
   - Contact server administrator
   - Try server status page if available

3. **Check your firewall**
   ```powershell
   # Windows - allow outbound TCP 5555
   netsh advfirewall firewall add rule name="Kenshi Online Client" dir=out action=allow protocol=TCP remoteport=5555
   ```

4. **Check your router/ISP**
   - Some ISPs block gaming ports
   - Try mobile hotspot to test
   - Contact ISP if consistently blocked

5. **Test network connectivity**
   ```bash
   # Test if port is reachable
   telnet server-ip 5555

   # Or use PowerShell
   Test-NetConnection -ComputerName server-ip -Port 5555
   ```

### Authentication failed

**Symptoms:**
- "Invalid credentials" error
- "Account not found"
- "Session expired"

**Solutions:**

1. **Check username/password**
   - Usernames are case-sensitive
   - Passwords are case-sensitive
   - No leading/trailing spaces

2. **Password reset**
   - Contact server administrator
   - Use password reset if available

3. **Session expired**
   - Log out and log in again
   - Restart client application

4. **Account banned**
   - Contact server administrator
   - Check server rules you may have violated

### Players not visible

**Symptoms:**
- Connected but don't see other players
- Other players appear/disappear randomly
- Delayed player positions

**Solutions:**

1. **Distance check**
   - Players outside 5km radius won't sync
   - Move closer to expected player locations

2. **High latency**
   - Check your ping to server
   - Close bandwidth-heavy applications
   - Use wired connection

3. **Desync issue**
   - Reconnect to server
   - Both players reconnect simultaneously

### Inventory not syncing

**Symptoms:**
- Items disappear from inventory
- Picked up items don't show
- Duplicate items

**Solutions:**

1. **Wait for sync**
   - Inventory is Tier 2 (1 Hz sync)
   - Changes may take up to 5 seconds

2. **Server authority**
   - Server owns inventory state
   - Client-side changes will be overwritten

3. **Reconnect**
   - Disconnect and reconnect
   - Full state sync on reconnect

### Combat not working correctly

**Symptoms:**
- Hits don't register
- Damage seems wrong
- Combat feels laggy

**Solutions:**

1. **Server validates combat**
   - Combat is server-authoritative (30 Hz)
   - Lag compensation is limited

2. **Range issues**
   - Server validates attack range
   - Visual attack may not match server range

3. **High ping**
   - Combat requires low latency
   - 100ms+ ping will feel delayed

## Server Issues

### Server won't start

**Symptoms:**
- Immediate crash on startup
- "Port already in use" error
- Configuration error

**Solutions:**

1. **Port conflict**
   ```powershell
   # Find process using port 5555
   netstat -ano | findstr 5555

   # Kill process if needed
   taskkill /PID [process-id] /F
   ```

2. **Invalid configuration**
   ```bash
   # Validate JSON syntax
   # Check for missing commas, brackets
   # Use JSON validator
   ```

3. **Missing dependencies**
   - Install .NET 8.0 Runtime
   - Check all DLLs present

4. **Insufficient permissions**
   - Run as administrator
   - Check folder permissions

### Players can't connect

**Symptoms:**
- Server running but no connections
- Timeouts for all players

**Solutions:**

1. **Firewall not configured**
   ```powershell
   netsh advfirewall firewall add rule name="Kenshi Online Server" dir=in action=allow protocol=TCP localport=5555
   ```

2. **Port not forwarded**
   - Access router admin
   - Forward TCP 5555 to server IP
   - Verify with port checker website

3. **Wrong bind address**
   ```json
   // Bind to all interfaces
   {
     "BindAddress": "0.0.0.0"
   }
   ```

4. **Server behind NAT**
   - Configure port forwarding
   - Or use VPN/tunneling solution

### High CPU usage

**Symptoms:**
- Server using 100% CPU
- Tick rate dropping
- Players experiencing lag

**Solutions:**

1. **Reduce tick rate**
   ```json
   {
     "TickRate": 10,
     "CombatTickRate": 20,
     "NPCTickRate": 5
   }
   ```

2. **Reduce sync scope**
   ```json
   {
     "MaxNPCsPerUpdate": 25,
     "InterestRadius": 3000
   }
   ```

3. **Reduce player limit**
   ```json
   {
     "MaxPlayers": 16
   }
   ```

4. **Check for infinite loops**
   - Review server logs
   - Check for error spam

### Memory leak

**Symptoms:**
- Memory usage grows over time
- Eventually crashes with out of memory

**Solutions:**

1. **Reduce state history**
   - Currently 100 states kept
   - May need code change to reduce

2. **Enable compression**
   ```json
   {
     "CompressSaves": true
   }
   ```

3. **Scheduled restarts**
   - Restart server daily
   - Use monitoring to detect growth

4. **Clean up disconnected players**
   - Ensure cleanup runs on disconnect
   - Check logs for cleanup errors

### Save corruption

**Symptoms:**
- Players lose progress
- Error loading save file
- Inconsistent state

**Solutions:**

1. **Restore from backup**
   ```bash
   # Stop server
   # Replace corrupted save with backup
   cp saves/backups/player_123.json saves/players/player.json
   # Start server
   ```

2. **Increase backup frequency**
   ```json
   {
     "AutoSaveInterval": 30,
     "BackupCount": 20
   }
   ```

3. **Check disk space**
   - Ensure adequate free space
   - Clean old backups if needed

4. **Report bug**
   - Collect logs around corruption time
   - Note what actions preceded it

## Network Issues

### High latency (ping)

**Symptoms:**
- Actions feel delayed
- Players teleport
- Combat is unplayable

**Solutions:**

1. **Geographic distance**
   - Choose closer servers
   - Host server in central location

2. **Network congestion**
   - Use wired connection
   - Close streaming/downloads
   - Check for network issues

3. **ISP routing**
   - Try VPN to different route
   - Contact ISP about gaming optimization

### Packet loss

**Symptoms:**
- Intermittent disconnects
- Missing state updates
- Rubber-banding

**Solutions:**

1. **Test connection**
   ```bash
   # Continuous ping test
   ping -t server-ip

   # Look for lost packets
   ```

2. **Router issues**
   - Restart router
   - Update router firmware
   - Check for overheating

3. **WiFi interference**
   - Use 5GHz if available
   - Move closer to router
   - Use wired connection

### Frequent disconnects

**Symptoms:**
- Connection drops every few minutes
- "Connection lost" errors

**Solutions:**

1. **Check timeout settings**
   ```json
   // Increase timeout (server)
   {
     "ConnectionTimeout": 60000
   }
   ```

2. **Network stability**
   - Test with other online games
   - Check for pattern (time of day, etc.)

3. **Firewall/antivirus**
   - May be terminating idle connections
   - Add exception for client

## Performance Issues

### Low FPS with multiplayer

**Symptoms:**
- Kenshi runs slower with client
- FPS drops near other players

**Solutions:**

1. **Reduce Kenshi graphics**
   - Lower draw distance
   - Reduce shadow quality
   - Disable ambient occlusion

2. **Reduce player count**
   - Interest radius limits help
   - Avoid large player gatherings

3. **Update graphics drivers**
   - Use latest stable drivers
   - Avoid beta drivers

### Client consuming high CPU

**Symptoms:**
- Client process using high CPU
- System feels slow

**Solutions:**

1. **Enable efficiency mode**
   - Task Manager → Details
   - Right-click client → Efficiency mode

2. **Reduce sync frequency**
   - Configured server-side
   - Contact server admin

## Debug Mode

For advanced troubleshooting, enable debug logging:

### Client Debug
```json
// client_config.json
{
  "Debug": true,
  "LogLevel": "Debug"
}
```

### Server Debug
```json
// server_config.json
{
  "Debug": true,
  "LogLevel": "Debug"
}
```

### Reading Logs
```
logs/debug.log - Verbose output
logs/error.log - Errors only
logs/network.log - Network events
```

### Common Log Patterns

**Healthy connection:**
```
[INFO] Connected to server
[INFO] Authenticated successfully
[INFO] State sync complete
```

**Authentication failure:**
```
[ERROR] Authentication failed: Invalid credentials
```

**Network issue:**
```
[WARN] High latency detected: 250ms
[ERROR] Connection timeout after 30000ms
```

**Desync:**
```
[WARN] Client state diverged, forcing resync
[INFO] Received server correction
```

## Getting Help

If this guide doesn't solve your issue:

1. **Collect information:**
   - Client/server version
   - Kenshi version
   - Windows version
   - Full error message
   - Steps to reproduce
   - Relevant log sections

2. **Check existing issues:**
   - GitHub Issues page
   - Community forums

3. **Report new issue:**
   - GitHub Issues with template
   - Include all collected information

4. **Community help:**
   - Discord server
   - Reddit community

## Known Issues

Current known issues and workarounds:

| Issue | Status | Workaround |
|-------|--------|------------|
| Overlay flickers with HDR | Investigating | Disable HDR |
| Memory grows on long sessions | Investigating | Restart every 4 hours |
| Some mods cause crashes | By design | Use vanilla or tested mods |
