using System;
using System.Collections.Generic;
using System.Collections.Concurrent;
using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Threading;
using KenshiMultiplayer.Utility;
using KenshiMultiplayer.Game;

namespace KenshiMultiplayer.Networking
{
    /// <summary>
    /// Extensions to EnhancedServer for handling raw GameBridge protocol from DLLs
    /// Runs on a separate port for efficient pipe-delimited game state sync
    /// </summary>
    public static class GameBridgeServerExtensions
    {
        private static TcpListener _bridgeListener;
        private static GameBridgeProtocolHandler _protocolHandler;
        private static Thread _bridgeThread;
        private static Thread _tickThread;
        private static bool _running = false;
        private static ConcurrentDictionary<TcpClient, Thread> _clientThreads = new();

        // Default port for GameBridge (separate from main server)
        public const int DEFAULT_BRIDGE_PORT = 5556;

        /// <summary>
        /// Start the GameBridge listener on a separate port
        /// This handles raw pipe-delimited messages from the C++ DLL
        /// </summary>
        public static void StartGameBridge(this EnhancedServer server, int port = DEFAULT_BRIDGE_PORT)
        {
            if (_running)
            {
                Logger.Log("[GameBridge] Already running");
                return;
            }

            _protocolHandler = new GameBridgeProtocolHandler();
            _running = true;

            // Start tick thread
            _tickThread = new Thread(TickLoop)
            {
                IsBackground = true,
                Name = "GameBridge-Tick"
            };
            _tickThread.Start();

            // Start listener thread
            _bridgeThread = new Thread(() => BridgeListenerLoop(port))
            {
                IsBackground = true,
                Name = "GameBridge-Listener"
            };
            _bridgeThread.Start();

            Logger.Log($"[GameBridge] Started on port {port}");
        }

        /// <summary>
        /// Stop the GameBridge listener
        /// </summary>
        public static void StopGameBridge(this EnhancedServer server)
        {
            _running = false;

            try
            {
                _bridgeListener?.Stop();
            }
            catch { }

            // Close all client connections
            foreach (var kvp in _clientThreads)
            {
                try
                {
                    kvp.Key.Close();
                }
                catch { }
            }
            _clientThreads.Clear();

            Logger.Log("[GameBridge] Stopped");
        }

        /// <summary>
        /// Get the protocol handler for direct access
        /// </summary>
        public static GameBridgeProtocolHandler GetBridgeProtocolHandler(this EnhancedServer server)
        {
            return _protocolHandler;
        }

        private static void BridgeListenerLoop(int port)
        {
            try
            {
                _bridgeListener = new TcpListener(IPAddress.Any, port);
                _bridgeListener.Start();

                while (_running)
                {
                    try
                    {
                        var client = _bridgeListener.AcceptTcpClient();

                        // Configure socket for low-latency
                        client.NoDelay = true;
                        client.ReceiveBufferSize = 8192;
                        client.SendBufferSize = 8192;

                        Logger.Log($"[GameBridge] DLL client connected from {((IPEndPoint)client.Client.RemoteEndPoint).Address}");

                        var clientThread = new Thread(() => HandleBridgeClient(client))
                        {
                            IsBackground = true,
                            Name = $"GameBridge-Client-{client.GetHashCode()}"
                        };

                        _clientThreads[client] = clientThread;
                        clientThread.Start();
                    }
                    catch (SocketException) when (!_running)
                    {
                        // Expected when stopping
                        break;
                    }
                }
            }
            catch (Exception ex)
            {
                Logger.Log($"[GameBridge] Listener error: {ex.Message}");
            }
        }

        private static void HandleBridgeClient(TcpClient client)
        {
            var stream = client.GetStream();
            var buffer = new byte[4096];
            var messageBuffer = new StringBuilder();

            try
            {
                // Send handshake
                SendRaw(client, "BRIDGE_READY|1.0\n");

                while (_running && client.Connected)
                {
                    if (!stream.DataAvailable)
                    {
                        Thread.Sleep(1);
                        continue;
                    }

                    int bytesRead = stream.Read(buffer, 0, buffer.Length);
                    if (bytesRead == 0)
                        break;

                    // Append to message buffer
                    messageBuffer.Append(Encoding.UTF8.GetString(buffer, 0, bytesRead));

                    // Process complete messages (newline delimited)
                    string bufferContent = messageBuffer.ToString();
                    int newlineIndex;

                    while ((newlineIndex = bufferContent.IndexOf('\n')) >= 0)
                    {
                        string message = bufferContent.Substring(0, newlineIndex).Trim();
                        bufferContent = bufferContent.Substring(newlineIndex + 1);

                        if (!string.IsNullOrEmpty(message))
                        {
                            ProcessBridgeMessage(message, client);
                        }
                    }

                    messageBuffer.Clear();
                    messageBuffer.Append(bufferContent);
                }
            }
            catch (Exception ex)
            {
                Logger.Log($"[GameBridge] Client error: {ex.Message}");
            }
            finally
            {
                _protocolHandler?.RemoveClient(client);
                _clientThreads.TryRemove(client, out _);

                try { client.Close(); } catch { }
                Logger.Log("[GameBridge] DLL client disconnected");
            }
        }

        private static void ProcessBridgeMessage(string message, TcpClient client)
        {
            try
            {
                // Handle connection handshake
                if (message.StartsWith("HELLO|"))
                {
                    HandleHello(message, client);
                    return;
                }

                // Handle authentication
                if (message.StartsWith("AUTH|"))
                {
                    HandleAuth(message, client);
                    return;
                }

                // Handle ping
                if (message == "PING")
                {
                    SendRaw(client, "PONG\n");
                    return;
                }

                // Route to protocol handler
                var responses = _protocolHandler?.ProcessMessage(message, client);

                // Send any direct responses back
                if (responses != null)
                {
                    foreach (var response in responses)
                    {
                        SendRaw(client, response + "\n");
                    }
                }
            }
            catch (Exception ex)
            {
                Logger.Log($"[GameBridge] Message processing error: {ex.Message}");
            }
        }

        private static void HandleHello(string message, TcpClient client)
        {
            // HELLO|version|playerName
            var parts = message.Split('|');
            if (parts.Length >= 3)
            {
                string version = parts[1];
                string playerName = parts[2];

                Logger.Log($"[GameBridge] Hello from {playerName} (DLL v{version})");

                // Send welcome with server tick
                SendRaw(client, $"WELCOME|{playerName}|{GetServerTick()}\n");
            }
        }

        private static void HandleAuth(string message, TcpClient client)
        {
            // AUTH|username|sessionToken
            var parts = message.Split('|');
            if (parts.Length >= 3)
            {
                string username = parts[1];
                string token = parts[2];

                // TODO: Validate token against main server's active sessions
                // For now, accept all authenticated connections
                _protocolHandler?.RegisterClient(client, username);

                Logger.Log($"[GameBridge] Authenticated: {username}");
                SendRaw(client, $"AUTH_OK|{username}\n");
            }
        }

        private static void SendRaw(TcpClient client, string message)
        {
            if (client?.Connected != true)
                return;

            try
            {
                byte[] data = Encoding.UTF8.GetBytes(message);
                client.GetStream().Write(data, 0, data.Length);
            }
            catch (Exception ex)
            {
                Logger.Log($"[GameBridge] Send error: {ex.Message}");
            }
        }

        private static void TickLoop()
        {
            const int tickRateMs = 50; // 20 Hz

            while (_running)
            {
                try
                {
                    _protocolHandler?.Tick();
                    Thread.Sleep(tickRateMs);
                }
                catch (Exception ex)
                {
                    Logger.Log($"[GameBridge] Tick error: {ex.Message}");
                }
            }
        }

        private static ulong GetServerTick()
        {
            return (ulong)(DateTime.UtcNow.Ticks / TimeSpan.TicksPerMillisecond);
        }
    }
}
