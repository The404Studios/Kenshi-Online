# Server Setup Guide

## Overview

The Kenshi Online server is the authoritative source for all game state. It must be running for clients to connect and play together.

## System Requirements

### Minimum
- Windows 10/11 (64-bit)
- 2 CPU cores
- 4GB RAM
- 10GB storage
- 10 Mbps upload bandwidth

### Recommended (10+ players)
- 4+ CPU cores
- 8GB RAM
- SSD storage
- 100 Mbps upload bandwidth

## Quick Start

### 1. Download/Build
```bash
# Option A: Use release build
# Download from releases page

# Option B: Build from source
git clone https://github.com/The404Studios/Kenshi-Online.git
cd Kenshi-Online
dotnet build -c Release
```

### 2. Configure Server
Create `server_config.json`:
```json
{
  "Port": 5555,
  "MaxPlayers": 32,
  "ServerName": "My Kenshi Server",
  "Password": "",
  "AdminPassword": "changeme",
  "AutoSaveInterval": 60,
  "TickRate": 20
}
```

### 3. Start Server
```bash
cd bin/Release/net8.0
./KenshiMultiplayer.exe --server
```

## Configuration Options

### Network Settings
```json
{
  "Port": 5555,              // TCP port (default 5555)
  "MaxPlayers": 32,          // Maximum concurrent players
  "BufferSize": 16384,       // Network buffer size in bytes
  "ConnectionTimeout": 30000 // Client timeout in ms
}
```

### Authentication
```json
{
  "RequireAuthentication": true,
  "JWTSecret": "your-secret-key-min-32-chars-long",
  "TokenExpiry": 3600,       // Token lifetime in seconds
  "AllowRegistration": true   // Allow new accounts
}
```

### Save Settings
```json
{
  "SavePath": "./saves",
  "AutoSaveInterval": 60,    // Seconds between auto-saves
  "BackupCount": 10,         // Number of backups to keep
  "CompressSaves": true
}
```

### Performance Tuning
```json
{
  "TickRate": 20,            // State updates per second
  "CombatTickRate": 30,      // Combat updates per second
  "NPCTickRate": 10,         // NPC updates per second
  "MaxNPCsPerUpdate": 50,    // NPCs synced per tick
  "InterestRadius": 5000     // Sync radius in game units
}
```

## Port Configuration

### Required Ports
- **TCP 5555** - Main game traffic (configurable)

### Firewall Setup (Windows)
```powershell
# Allow inbound TCP on port 5555
netsh advfirewall firewall add rule name="Kenshi Online Server" dir=in action=allow protocol=TCP localport=5555
```

### Router Port Forwarding
1. Access router admin (usually 192.168.1.1)
2. Find Port Forwarding / NAT settings
3. Add rule:
   - Protocol: TCP
   - External Port: 5555
   - Internal Port: 5555
   - Internal IP: Your server's local IP

### Verify Port is Open
```bash
# From another machine
nc -zv your-server-ip 5555

# Or use online port checker
# https://www.yougetsignal.com/tools/open-ports/
```

## Running as a Service

### Windows Service
1. Install NSSM (Non-Sucking Service Manager):
```powershell
choco install nssm
```

2. Create service:
```powershell
nssm install KenshiOnline "C:\path\to\KenshiMultiplayer.exe" "--server"
nssm set KenshiOnline AppDirectory "C:\path\to"
nssm set KenshiOnline Start SERVICE_AUTO_START
```

3. Start service:
```powershell
nssm start KenshiOnline
```

### Using Task Scheduler
1. Open Task Scheduler
2. Create Basic Task
3. Trigger: At startup
4. Action: Start a program
5. Program: `KenshiMultiplayer.exe`
6. Arguments: `--server`
7. Check "Run with highest privileges"

## Server Commands

While server is running:
```
help                    - Show available commands
status                  - Server status and player count
players                 - List connected players
kick <player>           - Kick a player
ban <player>            - Ban a player
save                    - Force save all data
reload config           - Reload configuration
shutdown                - Graceful shutdown
```

## Monitoring

### Server Logs
```
logs/
├── server.log          # Main server log
├── auth.log            # Authentication events
├── error.log           # Errors only
└── debug.log           # Verbose (debug mode)
```

### Log Levels
Set in config:
```json
{
  "LogLevel": "Info"     // Debug, Info, Warning, Error
}
```

### Health Checks
Server exposes status endpoint:
```
http://localhost:8080/status

Response:
{
  "status": "healthy",
  "players": 5,
  "uptime": 3600,
  "tickRate": 20
}
```

## Backup Strategy

### Automatic Backups
Configured via `AutoSaveInterval` and `BackupCount`.

### Manual Backup
```bash
# Stop server gracefully
# Copy saves/ directory
cp -r saves/ saves_backup_$(date +%Y%m%d)/
# Restart server
```

### Restore from Backup
1. Stop server
2. Replace saves/ with backup
3. Start server

## Security Recommendations

### 1. Change Default Passwords
```json
{
  "AdminPassword": "strong-unique-password",
  "JWTSecret": "at-least-32-characters-random-string"
}
```

### 2. Use Strong JWT Secret
```bash
# Generate random secret
openssl rand -base64 32
```

### 3. Limit Admin Access
- Use separate admin password
- Don't share admin credentials
- Monitor admin actions in logs

### 4. Keep Updated
- Check for security updates
- Update dependencies regularly

### 5. Network Security
- Use firewall rules
- Consider VPN for private servers
- Monitor for unusual traffic

## Troubleshooting

### Server won't start
**Check port availability:**
```bash
netstat -an | findstr 5555
```
If port in use, change port or stop conflicting service.

### Players can't connect
**Verify server is reachable:**
```bash
# On server
netstat -an | findstr LISTEN | findstr 5555

# From client network
telnet server-ip 5555
```

**Check firewall:**
```bash
netsh advfirewall firewall show rule name="Kenshi Online Server"
```

### High CPU usage
- Reduce `TickRate` to 10
- Reduce `MaxNPCsPerUpdate` to 25
- Reduce `InterestRadius` to 3000

### Memory growing
- Reduce `BackupCount`
- Enable `CompressSaves`
- Reduce `MaxPlayers`

### Save corruption
1. Stop server
2. Check logs for errors
3. Restore from backup
4. Report issue with logs

## Cloud Deployment

### AWS EC2
- Instance: t3.medium or larger
- Storage: gp3 SSD
- Security Group: Allow TCP 5555

### Azure VM
- Size: B2ms or larger
- Disk: Standard SSD
- NSG: Allow TCP 5555

### Digital Ocean
- Droplet: 4GB RAM or larger
- Enable backups
- Configure firewall

## Next Steps

- [Client Install](CLIENT_INSTALL.md) - Help players connect
- [Troubleshooting](TROUBLESHOOTING.md) - Common issues
- [Architecture](ARCHITECTURE.md) - Technical details
