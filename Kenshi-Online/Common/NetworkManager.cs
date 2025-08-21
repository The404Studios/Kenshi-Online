using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;
using KenshiMultiplayer.Auth;
using KenshiMultiplayer.Networking;
using KenshiMultiplayer.Networking.Player;
using KenshiMultiplayer.Networking.Inventory;

namespace KenshiMultiplayer.Common
{
    public enum NetworkMode
    {
        Server,
        Client,
        Host // Both server and client
    }

    public enum MessageType
    {
        // Core synchronization
        PositionSync,
        ActionRequest,
        ActionExecuted,
        StateSync,
        WorldStateSync,

        // Inventory and items
        InventorySync,
        ItemPickup,
        ItemDrop,
        ItemTransfer,

        // Combat
        CombatSync,
        DamageDealt,
        Death,
        Unconscious,

        // Character stats
        StatsSync,
        SkillUpdate,
        HungerUpdate,

        // Squad management
        SquadUpdate,
        SquadMemberAdded,
        SquadMemberRemoved,
        SquadOrdersChanged,

        // Building and construction
        BuildingPlaced,
        BuildingRemoved,
        BuildingDamaged,
        BuildingRepaired,

        // Path cache
        PathCacheRequest,
        PathCacheData,
        PathCacheUpdate,

        // Connection management
        Handshake,
        Heartbeat,
        PlayerJoined,
        PlayerLeft,
        Kick,

        // Chat and UI
        ChatMessage,
        Notification,
        ServerAnnouncement,

        // Save/Load
        SaveRequest,
        SaveData,
        LoadRequest,
        LoadData,

        // Time control
        TimeSync,
        PauseRequest,
        SpeedChangeRequest,
        BuildingPlace
    }

    public class NetworkMessage
    {
        public MessageType Type { get; set; }
        public string SenderId { get; set; }
        public object Data { get; set; }
        public long Timestamp { get; set; }
        public int SequenceNumber { get; set; }
        public bool RequiresAck { get; set; }
    }

    public class NetworkManager
    {
        private NetworkMode mode;
        private TcpListener tcpListener;
        private TcpClient tcpClient;
        private UdpClient udpClient;

        // Client management (server only)
        private ConcurrentDictionary<string, ClientConnection> connectedClients;
        private ConcurrentDictionary<string, DateTime> lastHeartbeat;

        // Message handling
        private ConcurrentQueue<NetworkMessage> incomingMessages;
        private ConcurrentQueue<NetworkMessage> outgoingMessages;
        private ConcurrentDictionary<int, NetworkMessage> pendingAcks;

        // Sequence tracking
        private int currentSequence = 0;
        private ConcurrentDictionary<string, int> clientSequences;

        // Network settings
        private readonly int maxClients = 16;
        private readonly int heartbeatInterval = 5000; // ms
        private readonly int heartbeatTimeout = 15000; // ms
        private readonly int maxMessageSize = 65536; // 64KB
        private readonly int udpPort = 27016;

        // Bandwidth management
        private BandwidthManager bandwidthManager;
        private readonly int maxBytesPerSecond = 100000; // 100KB/s per client

        // Encryption
        private EncryptionManager encryptionManager;

        // Events
        public event Action<NetworkMessage> OnMessageReceived;
        public event Action<string> OnClientConnected;
        public event Action<string> OnClientDisconnected;
        public event Action<string, string> OnChatMessage;

        // Statistics
        private NetworkStatistics stats;

        private CancellationTokenSource cancellationTokenSource;
        private bool isRunning;

        public NetworkManager(NetworkMode networkMode)
        {
            mode = networkMode;
            connectedClients = new ConcurrentDictionary<string, ClientConnection>();
            lastHeartbeat = new ConcurrentDictionary<string, DateTime>();
            incomingMessages = new ConcurrentQueue<NetworkMessage>();
            outgoingMessages = new ConcurrentQueue<NetworkMessage>();
            pendingAcks = new ConcurrentDictionary<int, NetworkMessage>();
            clientSequences = new ConcurrentDictionary<string, int>();

            bandwidthManager = new BandwidthManager(maxBytesPerSecond);
            encryptionManager = new EncryptionManager();
            stats = new NetworkStatistics();

            cancellationTokenSource = new CancellationTokenSource();
        }

        /// <summary>
        /// Start the server
        /// </summary>
        public async Task<bool> StartServer(int port)
        {
            if (mode != NetworkMode.Server && mode != NetworkMode.Host)
                return false;

            try
            {
                // Start TCP listener for reliable messages
                tcpListener = new TcpListener(IPAddress.Any, port);
                tcpListener.Start();

                // Start UDP listener for unreliable messages
                udpClient = new UdpClient(udpPort);

                isRunning = true;

                // Start accept loop
                Task.Run(() => AcceptClientsLoop(), cancellationTokenSource.Token);

                // Start message processing loops
                Task.Run(() => ProcessIncomingMessages(), cancellationTokenSource.Token);
                Task.Run(() => ProcessOutgoingMessages(), cancellationTokenSource.Token);

                // Start heartbeat loop
                Task.Run(() => HeartbeatLoop(), cancellationTokenSource.Token);

                // Start UDP receive loop
                Task.Run(() => ReceiveUdpLoop(), cancellationTokenSource.Token);

                Logger.Log($"Server started on TCP port {port} and UDP port {udpPort}");
                return true;
            }
            catch (Exception ex)
            {
                Logger.Log($"Failed to start server: {ex.Message}");
                return false;
            }
        }

        /// <summary>
        /// Connect to a server
        /// </summary>
        public async Task<bool> ConnectToServer(string address, int port)
        {
            if (mode != NetworkMode.Client && mode != NetworkMode.Host)
                return false;

            try
            {
                // Connect TCP
                tcpClient = new TcpClient();
                await tcpClient.ConnectAsync(address, port);

                // Setup UDP
                udpClient = new UdpClient();
                udpClient.Connect(address, udpPort);

                isRunning = true;

                // Start receive loops
                Task.Run(() => ReceiveTcpLoop(), cancellationTokenSource.Token);
                Task.Run(() => ReceiveUdpLoop(), cancellationTokenSource.Token);

                // Start message processing
                Task.Run(() => ProcessIncomingMessages(), cancellationTokenSource.Token);
                Task.Run(() => ProcessOutgoingMessages(), cancellationTokenSource.Token);

                // Send handshake
                await SendHandshake();

                // Start heartbeat
                Task.Run(() => ClientHeartbeatLoop(), cancellationTokenSource.Token);

                Logger.Log($"Connected to server at {address}:{port}");
                return true;
            }
            catch (Exception ex)
            {
                Logger.Log($"Failed to connect to server: {ex.Message}");
                return false;
            }
        }

        /// <summary>
        /// Accept clients loop (server only)
        /// </summary>
        private async Task AcceptClientsLoop()
        {
            while (isRunning && !cancellationTokenSource.Token.IsCancellationRequested)
            {
                try
                {
                    var tcpClient = await tcpListener.AcceptTcpClientAsync();

                    if (connectedClients.Count >= maxClients)
                    {
                        Logger.Log("Max clients reached, rejecting connection");
                        tcpClient.Close();
                        continue;
                    }

                    var clientId = Guid.NewGuid().ToString();
                    var client = new ClientConnection
                    {
                        Id = clientId,
                        TcpClient = tcpClient,
                        Stream = tcpClient.GetStream(),
                        LastActivity = DateTime.Now
                    };

                    if (connectedClients.TryAdd(clientId, client))
                    {
                        lastHeartbeat[clientId] = DateTime.Now;

                        // Start receive loop for this client
                        Task.Run(() => ReceiveFromClient(client), cancellationTokenSource.Token);

                        Logger.Log($"Client connected: {clientId}");
                    }
                }
                catch (Exception ex)
                {
                    Logger.Log($"Accept client error: {ex.Message}");
                }
            }
        }

        /// <summary>
        /// Receive from specific client (server only)
        /// </summary>
        private async Task ReceiveFromClient(ClientConnection client)
        {
            byte[] buffer = new byte[maxMessageSize];

            while (isRunning && client.TcpClient.Connected)
            {
                try
                {
                    int bytesRead = await client.Stream.ReadAsync(buffer, 0, buffer.Length);

                    if (bytesRead == 0)
                    {
                        // Client disconnected
                        DisconnectClient(client.Id);
                        break;
                    }

                    // Decrypt if needed
                    var data = encryptionManager.Decrypt(buffer, bytesRead);

                    // Deserialize message
                    var message = DeserializeMessage(data);
                    message.SenderId = client.Id;

                    // Update stats
                    stats.BytesReceived += bytesRead;
                    stats.MessagesReceived++;

                    // Update heartbeat
                    if (message.Type == MessageType.Heartbeat)
                    {
                        lastHeartbeat[client.Id] = DateTime.Now;
                        continue;
                    }

                    // Handle handshake
                    if (message.Type == MessageType.Handshake)
                    {
                        await HandleHandshake(client, message);
                        continue;
                    }

                    // Queue message for processing
                    incomingMessages.Enqueue(message);

                    // Send acknowledgment if required
                    if (message.RequiresAck)
                    {
                        await SendAck(client.Id, message.SequenceNumber);
                    }
                }
                catch (Exception ex)
                {
                    Logger.Log($"Receive error from client {client.Id}: {ex.Message}");
                    DisconnectClient(client.Id);
                    break;
                }
            }
        }

        /// <summary>
        /// Receive TCP loop (client only)
        /// </summary>
        private async Task ReceiveTcpLoop()
        {
            byte[] buffer = new byte[maxMessageSize];
            var stream = tcpClient.GetStream();

            while (isRunning && tcpClient.Connected)
            {
                try
                {
                    int bytesRead = await stream.ReadAsync(buffer, 0, buffer.Length);

                    if (bytesRead == 0)
                    {
                        // Server disconnected
                        Logger.Log("Disconnected from server");
                        await Shutdown();
                        break;
                    }

                    // Decrypt if needed
                    var data = encryptionManager.Decrypt(buffer, bytesRead);

                    // Deserialize message
                    var message = DeserializeMessage(data);

                    // Update stats
                    stats.BytesReceived += bytesRead;
                    stats.MessagesReceived++;

                    // Queue message for processing
                    incomingMessages.Enqueue(message);

                    // Send acknowledgment if required
                    if (message.RequiresAck)
                    {
                        await SendAck("server", message.SequenceNumber);
                    }
                }
                catch (Exception ex)
                {
                    Logger.Log($"TCP receive error: {ex.Message}");
                }
            }
        }

        /// <summary>
        /// Receive UDP loop
        /// </summary>
        private async Task ReceiveUdpLoop()
        {
            while (isRunning)
            {
                try
                {
                    var result = await udpClient.ReceiveAsync();

                    // Decrypt if needed
                    var data = encryptionManager.Decrypt(result.Buffer, result.Buffer.Length);

                    // Deserialize message
                    var message = DeserializeMessage(data);

                    // For server, identify sender
                    if (mode == NetworkMode.Server || mode == NetworkMode.Host)
                    {
                        var client = connectedClients.Values.FirstOrDefault(c =>
                            ((IPEndPoint)c.TcpClient.Client.RemoteEndPoint).Address.Equals(result.RemoteEndPoint.Address));

                        if (client != null)
                        {
                            message.SenderId = client.Id;
                        }
                    }

                    // Update stats
                    stats.BytesReceived += result.Buffer.Length;
                    stats.MessagesReceived++;

                    // Queue message for processing
                    incomingMessages.Enqueue(message);
                }
                catch (Exception ex)
                {
                    Logger.Log($"UDP receive error: {ex.Message}");
                }
            }
        }

        /// <summary>
        /// Process incoming messages
        /// </summary>
        private async Task ProcessIncomingMessages()
        {
            while (isRunning)
            {
                try
                {
                    if (incomingMessages.TryDequeue(out var message))
                    {
                        // Check sequence number for ordering
                        if (clientSequences.ContainsKey(message.SenderId))
                        {
                            var expectedSeq = clientSequences[message.SenderId];
                            if (message.SequenceNumber < expectedSeq)
                            {
                                // Old message, discard
                                continue;
                            }
                            clientSequences[message.SenderId] = message.SequenceNumber + 1;
                        }
                        else
                        {
                            clientSequences[message.SenderId] = message.SequenceNumber + 1;
                        }

                        // Raise event
                        OnMessageReceived?.Invoke(message);
                    }
                    else
                    {
                        await Task.Delay(1);
                    }
                }
                catch (Exception ex)
                {
                    Logger.Log($"Process incoming message error: {ex.Message}");
                }
            }
        }

        /// <summary>
        /// Process outgoing messages
        /// </summary>
        private async Task ProcessOutgoingMessages()
        {
            while (isRunning)
            {
                try
                {
                    if (outgoingMessages.TryDequeue(out var message))
                    {
                        // Apply bandwidth throttling
                        await bandwidthManager.WaitForBandwidth(GetMessageSize(message));

                        // Send the message
                        await SendMessageInternal(message);
                    }
                    else
                    {
                        await Task.Delay(1);
                    }
                }
                catch (Exception ex)
                {
                    Logger.Log($"Process outgoing message error: {ex.Message}");
                }
            }
        }

        /// <summary>
        /// Send a message
        /// </summary>
        public async Task SendMessage(NetworkMessage message)
        {
            message.Timestamp = DateTimeOffset.Now.ToUnixTimeMilliseconds();
            message.SequenceNumber = Interlocked.Increment(ref currentSequence);

            outgoingMessages.Enqueue(message);
        }

        /// <summary>
        /// Send a message to specific client (server only)
        /// </summary>
        public async Task SendToClient(string clientId, NetworkMessage message)
        {
            if (!connectedClients.TryGetValue(clientId, out var client))
                return;

            message.Timestamp = DateTimeOffset.Now.ToUnixTimeMilliseconds();
            message.SequenceNumber = Interlocked.Increment(ref currentSequence);

            await SendToClientInternal(client, message);
        }

        /// <summary>
        /// Broadcast message to all clients (server only)
        /// </summary>
        public async Task BroadcastMessage(NetworkMessage message, string excludeClient = null)
        {
            message.Timestamp = DateTimeOffset.Now.ToUnixTimeMilliseconds();
            message.SequenceNumber = Interlocked.Increment(ref currentSequence);

            var tasks = new List<Task>();

            foreach (var client in connectedClients.Values)
            {
                if (client.Id != excludeClient)
                {
                    tasks.Add(SendToClientInternal(client, message));
                }
            }

            await Task.WhenAll(tasks);
        }

        /// <summary>
        /// Internal send message implementation
        /// </summary>
        private async Task SendMessageInternal(NetworkMessage message)
        {
            try
            {
                var data = SerializeMessage(message);
                var encrypted = encryptionManager.Encrypt(data);

                // Determine if TCP or UDP based on message type
                bool useTcp = IsReliableMessage(message.Type);

                if (useTcp)
                {
                    if (mode == NetworkMode.Client || mode == NetworkMode.Host)
                    {
                        var stream = tcpClient.GetStream();
                        await stream.WriteAsync(encrypted, 0, encrypted.Length);
                    }
                }
                else
                {
                    // Use UDP for unreliable messages
                    await udpClient.SendAsync(encrypted, encrypted.Length);
                }

                // Update stats
                stats.BytesSent += encrypted.Length;
                stats.MessagesSent++;

                // Track for acknowledgment if required
                if (message.RequiresAck)
                {
                    pendingAcks[message.SequenceNumber] = message;

                    // Start retry timer
                    Task.Run(async () =>
                    {
                        await Task.Delay(5000);
                        if (pendingAcks.ContainsKey(message.SequenceNumber))
                        {
                            // Retry
                            await SendMessageInternal(message);
                        }
                    });
                }
            }
            catch (Exception ex)
            {
                Logger.Log($"Send message error: {ex.Message}");
            }
        }

        /// <summary>
        /// Send message to specific client
        /// </summary>
        private async Task SendToClientInternal(ClientConnection client, NetworkMessage message)
        {
            try
            {
                var data = SerializeMessage(message);
                var encrypted = encryptionManager.Encrypt(data);

                // Determine if TCP or UDP
                bool useTcp = IsReliableMessage(message.Type);

                if (useTcp)
                {
                    await client.Stream.WriteAsync(encrypted, 0, encrypted.Length);
                }
                else
                {
                    // Get client endpoint for UDP
                    var endpoint = (IPEndPoint)client.TcpClient.Client.RemoteEndPoint;
                    await udpClient.SendAsync(encrypted, encrypted.Length, endpoint);
                }

                // Update stats
                stats.BytesSent += encrypted.Length;
                stats.MessagesSent++;
            }
            catch (Exception ex)
            {
                Logger.Log($"Send to client error: {ex.Message}");
                DisconnectClient(client.Id);
            }
        }

        /// <summary>
        /// Heartbeat loop (server only)
        /// </summary>
        private async Task HeartbeatLoop()
        {
            while (isRunning)
            {
                try
                {
                    var now = DateTime.Now;
                    var timeoutClients = new List<string>();

                    foreach (var kvp in lastHeartbeat)
                    {
                        if ((now - kvp.Value).TotalMilliseconds > heartbeatTimeout)
                        {
                            timeoutClients.Add(kvp.Key);
                        }
                    }

                    foreach (var clientId in timeoutClients)
                    {
                        Logger.Log($"Client {clientId} timed out");
                        DisconnectClient(clientId);
                    }

                    await Task.Delay(heartbeatInterval);
                }
                catch (Exception ex)
                {
                    Logger.Log($"Heartbeat loop error: {ex.Message}");
                }
            }
        }

        /// <summary>
        /// Client heartbeat loop
        /// </summary>
        private async Task ClientHeartbeatLoop()
        {
            while (isRunning)
            {
                try
                {
                    await SendMessage(new NetworkMessage
                    {
                        Type = MessageType.Heartbeat
                    });

                    await Task.Delay(heartbeatInterval);
                }
                catch (Exception ex)
                {
                    Logger.Log($"Client heartbeat error: {ex.Message}");
                }
            }
        }

        /// <summary>
        /// Disconnect a client
        /// </summary>
        private void DisconnectClient(string clientId)
        {
            if (connectedClients.TryRemove(clientId, out var client))
            {
                client.TcpClient?.Close();
                lastHeartbeat.TryRemove(clientId, out _);

                OnClientDisconnected?.Invoke(clientId);
            }
        }

        /// <summary>
        /// Send handshake (client only)
        /// </summary>
        private async Task SendHandshake()
        {
            var handshake = new HandshakeData
            {
                Version = "1.0.0",
                PlayerName = Environment.UserName,
                ModVersion = "KenshiMP_1.0",
                Checksum = GetGameChecksum()
            };

            await SendMessage(new NetworkMessage
            {
                Type = MessageType.Handshake,
                Data = handshake,
                RequiresAck = true
            });
        }

        /// <summary>
        /// Handle handshake (server only)
        /// </summary>
        private async Task HandleHandshake(ClientConnection client, NetworkMessage message)
        {
            var handshake = (HandshakeData)message.Data;

            // Validate version
            if (handshake.Version != "1.0.0")
            {
                await KickClient(client.Id, "Version mismatch");
                return;
            }

            // Validate checksum
            if (handshake.Checksum != GetGameChecksum())
            {
                await KickClient(client.Id, "Game files mismatch");
                return;
            }

            // Accept client
            client.PlayerName = handshake.PlayerName;
            client.IsAuthenticated = true;

            OnClientConnected?.Invoke(client.Id);
        }

        /// <summary>
        /// Kick a client
        /// </summary>
        private async Task KickClient(string clientId, string reason)
        {
            await SendToClient(clientId, new NetworkMessage
            {
                Type = MessageType.Kick,
                Data = reason
            });

            DisconnectClient(clientId);
        }

        /// <summary>
        /// Send acknowledgment
        /// </summary>
        private async Task SendAck(string target, int sequenceNumber)
        {
            // Simple ack implementation
            // In production, would batch acks
        }

        /// <summary>
        /// Shutdown the network manager
        /// </summary>
        public async Task Shutdown()
        {
            isRunning = false;
            cancellationTokenSource.Cancel();

            // Disconnect all clients
            foreach (var client in connectedClients.Values)
            {
                client.TcpClient?.Close();
            }

            tcpListener?.Stop();
            tcpClient?.Close();
            udpClient?.Close();

            Logger.Log("Network manager shut down");
        }

        // Helper methods
        private byte[] SerializeMessage(NetworkMessage message)
        {
            var json = JsonSerializer.Serialize(message);
            return Encoding.UTF8.GetBytes(json);
        }

        private NetworkMessage DeserializeMessage(byte[] data)
        {
            var json = Encoding.UTF8.GetString(data);
            return JsonSerializer.Deserialize<NetworkMessage>(json);
        }

        private bool IsReliableMessage(MessageType type)
        {
            // Position updates and similar can use UDP
            switch (type)
            {
                case MessageType.PositionSync:
                case MessageType.Heartbeat:
                    return false;
                default:
                    return true;
            }
        }

        private int GetMessageSize(NetworkMessage message)
        {
            return SerializeMessage(message).Length;
        }

        private string GetGameChecksum()
        {
            // Calculate checksum of game files for validation
            return "PLACEHOLDER_CHECKSUM";
        }
    }

    /// <summary>
    /// Client connection info
    /// </summary>
    public class ClientConnection
    {
        public string Id { get; set; }
        public string PlayerName { get; set; }
        public TcpClient TcpClient { get; set; }
        public NetworkStream Stream { get; set; }
        public DateTime LastActivity { get; set; }
        public bool IsAuthenticated { get; set; }
    }

    /// <summary>
    /// Bandwidth management
    /// </summary>
    public class BandwidthManager
    {
        private readonly int maxBytesPerSecond;
        private long bytesSentThisSecond;
        private DateTime currentSecond;
        private SemaphoreSlim semaphore;

        public BandwidthManager(int maxBps)
        {
            maxBytesPerSecond = maxBps;
            currentSecond = DateTime.Now;
            semaphore = new SemaphoreSlim(1, 1);
        }

        public async Task WaitForBandwidth(int bytes)
        {
            await semaphore.WaitAsync();
            try
            {
                var now = DateTime.Now;
                if ((now - currentSecond).TotalSeconds >= 1)
                {
                    bytesSentThisSecond = 0;
                    currentSecond = now;
                }

                if (bytesSentThisSecond + bytes > maxBytesPerSecond)
                {
                    var waitTime = 1000 - (int)(now - currentSecond).TotalMilliseconds;
                    if (waitTime > 0)
                    {
                        await Task.Delay(waitTime);
                        bytesSentThisSecond = bytes;
                        currentSecond = DateTime.Now;
                    }
                }
                else
                {
                    bytesSentThisSecond += bytes;
                }
            }
            finally
            {
                semaphore.Release();
            }
        }
    }

    /// <summary>
    /// Network statistics
    /// </summary>
    public class NetworkStatistics
    {
        public long BytesSent { get; set; }
        public long BytesReceived { get; set; }
        public long MessagesSent { get; set; }
        public long MessagesReceived { get; set; }
        public double AverageLatency { get; set; }
        public int DroppedPackets { get; set; }
    }
}