using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;

namespace ReKenshi.Server
{
    /// <summary>
    /// Multiplayer game state message
    /// </summary>
    public class GameStateMessage
    {
        public string Type { get; set; }  // "player_update", "player_join", "player_leave"
        public string PlayerId { get; set; }
        public Dictionary<string, object> Data { get; set; } = new();
        public long Timestamp { get; set; }
    }

    /// <summary>
    /// Connected player information
    /// </summary>
    public class ConnectedPlayer
    {
        public string PlayerId { get; set; }
        public TcpClient Client { get; set; }
        public DateTime ConnectedAt { get; set; }
        public DateTime LastUpdate { get; set; }

        // Player state
        public float PosX { get; set; }
        public float PosY { get; set; }
        public float PosZ { get; set; }
        public float Health { get; set; }
        public bool IsAlive { get; set; }
    }

    /// <summary>
    /// Re_Kenshi Multiplayer Server
    /// Handles TCP connections from game clients and synchronizes player state
    /// </summary>
    public class ReKenshiServer
    {
        private TcpListener _listener;
        private readonly int _port;
        private readonly ConcurrentDictionary<string, ConnectedPlayer> _players = new();
        private CancellationTokenSource _cts;
        private bool _isRunning;

        public ReKenshiServer(int port = 7777)
        {
            _port = port;
        }

        public void Start()
        {
            _cts = new CancellationTokenSource();
            _isRunning = true;

            _listener = new TcpListener(IPAddress.Any, _port);
            _listener.Start();

            Console.WriteLine($"╔════════════════════════════════════════════════════════╗");
            Console.WriteLine($"║          Re_Kenshi Multiplayer Server v1.0            ║");
            Console.WriteLine($"╚════════════════════════════════════════════════════════╝");
            Console.WriteLine($"Server listening on port {_port}");
            Console.WriteLine($"Waiting for players to connect...\n");

            // Start accepting clients
            Task.Run(() => AcceptClientsAsync(_cts.Token));

            // Start cleanup task
            Task.Run(() => CleanupTask(_cts.Token));

            // Start console command handler
            Task.Run(() => HandleConsoleCommands());
        }

        private async Task AcceptClientsAsync(CancellationToken ct)
        {
            while (!ct.IsCancellationRequested && _isRunning)
            {
                try
                {
                    var client = await _listener.AcceptTcpClientAsync();
                    _ = Task.Run(() => HandleClientAsync(client, ct), ct);
                }
                catch (Exception ex)
                {
                    if (_isRunning)
                    {
                        Console.WriteLine($"[ERROR] Accept client failed: {ex.Message}");
                    }
                }
            }
        }

        private async Task HandleClientAsync(TcpClient client, CancellationToken ct)
        {
            string playerId = null;
            NetworkStream stream = null;

            try
            {
                stream = client.GetStream();
                var buffer = new byte[8192];

                // First message should be player join
                var bytesRead = await stream.ReadAsync(buffer, 0, buffer.Length, ct);
                if (bytesRead == 0) return;

                var jsonMessage = Encoding.UTF8.GetString(buffer, 0, bytesRead);
                var message = JsonSerializer.Deserialize<GameStateMessage>(jsonMessage);

                if (message?.Type == "player_join" && message.PlayerId != null)
                {
                    playerId = message.PlayerId;

                    var player = new ConnectedPlayer
                    {
                        PlayerId = playerId,
                        Client = client,
                        ConnectedAt = DateTime.UtcNow,
                        LastUpdate = DateTime.UtcNow,
                        IsAlive = true
                    };

                    _players[playerId] = player;

                    Console.WriteLine($"[JOIN] Player '{playerId}' connected ({_players.Count} players online)");

                    // Send current players to new player
                    await SendCurrentPlayersToNewPlayer(player, ct);

                    // Broadcast join to other players
                    await BroadcastMessage(message, playerId, ct);

                    // Handle ongoing messages
                    while (!ct.IsCancellationRequested && client.Connected)
                    {
                        bytesRead = await stream.ReadAsync(buffer, 0, buffer.Length, ct);
                        if (bytesRead == 0) break;

                        jsonMessage = Encoding.UTF8.GetString(buffer, 0, bytesRead);
                        message = JsonSerializer.Deserialize<GameStateMessage>(jsonMessage);

                        if (message != null)
                        {
                            message.Timestamp = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();

                            // Update player state
                            UpdatePlayerState(playerId, message);

                            // Broadcast to other players
                            await BroadcastMessage(message, playerId, ct);
                        }
                    }
                }
            }
            catch (Exception ex)
            {
                Console.WriteLine($"[ERROR] Client handler error: {ex.Message}");
            }
            finally
            {
                if (playerId != null)
                {
                    _players.TryRemove(playerId, out _);
                    Console.WriteLine($"[LEAVE] Player '{playerId}' disconnected ({_players.Count} players online)");

                    // Broadcast leave message
                    var leaveMessage = new GameStateMessage
                    {
                        Type = "player_leave",
                        PlayerId = playerId,
                        Timestamp = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds()
                    };

                    await BroadcastMessage(leaveMessage, playerId, ct);
                }

                client?.Close();
            }
        }

        private void UpdatePlayerState(string playerId, GameStateMessage message)
        {
            if (_players.TryGetValue(playerId, out var player) && message.Data != null)
            {
                player.LastUpdate = DateTime.UtcNow;

                if (message.Data.TryGetValue("posX", out var posX))
                    player.PosX = Convert.ToSingle(posX);
                if (message.Data.TryGetValue("posY", out var posY))
                    player.PosY = Convert.ToSingle(posY);
                if (message.Data.TryGetValue("posZ", out var posZ))
                    player.PosZ = Convert.ToSingle(posZ);
                if (message.Data.TryGetValue("health", out var health))
                    player.Health = Convert.ToSingle(health);
                if (message.Data.TryGetValue("isAlive", out var isAlive))
                    player.IsAlive = Convert.ToBoolean(isAlive);
            }
        }

        private async Task SendCurrentPlayersToNewPlayer(ConnectedPlayer newPlayer, CancellationToken ct)
        {
            foreach (var existingPlayer in _players.Values)
            {
                if (existingPlayer.PlayerId == newPlayer.PlayerId)
                    continue;

                var playerMessage = new GameStateMessage
                {
                    Type = "player_join",
                    PlayerId = existingPlayer.PlayerId,
                    Data = new Dictionary<string, object>
                    {
                        { "posX", existingPlayer.PosX },
                        { "posY", existingPlayer.PosY },
                        { "posZ", existingPlayer.PosZ },
                        { "health", existingPlayer.Health },
                        { "isAlive", existingPlayer.IsAlive }
                    },
                    Timestamp = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds()
                };

                await SendMessageToClient(newPlayer.Client, playerMessage, ct);
            }
        }

        private async Task BroadcastMessage(GameStateMessage message, string excludePlayerId, CancellationToken ct)
        {
            var tasks = new List<Task>();

            foreach (var player in _players.Values)
            {
                if (player.PlayerId != excludePlayerId && player.Client.Connected)
                {
                    tasks.Add(SendMessageToClient(player.Client, message, ct));
                }
            }

            await Task.WhenAll(tasks);
        }

        private async Task SendMessageToClient(TcpClient client, GameStateMessage message, CancellationToken ct)
        {
            try
            {
                var json = JsonSerializer.Serialize(message);
                var bytes = Encoding.UTF8.GetBytes(json);

                var stream = client.GetStream();
                await stream.WriteAsync(bytes, 0, bytes.Length, ct);
            }
            catch (Exception ex)
            {
                Console.WriteLine($"[ERROR] Send to client failed: {ex.Message}");
            }
        }

        private async Task CleanupTask(CancellationToken ct)
        {
            while (!ct.IsCancellationRequested)
            {
                await Task.Delay(30000, ct); // Every 30 seconds

                var timeout = TimeSpan.FromMinutes(5);
                var now = DateTime.UtcNow;

                foreach (var player in _players.Values)
                {
                    if (now - player.LastUpdate > timeout)
                    {
                        Console.WriteLine($"[TIMEOUT] Player '{player.PlayerId}' timed out");
                        _players.TryRemove(player.PlayerId, out _);
                        player.Client.Close();
                    }
                }
            }
        }

        private void HandleConsoleCommands()
        {
            Console.WriteLine("Server commands: /list, /kick <playerId>, /stop");
            Console.WriteLine();

            while (_isRunning)
            {
                try
                {
                    var cmd = Console.ReadLine();
                    if (string.IsNullOrWhiteSpace(cmd)) continue;

                    if (cmd == "/list")
                    {
                        Console.WriteLine($"\nConnected Players ({_players.Count}):");
                        foreach (var player in _players.Values)
                        {
                            Console.WriteLine($"  - {player.PlayerId}");
                            Console.WriteLine($"      Position: ({player.PosX:F1}, {player.PosY:F1}, {player.PosZ:F1})");
                            Console.WriteLine($"      Health: {player.Health:F1} | Alive: {player.IsAlive}");
                            Console.WriteLine($"      Last Update: {(DateTime.UtcNow - player.LastUpdate).TotalSeconds:F1}s ago");
                        }
                        Console.WriteLine();
                    }
                    else if (cmd.StartsWith("/kick "))
                    {
                        var playerId = cmd.Substring(6).Trim();
                        if (_players.TryRemove(playerId, out var player))
                        {
                            player.Client.Close();
                            Console.WriteLine($"Kicked player '{playerId}'");
                        }
                        else
                        {
                            Console.WriteLine($"Player '{playerId}' not found");
                        }
                    }
                    else if (cmd == "/stop")
                    {
                        Stop();
                    }
                    else
                    {
                        Console.WriteLine("Unknown command. Available: /list, /kick <playerId>, /stop");
                    }
                }
                catch (Exception ex)
                {
                    Console.WriteLine($"[ERROR] Command error: {ex.Message}");
                }
            }
        }

        public void Stop()
        {
            Console.WriteLine("Stopping server...");
            _isRunning = false;
            _cts?.Cancel();
            _listener?.Stop();

            foreach (var player in _players.Values)
            {
                player.Client.Close();
            }

            _players.Clear();
            Console.WriteLine("Server stopped");
        }

        public static void Main(string[] args)
        {
            var port = args.Length > 0 && int.TryParse(args[0], out var p) ? p : 7777;
            var server = new ReKenshiServer(port);
            server.Start();

            // Keep running
            while (true)
            {
                Thread.Sleep(1000);
            }
        }
    }
}
