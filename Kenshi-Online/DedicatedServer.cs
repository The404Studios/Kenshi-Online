using System;
using System.Collections.Generic;
using System.Collections.Concurrent;
using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Text.Json;
using System.Threading;
using System.Linq;
using System.IO;

namespace KenshiMultiplayer
{
    #region Spawn Point System

    /// <summary>
    /// Represents a spawn location in the world.
    /// </summary>
    public class SpawnPoint
    {
        public string Id { get; set; }
        public string Name { get; set; }
        public string Description { get; set; }
        public float X { get; set; }
        public float Y { get; set; }
        public float Z { get; set; }
        public bool IsDefault { get; set; }
        public string Region { get; set; }

        public override string ToString() => $"{Name} ({Region}) at ({X:F0}, {Z:F0})";
    }

    /// <summary>
    /// Manages spawn points loaded from config or defaults.
    /// </summary>
    public static class SpawnPointManager
    {
        private static List<SpawnPoint> _spawnPoints = new();
        private static readonly string ConfigPath = "spawnpoints.json";

        public static IReadOnlyList<SpawnPoint> SpawnPoints => _spawnPoints;

        static SpawnPointManager()
        {
            LoadSpawnPoints();
        }

        public static void LoadSpawnPoints()
        {
            if (File.Exists(ConfigPath))
            {
                try
                {
                    var json = File.ReadAllText(ConfigPath);
                    _spawnPoints = JsonSerializer.Deserialize<List<SpawnPoint>>(json) ?? GetDefaultSpawnPoints();
                    Console.WriteLine($"Loaded {_spawnPoints.Count} spawn points from config.");
                    return;
                }
                catch (Exception ex)
                {
                    Console.WriteLine($"Error loading spawn points: {ex.Message}");
                }
            }

            _spawnPoints = GetDefaultSpawnPoints();
            SaveSpawnPoints();
        }

        public static void SaveSpawnPoints()
        {
            try
            {
                var json = JsonSerializer.Serialize(_spawnPoints, new JsonSerializerOptions { WriteIndented = true });
                File.WriteAllText(ConfigPath, json);
            }
            catch { }
        }

        public static List<SpawnPoint> GetDefaultSpawnPoints()
        {
            return new List<SpawnPoint>
            {
                new SpawnPoint
                {
                    Id = "hub",
                    Name = "The Hub",
                    Description = "Central trading town, good for beginners",
                    X = -15850,
                    Y = 0,
                    Z = 11930,
                    IsDefault = true,
                    Region = "Border Zone"
                },
                new SpawnPoint
                {
                    Id = "squin",
                    Name = "Squin",
                    Description = "Shek Kingdom trading post",
                    X = -26040,
                    Y = 0,
                    Z = 16640,
                    IsDefault = false,
                    Region = "Border Zone"
                },
                new SpawnPoint
                {
                    Id = "stack",
                    Name = "Stack",
                    Description = "Holy Nation city",
                    X = -7200,
                    Y = 0,
                    Z = 22800,
                    IsDefault = false,
                    Region = "Okran's Pride"
                },
                new SpawnPoint
                {
                    Id = "mongrel",
                    Name = "Mongrel",
                    Description = "Hidden city in the fog",
                    X = 3500,
                    Y = 0,
                    Z = 38500,
                    IsDefault = false,
                    Region = "Fog Islands"
                },
                new SpawnPoint
                {
                    Id = "shark",
                    Name = "Shark",
                    Description = "Swamp town, rough area",
                    X = -35100,
                    Y = 0,
                    Z = -10400,
                    IsDefault = false,
                    Region = "The Swamp"
                },
                new SpawnPoint
                {
                    Id = "waystation",
                    Name = "Way Station",
                    Description = "Small outpost between zones",
                    X = -19500,
                    Y = 0,
                    Z = 8500,
                    IsDefault = false,
                    Region = "Border Zone"
                },
                new SpawnPoint
                {
                    Id = "admag",
                    Name = "Admag",
                    Description = "Shek capital city",
                    X = -30200,
                    Y = 0,
                    Z = 27800,
                    IsDefault = false,
                    Region = "Shek Kingdom"
                },
                new SpawnPoint
                {
                    Id = "heft",
                    Name = "Heft",
                    Description = "United Cities trade hub",
                    X = 13400,
                    Y = 0,
                    Z = -24600,
                    IsDefault = false,
                    Region = "Great Desert"
                }
            };
        }

        public static SpawnPoint GetById(string id)
        {
            return _spawnPoints.FirstOrDefault(s => s.Id.Equals(id, StringComparison.OrdinalIgnoreCase));
        }

        public static SpawnPoint GetDefault()
        {
            return _spawnPoints.FirstOrDefault(s => s.IsDefault) ?? _spawnPoints.FirstOrDefault();
        }

        public static void AddSpawnPoint(SpawnPoint point)
        {
            _spawnPoints.Add(point);
            SaveSpawnPoints();
        }
    }

    #endregion

    /// <summary>
    /// DEDICATED SERVER MODEL
    ///
    /// A standalone server that doesn't run Kenshi.
    /// It maintains authoritative game state and relays to all clients.
    ///
    /// How it works:
    /// 1. Server tracks world state (positions, health, squads, items)
    /// 2. Clients run Kenshi and connect to server
    /// 3. Clients send their local state changes
    /// 4. Server validates, merges, and broadcasts to all clients
    /// 5. Clients apply server state to their local Kenshi
    ///
    /// Usage:
    ///   Server: KenshiOnline.exe --dedicated [port]
    ///   Client: KenshiOnline.exe --connect [server-ip] [port]
    /// </summary>
    public static class DedicatedServerMode
    {
        public static void RunDedicatedServer(int port = 7777)
        {
            Console.Clear();
            PrintBanner("KENSHI ONLINE - DEDICATED SERVER");
            Console.WriteLine();
            Console.WriteLine("Starting dedicated server...");
            Console.WriteLine("This server does NOT run Kenshi. It coordinates multiple players.");
            Console.WriteLine();

            var server = new DedicatedServer(port);
            server.Start();

            Console.ForegroundColor = ConsoleColor.Green;
            Console.WriteLine($"Server running on port {port}");
            Console.ResetColor();
            Console.WriteLine();

            // Show connection info
            Console.WriteLine("═══════════════════════════════════════════════════════════════");
            Console.WriteLine("  PLAYERS CONNECT WITH:");
            Console.ForegroundColor = ConsoleColor.Yellow;
            Console.WriteLine($"  KenshiOnline.exe --connect {GetLocalIP()} {port}");
            Console.ResetColor();
            Console.WriteLine("═══════════════════════════════════════════════════════════════");
            Console.WriteLine();

            Console.WriteLine("Commands:");
            Console.WriteLine("  /status     - Show server status");
            Console.WriteLine("  /players    - List connected players");
            Console.WriteLine("  /world      - Show world state");
            Console.WriteLine("  /spawns     - List spawn points");
            Console.WriteLine("  /addspawn [id] [name] [x] [z] [region] - Add spawn point");
            Console.WriteLine("  /teleport [player] [x] [z]  - Teleport player");
            Console.WriteLine("  /kick [player]  - Kick a player");
            Console.WriteLine("  /broadcast [msg] - Send message to all");
            Console.WriteLine("  /save       - Save world state");
            Console.WriteLine("  /load       - Load world state");
            Console.WriteLine("  /quit       - Stop server");
            Console.WriteLine();

            // Admin command loop
            while (true)
            {
                Console.Write("server> ");
                var input = Console.ReadLine()?.Trim();

                if (string.IsNullOrWhiteSpace(input)) continue;
                if (input == "/quit" || input == "quit") break;

                server.HandleAdminCommand(input);
            }

            server.Stop();
            Console.WriteLine("Server stopped.");
        }

        public static void RunDedicatedClient(string serverIp, int port = 7777)
        {
            Console.Clear();
            PrintBanner("KENSHI ONLINE - MULTIPLAYER CLIENT");
            Console.WriteLine();

            // Get player name
            Console.Write("Enter your name: ");
            var playerName = Console.ReadLine()?.Trim();
            if (string.IsNullOrWhiteSpace(playerName))
                playerName = "Player_" + new Random().Next(1000, 9999);

            Console.WriteLine();
            Console.WriteLine("Connecting to Kenshi...");

            // Connect to local Kenshi
            var gameLink = new GameMemoryLink();
            if (!gameLink.Connect())
            {
                Console.ForegroundColor = ConsoleColor.Red;
                Console.WriteLine("ERROR: Could not connect to Kenshi!");
                Console.WriteLine("Make sure Kenshi is running and try as Administrator.");
                Console.ResetColor();
                Console.WriteLine("\nPress Enter to exit...");
                Console.ReadLine();
                return;
            }
            Console.ForegroundColor = ConsoleColor.Green;
            Console.WriteLine("Connected to Kenshi!");
            Console.ResetColor();

            Console.WriteLine($"\nConnecting to server {serverIp}:{port}...");

            var client = new DedicatedClient(playerName, gameLink);
            if (!client.Connect(serverIp, port))
            {
                Console.ForegroundColor = ConsoleColor.Red;
                Console.WriteLine("ERROR: Could not connect to server!");
                Console.ResetColor();
                return;
            }

            Console.ForegroundColor = ConsoleColor.Green;
            Console.WriteLine("Connected to server!");
            Console.ResetColor();
            Console.WriteLine();

            // Show spawn selection menu
            client.ShowSpawnMenu();

            // Get spawn selection from user
            while (!client.HasSpawned)
            {
                var spawnChoice = Console.ReadLine()?.Trim();
                if (!client.SelectSpawn(spawnChoice))
                {
                    Console.Write("Enter number to spawn (or press Enter for default): ");
                }
            }

            Console.WriteLine();
            Console.WriteLine("═══════════════════════════════════════════════════════════════");
            Console.WriteLine("  MULTIPLAYER ACTIVE");
            Console.WriteLine("  Your position is being synced with other players.");
            Console.WriteLine("  Other players will appear as they move around.");
            Console.WriteLine("═══════════════════════════════════════════════════════════════");
            Console.WriteLine();
            Console.WriteLine("Commands:");
            Console.WriteLine("  /status  - Show your status and online players");
            Console.WriteLine("  /chat    - Send a message to all players");
            Console.WriteLine("  /quit    - Disconnect from server");
            Console.WriteLine();

            // Start sync
            client.StartSync();

            // Wait for commands
            while (client.IsConnected)
            {
                var input = Console.ReadLine()?.Trim();
                if (string.IsNullOrWhiteSpace(input)) continue;

                if (input.ToLower() == "/quit" || input.ToLower() == "quit")
                    break;

                if (input.ToLower() == "/status")
                {
                    client.ShowStatus();
                }
                else if (input.StartsWith("/chat ", StringComparison.OrdinalIgnoreCase))
                {
                    var message = input.Substring(6);
                    client.SendChat(message);
                }
                else if (input.StartsWith("/"))
                {
                    Console.WriteLine("Unknown command. Use /status, /chat <message>, or /quit");
                }
                else
                {
                    // Treat bare text as chat
                    client.SendChat(input);
                }
            }

            client.Disconnect();
        }

        private static void PrintBanner(string title)
        {
            Console.ForegroundColor = ConsoleColor.Cyan;
            var line = new string('═', title.Length + 8);
            Console.WriteLine($"╔{line}╗");
            Console.WriteLine($"║    {title}    ║");
            Console.WriteLine($"╚{line}╝");
            Console.ResetColor();
        }

        private static string GetLocalIP()
        {
            try
            {
                using var socket = new Socket(AddressFamily.InterNetwork, SocketType.Dgram, 0);
                socket.Connect("8.8.8.8", 65530);
                return (socket.LocalEndPoint as IPEndPoint)?.Address.ToString() ?? "localhost";
            }
            catch { return "localhost"; }
        }
    }

    #region World State (Server's Authoritative State)

    /// <summary>
    /// The server's authoritative view of the world.
    /// This is the ONLY source of truth.
    /// </summary>
    public class WorldState
    {
        public long Tick { get; set; }
        public ConcurrentDictionary<string, PlayerState> Players { get; } = new();
        public ConcurrentDictionary<string, EntityState> NPCs { get; } = new();
        public ConcurrentDictionary<string, ItemState> Items { get; } = new();
        public List<WorldEvent> RecentEvents { get; } = new();

        private readonly object _lock = new();

        public void UpdatePlayer(string playerId, float x, float y, float z, float health)
        {
            lock (_lock)
            {
                if (Players.TryGetValue(playerId, out var player))
                {
                    player.X = x;
                    player.Y = y;
                    player.Z = z;
                    player.Health = health;
                    player.LastUpdate = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();
                }
            }
        }

        public PlayerState AddPlayer(string playerId, string name, float x, float z)
        {
            var player = new PlayerState
            {
                PlayerId = playerId,
                Name = name,
                X = x,
                Y = 0,
                Z = z,
                Health = 100,
                MaxHealth = 100,
                LastUpdate = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds()
            };
            Players[playerId] = player;

            RecentEvents.Add(new WorldEvent
            {
                Type = "player_join",
                PlayerId = playerId,
                Data = name
            });

            return player;
        }

        public void RemovePlayer(string playerId)
        {
            if (Players.TryRemove(playerId, out var player))
            {
                RecentEvents.Add(new WorldEvent
                {
                    Type = "player_leave",
                    PlayerId = playerId,
                    Data = player.Name
                });
            }
        }

        /// <summary>
        /// Get snapshot for sending to clients.
        /// </summary>
        public WorldSnapshot GetSnapshot()
        {
            lock (_lock)
            {
                return new WorldSnapshot
                {
                    Tick = Tick,
                    Players = Players.Values.ToList(),
                    NPCs = NPCs.Values.Take(50).ToList(), // Limit for bandwidth
                    Events = RecentEvents.TakeLast(10).ToList()
                };
            }
        }

        public void ClearOldEvents()
        {
            while (RecentEvents.Count > 100)
                RecentEvents.RemoveAt(0);
        }
    }

    public class PlayerState
    {
        public string PlayerId { get; set; }
        public string Name { get; set; }
        public float X { get; set; }
        public float Y { get; set; }
        public float Z { get; set; }
        public float Health { get; set; }
        public float MaxHealth { get; set; }
        public long LastUpdate { get; set; }
        public bool HasSpawned { get; set; }
        public string SpawnPointId { get; set; }
    }

    public class EntityState
    {
        public string EntityId { get; set; }
        public string Name { get; set; }
        public float X { get; set; }
        public float Y { get; set; }
        public float Z { get; set; }
        public float Health { get; set; }
        public string Faction { get; set; }
    }

    public class ItemState
    {
        public string ItemId { get; set; }
        public string ItemType { get; set; }
        public float X { get; set; }
        public float Y { get; set; }
        public float Z { get; set; }
        public string OwnerId { get; set; } // null if on ground
    }

    public class WorldEvent
    {
        public string Type { get; set; }
        public string PlayerId { get; set; }
        public string Data { get; set; }
        public long Timestamp { get; set; } = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();
    }

    public class WorldSnapshot
    {
        public long Tick { get; set; }
        public List<PlayerState> Players { get; set; } = new();
        public List<EntityState> NPCs { get; set; } = new();
        public List<WorldEvent> Events { get; set; } = new();

        public string ToJson() => JsonSerializer.Serialize(this);
        public static WorldSnapshot FromJson(string json) => JsonSerializer.Deserialize<WorldSnapshot>(json);
    }

    #endregion

    #region Network Protocol

    public class ServerMessage
    {
        public string Type { get; set; }  // "welcome", "spawnpoints", "spawned", "state", "kick", "error"
        public string Data { get; set; }

        public string ToJson() => JsonSerializer.Serialize(this);
        public static ServerMessage FromJson(string json) => JsonSerializer.Deserialize<ServerMessage>(json);
    }

    public class ClientMessage
    {
        public string Type { get; set; }  // "join", "spawn", "update", "action", "chat"
        public string PlayerId { get; set; }
        public string Data { get; set; }

        public string ToJson() => JsonSerializer.Serialize(this);
        public static ClientMessage FromJson(string json) => JsonSerializer.Deserialize<ClientMessage>(json);
    }

    public class SpawnRequest
    {
        public string SpawnPointId { get; set; }

        public string ToJson() => JsonSerializer.Serialize(this);
        public static SpawnRequest FromJson(string json) => JsonSerializer.Deserialize<SpawnRequest>(json);
    }

    public class SpawnPointInfo
    {
        public string Id { get; set; }
        public string Name { get; set; }
        public string Description { get; set; }
        public string Region { get; set; }
        public float X { get; set; }
        public float Z { get; set; }
        public bool IsDefault { get; set; }
    }

    public class PositionUpdate
    {
        public float X { get; set; }
        public float Y { get; set; }
        public float Z { get; set; }
        public float Health { get; set; }

        public string ToJson() => JsonSerializer.Serialize(this);
        public static PositionUpdate FromJson(string json) => JsonSerializer.Deserialize<PositionUpdate>(json);
    }

    #endregion

    #region Dedicated Server

    public class DedicatedServer
    {
        private readonly int port;
        private TcpListener listener;
        private bool running;

        private readonly WorldState world = new();
        private readonly ConcurrentDictionary<string, ClientConnection> clients = new();

        public DedicatedServer(int port)
        {
            this.port = port;
        }

        public void Start()
        {
            listener = new TcpListener(IPAddress.Any, port);
            listener.Start();
            running = true;

            // Accept connections
            new Thread(AcceptLoop) { IsBackground = true, Name = "AcceptLoop" }.Start();

            // Broadcast state (20 Hz)
            new Thread(BroadcastLoop) { IsBackground = true, Name = "BroadcastLoop" }.Start();

            // Tick loop
            new Thread(TickLoop) { IsBackground = true, Name = "TickLoop" }.Start();
        }

        public void Stop()
        {
            running = false;
            listener?.Stop();
            foreach (var c in clients.Values)
                c.Client?.Close();
        }

        public void HandleAdminCommand(string input)
        {
            var parts = input.Split(' ', StringSplitOptions.RemoveEmptyEntries);
            if (parts.Length == 0) return;

            switch (parts[0].ToLower())
            {
                case "/status":
                    Console.WriteLine($"Tick: {world.Tick}");
                    Console.WriteLine($"Players: {world.Players.Count} (spawned), Clients: {clients.Count} (connected)");
                    Console.WriteLine($"Spawn Points: {SpawnPointManager.SpawnPoints.Count}");
                    break;

                case "/players":
                    Console.WriteLine($"Connected players ({world.Players.Count}):");
                    foreach (var p in world.Players.Values)
                    {
                        var spawnedStr = p.HasSpawned ? $"at ({p.X:F0}, {p.Z:F0})" : "(not spawned)";
                        Console.WriteLine($"  {p.Name} {spawnedStr} HP: {p.Health:F0}");
                    }
                    if (world.Players.Count == 0)
                        Console.WriteLine("  (no players spawned)");
                    break;

                case "/world":
                    Console.WriteLine($"World state:");
                    Console.WriteLine($"  Players: {world.Players.Count}");
                    Console.WriteLine($"  NPCs: {world.NPCs.Count}");
                    Console.WriteLine($"  Items: {world.Items.Count}");
                    Console.WriteLine($"  Events: {world.RecentEvents.Count}");
                    break;

                case "/spawns":
                    Console.WriteLine($"Spawn Points ({SpawnPointManager.SpawnPoints.Count}):");
                    foreach (var sp in SpawnPointManager.SpawnPoints)
                    {
                        var defaultMark = sp.IsDefault ? " [DEFAULT]" : "";
                        Console.WriteLine($"  {sp.Id}: {sp.Name} ({sp.Region}) at ({sp.X:F0}, {sp.Z:F0}){defaultMark}");
                    }
                    break;

                case "/addspawn":
                    if (parts.Length >= 6 &&
                        float.TryParse(parts[3], out float ax) &&
                        float.TryParse(parts[4], out float az))
                    {
                        var newSpawn = new SpawnPoint
                        {
                            Id = parts[1],
                            Name = parts[2].Replace("_", " "),
                            X = ax,
                            Z = az,
                            Region = parts[5].Replace("_", " "),
                            Description = parts.Length > 6 ? string.Join(" ", parts.Skip(6)) : "Custom spawn point",
                            IsDefault = false
                        };
                        SpawnPointManager.AddSpawnPoint(newSpawn);
                        Console.WriteLine($"Added spawn point: {newSpawn}");
                    }
                    else
                    {
                        Console.WriteLine("Usage: /addspawn [id] [name] [x] [z] [region] [description]");
                        Console.WriteLine("Example: /addspawn mybase My_Base -5000 10000 Border_Zone My custom base");
                    }
                    break;

                case "/teleport":
                    if (parts.Length >= 4 &&
                        float.TryParse(parts[2], out float tx) &&
                        float.TryParse(parts[3], out float tz))
                    {
                        var playerName = parts[1];
                        var player = world.Players.Values.FirstOrDefault(p =>
                            p.Name.Equals(playerName, StringComparison.OrdinalIgnoreCase));

                        if (player != null)
                        {
                            player.X = tx;
                            player.Z = tz;
                            Console.WriteLine($"Teleported {playerName} to ({tx:F0}, {tz:F0})");
                        }
                        else
                        {
                            Console.WriteLine($"Player '{playerName}' not found");
                        }
                    }
                    else
                    {
                        Console.WriteLine("Usage: /teleport [player] [x] [z]");
                    }
                    break;

                case "/kick":
                    if (parts.Length >= 2)
                    {
                        var playerName = parts[1];
                        var conn = clients.Values.FirstOrDefault(c =>
                            c.PlayerName?.Equals(playerName, StringComparison.OrdinalIgnoreCase) == true);

                        if (conn != null)
                        {
                            SendToClient(conn, new ServerMessage { Type = "kick", Data = "Kicked by admin" });
                            conn.Client?.Close();
                            Console.WriteLine($"Kicked {playerName}");
                        }
                        else
                        {
                            Console.WriteLine($"Player '{playerName}' not found");
                        }
                    }
                    else
                    {
                        Console.WriteLine("Usage: /kick [player]");
                    }
                    break;

                case "/broadcast":
                    if (parts.Length >= 2)
                    {
                        var message = string.Join(" ", parts.Skip(1));
                        world.RecentEvents.Add(new WorldEvent
                        {
                            Type = "broadcast",
                            PlayerId = "SERVER",
                            Data = message
                        });
                        Console.WriteLine($"Broadcasted: {message}");
                    }
                    else
                    {
                        Console.WriteLine("Usage: /broadcast [message]");
                    }
                    break;

                case "/save":
                    SaveWorld();
                    Console.WriteLine("World saved.");
                    break;

                case "/load":
                    LoadWorld();
                    Console.WriteLine("World loaded.");
                    break;

                default:
                    Console.WriteLine($"Unknown command: {parts[0]}");
                    break;
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
                        var tcp = listener.AcceptTcpClient();
                        new Thread(() => HandleClient(tcp)) { IsBackground = true }.Start();
                    }
                    Thread.Sleep(50);
                }
                catch { }
            }
        }

        private void HandleClient(TcpClient tcp)
        {
            var conn = new ClientConnection { Client = tcp };
            var stream = tcp.GetStream();
            var buffer = new byte[4096];

            try
            {
                while (running && tcp.Connected)
                {
                    if (!stream.DataAvailable)
                    {
                        Thread.Sleep(10);
                        continue;
                    }

                    int bytes = stream.Read(buffer, 0, buffer.Length);
                    if (bytes == 0) break;

                    var json = Encoding.UTF8.GetString(buffer, 0, bytes);

                    // Handle multiple messages in one packet
                    foreach (var msgJson in SplitMessages(json))
                    {
                        try
                        {
                            var msg = ClientMessage.FromJson(msgJson);
                            HandleMessage(conn, msg);
                        }
                        catch { }
                    }
                }
            }
            catch (Exception ex)
            {
                Console.WriteLine($"[!] Client error: {ex.Message}");
            }
            finally
            {
                if (conn.PlayerId != null)
                {
                    Console.WriteLine($"[-] {conn.PlayerName} disconnected");
                    world.RemovePlayer(conn.PlayerId);
                    clients.TryRemove(conn.PlayerId, out _);
                }
                tcp.Close();
            }
        }

        private IEnumerable<string> SplitMessages(string json)
        {
            // Simple split - in production use proper message framing
            yield return json;
        }

        private void HandleMessage(ClientConnection conn, ClientMessage msg)
        {
            switch (msg.Type)
            {
                case "join":
                    conn.PlayerId = Guid.NewGuid().ToString();
                    conn.PlayerName = msg.Data ?? "Unknown";
                    clients[conn.PlayerId] = conn;

                    Console.WriteLine($"[+] {conn.PlayerName} connected (awaiting spawn selection)");

                    // Send welcome with player ID
                    SendToClient(conn, new ServerMessage
                    {
                        Type = "welcome",
                        Data = JsonSerializer.Serialize(new
                        {
                            PlayerId = conn.PlayerId,
                            Name = conn.PlayerName
                        })
                    });

                    // Send available spawn points
                    var spawnInfos = SpawnPointManager.SpawnPoints.Select(s => new SpawnPointInfo
                    {
                        Id = s.Id,
                        Name = s.Name,
                        Description = s.Description,
                        Region = s.Region,
                        X = s.X,
                        Z = s.Z,
                        IsDefault = s.IsDefault
                    }).ToList();

                    SendToClient(conn, new ServerMessage
                    {
                        Type = "spawnpoints",
                        Data = JsonSerializer.Serialize(spawnInfos)
                    });
                    break;

                case "spawn":
                    if (conn.PlayerId != null)
                    {
                        var spawnReq = SpawnRequest.FromJson(msg.Data);
                        var spawnPoint = SpawnPointManager.GetById(spawnReq.SpawnPointId)
                                        ?? SpawnPointManager.GetDefault();

                        if (spawnPoint == null)
                        {
                            SendToClient(conn, new ServerMessage
                            {
                                Type = "error",
                                Data = "No spawn points available"
                            });
                            break;
                        }

                        // Add player to world at chosen spawn
                        var player = world.AddPlayer(conn.PlayerId, conn.PlayerName, spawnPoint.X, spawnPoint.Z);
                        player.HasSpawned = true;
                        player.SpawnPointId = spawnPoint.Id;

                        Console.WriteLine($"[*] {conn.PlayerName} spawned at {spawnPoint.Name} ({spawnPoint.X:F0}, {spawnPoint.Z:F0})");

                        // Send spawn confirmation
                        SendToClient(conn, new ServerMessage
                        {
                            Type = "spawned",
                            Data = JsonSerializer.Serialize(new
                            {
                                SpawnPoint = spawnPoint.Name,
                                X = spawnPoint.X,
                                Y = spawnPoint.Y,
                                Z = spawnPoint.Z,
                                Region = spawnPoint.Region
                            })
                        });
                    }
                    break;

                case "update":
                    if (conn.PlayerId != null && world.Players.TryGetValue(conn.PlayerId, out var existingPlayer) && existingPlayer.HasSpawned)
                    {
                        var pos = PositionUpdate.FromJson(msg.Data);

                        // Validate position (anti-cheat)
                        if (IsValidPositionUpdate(conn.PlayerId, pos))
                        {
                            world.UpdatePlayer(conn.PlayerId, pos.X, pos.Y, pos.Z, pos.Health);
                        }
                    }
                    break;

                case "action":
                    // Handle game actions (attack, pickup, etc.)
                    // For now, just log
                    Console.WriteLine($"[Action] {conn.PlayerName}: {msg.Data}");
                    break;

                case "chat":
                    if (conn.PlayerId != null && !string.IsNullOrEmpty(msg.Data))
                    {
                        Console.WriteLine($"[Chat] {conn.PlayerName}: {msg.Data}");
                        // Broadcast to all clients
                        world.RecentEvents.Add(new WorldEvent
                        {
                            Type = "chat",
                            PlayerId = conn.PlayerId,
                            Data = $"{conn.PlayerName}: {msg.Data}"
                        });
                    }
                    break;
            }
        }

        private bool IsValidPositionUpdate(string playerId, PositionUpdate pos)
        {
            // Basic validation - check for teleportation
            if (world.Players.TryGetValue(playerId, out var player))
            {
                float dx = pos.X - player.X;
                float dz = pos.Z - player.Z;
                float dist = (float)Math.Sqrt(dx * dx + dz * dz);

                // Allow max 50 units per update (roughly 1000 units/sec at 20Hz)
                // Kenshi characters move ~10-15 units/sec normally
                if (dist > 50)
                {
                    Console.WriteLine($"[!] {player.Name} position rejected (moved {dist:F0} units)");
                    return false;
                }
            }
            return true;
        }

        private (float x, float z) GetSpawnPoint()
        {
            // Hub default spawn
            return (0, 0);
        }

        private void BroadcastLoop()
        {
            while (running)
            {
                try
                {
                    var snapshot = world.GetSnapshot();
                    var msg = new ServerMessage
                    {
                        Type = "state",
                        Data = snapshot.ToJson()
                    };

                    foreach (var conn in clients.Values)
                    {
                        SendToClient(conn, msg);
                    }

                    Thread.Sleep(50); // 20 Hz
                }
                catch { }
            }
        }

        private void TickLoop()
        {
            while (running)
            {
                world.Tick++;
                world.ClearOldEvents();
                Thread.Sleep(50); // 20 Hz tick
            }
        }

        private void SendToClient(ClientConnection conn, ServerMessage msg)
        {
            try
            {
                var json = msg.ToJson();
                var bytes = Encoding.UTF8.GetBytes(json);
                conn.Client?.GetStream().Write(bytes, 0, bytes.Length);
            }
            catch { }
        }

        private void SaveWorld()
        {
            try
            {
                var json = JsonSerializer.Serialize(new
                {
                    world.Tick,
                    Players = world.Players.Values.ToList()
                });
                System.IO.File.WriteAllText("world_save.json", json);
            }
            catch (Exception ex)
            {
                Console.WriteLine($"Save failed: {ex.Message}");
            }
        }

        private void LoadWorld()
        {
            try
            {
                if (System.IO.File.Exists("world_save.json"))
                {
                    // Would deserialize and restore state
                    Console.WriteLine("Load not fully implemented yet.");
                }
            }
            catch (Exception ex)
            {
                Console.WriteLine($"Load failed: {ex.Message}");
            }
        }
    }

    public class ClientConnection
    {
        public TcpClient Client { get; set; }
        public string PlayerId { get; set; }
        public string PlayerName { get; set; }
    }

    #endregion

    #region Dedicated Client

    public class DedicatedClient
    {
        private readonly string playerName;
        private readonly GameMemoryLink gameLink;

        private TcpClient client;
        private NetworkStream stream;
        private bool running;
        private bool hasSpawned;

        private string playerId;
        private WorldSnapshot lastSnapshot;
        private List<SpawnPointInfo> availableSpawnPoints = new();
        private readonly ConcurrentDictionary<string, OtherPlayer> otherPlayers = new();

        public bool IsConnected => client?.Connected == true;
        public bool HasSpawned => hasSpawned;

        public DedicatedClient(string playerName, GameMemoryLink gameLink)
        {
            this.playerName = playerName;
            this.gameLink = gameLink;
        }

        public bool Connect(string ip, int port)
        {
            try
            {
                client = new TcpClient(ip, port);
                stream = client.GetStream();

                // Send join
                SendMessage(new ClientMessage { Type = "join", Data = playerName });

                // Wait for welcome and spawn points
                WaitForWelcome();

                return true;
            }
            catch
            {
                return false;
            }
        }

        private void WaitForWelcome()
        {
            var buffer = new byte[65536];
            var timeout = DateTime.Now.AddSeconds(5);

            while (DateTime.Now < timeout)
            {
                if (stream.DataAvailable)
                {
                    int bytes = stream.Read(buffer, 0, buffer.Length);
                    if (bytes > 0)
                    {
                        var json = Encoding.UTF8.GetString(buffer, 0, bytes);

                        // May contain multiple messages
                        foreach (var part in json.Split(new[] { "}{" }, StringSplitOptions.None))
                        {
                            var msgJson = part;
                            if (!msgJson.StartsWith("{")) msgJson = "{" + msgJson;
                            if (!msgJson.EndsWith("}")) msgJson = msgJson + "}";

                            try
                            {
                                var msg = ServerMessage.FromJson(msgJson);
                                ProcessMessage(msg);
                            }
                            catch { }
                        }

                        if (playerId != null && availableSpawnPoints.Count > 0)
                            return;
                    }
                }
                Thread.Sleep(50);
            }
        }

        public void ShowSpawnMenu()
        {
            Console.Clear();
            Console.ForegroundColor = ConsoleColor.Cyan;
            Console.WriteLine(@"
╔═══════════════════════════════════════════════════════════════════╗
║                     SELECT SPAWN LOCATION                          ║
╚═══════════════════════════════════════════════════════════════════╝");
            Console.ResetColor();
            Console.WriteLine();

            if (availableSpawnPoints.Count == 0)
            {
                Console.ForegroundColor = ConsoleColor.Red;
                Console.WriteLine("No spawn points available!");
                Console.ResetColor();
                return;
            }

            for (int i = 0; i < availableSpawnPoints.Count; i++)
            {
                var sp = availableSpawnPoints[i];
                var defaultMark = sp.IsDefault ? " [DEFAULT]" : "";

                Console.ForegroundColor = ConsoleColor.Yellow;
                Console.Write($"  {i + 1}. ");
                Console.ForegroundColor = ConsoleColor.White;
                Console.Write($"{sp.Name}");
                Console.ForegroundColor = ConsoleColor.DarkGray;
                Console.WriteLine($"{defaultMark}");

                Console.ForegroundColor = ConsoleColor.Gray;
                Console.WriteLine($"     Region: {sp.Region}");
                Console.WriteLine($"     {sp.Description}");
                Console.WriteLine($"     Coordinates: ({sp.X:F0}, {sp.Z:F0})");
                Console.ResetColor();
                Console.WriteLine();
            }

            Console.WriteLine("═══════════════════════════════════════════════════════════════════");
            Console.Write("Enter number to spawn (or press Enter for default): ");
        }

        public bool SelectSpawn(string input)
        {
            SpawnPointInfo selectedSpawn;

            if (string.IsNullOrWhiteSpace(input))
            {
                // Use default spawn
                selectedSpawn = availableSpawnPoints.FirstOrDefault(s => s.IsDefault)
                               ?? availableSpawnPoints.FirstOrDefault();
            }
            else if (int.TryParse(input, out int choice) && choice >= 1 && choice <= availableSpawnPoints.Count)
            {
                selectedSpawn = availableSpawnPoints[choice - 1];
            }
            else
            {
                Console.ForegroundColor = ConsoleColor.Red;
                Console.WriteLine("Invalid choice!");
                Console.ResetColor();
                return false;
            }

            if (selectedSpawn == null)
            {
                Console.ForegroundColor = ConsoleColor.Red;
                Console.WriteLine("No spawn point selected!");
                Console.ResetColor();
                return false;
            }

            Console.WriteLine();
            Console.ForegroundColor = ConsoleColor.Yellow;
            Console.WriteLine($"Spawning at {selectedSpawn.Name}...");
            Console.ResetColor();

            // Send spawn request
            SendMessage(new ClientMessage
            {
                Type = "spawn",
                PlayerId = playerId,
                Data = new SpawnRequest { SpawnPointId = selectedSpawn.Id }.ToJson()
            });

            // Wait for spawn confirmation
            var timeout = DateTime.Now.AddSeconds(5);
            while (DateTime.Now < timeout && !hasSpawned)
            {
                if (stream.DataAvailable)
                {
                    var buffer = new byte[65536];
                    int bytes = stream.Read(buffer, 0, buffer.Length);
                    if (bytes > 0)
                    {
                        var json = Encoding.UTF8.GetString(buffer, 0, bytes);
                        try
                        {
                            var msg = ServerMessage.FromJson(json);
                            ProcessMessage(msg);
                        }
                        catch { }
                    }
                }
                Thread.Sleep(50);
            }

            return hasSpawned;
        }

        private void ProcessMessage(ServerMessage msg)
        {
            switch (msg.Type)
            {
                case "welcome":
                    var welcome = JsonSerializer.Deserialize<Dictionary<string, object>>(msg.Data);
                    playerId = welcome["PlayerId"].ToString();
                    Console.WriteLine($"Connected as: {welcome["Name"]}");
                    break;

                case "spawnpoints":
                    availableSpawnPoints = JsonSerializer.Deserialize<List<SpawnPointInfo>>(msg.Data) ?? new();
                    Console.WriteLine($"Received {availableSpawnPoints.Count} spawn points.");
                    break;

                case "spawned":
                    hasSpawned = true;
                    var spawnData = JsonSerializer.Deserialize<Dictionary<string, object>>(msg.Data);
                    Console.ForegroundColor = ConsoleColor.Green;
                    Console.WriteLine($"\nSpawned at {spawnData["SpawnPoint"]} in {spawnData["Region"]}!");
                    Console.ResetColor();
                    break;

                case "state":
                    lastSnapshot = WorldSnapshot.FromJson(msg.Data);
                    UpdateOtherPlayers(lastSnapshot);
                    break;

                case "kick":
                    Console.WriteLine("\n[!] You have been kicked from the server.");
                    running = false;
                    break;

                case "error":
                    Console.ForegroundColor = ConsoleColor.Red;
                    Console.WriteLine($"\n[Error] {msg.Data}");
                    Console.ResetColor();
                    break;
            }
        }

        public void StartSync()
        {
            if (!hasSpawned)
            {
                Console.ForegroundColor = ConsoleColor.Red;
                Console.WriteLine("Error: Cannot sync without spawning first!");
                Console.ResetColor();
                return;
            }

            running = true;

            // Receive server state
            new Thread(ReceiveLoop) { IsBackground = true, Name = "Receive" }.Start();

            // Send local position
            new Thread(SendLoop) { IsBackground = true, Name = "Send" }.Start();
        }

        public void Disconnect()
        {
            running = false;
            client?.Close();
        }

        public void ShowStatus()
        {
            Console.WriteLine($"\n=== STATUS ===");
            Console.WriteLine($"Player: {playerName} ({playerId})");
            Console.WriteLine($"Connected: {IsConnected}");
            Console.WriteLine($"Spawned: {hasSpawned}");

            if (lastSnapshot != null)
            {
                Console.WriteLine($"Server tick: {lastSnapshot.Tick}");
                Console.WriteLine($"Players online: {lastSnapshot.Players.Count}");
                foreach (var p in lastSnapshot.Players)
                {
                    var isMe = p.PlayerId == playerId ? " (YOU)" : "";
                    Console.WriteLine($"  {p.Name}{isMe} at ({p.X:F0}, {p.Z:F0})");
                }
            }
            Console.WriteLine();
        }

        public void SendChat(string message)
        {
            if (hasSpawned && !string.IsNullOrWhiteSpace(message))
            {
                SendMessage(new ClientMessage
                {
                    Type = "chat",
                    PlayerId = playerId,
                    Data = message
                });
            }
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
                    try
                    {
                        var msg = ServerMessage.FromJson(json);
                        ProcessMessage(msg);
                    }
                    catch { }
                }
                catch { }
            }
        }

        private void SendLoop()
        {
            float lastX = 0, lastZ = 0;

            while (running && IsConnected)
            {
                try
                {
                    // Read local player position from Kenshi
                    var pos = gameLink.ReadPlayerPosition();
                    if (pos != null)
                    {
                        // Only send if moved significantly
                        float dx = pos.Value.x - lastX;
                        float dz = pos.Value.z - lastZ;
                        if (dx * dx + dz * dz > 0.25f) // Moved > 0.5 units
                        {
                            SendMessage(new ClientMessage
                            {
                                Type = "update",
                                PlayerId = playerId,
                                Data = new PositionUpdate
                                {
                                    X = pos.Value.x,
                                    Y = pos.Value.y,
                                    Z = pos.Value.z,
                                    Health = pos.Value.health
                                }.ToJson()
                            });

                            lastX = pos.Value.x;
                            lastZ = pos.Value.z;

                            // Update console
                            var playerCount = lastSnapshot?.Players.Count ?? 0;
                            Console.Write($"\rPos: ({pos.Value.x:F0}, {pos.Value.z:F0}) | Players: {playerCount}   ");
                        }
                    }

                    Thread.Sleep(50); // 20 Hz
                }
                catch { }
            }
        }

        private void UpdateOtherPlayers(WorldSnapshot snapshot)
        {
            // Update or create markers for other players
            foreach (var p in snapshot.Players)
            {
                if (p.PlayerId == playerId) continue; // Skip self

                if (!otherPlayers.TryGetValue(p.PlayerId, out var other))
                {
                    other = new OtherPlayer { PlayerId = p.PlayerId, Name = p.Name };
                    otherPlayers[p.PlayerId] = other;
                    Console.WriteLine($"\n[+] {p.Name} is nearby");
                }

                other.X = p.X;
                other.Y = p.Y;
                other.Z = p.Z;

                // In full implementation: write other player position to Kenshi
                // to render them in-game
                // gameLink.SpawnOrUpdatePlayer(p.PlayerId, p.Name, p.X, p.Y, p.Z);
            }

            // Remove players who left
            var currentIds = snapshot.Players.Select(p => p.PlayerId).ToHashSet();
            foreach (var id in otherPlayers.Keys.ToList())
            {
                if (!currentIds.Contains(id))
                {
                    if (otherPlayers.TryRemove(id, out var removed))
                    {
                        Console.WriteLine($"\n[-] {removed.Name} left");
                        // gameLink.DespawnPlayer(id);
                    }
                }
            }
        }

        private void SendMessage(ClientMessage msg)
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

    public class OtherPlayer
    {
        public string PlayerId { get; set; }
        public string Name { get; set; }
        public float X { get; set; }
        public float Y { get; set; }
        public float Z { get; set; }
    }

    #endregion

    #region Game Memory Link (Simplified)

    /// <summary>
    /// Simple memory link for reading local player position.
    /// Used by clients to report their position to the server.
    /// </summary>
    public class GameMemoryLink
    {
        [System.Runtime.InteropServices.DllImport("kernel32.dll")]
        static extern IntPtr OpenProcess(int access, bool inherit, int pid);

        [System.Runtime.InteropServices.DllImport("kernel32.dll")]
        static extern bool ReadProcessMemory(IntPtr handle, IntPtr addr, byte[] buffer, int size, out int read);

        const int PROCESS_ALL_ACCESS = 0x1F0FFF;
        const long SELECTED_CHARACTER = 0x24C5A30;
        const int POS_OFFSET = 0x70;
        const int HEALTH_OFFSET = 0xC0;

        private IntPtr processHandle;
        private IntPtr baseAddress;

        public bool Connect()
        {
            try
            {
                var procs = System.Diagnostics.Process.GetProcessesByName("kenshi_x64");
                if (procs.Length == 0)
                    procs = System.Diagnostics.Process.GetProcessesByName("kenshi");

                if (procs.Length == 0) return false;

                var proc = procs[0];
                processHandle = OpenProcess(PROCESS_ALL_ACCESS, false, proc.Id);
                baseAddress = proc.MainModule.BaseAddress;

                return processHandle != IntPtr.Zero;
            }
            catch { return false; }
        }

        public (float x, float y, float z, float health)? ReadPlayerPosition()
        {
            try
            {
                // Read character pointer
                var ptrBuf = new byte[8];
                ReadProcessMemory(processHandle, baseAddress + (int)SELECTED_CHARACTER, ptrBuf, 8, out _);
                var charPtr = (IntPtr)BitConverter.ToInt64(ptrBuf, 0);

                if (charPtr == IntPtr.Zero) return null;

                // Read position
                var posBuf = new byte[12];
                ReadProcessMemory(processHandle, charPtr + POS_OFFSET, posBuf, 12, out _);
                float x = BitConverter.ToSingle(posBuf, 0);
                float y = BitConverter.ToSingle(posBuf, 4);
                float z = BitConverter.ToSingle(posBuf, 8);

                // Read health
                var healthBuf = new byte[4];
                ReadProcessMemory(processHandle, charPtr + HEALTH_OFFSET, healthBuf, 4, out _);
                float health = BitConverter.ToSingle(healthBuf, 0);

                return (x, y, z, health);
            }
            catch { return null; }
        }
    }

    #endregion
}
