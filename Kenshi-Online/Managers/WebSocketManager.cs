using System;
using System.Collections.Concurrent;
using System.Net.WebSockets;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using System.Collections.Generic;
using System.Text.Json;
using KenshiMultiplayer.Networking;
using KenshiMultiplayer.Utility;
using KenshiMultiplayer.Data;

namespace KenshiMultiplayer.Managers
{
    public class WebSocketManager
    {
        private readonly ConcurrentDictionary<string, WebSocket> clients = new ConcurrentDictionary<string, WebSocket>();
        private readonly ConcurrentDictionary<string, string> userIdToSocketId = new ConcurrentDictionary<string, string>();
        private readonly int bufferSize = 4096;
        private readonly CancellationTokenSource cancellationTokenSource = new CancellationTokenSource();

        public async Task HandleWebSocketAsync(WebSocket webSocket, string clientId, string userId = null)
        {
            if (clients.TryAdd(clientId, webSocket))
            {
                // Map userId to socketId if provided
                if (!string.IsNullOrEmpty(userId))
                {
                    userIdToSocketId[userId] = clientId;
                }

                try
                {
                    // Send connection confirmation
                    await SendMessageAsync(clientId, new Dictionary<string, object>
                    {
                        { "type", MessageType.WebSocketConnect },
                        { "message", "Connected to Kenshi Online WebSocket" }
                    });

                    // Handle incoming messages
                    await ReceiveMessagesAsync(webSocket, clientId);
                }
                catch (Exception ex)
                {
                    Logger.Log($"WebSocket error: {ex.Message}");
                }
                finally
                {
                    // Clean up on disconnect
                    RemoveClient(clientId);

                    // Remove userId mapping
                    if (!string.IsNullOrEmpty(userId) && userIdToSocketId.TryGetValue(userId, out var socketId) && socketId == clientId)
                    {
                        userIdToSocketId.TryRemove(userId, out _);
                    }
                }
            }
        }

        private async Task ReceiveMessagesAsync(WebSocket webSocket, string clientId)
        {
            var buffer = new byte[bufferSize];
            var receiveBuffer = new ArraySegment<byte>(buffer);

            while (webSocket.State == WebSocketState.Open && !cancellationTokenSource.IsCancellationRequested)
            {
                try
                {
                    WebSocketReceiveResult result = await webSocket.ReceiveAsync(receiveBuffer, cancellationTokenSource.Token);

                    if (result.MessageType == WebSocketMessageType.Close)
                    {
                        await webSocket.CloseAsync(WebSocketCloseStatus.NormalClosure, "Client closed connection", cancellationTokenSource.Token);
                        RemoveClient(clientId);
                        break;
                    }

                    if (result.MessageType == WebSocketMessageType.Text)
                    {
                        string message = Encoding.UTF8.GetString(buffer, 0, result.Count);

                        // Process message
                        await ProcessMessageAsync(clientId, message);
                    }
                }
                catch (WebSocketException)
                {
                    // Connection was closed abruptly
                    RemoveClient(clientId);
                    break;
                }
                catch (OperationCanceledException)
                {
                    // Cancellation was requested
                    break;
                }
                catch (Exception ex)
                {
                    Logger.Log($"Error processing WebSocket message: {ex.Message}");
                }
            }
        }

        private async Task ProcessMessageAsync(string clientId, string message)
        {
            try
            {
                var data = JsonSerializer.Deserialize<Dictionary<string, object>>(message);

                if (data.TryGetValue("type", out var typeObj))
                {
                    string messageType = typeObj.ToString();

                    // Handle heartbeat messages
                    if (messageType == "ping")
                    {
                        await SendMessageAsync(clientId, new Dictionary<string, object>
                        {
                            { "type", "pong" },
                            { "timestamp", DateTimeOffset.UtcNow.ToUnixTimeMilliseconds() }
                        });
                    }

                    // Other message types can be handled here
                    // For now we just log them
                    Logger.Log($"WebSocket message from {clientId}: {messageType}");
                }
            }
            catch (Exception ex)
            {
                Logger.Log($"Error parsing WebSocket message: {ex.Message}");
            }
        }

        public async Task SendMessageAsync(string clientId, Dictionary<string, object> message)
        {
            if (clients.TryGetValue(clientId, out var webSocket) && webSocket.State == WebSocketState.Open)
            {
                try
                {
                    string jsonMessage = JsonSerializer.Serialize(message);
                    byte[] bytes = Encoding.UTF8.GetBytes(jsonMessage);
                    var sendBuffer = new ArraySegment<byte>(bytes);

                    await webSocket.SendAsync(sendBuffer, WebSocketMessageType.Text, true, cancellationTokenSource.Token);
                }
                catch (Exception ex)
                {
                    Logger.Log($"Error sending WebSocket message: {ex.Message}");
                    RemoveClient(clientId);
                }
            }
        }

        public async Task SendMessageToUserAsync(string userId, Dictionary<string, object> message)
        {
            if (userIdToSocketId.TryGetValue(userId, out var clientId))
            {
                await SendMessageAsync(clientId, message);
            }
        }

        public async Task BroadcastMessageAsync(Dictionary<string, object> message, string excludeClientId = null)
        {
            foreach (var client in clients)
            {
                if (client.Key != excludeClientId)
                {
                    await SendMessageAsync(client.Key, message);
                }
            }
        }

        public async Task BroadcastToGroupAsync(IEnumerable<string> clientIds, Dictionary<string, object> message)
        {
            foreach (var clientId in clientIds)
            {
                await SendMessageAsync(clientId, message);
            }
        }

        public async Task BroadcastToUserGroupAsync(IEnumerable<string> userIds, Dictionary<string, object> message)
        {
            foreach (var userId in userIds)
            {
                await SendMessageToUserAsync(userId, message);
            }
        }

        private void RemoveClient(string clientId)
        {
            if (clients.TryRemove(clientId, out var webSocket))
            {
                try
                {
                    webSocket.Abort();
                    webSocket.Dispose();
                }
                catch (Exception ex)
                {
                    Logger.Log($"Error disposing WebSocket: {ex.Message}");
                }
            }
        }

        public void Shutdown()
        {
            cancellationTokenSource.Cancel();

            // Close all connections
            foreach (var client in clients)
            {
                try
                {
                    client.Value.Abort();
                    client.Value.Dispose();
                }
                catch { }
            }

            clients.Clear();
            userIdToSocketId.Clear();
        }
    }
}