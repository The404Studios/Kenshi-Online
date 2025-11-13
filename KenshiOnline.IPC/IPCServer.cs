using System;
using System.IO;
using System.IO.Pipes;
using System.Threading;
using System.Threading.Tasks;
using System.Collections.Concurrent;
using System.Text;
using System.Text.Json;

namespace KenshiOnline.IPC
{
    /// <summary>
    /// Named Pipe IPC Server for communicating with Re_Kenshi C++ plugin
    /// </summary>
    public class IPCServer : IDisposable
    {
        private const string PipeName = "ReKenshi_IPC";
        private const int MaxClients = 10;

        private readonly CancellationTokenSource _cancellationSource;
        private readonly ConcurrentBag<ClientConnection> _clients;
        private readonly IMessageHandler _messageHandler;
        private Task _serverTask;

        public event EventHandler<MessageReceivedEventArgs> MessageReceived;
        public event EventHandler<ClientEventArgs> ClientConnected;
        public event EventHandler<ClientEventArgs> ClientDisconnected;

        public int ConnectedClients => _clients.Count;
        public bool IsRunning { get; private set; }

        public IPCServer(IMessageHandler messageHandler = null)
        {
            _cancellationSource = new CancellationTokenSource();
            _clients = new ConcurrentBag<ClientConnection>();
            _messageHandler = messageHandler ?? new DefaultMessageHandler();
        }

        /// <summary>
        /// Start the IPC server
        /// </summary>
        public void Start()
        {
            if (IsRunning)
                return;

            IsRunning = true;
            _serverTask = Task.Run(() => ServerLoop(_cancellationSource.Token));
            Console.WriteLine($"[IPC] Server started on pipe: {PipeName}");
        }

        /// <summary>
        /// Stop the IPC server
        /// </summary>
        public async Task StopAsync()
        {
            if (!IsRunning)
                return;

            IsRunning = false;
            _cancellationSource.Cancel();

            // Disconnect all clients
            foreach (var client in _clients)
            {
                client.Dispose();
            }

            if (_serverTask != null)
            {
                await _serverTask;
            }

            Console.WriteLine("[IPC] Server stopped");
        }

        /// <summary>
        /// Broadcast message to all connected clients
        /// </summary>
        public async Task BroadcastAsync(IPCMessage message)
        {
            foreach (var client in _clients)
            {
                await client.SendAsync(message);
            }
        }

        /// <summary>
        /// Send message to specific client
        /// </summary>
        public async Task SendToClientAsync(string clientId, IPCMessage message)
        {
            var client = FindClient(clientId);
            if (client != null)
            {
                await client.SendAsync(message);
            }
        }

        private ClientConnection FindClient(string clientId)
        {
            foreach (var client in _clients)
            {
                if (client.Id == clientId)
                    return client;
            }
            return null;
        }

        private async Task ServerLoop(CancellationToken cancellationToken)
        {
            while (!cancellationToken.IsCancellationRequested)
            {
                try
                {
                    // Create new pipe server
                    var pipeServer = new NamedPipeServerStream(
                        PipeName,
                        PipeDirection.InOut,
                        MaxClients,
                        PipeTransmissionMode.Message,
                        PipeOptions.Asynchronous
                    );

                    // Wait for client connection
                    await pipeServer.WaitForConnectionAsync(cancellationToken);

                    // Create client connection
                    var client = new ClientConnection(pipeServer, this);
                    _clients.Add(client);

                    // Start handling client
                    _ = Task.Run(() => HandleClient(client, cancellationToken), cancellationToken);

                    ClientConnected?.Invoke(this, new ClientEventArgs(client.Id));
                    Console.WriteLine($"[IPC] Client connected: {client.Id}");
                }
                catch (OperationCanceledException)
                {
                    break;
                }
                catch (Exception ex)
                {
                    Console.WriteLine($"[IPC] Server error: {ex.Message}");
                    await Task.Delay(1000, cancellationToken);
                }
            }
        }

        private async Task HandleClient(ClientConnection client, CancellationToken cancellationToken)
        {
            try
            {
                while (!cancellationToken.IsCancellationRequested && client.IsConnected)
                {
                    var message = await client.ReceiveAsync(cancellationToken);
                    if (message == null)
                        break;

                    // Process message
                    await ProcessMessage(client, message);
                }
            }
            catch (Exception ex)
            {
                Console.WriteLine($"[IPC] Client error: {ex.Message}");
            }
            finally
            {
                // Remove client
                var newBag = new ConcurrentBag<ClientConnection>();
                foreach (var c in _clients)
                {
                    if (c != client)
                        newBag.Add(c);
                }
                _clients.Clear();
                foreach (var c in newBag)
                    _clients.Add(c);

                client.Dispose();
                ClientDisconnected?.Invoke(this, new ClientEventArgs(client.Id));
                Console.WriteLine($"[IPC] Client disconnected: {client.Id}");
            }
        }

        private async Task ProcessMessage(ClientConnection client, IPCMessage message)
        {
            // Raise event
            MessageReceived?.Invoke(this, new MessageReceivedEventArgs(client.Id, message));

            // Handle message
            var response = await _messageHandler.HandleMessageAsync(client.Id, message);

            // Send response if any
            if (response != null)
            {
                await client.SendAsync(response);
            }
        }

        public void Dispose()
        {
            StopAsync().Wait();
            _cancellationSource?.Dispose();
        }
    }

    /// <summary>
    /// Represents a connected client
    /// </summary>
    public class ClientConnection : IDisposable
    {
        private readonly NamedPipeServerStream _pipe;
        private readonly IPCServer _server;
        private readonly SemaphoreSlim _writeLock;

        public string Id { get; }
        public bool IsConnected => _pipe?.IsConnected ?? false;

        public ClientConnection(NamedPipeServerStream pipe, IPCServer server)
        {
            _pipe = pipe;
            _server = server;
            _writeLock = new SemaphoreSlim(1, 1);
            Id = Guid.NewGuid().ToString();
        }

        public async Task<IPCMessage> ReceiveAsync(CancellationToken cancellationToken = default)
        {
            try
            {
                // Read header (16 bytes)
                var headerBuffer = new byte[16];
                int bytesRead = await _pipe.ReadAsync(headerBuffer, 0, 16, cancellationToken);

                if (bytesRead != 16)
                    return null;

                // Parse header
                uint length = BitConverter.ToUInt32(headerBuffer, 0);
                uint type = BitConverter.ToUInt32(headerBuffer, 4);
                uint sequence = BitConverter.ToUInt32(headerBuffer, 8);
                ulong timestamp = BitConverter.ToUInt64(headerBuffer, 12);

                // Read payload
                byte[] payload = null;
                if (length > 0)
                {
                    payload = new byte[length];
                    bytesRead = await _pipe.ReadAsync(payload, 0, (int)length, cancellationToken);

                    if (bytesRead != length)
                        return null;
                }

                return new IPCMessage
                {
                    Type = (MessageType)type,
                    Sequence = sequence,
                    Timestamp = timestamp,
                    Payload = payload != null ? Encoding.UTF8.GetString(payload) : string.Empty
                };
            }
            catch (Exception ex)
            {
                Console.WriteLine($"[IPC] Receive error: {ex.Message}");
                return null;
            }
        }

        public async Task SendAsync(IPCMessage message)
        {
            await _writeLock.WaitAsync();
            try
            {
                // Build message
                var payloadBytes = string.IsNullOrEmpty(message.Payload)
                    ? Array.Empty<byte>()
                    : Encoding.UTF8.GetBytes(message.Payload);

                // Write header
                var header = new byte[16];
                BitConverter.GetBytes((uint)payloadBytes.Length).CopyTo(header, 0);
                BitConverter.GetBytes((uint)message.Type).CopyTo(header, 4);
                BitConverter.GetBytes(message.Sequence).CopyTo(header, 8);
                BitConverter.GetBytes(message.Timestamp).CopyTo(header, 12);

                await _pipe.WriteAsync(header, 0, 16);

                // Write payload
                if (payloadBytes.Length > 0)
                {
                    await _pipe.WriteAsync(payloadBytes, 0, payloadBytes.Length);
                }

                await _pipe.FlushAsync();
            }
            finally
            {
                _writeLock.Release();
            }
        }

        public void Dispose()
        {
            _writeLock?.Dispose();
            _pipe?.Dispose();
        }
    }

    // Event args
    public class MessageReceivedEventArgs : EventArgs
    {
        public string ClientId { get; }
        public IPCMessage Message { get; }

        public MessageReceivedEventArgs(string clientId, IPCMessage message)
        {
            ClientId = clientId;
            Message = message;
        }
    }

    public class ClientEventArgs : EventArgs
    {
        public string ClientId { get; }

        public ClientEventArgs(string clientId)
        {
            ClientId = clientId;
        }
    }
}
