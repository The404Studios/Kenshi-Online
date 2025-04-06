# Kenshi Online - Installation and Usage Tutorial

![Kenshi Online Banner](https://via.placeholder.com/800x200?text=Kenshi+Online+Multiplayer+Mod)

Join our [Discord server](https://discord.gg/62aDDmtkgb) for support and updates!

## Introduction

This tutorial will guide you through setting up and using Kenshi Online, a multiplayer mod for Kenshi that lets you play with friends in the same world. This guide is intended for both server hosts and players who want to connect to existing servers.

## Table of Contents

1. [Requirements](#requirements)
2. [Installation Guide](#installation-guide)
   - [Server Installation](#server-installation)
   - [Client Installation](#client-installation)
3. [Hosting a Server](#hosting-a-server)
4. [Connecting to a Server](#connecting-to-a-server)
5. [Playing with Friends](#playing-with-friends)
6. [Troubleshooting](#troubleshooting)

## Requirements

### For Server Hosts
- Windows 10/11 (64-bit)
- .NET 8.0 Runtime or SDK
- Kenshi game installation (version 1.0.59+)
- Minimum 16GB RAM recommended
- Port forwarding capabilities on your router
- Static IP address (recommended) or dynamic DNS service

### For Players
- Windows 10/11 (64-bit)
- Kenshi game (version 1.0.59+)
- Minimum 8GB RAM
- Internet connection

## Installation Guide

### Server Installation

1. **Download the Server Package**
   - Download the latest release from [GitHub Releases](https://github.com/The404Studios/Kenshi-Online/releases)
   - Extract the ZIP file to a directory of your choice

2. **Verify .NET Installation**
   - Make sure you have .NET 8.0 Runtime installed
   - You can download it from the [Microsoft .NET download page](https://dotnet.microsoft.com/download/dotnet/8.0)
   - Verify installation by running `dotnet --version` in a command prompt

3. **Initial Setup**
   - Navigate to the extracted server directory
   - Run `KenshiMultiplayer.exe`
   - The first launch will create necessary configuration files

### Client Installation

1. **Download the Client Mod**
   - Download the client package from [GitHub Releases](https://github.com/The404Studios/Kenshi-Online/releases)
   - Extract the contents to a temporary location

2. **Install the Mod**
   - Locate your Kenshi installation directory:
     - Steam: Right-click on Kenshi in Steam → Properties → Local Files → Browse
     - GOG: Find the installation folder you chose during installation
   - Navigate to the `mods` folder in your Kenshi directory
     - If the folder doesn't exist, create it
   - Create a new folder named `KenshiOnline` in the `mods` directory
   - Copy all files from the client package into this folder

3. **Configure the Mod**
   - Launch Kenshi
   - At the main menu, click on "Mods"
   - Enable "Kenshi Online" in the mod list
   - Apply changes and restart the game when prompted

## Hosting a Server

1. **Start the Server**
   - Run `KenshiMultiplayer.exe` from the server package
   - Select option `1` to start the server
   - Enter the path to your Kenshi installation when prompted
   - Choose a port number (default: 5555)

2. **Configure Port Forwarding**
   - Access your router's configuration page (typically 192.168.1.1 or 192.168.0.1)
   - Log in with your credentials
   - Find the port forwarding section (may be under Advanced Settings)
   - Create a new rule:
     - Protocol: TCP
     - External port: 5555 (or your chosen port)
     - Internal port: 5555 (or your chosen port)
     - Internal IP: Your computer's local IP address
   - Save the changes

3. **Find Your IP Address**
   - Visit [whatismyip.com](https://www.whatismyip.com/) to find your external IP
   - This is the address players will use to connect to your server
   - If you have a dynamic IP, consider using a dynamic DNS service

4. **Server Administration**
   - The server console supports various commands for administration:
     - `/help` - Show list of commands
     - `/list` - Show connected players
     - `/kick <username>` - Kick a player from the server
     - `/ban <username> <hours>` - Ban a player for the specified hours
     - `/create-lobby <id> <isPrivate> <password> <maxPlayers>` - Create a new lobby
     - `/list-lobbies` - List all active lobbies
     - `/broadcast <message>` - Send a message to all connected players

## Connecting to a Server

1. **Launch Kenshi with the Mod**
   - Start Kenshi with the Kenshi Online mod enabled
   - From the main menu, you'll see a new option: "Multiplayer"
   - Click on "Multiplayer" to access the multiplayer menu

2. **Register or Login**
   - If it's your first time, select "Register" to create an account
   - Fill in your desired username, password, and email
   - If you already have an account, select "Login"
   - Enter your credentials and connect

3. **Enter Server Details**
   - Enter the server's IP address (provided by the server host)
   - Enter the port (default: 5555, or as specified by the host)
   - Click "Connect" to join the server

4. **Character Creation/Selection**
   - Create a new character or select an existing one
   - The character creation process works similarly to the base game
   - Once your character is ready, click "Play" to enter the world

## Playing with Friends

1. **In-Game Communication**
   - Open the chat window by pressing `Enter`
   - Type messages to communicate with other players
   - Chat commands:
     - `/g <message>` - Global chat visible to all players
     - `/p <message>` - Proximity chat for nearby players
     - `/w <username> <message>` - Whisper to a specific player
     - `/f <message>` - Faction chat (if you're in a faction)

2. **Meeting Up**
   - Coordinate your location with friends via chat
   - Use the `/coords` command to share your current map coordinates
   - The map is the same as in single-player, so familiar landmarks can help

3. **Forming a Squad**
   - Approach another player and press the `F` key to interact
   - Select "Invite to Squad" from the interaction menu
   - The other player must accept your invitation
   - Squad members will appear with a special indicator above their characters

4. **Shared Gameplay**
   - Build bases together by placing structures near each other
   - Share resources by dropping them on the ground or using containers
   - Fight together against enemies with coordinated combat
   - Research and craft items to help each other

## Troubleshooting

### Connection Issues

**Problem**: Can't connect to the server
**Solutions**:
- Verify the server is running
- Double-check the IP address and port
- Ensure the mod is properly installed and enabled
- Check if your firewall is blocking the connection
- Verify port forwarding is correctly set up (for server hosts)

**Problem**: Disconnecting frequently
**Solutions**:
- Check your internet connection stability
- Reduce the number of players on the server
- Ensure your computer meets the minimum requirements
- Try restarting both the client and server

### Gameplay Issues

**Problem**: Character desynchronization
**Solutions**:
- Use the `/resync` command to force a state update
- Log out and log back in
- Ask the server host to restart the server

**Problem**: Missing items or inventory issues
**Solutions**:
- Use the `/fixinventory` command
- Log out and log back in
- Check if there are any known issues on the Discord server

**Problem**: Can't see other players
**Solutions**:
- Make sure you're in the same game world/lobby
- Use `/playerlist` to see if others are connected
- Try teleporting to a major city as a meeting point

### Server Issues

**Problem**: Server won't start
**Solutions**:
- Verify .NET 8.0 is installed correctly
- Check that the path to Kenshi is correct
- Run the server as Administrator
- Look for error messages in `server_log.txt`

**Problem**: High server lag
**Solutions**:
- Reduce the maximum number of players
- Ensure your server machine meets the recommended specs
- Close unnecessary applications
- Consider upgrading your RAM if you're experiencing memory issues

## Additional Resources

- **Discord Support**: Join our [Discord server](https://discord.gg/62aDDmtkgb) for live support
- **GitHub Issues**: Report bugs on our [GitHub Issues page](https://github.com/The404Studios/Kenshi-Online/issues)
- **Wiki**: Check our [community wiki](https://github.com/The404Studios/Kenshi-Online/wiki) for more guides

---

Thank you for using Kenshi Online! We hope you enjoy exploring the wasteland with your friends.

<div align="center">
  <p><i>Developed by The404Studios</i></p>
  <p><small>Kenshi is a property of Lo-Fi Games. This modification is unofficial and not affiliated with Lo-Fi Games.</small></p>
</div>
