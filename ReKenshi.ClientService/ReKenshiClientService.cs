using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.IO;
using System.IO.Pipes;
using System.Net.Sockets;
using System.Text;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;

namespace ReKenshi.ClientService
{
    /// <summary>
    /// IPC Message from C++ plugin
    /// </summary>
    public class IPCMessage
    {
        public string Type { get; set; }
        public Dictionary<string, object> Data { get; set; } = new();
    }

    /// <summary>
    /// Re_Kenshi Client Service
    /// Bridges C++ plugin (IPC) with game server (TCP)
    /// </summary>
    public class ReKenshiClientService
    {
        private const string PIPE_NAME = "ReKenshi_IPC";
        private readonly string _serverHost;
        private readonly int _serverPort;
        private string _playerId;

        private NamedPipeServerStream _pipeServer;
        private TcpClient _tcpClient;
        private NetworkStream _networkStream;
        private CancellationTokenSource _cts;
        private bool _isRunning;

        // Remote players received from server
        private readonly ConcurrentDictionary<string, RemotePlayer> _remotePlayers = new();

        public class RemotePlayer
        {
            public string PlayerId { get; set; }
            public float PosX { get; set; }
            public float PosY { get; set; }
            public float PosZ { get; set; }
            public float Health { get; set; }
            public bool IsAlive { get; set; }
        }

        public ReKenshiClientService(string serverHost = "localhost", int serverPort = 7777)
        {
            _serverHost = serverHost;
            _serverPort = serverPort;
        }

        public async Task StartAsync()
        {
            _cts = new CancellationTokenSource();
            _isRunning = true;

            Console.WriteLine($"╔════════════════════════════════════════════════════════╗");
            Console.WriteLine($"║          Re_Kenshi Client Service v1.0                ║");
            Console.WriteLine($"╚════════════════════════════════════════════════════════╝");

            // Generate player ID
            _playerId = $"Player_{Environment.MachineName}_{DateTime.UtcNow.Ticks % 10000}";
            Console.WriteLine($"Player ID: {_playerId}");

            // Connect to game server
            Console.WriteLine($"Connecting to game server {_serverHost}:{_serverPort}...");
            await ConnectToServerAsync();

            // Start IPC server for C++ plugin
            Console.WriteLine("Starting IPC server for C++ plugin...");
            _ = Task.Run(() => RunIPCServerAsync(_cts.Token));

            // Start receiving from game server
            _ = Task.Run(() => ReceiveFromServerAsync(_cts.Token));

            Console.WriteLine("Client service ready!");
            Console.WriteLine("Waiting for C++ plugin to connect...\n");

            // Keep running
            while (_isRunning)
            {
                await Task.Delay(1000);
            }
        }

        private async Task ConnectToServerAsync()
        {
            try
            {
                _tcpClient = new TcpClient();
                await _tcpClient.ConnectAsync(_serverHost, _serverPort);
                _networkStream = _tcpClient.GetStream();

                // Send join message
                var joinMessage = new
                {
                    Type = "player_join",
                    PlayerId = _playerId,
                    Data = new Dictionary<string, object>(),
                    Timestamp = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds()
                };

                await SendToServerAsync(joinMessage);
                Console.WriteLine("✓ Connected to game server");
            }
            catch (Exception ex)
            {
                Console.WriteLine($"[ERROR] Failed to connect to server: {ex.Message}");
                throw;
            }
        }

        private async Task RunIPCServerAsync(CancellationToken ct)
        {
            while (!ct.IsCancellationRequested && _isRunning)
            {
                try
                {
                    _pipeServer = new NamedPipeServerStream(
                        PIPE_NAME,
                        PipeDirection.InOut,
                        1,
                        PipeTransmissionMode.Message,
                        PipeOptions.Asynchronous
                    );

                    Console.WriteLine($"Waiting for C++ plugin on pipe '{PIPE_NAME}'...");
                    await _pipeServer.WaitForConnectionAsync(ct);
                    Console.WriteLine("✓ C++ plugin connected via IPC");

                    await HandleIPCClientAsync(ct);
                }
                catch (Exception ex)
                {
                    if (_isRunning)
                    {
                        Console.WriteLine($"[ERROR] IPC server error: {ex.Message}");
                        await Task.Delay(1000, ct);
                    }
                }
                finally
                {
                    _pipeServer?.Dispose();
                }
            }
        }

        private async Task HandleIPCClientAsync(CancellationToken ct)
        {
            try
            {
                var reader = new StreamReader(_pipeServer, Encoding.UTF8);
                var writer = new StreamWriter(_pipeServer, Encoding.UTF8) { AutoFlush = true };

                // Send remote players to C++ plugin
                _ = Task.Run(async () =>
                {
                    while (!ct.IsCancellationRequested && _pipeServer.IsConnected)
                    {
                        await SendRemotePlayersToPlugin(writer, ct);
                        await Task.Delay(100, ct); // 10 Hz update rate
                    }
                }, ct);

                // Receive from C++ plugin
                while (!ct.IsCancellationRequested && _pipeServer.IsConnected)
                {
                    var line = await reader.ReadLineAsync();
                    if (line == null) break;

                    var message = JsonSerializer.Deserialize<IPCMessage>(line);
                    if (message != null)
                    {
                        await HandleIPCMessage(message);
                    }
                }
            }
            catch (Exception ex)
            {
                Console.WriteLine($"[ERROR] IPC client handler error: {ex.Message}");
            }
        }

        private async Task HandleIPCMessage(IPCMessage message)
        {
            try
            {
                switch (message.Type)
                {
                    case "player_update":
                        // Forward player state to server
                        var serverMessage = new
                        {
                            Type = "player_update",
                            PlayerId = _playerId,
                            Data = message.Data,
                            Timestamp = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds()
                        };
                        await SendToServerAsync(serverMessage);
                        break;

                    case "chat":
                        // Forward chat to server
                        var chatMessage = new
                        {
                            Type = "chat",
                            PlayerId = _playerId,
                            Data = message.Data,
                            Timestamp = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds()
                        };
                        await SendToServerAsync(chatMessage);
                        break;

                    default:
                        Console.WriteLine($"[WARN] Unknown IPC message type: {message.Type}");
                        break;
                }
            }
            catch (Exception ex)
            {
                Console.WriteLine($"[ERROR] Handle IPC message error: {ex.Message}");
            }
        }

        private async Task SendToServerAsync(object message)
        {
            try
            {
                if (_networkStream != null && _networkStream.CanWrite)
                {
                    var json = JsonSerializer.Serialize(message);
                    var bytes = Encoding.UTF8.GetBytes(json);
                    await _networkStream.WriteAsync(bytes, 0, bytes.Length);
                }
            }
            catch (Exception ex)
            {
                Console.WriteLine($"[ERROR] Send to server error: {ex.Message}");
            }
        }

        private async Task ReceiveFromServerAsync(CancellationToken ct)
        {
            var buffer = new byte[8192];

            try
            {
                while (!ct.IsCancellationRequested && _isRunning)
                {
                    var bytesRead = await _networkStream.ReadAsync(buffer, 0, buffer.Length, ct);
                    if (bytesRead == 0) break;

                    var json = Encoding.UTF8.GetString(buffer, 0, bytesRead);
                    var message = JsonSerializer.Deserialize<Dictionary<string, object>>(json);

                    if (message != null)
                    {
                        await HandleServerMessage(message);
                    }
                }
            }
            catch (Exception ex)
            {
                if (_isRunning)
                {
                    Console.WriteLine($"[ERROR] Receive from server error: {ex.Message}");
                }
            }
        }

        private async Task HandleServerMessage(Dictionary<string, object> message)
        {
            try
            {
                var type = message.GetValueOrDefault("Type")?.ToString();
                var playerId = message.GetValueOrDefault("PlayerId")?.ToString();

                if (playerId == _playerId)
                    return; // Ignore own messages

                switch (type)
                {
                    case "player_join":
                        Console.WriteLine($"[SERVER] Player joined: {playerId}");
                        UpdateRemotePlayer(playerId, message);
                        break;

                    case "player_update":
                        UpdateRemotePlayer(playerId, message);
                        break;

                    case "player_leave":
                        Console.WriteLine($"[SERVER] Player left: {playerId}");
                        _remotePlayers.TryRemove(playerId, out _);
                        break;

                    case "chat":
                        var chatMsg = message.GetValueOrDefault("Data") as JsonElement?;
                        if (chatMsg.HasValue)
                        {
                            var msgText = chatMsg.Value.GetProperty("message").GetString();
                            Console.WriteLine($"[CHAT] {playerId}: {msgText}");
                        }
                        break;
                }
            }
            catch (Exception ex)
            {
                Console.WriteLine($"[ERROR] Handle server message error: {ex.Message}");
            }

            await Task.CompletedTask;
        }

        private void UpdateRemotePlayer(string playerId, Dictionary<string, object> message)
        {
            var dataElement = message.GetValueOrDefault("Data") as JsonElement?;
            if (!dataElement.HasValue) return;

            var data = dataElement.Value;

            var player = _remotePlayers.GetOrAdd(playerId, _ => new RemotePlayer { PlayerId = playerId });

            if (data.TryGetProperty("posX", out var posX))
                player.PosX = posX.GetSingle();
            if (data.TryGetProperty("posY", out var posY))
                player.PosY = posY.GetSingle();
            if (data.TryGetProperty("posZ", out var posZ))
                player.PosZ = posZ.GetSingle();
            if (data.TryGetProperty("health", out var health))
                player.Health = health.GetSingle();
            if (data.TryGetProperty("isAlive", out var isAlive))
                player.IsAlive = isAlive.GetBoolean();
        }

        private async Task SendRemotePlayersToPlugin(StreamWriter writer, CancellationToken ct)
        {
            try
            {
                foreach (var player in _remotePlayers.Values)
                {
                    var message = new
                    {
                        Type = "remote_player",
                        PlayerId = player.PlayerId,
                        Data = new Dictionary<string, object>
                        {
                            { "posX", player.PosX },
                            { "posY", player.PosY },
                            { "posZ", player.PosZ },
                            { "health", player.Health },
                            { "isAlive", player.IsAlive }
                        }
                    };

                    var json = JsonSerializer.Serialize(message);
                    await writer.WriteLineAsync(json);
                }
            }
            catch (Exception ex)
            {
                if (_isRunning)
                {
                    Console.WriteLine($"[ERROR] Send to plugin error: {ex.Message}");
                }
            }
        }

        public void Stop()
        {
            Console.WriteLine("Stopping client service...");
            _isRunning = false;
            _cts?.Cancel();
            _pipeServer?.Dispose();
            _networkStream?.Dispose();
            _tcpClient?.Close();
            Console.WriteLine("Client service stopped");
        }

        public static async Task Main(string[] args)
        {
            var host = args.Length > 0 ? args[0] : "localhost";
            var port = args.Length > 1 && int.TryParse(args[1], out var p) ? p : 7777;

            var service = new ReKenshiClientService(host, port);

            Console.CancelKeyPress += (sender, e) =>
            {
                e.Cancel = true;
                service.Stop();
            };

            try
            {
                await service.StartAsync();
            }
            catch (Exception ex)
            {
                Console.WriteLine($"[FATAL] Service error: {ex.Message}");
                Environment.Exit(1);
            }
        }
    }
}
