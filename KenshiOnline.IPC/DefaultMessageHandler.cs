using System;
using System.Text.Json;
using System.Threading.Tasks;

namespace KenshiOnline.IPC
{
    /// <summary>
    /// Default message handler - echoes back for testing
    /// Override this with your own handler that integrates with the multiplayer backend
    /// </summary>
    public class DefaultMessageHandler : IMessageHandler
    {
        public virtual Task<IPCMessage> HandleMessageAsync(string clientId, IPCMessage message)
        {
            Console.WriteLine($"[IPC] Handling message from {clientId}: {message.Type}");

            switch (message.Type)
            {
                case MessageType.AUTHENTICATE_REQUEST:
                    return Task.FromResult(HandleAuthRequest(message));

                case MessageType.SERVER_LIST_REQUEST:
                    return Task.FromResult(HandleServerListRequest(message));

                case MessageType.CONNECT_SERVER_REQUEST:
                    return Task.FromResult(HandleConnectRequest(message));

                case MessageType.DISCONNECT_REQUEST:
                    return Task.FromResult(HandleDisconnectRequest(message));

                case MessageType.CHAT_MESSAGE:
                    return Task.FromResult(HandleChatMessage(message));

                default:
                    Console.WriteLine($"[IPC] Unknown message type: {message.Type}");
                    return Task.FromResult<IPCMessage>(null);
            }
        }

        protected virtual IPCMessage HandleAuthRequest(IPCMessage request)
        {
            try
            {
                var data = JsonSerializer.Deserialize<AuthRequestData>(request.Payload);

                // TODO: Integrate with actual authentication system
                // For now, accept any login
                var response = new
                {
                    success = true,
                    token = Guid.NewGuid().ToString(),
                    error = ""
                };

                return new IPCMessage(MessageType.AUTH_RESPONSE, JsonSerializer.Serialize(response));
            }
            catch (Exception ex)
            {
                var response = new
                {
                    success = false,
                    token = "",
                    error = ex.Message
                };

                return new IPCMessage(MessageType.AUTH_RESPONSE, JsonSerializer.Serialize(response));
            }
        }

        protected virtual IPCMessage HandleServerListRequest(IPCMessage request)
        {
            // TODO: Integrate with actual server browser
            var response = new
            {
                servers = new[]
                {
                    new { id = "server1", name = "Test Server 1", playerCount = 2, maxPlayers = 10, ping = 25 },
                    new { id = "server2", name = "Test Server 2", playerCount = 5, maxPlayers = 8, ping = 50 },
                    new { id = "server3", name = "Local Server", playerCount = 1, maxPlayers = 4, ping = 5 },
                }
            };

            return new IPCMessage(MessageType.SERVER_LIST_RESPONSE, JsonSerializer.Serialize(response));
        }

        protected virtual IPCMessage HandleConnectRequest(IPCMessage request)
        {
            try
            {
                var data = JsonSerializer.Deserialize<ConnectRequestData>(request.Payload);

                // TODO: Integrate with actual multiplayer connection system
                var response = new
                {
                    status = (int)ConnectionStatus.CONNECTED,
                    message = $"Connected to server {data.serverId}"
                };

                return new IPCMessage(MessageType.CONNECTION_STATUS, JsonSerializer.Serialize(response));
            }
            catch (Exception ex)
            {
                var response = new
                {
                    status = (int)ConnectionStatus.ERROR_TIMEOUT,
                    message = ex.Message
                };

                return new IPCMessage(MessageType.CONNECTION_STATUS, JsonSerializer.Serialize(response));
            }
        }

        protected virtual IPCMessage HandleDisconnectRequest(IPCMessage request)
        {
            // TODO: Integrate with actual multiplayer system
            var response = new
            {
                status = (int)ConnectionStatus.DISCONNECTED,
                message = "Disconnected"
            };

            return new IPCMessage(MessageType.CONNECTION_STATUS, JsonSerializer.Serialize(response));
        }

        protected virtual IPCMessage HandleChatMessage(IPCMessage request)
        {
            // Echo back as broadcast
            return new IPCMessage(MessageType.CHAT_MESSAGE_BROADCAST, request.Payload);
        }
    }

    // Request data structures
    internal class AuthRequestData
    {
        public string username { get; set; }
        public string password { get; set; }
    }

    internal class ConnectRequestData
    {
        public string serverId { get; set; }
    }
}
