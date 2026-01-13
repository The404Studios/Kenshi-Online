using System;
using System.Collections.Generic;
using System.Collections.Concurrent;
using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;
using System.Threading;
using System.Diagnostics;
using System.Runtime.InteropServices;

namespace KenshiMultiplayer
{
    /// <summary>
    /// HOST-AUTHORITATIVE CO-OP
    ///
    /// Only the HOST runs Kenshi. Clients send inputs, receive view state.
    /// This is the only realistic way to add multiplayer to Kenshi.
    ///
    /// Usage:
    ///   Host:   KenshiOnline.exe --host
    ///   Client: KenshiOnline.exe --join [host-ip]
    /// </summary>
    public static class HostCoop
    {
        public static void RunHost(int port = 7777)
        {
            Console.Clear();
            Console.WriteLine("╔════════════════════════════════════════════════════════════╗");
            Console.WriteLine("║         KENSHI ONLINE - HOST MODE                          ║");
            Console.WriteLine("║                                                            ║");
            Console.WriteLine("║  You are running the game. Friends will connect to you.   ║");
            Console.WriteLine("╚════════════════════════════════════════════════════════════╝");
            Console.WriteLine();

            // Connect to Kenshi
            Console.WriteLine("[1/2] Connecting to Kenshi...");
            var gameLink = new KenshiLink();
            if (!gameLink.Connect())
            {
                Console.ForegroundColor = ConsoleColor.Red;
                Console.WriteLine("      FAILED: Could not connect to Kenshi!");
                Console.WriteLine("      Make sure Kenshi is running and try as Administrator.");
                Console.ResetColor();
                return;
            }
            Console.ForegroundColor = ConsoleColor.Green;
            Console.WriteLine("      Connected to Kenshi!");
            Console.ResetColor();

            // Start server
            Console.WriteLine($"[2/2] Starting server on port {port}...");
            var server = new CoopHost(gameLink, port);
            server.Start();
            Console.ForegroundColor = ConsoleColor.Green;
            Console.WriteLine($"      Server running on port {port}");
            Console.ResetColor();
            Console.WriteLine();

            // Get local IP for sharing
            Console.WriteLine("═══════════════════════════════════════════════════════════════");
            Console.WriteLine("  SHARE THIS WITH YOUR FRIENDS:");
            Console.ForegroundColor = ConsoleColor.Yellow;
            Console.WriteLine($"  IP: {GetLocalIP()}");
            Console.WriteLine($"  Port: {port}");
            Console.ResetColor();
            Console.WriteLine("═══════════════════════════════════════════════════════════════");
            Console.WriteLine();
            Console.WriteLine("Commands:");
            Console.WriteLine("  /players  - List connected players");
            Console.WriteLine("  /give [player] [squad]  - Give squad control to player");
            Console.WriteLine("  /kick [player]  - Kick a player");
            Console.WriteLine("  /quit     - Stop server");
            Console.WriteLine();

            // Command loop
            while (true)
            {
                Console.Write("> ");
                var input = Console.ReadLine()?.Trim().ToLower();

                if (input == "/quit" || input == "quit" || input == "exit")
                    break;

                if (input == "/players")
                {
                    Console.WriteLine($"Connected players: {server.PlayerCount}");
                    foreach (var p in server.GetPlayers())
                        Console.WriteLine($"  - {p.Name} (controls: {string.Join(", ", p.OwnedSquads)})");
                    continue;
                }

                if (input?.StartsWith("/give ") == true)
                {
                    var parts = input.Substring(6).Split(' ');
                    if (parts.Length >= 2)
                    {
                        server.AssignSquad(parts[0], parts[1]);
                        Console.WriteLine($"Assigned squad '{parts[1]}' to player '{parts[0]}'");
                    }
                    continue;
                }

                if (input?.StartsWith("/kick ") == true)
                {
                    var name = input.Substring(6);
                    server.KickPlayer(name);
                    Console.WriteLine($"Kicked player '{name}'");
                    continue;
                }
            }

            server.Stop();
            Console.WriteLine("Server stopped.");
        }

        public static void RunClient(string hostIp = "localhost", int port = 7777)
        {
            Console.Clear();
            Console.WriteLine("╔════════════════════════════════════════════════════════════╗");
            Console.WriteLine("║         KENSHI ONLINE - CLIENT MODE                        ║");
            Console.WriteLine("║                                                            ║");
            Console.WriteLine("║  You will control squads in your friend's game.           ║");
            Console.WriteLine("╚════════════════════════════════════════════════════════════╝");
            Console.WriteLine();

            // Get player name
            Console.Write("Enter your name: ");
            var playerName = Console.ReadLine()?.Trim();
            if (string.IsNullOrWhiteSpace(playerName))
                playerName = "Player_" + new Random().Next(1000, 9999);

            // Connect to host
            Console.WriteLine($"\nConnecting to {hostIp}:{port}...");
            var client = new CoopClient(playerName);

            if (!client.Connect(hostIp, port))
            {
                Console.ForegroundColor = ConsoleColor.Red;
                Console.WriteLine("FAILED: Could not connect to host!");
                Console.WriteLine("Check the IP address and make sure the host is running.");
                Console.ResetColor();
                return;
            }

            Console.ForegroundColor = ConsoleColor.Green;
            Console.WriteLine("Connected!");
            Console.ResetColor();
            Console.WriteLine();
            Console.WriteLine("═══════════════════════════════════════════════════════════════");
            Console.WriteLine("  YOU ARE NOW IN THE HOST'S GAME");
            Console.WriteLine("  ");
            Console.WriteLine("  The host needs to assign you a squad with: /give " + playerName + " [squad]");
            Console.WriteLine("═══════════════════════════════════════════════════════════════");
            Console.WriteLine();
            Console.WriteLine("Commands:");
            Console.WriteLine("  /move x z     - Move your squad to coordinates");
            Console.WriteLine("  /follow [id]  - Follow a character");
            Console.WriteLine("  /attack [id]  - Attack a target");
            Console.WriteLine("  /status       - Show your squads and their status");
            Console.WriteLine("  /quit         - Disconnect");
            Console.WriteLine();

            // Start receiving view updates
            client.StartReceiving();

            // Command loop
            while (client.IsConnected)
            {
                Console.Write("> ");
                var input = Console.ReadLine()?.Trim();

                if (string.IsNullOrWhiteSpace(input))
                    continue;

                if (input.ToLower() == "/quit" || input.ToLower() == "quit")
                    break;

                if (input.ToLower() == "/status")
                {
                    client.ShowStatus();
                    continue;
                }

                if (input.ToLower().StartsWith("/move "))
                {
                    var parts = input.Substring(6).Split(' ');
                    if (parts.Length >= 2 &&
                        float.TryParse(parts[0], out float x) &&
                        float.TryParse(parts[1], out float z))
                    {
                        client.SendCommand(new InputCommand
                        {
                            Type = CommandType.Move,
                            TargetX = x,
                            TargetZ = z
                        });
                        Console.WriteLine($"Moving to ({x}, {z})");
                    }
                    else
                    {
                        Console.WriteLine("Usage: /move x z");
                    }
                    continue;
                }

                if (input.ToLower().StartsWith("/attack "))
                {
                    var targetId = input.Substring(8).Trim();
                    client.SendCommand(new InputCommand
                    {
                        Type = CommandType.Attack,
                        TargetId = targetId
                    });
                    Console.WriteLine($"Attacking {targetId}");
                    continue;
                }

                if (input.ToLower().StartsWith("/follow "))
                {
                    var targetId = input.Substring(8).Trim();
                    client.SendCommand(new InputCommand
                    {
                        Type = CommandType.Follow,
                        TargetId = targetId
                    });
                    Console.WriteLine($"Following {targetId}");
                    continue;
                }
            }

            client.Disconnect();
            Console.WriteLine("Disconnected.");
        }

        private static string GetLocalIP()
        {
            try
            {
                using var socket = new Socket(AddressFamily.InterNetwork, SocketType.Dgram, 0);
                socket.Connect("8.8.8.8", 65530);
                var endpoint = socket.LocalEndPoint as IPEndPoint;
                return endpoint?.Address.ToString() ?? "localhost";
            }
            catch
            {
                return "localhost";
            }
        }
    }

    #region Input Commands (Client -> Host)

    public enum CommandType
    {
        Move,       // Move squad to location
        Attack,     // Attack a target
        Follow,     // Follow a target
        Stop,       // Stop current action
        Pickup,     // Pick up an item
        Drop,       // Drop an item
        Recruit,    // Recruit an NPC
        Build,      // Place a building
        Research,   // Start research
    }

    /// <summary>
    /// Command sent from client to host.
    /// Host executes this on the client's owned squads.
    /// </summary>
    public class InputCommand
    {
        public CommandType Type { get; set; }
        public string SquadId { get; set; }      // Which squad (null = all owned)
        public string TargetId { get; set; }     // Target entity ID
        public float TargetX { get; set; }       // Target position X
        public float TargetZ { get; set; }       // Target position Z
        public string ItemId { get; set; }       // Item for pickup/drop
        public string BuildingType { get; set; } // Building to place
        public long Timestamp { get; set; } = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();

        public string ToJson() => JsonSerializer.Serialize(this);
        public static InputCommand FromJson(string json) => JsonSerializer.Deserialize<InputCommand>(json);
    }

    #endregion

    #region View State (Host -> Client)

    /// <summary>
    /// Snapshot of game state sent from host to clients.
    /// </summary>
    public class ViewState
    {
        public long Tick { get; set; }
        public List<EntityView> Entities { get; set; } = new();
        public List<GameEvent> Events { get; set; } = new();
        public List<string> YourSquads { get; set; } = new(); // Squads this client owns

        public string ToJson() => JsonSerializer.Serialize(this);
        public static ViewState FromJson(string json) => JsonSerializer.Deserialize<ViewState>(json);
    }

    public class EntityView
    {
        public string Id { get; set; }
        public string Name { get; set; }
        public float X { get; set; }
        public float Y { get; set; }
        public float Z { get; set; }
        public float Health { get; set; }
        public float MaxHealth { get; set; }
        public string SquadId { get; set; }
        public string Owner { get; set; }   // "host", player name, or "npc"
        public bool IsSelected { get; set; }
    }

    public class GameEvent
    {
        public string Type { get; set; }    // "damage", "death", "chat", "join", "leave"
        public string Data { get; set; }
    }

    #endregion

    #region Network Messages

    public class NetMessage
    {
        public string Type { get; set; }    // "join", "command", "view", "assign", "kick"
        public string PlayerId { get; set; }
        public string Data { get; set; }

        public string ToJson() => JsonSerializer.Serialize(this);
        public static NetMessage FromJson(string json) => JsonSerializer.Deserialize<NetMessage>(json);
    }

    #endregion

    #region Player Info

    public class PlayerInfo
    {
        public string Id { get; set; }
        public string Name { get; set; }
        public List<string> OwnedSquads { get; set; } = new();
        public TcpClient Connection { get; set; }
        public bool IsHost { get; set; }
    }

    #endregion

    #region Kenshi Memory Link

    /// <summary>
    /// Reads and writes to Kenshi's memory.
    /// This runs ONLY on the host.
    /// </summary>
    public class KenshiLink
    {
        [DllImport("kernel32.dll")]
        static extern IntPtr OpenProcess(int access, bool inherit, int pid);

        [DllImport("kernel32.dll")]
        static extern bool ReadProcessMemory(IntPtr handle, IntPtr addr, byte[] buffer, int size, out int read);

        [DllImport("kernel32.dll")]
        static extern bool WriteProcessMemory(IntPtr handle, IntPtr addr, byte[] buffer, int size, out int written);

        const int PROCESS_ALL_ACCESS = 0x1F0FFF;

        // Kenshi 1.0.64 offsets (64-bit)
        const long PLAYER_SQUAD_LIST = 0x24C5A20;
        const long SELECTED_CHARACTER = 0x24C5A30;
        const long ALL_CHARACTERS = 0x24C5B00;
        const int POS_OFFSET = 0x70;
        const int HEALTH_OFFSET = 0xC0;
        const int NAME_OFFSET = 0x08;

        private IntPtr processHandle;
        private IntPtr baseAddress;
        private Process kenshiProcess;

        public bool IsConnected => processHandle != IntPtr.Zero;

        public bool Connect()
        {
            try
            {
                var procs = Process.GetProcessesByName("kenshi_x64");
                if (procs.Length == 0)
                    procs = Process.GetProcessesByName("kenshi");

                if (procs.Length == 0)
                    return false;

                kenshiProcess = procs[0];
                processHandle = OpenProcess(PROCESS_ALL_ACCESS, false, kenshiProcess.Id);
                baseAddress = kenshiProcess.MainModule.BaseAddress;

                return processHandle != IntPtr.Zero;
            }
            catch
            {
                return false;
            }
        }

        /// <summary>
        /// Read all visible entities (for view replication).
        /// </summary>
        public List<EntityView> ReadEntities()
        {
            var entities = new List<EntityView>();

            try
            {
                // Read selected character as a test
                var selectedPtr = ReadPointer(baseAddress + (int)SELECTED_CHARACTER);
                if (selectedPtr != IntPtr.Zero)
                {
                    var entity = ReadEntity(selectedPtr, "selected");
                    if (entity != null)
                        entities.Add(entity);
                }

                // In a full implementation, we'd iterate through:
                // - Player squad list
                // - All characters list (for nearby NPCs)
                // - Items on ground
                // - Buildings
            }
            catch { }

            return entities;
        }

        private EntityView ReadEntity(IntPtr ptr, string id)
        {
            try
            {
                float x = ReadFloat(ptr + POS_OFFSET);
                float y = ReadFloat(ptr + POS_OFFSET + 4);
                float z = ReadFloat(ptr + POS_OFFSET + 8);
                float health = ReadFloat(ptr + HEALTH_OFFSET);

                return new EntityView
                {
                    Id = id,
                    Name = "Character", // Would read actual name
                    X = x,
                    Y = y,
                    Z = z,
                    Health = health,
                    MaxHealth = 100,
                    Owner = "host"
                };
            }
            catch
            {
                return null;
            }
        }

        /// <summary>
        /// Execute a move command (write to game memory).
        /// </summary>
        public void ExecuteMove(string squadId, float x, float z)
        {
            // In full implementation:
            // 1. Find the squad
            // 2. Write move target to squad's move order memory
            // 3. Or: select squad, simulate move click

            Console.WriteLine($"[Kenshi] Moving squad {squadId} to ({x}, {z})");
        }

        /// <summary>
        /// Execute an attack command.
        /// </summary>
        public void ExecuteAttack(string squadId, string targetId)
        {
            Console.WriteLine($"[Kenshi] Squad {squadId} attacking {targetId}");
        }

        /// <summary>
        /// Execute a follow command.
        /// </summary>
        public void ExecuteFollow(string squadId, string targetId)
        {
            Console.WriteLine($"[Kenshi] Squad {squadId} following {targetId}");
        }

        private IntPtr ReadPointer(IntPtr addr)
        {
            byte[] buffer = new byte[8];
            ReadProcessMemory(processHandle, addr, buffer, 8, out _);
            return (IntPtr)BitConverter.ToInt64(buffer, 0);
        }

        private float ReadFloat(IntPtr addr)
        {
            byte[] buffer = new byte[4];
            ReadProcessMemory(processHandle, addr, buffer, 4, out _);
            return BitConverter.ToSingle(buffer, 0);
        }
    }

    #endregion

    #region Coop Host (Server)

    /// <summary>
    /// The host server. Runs Kenshi, receives inputs, sends view state.
    /// </summary>
    public class CoopHost
    {
        private readonly KenshiLink gameLink;
        private readonly int port;
        private TcpListener listener;
        private bool running;

        private readonly ConcurrentDictionary<string, PlayerInfo> players = new();
        private readonly ConcurrentDictionary<string, string> squadOwnership = new(); // squadId -> playerId
        private long currentTick = 0;

        public int PlayerCount => players.Count;

        public CoopHost(KenshiLink gameLink, int port)
        {
            this.gameLink = gameLink;
            this.port = port;
        }

        public void Start()
        {
            listener = new TcpListener(IPAddress.Any, port);
            listener.Start();
            running = true;

            // Accept connections thread
            new Thread(AcceptLoop) { IsBackground = true }.Start();

            // View replication thread (10 Hz)
            new Thread(ViewLoop) { IsBackground = true }.Start();
        }

        public void Stop()
        {
            running = false;
            listener?.Stop();

            foreach (var p in players.Values)
                p.Connection?.Close();
        }

        public IEnumerable<PlayerInfo> GetPlayers() => players.Values;

        public void AssignSquad(string playerName, string squadId)
        {
            var player = players.Values.FirstOrDefault(p =>
                p.Name.Equals(playerName, StringComparison.OrdinalIgnoreCase));

            if (player != null)
            {
                // Remove from previous owner
                var prevOwner = squadOwnership.GetValueOrDefault(squadId);
                if (prevOwner != null && players.TryGetValue(prevOwner, out var prev))
                    prev.OwnedSquads.Remove(squadId);

                // Assign to new owner
                squadOwnership[squadId] = player.Id;
                if (!player.OwnedSquads.Contains(squadId))
                    player.OwnedSquads.Add(squadId);

                // Notify player
                SendToPlayer(player, new NetMessage
                {
                    Type = "assign",
                    Data = squadId
                });
            }
        }

        public void KickPlayer(string playerName)
        {
            var player = players.Values.FirstOrDefault(p =>
                p.Name.Equals(playerName, StringComparison.OrdinalIgnoreCase));

            if (player != null)
            {
                SendToPlayer(player, new NetMessage { Type = "kick" });
                player.Connection?.Close();
                players.TryRemove(player.Id, out _);
            }
        }

        private void AcceptLoop()
        {
            while (running)
            {
                try
                {
                    if (listener.Pending())
                    {
                        var client = listener.AcceptTcpClient();
                        new Thread(() => HandleClient(client)) { IsBackground = true }.Start();
                    }
                    Thread.Sleep(100);
                }
                catch { }
            }
        }

        private void HandleClient(TcpClient client)
        {
            PlayerInfo player = null;
            var stream = client.GetStream();
            var buffer = new byte[8192];

            try
            {
                while (running && client.Connected)
                {
                    if (!stream.DataAvailable)
                    {
                        Thread.Sleep(10);
                        continue;
                    }

                    int bytes = stream.Read(buffer, 0, buffer.Length);
                    if (bytes == 0) break;

                    var json = Encoding.UTF8.GetString(buffer, 0, bytes);
                    var msg = NetMessage.FromJson(json);

                    switch (msg.Type)
                    {
                        case "join":
                            player = new PlayerInfo
                            {
                                Id = Guid.NewGuid().ToString(),
                                Name = msg.Data ?? "Unknown",
                                Connection = client
                            };
                            players[player.Id] = player;
                            Console.WriteLine($"[+] {player.Name} joined");

                            // Send welcome
                            SendToPlayer(player, new NetMessage
                            {
                                Type = "welcome",
                                PlayerId = player.Id,
                                Data = "Connected! Ask host to assign you a squad."
                            });
                            break;

                        case "command":
                            if (player != null)
                            {
                                var cmd = InputCommand.FromJson(msg.Data);
                                ExecuteCommand(player, cmd);
                            }
                            break;
                    }
                }
            }
            catch (Exception ex)
            {
                Console.WriteLine($"[!] Client error: {ex.Message}");
            }
            finally
            {
                if (player != null)
                {
                    Console.WriteLine($"[-] {player.Name} left");
                    players.TryRemove(player.Id, out _);
                }
                client.Close();
            }
        }

        private void ExecuteCommand(PlayerInfo player, InputCommand cmd)
        {
            // Determine which squad to command
            var squadId = cmd.SquadId;
            if (string.IsNullOrEmpty(squadId) && player.OwnedSquads.Count > 0)
                squadId = player.OwnedSquads[0];

            // Verify ownership
            if (!player.OwnedSquads.Contains(squadId))
            {
                Console.WriteLine($"[!] {player.Name} tried to command squad they don't own");
                return;
            }

            // Execute on Kenshi
            switch (cmd.Type)
            {
                case CommandType.Move:
                    gameLink.ExecuteMove(squadId, cmd.TargetX, cmd.TargetZ);
                    break;
                case CommandType.Attack:
                    gameLink.ExecuteAttack(squadId, cmd.TargetId);
                    break;
                case CommandType.Follow:
                    gameLink.ExecuteFollow(squadId, cmd.TargetId);
                    break;
            }
        }

        private void ViewLoop()
        {
            while (running)
            {
                try
                {
                    currentTick++;

                    // Read game state
                    var entities = gameLink.ReadEntities();

                    // Send to each player
                    foreach (var player in players.Values)
                    {
                        var view = new ViewState
                        {
                            Tick = currentTick,
                            Entities = entities,
                            YourSquads = player.OwnedSquads
                        };

                        SendToPlayer(player, new NetMessage
                        {
                            Type = "view",
                            Data = view.ToJson()
                        });
                    }

                    Thread.Sleep(100); // 10 Hz
                }
                catch { }
            }
        }

        private void SendToPlayer(PlayerInfo player, NetMessage msg)
        {
            try
            {
                var json = msg.ToJson();
                var bytes = Encoding.UTF8.GetBytes(json);
                player.Connection?.GetStream().Write(bytes, 0, bytes.Length);
            }
            catch { }
        }
    }

    #endregion

    #region Coop Client

    /// <summary>
    /// The client. Sends inputs, receives view state.
    /// Does NOT run Kenshi - just remote controls the host's game.
    /// </summary>
    public class CoopClient
    {
        private readonly string playerName;
        private TcpClient client;
        private NetworkStream stream;
        private bool running;

        private string playerId;
        private List<string> ownedSquads = new();
        private ViewState lastView;

        public bool IsConnected => client?.Connected == true;

        public CoopClient(string playerName)
        {
            this.playerName = playerName;
        }

        public bool Connect(string ip, int port)
        {
            try
            {
                client = new TcpClient(ip, port);
                stream = client.GetStream();

                // Send join message
                var joinMsg = new NetMessage
                {
                    Type = "join",
                    Data = playerName
                };
                SendMessage(joinMsg);

                return true;
            }
            catch
            {
                return false;
            }
        }

        public void StartReceiving()
        {
            running = true;
            new Thread(ReceiveLoop) { IsBackground = true }.Start();
        }

        public void Disconnect()
        {
            running = false;
            client?.Close();
        }

        public void SendCommand(InputCommand cmd)
        {
            if (ownedSquads.Count == 0)
            {
                Console.WriteLine("You don't control any squads yet. Ask the host to assign you one.");
                return;
            }

            if (string.IsNullOrEmpty(cmd.SquadId))
                cmd.SquadId = ownedSquads[0];

            var msg = new NetMessage
            {
                Type = "command",
                PlayerId = playerId,
                Data = cmd.ToJson()
            };
            SendMessage(msg);
        }

        public void ShowStatus()
        {
            Console.WriteLine($"\n=== STATUS ===");
            Console.WriteLine($"Player: {playerName}");
            Console.WriteLine($"Your squads: {(ownedSquads.Count > 0 ? string.Join(", ", ownedSquads) : "(none)")}");

            if (lastView != null)
            {
                Console.WriteLine($"\nVisible entities ({lastView.Entities.Count}):");
                foreach (var e in lastView.Entities.Take(10))
                {
                    var ownership = ownedSquads.Contains(e.SquadId) ? "[YOURS]" : "";
                    Console.WriteLine($"  {e.Name} {ownership} at ({e.X:F0}, {e.Z:F0}) HP: {e.Health:F0}");
                }
            }
            Console.WriteLine();
        }

        private void ReceiveLoop()
        {
            var buffer = new byte[65536];

            while (running && IsConnected)
            {
                try
                {
                    if (!stream.DataAvailable)
                    {
                        Thread.Sleep(10);
                        continue;
                    }

                    int bytes = stream.Read(buffer, 0, buffer.Length);
                    if (bytes == 0) break;

                    var json = Encoding.UTF8.GetString(buffer, 0, bytes);
                    var msg = NetMessage.FromJson(json);

                    switch (msg.Type)
                    {
                        case "welcome":
                            playerId = msg.PlayerId;
                            Console.WriteLine($"\n{msg.Data}\n");
                            break;

                        case "view":
                            lastView = ViewState.FromJson(msg.Data);
                            ownedSquads = lastView.YourSquads ?? new List<string>();
                            // Could update a GUI here
                            break;

                        case "assign":
                            if (!ownedSquads.Contains(msg.Data))
                                ownedSquads.Add(msg.Data);
                            Console.WriteLine($"\n[!] You now control squad: {msg.Data}\n");
                            break;

                        case "kick":
                            Console.WriteLine("\n[!] You have been kicked from the game.\n");
                            running = false;
                            break;
                    }
                }
                catch { }
            }
        }

        private void SendMessage(NetMessage msg)
        {
            try
            {
                var json = msg.ToJson();
                var bytes = Encoding.UTF8.GetBytes(json);
                stream.Write(bytes, 0, bytes.Length);
            }
            catch { }
        }
    }

    #endregion
}
