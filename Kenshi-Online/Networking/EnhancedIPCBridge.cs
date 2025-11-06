using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.IO.Pipes;
using System.Linq;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using Newtonsoft.Json;
using KenshiMultiplayer.Utility;
using KenshiMultiplayer.Data;
using KenshiMultiplayer.Game;

namespace KenshiMultiplayer.Networking
{
    /// <summary>
    /// Enhanced IPC Bridge - Handles heavy lifting for game-middleware communication
    /// Uses named pipes for high-performance bidirectional communication
    /// Offloads processing from the game mod to the middleware
    /// </summary>
    public class EnhancedIPCBridge : IDisposable
    {
        private const string PIPE_NAME = "KenshiOnlineIPC";
        private const int MAX_CONNECTIONS = 10;

        private NamedPipeServerStream _pipeServer;
        private readonly List<NamedPipeServerStream> _clientPipes;
        private readonly ConcurrentQueue<IPCMessage> _incomingMessages;
        private readonly ConcurrentQueue<IPCMessage> _outgoingMessages;

        private readonly InterpolationEngine _interpolationEngine;
        private readonly CompressionEngine _compressionEngine;
        private readonly NetworkScheduler _networkScheduler;
        private readonly SaveGameLoader _saveGameLoader;

        private readonly Thread _pipeListenerThread;
        private readonly Thread _messageProcessorThread;
        private bool _running;

        // Statistics
        private long _messagesReceived;
        private long _messagesSent;
        private long _bytesReceived;
        private long _bytesSent;

        public event Action<IPCMessage> OnMessageReceived;
        public event Action<string> OnClientConnected;
        public event Action<string> OnClientDisconnected;

        public EnhancedIPCBridge(
            InterpolationEngine interpolationEngine = null,
            CompressionEngine compressionEngine = null,
            NetworkScheduler networkScheduler = null)
        {
            _interpolationEngine = interpolationEngine ?? new InterpolationEngine();
            _compressionEngine = compressionEngine ?? new CompressionEngine();
            _networkScheduler = networkScheduler ?? new NetworkScheduler();
            _saveGameLoader = new SaveGameLoader();

            _clientPipes = new List<NamedPipeServerStream>();
            _incomingMessages = new ConcurrentQueue<IPCMessage>();
            _outgoingMessages = new ConcurrentQueue<IPCMessage>();

            _running = true;
            _pipeListenerThread = new Thread(ListenForConnections) { IsBackground = true };
            _messageProcessorThread = new Thread(ProcessMessages) { IsBackground = true };
        }

        public void Start()
        {
            Console.WriteLine("Starting Enhanced IPC Bridge...");

            _pipeListenerThread.Start();
            _messageProcessorThread.Start();

            Console.WriteLine("Enhanced IPC Bridge started successfully");
        }

        public void Stop()
        {
            Console.WriteLine("Stopping Enhanced IPC Bridge...");
            _running = false;

            // Close all pipes
            lock (_clientPipes)
            {
                foreach (var pipe in _clientPipes)
                {
                    try
                    {
                        pipe.Close();
                        pipe.Dispose();
                    }
                    catch { }
                }
                _clientPipes.Clear();
            }

            _pipeServer?.Close();
            _pipeServer?.Dispose();

            Console.WriteLine("Enhanced IPC Bridge stopped");
        }

        /// <summary>
        /// Listen for incoming pipe connections
        /// </summary>
        private void ListenForConnections()
        {
            while (_running)
            {
                try
                {
                    // Create new pipe server
                    _pipeServer = new NamedPipeServerStream(
                        PIPE_NAME,
                        PipeDirection.InOut,
                        MAX_CONNECTIONS,
                        PipeTransmissionMode.Message,
                        PipeOptions.Asynchronous);

                    Console.WriteLine("Waiting for IPC connection...");

                    // Wait for connection
                    _pipeServer.WaitForConnection();

                    Console.WriteLine("IPC client connected!");

                    lock (_clientPipes)
                    {
                        _clientPipes.Add(_pipeServer);
                    }

                    // Start reading from this pipe
                    Task.Run(() => ReadFromPipe(_pipeServer));

                    OnClientConnected?.Invoke(_pipeServer.GetHashCode().ToString());
                }
                catch (Exception ex)
                {
                    if (_running)
                    {
                        Console.WriteLine($"IPC connection error: {ex.Message}");
                        Thread.Sleep(1000);
                    }
                }
            }
        }

        /// <summary>
        /// Read messages from a pipe
        /// </summary>
        private async void ReadFromPipe(NamedPipeServerStream pipe)
        {
            try
            {
                byte[] buffer = new byte[8192];

                while (_running && pipe.IsConnected)
                {
                    int bytesRead = await pipe.ReadAsync(buffer, 0, buffer.Length);

                    if (bytesRead > 0)
                    {
                        Interlocked.Add(ref _bytesReceived, bytesRead);

                        // Deserialize message
                        string json = Encoding.UTF8.GetString(buffer, 0, bytesRead);
                        var message = JsonConvert.DeserializeObject<IPCMessage>(json);

                        if (message != null)
                        {
                            _incomingMessages.Enqueue(message);
                            Interlocked.Increment(ref _messagesReceived);
                        }
                    }
                }
            }
            catch (Exception ex)
            {
                Console.WriteLine($"Pipe read error: {ex.Message}");
            }
            finally
            {
                lock (_clientPipes)
                {
                    _clientPipes.Remove(pipe);
                }

                OnClientDisconnected?.Invoke(pipe.GetHashCode().ToString());
            }
        }

        /// <summary>
        /// Process incoming and outgoing messages
        /// </summary>
        private void ProcessMessages()
        {
            while (_running)
            {
                try
                {
                    // Process incoming messages
                    while (_incomingMessages.TryDequeue(out var message))
                    {
                        ProcessIncomingMessage(message);
                    }

                    // Process outgoing messages
                    while (_outgoingMessages.TryDequeue(out var message))
                    {
                        SendMessageToPipes(message);
                    }

                    Thread.Sleep(1);
                }
                catch (Exception ex)
                {
                    Console.WriteLine($"Message processing error: {ex.Message}");
                }
            }
        }

        /// <summary>
        /// Process an incoming message from the game
        /// </summary>
        private void ProcessIncomingMessage(IPCMessage message)
        {
            try
            {
                switch (message.Type)
                {
                    case IPCMessageType.PlayerState:
                        HandlePlayerState(message);
                        break;

                    case IPCMessageType.WorldState:
                        HandleWorldState(message);
                        break;

                    case IPCMessageType.CombatAction:
                        HandleCombatAction(message);
                        break;

                    case IPCMessageType.SpawnRequest:
                        HandleSpawnRequest(message);
                        break;

                    case IPCMessageType.JoinServer:
                        HandleJoinServer(message);
                        break;

                    case IPCMessageType.Disconnect:
                        HandleDisconnect(message);
                        break;

                    default:
                        Console.WriteLine($"Unknown IPC message type: {message.Type}");
                        break;
                }

                OnMessageReceived?.Invoke(message);
            }
            catch (Exception ex)
            {
                Console.WriteLine($"Error processing IPC message: {ex.Message}");
            }
        }

        #region Message Handlers

        private void HandlePlayerState(IPCMessage message)
        {
            // Extract player state
            var playerData = JsonConvert.DeserializeObject<PlayerData>(message.Payload);

            // Add to interpolation engine
            _interpolationEngine.AddSnapshot(
                playerData.PlayerId,
                new System.Numerics.Vector3(playerData.PositionX, playerData.PositionY, playerData.PositionZ),
                new System.Numerics.Vector3(0, 0, playerData.RotationZ),
                System.Numerics.Vector3.Zero,
                DateTimeOffset.Now.ToUnixTimeMilliseconds(),
                new Dictionary<string, float>
                {
                    { "health", playerData.Health },
                    { "hunger", playerData.Hunger }
                }
            );

            // Compress and forward to server
            var compressedData = _compressionEngine.Compress(playerData.PlayerId, playerData);

            // Schedule for network transmission
            _networkScheduler.ScheduleMessage(
                new GameMessage
                {
                    Type = MessageType.PlayerState,
                    PlayerId = playerData.PlayerId,
                    Data = new Dictionary<string, object>
                    {
                        { "compressed", Convert.ToBase64String(compressedData) }
                    }
                },
                (msg) =>
                {
                    // Send to server
                    // This would be handled by the network layer
                },
                NetworkScheduler.MessagePriority.High,
                tier: 1
            );
        }

        private void HandleWorldState(IPCMessage message)
        {
            // Process world state update
            Console.WriteLine("Processing world state update from game");
        }

        private void HandleCombatAction(IPCMessage message)
        {
            // Process combat action with high priority
            _networkScheduler.ScheduleMessage(
                message,
                (msg) =>
                {
                    Console.WriteLine($"Processing combat action: {message.Payload}");
                },
                NetworkScheduler.MessagePriority.Critical,
                tier: 1
            );
        }

        private async void HandleSpawnRequest(IPCMessage message)
        {
            // Handle spawn request
            var spawnData = JsonConvert.DeserializeObject<Dictionary<string, object>>(message.Payload);

            Console.WriteLine($"Processing spawn request for: {spawnData.GetValueOrDefault("characterName", "Unknown")}");

            // Validate and process spawn
            // This is where the middleware does the heavy lifting
        }

        private async void HandleJoinServer(IPCMessage message)
        {
            var joinData = JsonConvert.DeserializeObject<Dictionary<string, object>>(message.Payload);

            string serverName = joinData.GetValueOrDefault("serverName", "Unknown").ToString();
            string playerId = joinData.GetValueOrDefault("playerId", "Player").ToString();

            // Get spawn position from server
            var spawnPosition = new Position
            {
                X = -4200,  // The Hub by default
                Y = 150,
                Z = 18500
            };

            Console.WriteLine($"Processing server join request: Player {playerId} joining {serverName}");

            // Create save game for the player
            bool success = await _saveGameLoader.LoadPlayerIntoServer(
                serverName,
                playerId,
                spawnPosition,
                (saveName) =>
                {
                    Console.WriteLine($"Created multiplayer save: {saveName}");

                    // Send response back to game
                    SendMessage(new IPCMessage
                    {
                        Type = IPCMessageType.JoinServerResponse,
                        Payload = JsonConvert.SerializeObject(new
                        {
                            success = true,
                            saveName = saveName,
                            spawnPosition = new { spawnPosition.X, spawnPosition.Y, spawnPosition.Z }
                        })
                    });
                }
            );

            if (!success)
            {
                SendMessage(new IPCMessage
                {
                    Type = IPCMessageType.JoinServerResponse,
                    Payload = JsonConvert.SerializeObject(new { success = false, error = "Failed to create save game" })
                });
            }
        }

        private void HandleDisconnect(IPCMessage message)
        {
            Console.WriteLine("Processing disconnect request from game");
        }

        #endregion

        /// <summary>
        /// Send a message to all connected pipes
        /// </summary>
        private void SendMessageToPipes(IPCMessage message)
        {
            try
            {
                string json = JsonConvert.SerializeObject(message);
                byte[] data = Encoding.UTF8.GetBytes(json);

                lock (_clientPipes)
                {
                    foreach (var pipe in _clientPipes.ToList())
                    {
                        try
                        {
                            if (pipe.IsConnected)
                            {
                                pipe.Write(data, 0, data.Length);
                                pipe.Flush();

                                Interlocked.Add(ref _bytesSent, data.Length);
                                Interlocked.Increment(ref _messagesSent);
                            }
                        }
                        catch (Exception ex)
                        {
                            Console.WriteLine($"Failed to send to pipe: {ex.Message}");
                            _clientPipes.Remove(pipe);
                        }
                    }
                }
            }
            catch (Exception ex)
            {
                Console.WriteLine($"Error sending message to pipes: {ex.Message}");
            }
        }

        /// <summary>
        /// Send a message to the game
        /// </summary>
        public void SendMessage(IPCMessage message)
        {
            _outgoingMessages.Enqueue(message);
        }

        /// <summary>
        /// Get statistics
        /// </summary>
        public (long Received, long Sent, long BytesReceived, long BytesSent, int ActiveConnections) GetStatistics()
        {
            int activeConnections;
            lock (_clientPipes)
            {
                activeConnections = _clientPipes.Count;
            }

            return (
                Interlocked.Read(ref _messagesReceived),
                Interlocked.Read(ref _messagesSent),
                Interlocked.Read(ref _bytesReceived),
                Interlocked.Read(ref _bytesSent),
                activeConnections
            );
        }

        public void Dispose()
        {
            Stop();
            _networkScheduler?.Dispose();
        }
    }

    #region IPC Message Types

    public enum IPCMessageType
    {
        PlayerState,
        WorldState,
        CombatAction,
        SpawnRequest,
        JoinServer,
        JoinServerResponse,
        Disconnect,
        ServerList,
        FriendsList,
        LobbyInvite
    }

    public class IPCMessage
    {
        public IPCMessageType Type { get; set; }
        public string Payload { get; set; }
        public long Timestamp { get; set; } = DateTimeOffset.Now.ToUnixTimeMilliseconds();
        public string MessageId { get; set; } = Guid.NewGuid().ToString();
    }

    #endregion
}
