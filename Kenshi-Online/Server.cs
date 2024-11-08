using System;
using System.Collections.Generic;
using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Text.Json;
using System.Threading;

namespace KenshiMultiplayer
{
    public class Server
    {
        private TcpListener server;
        private List<TcpClient> connectedClients = new List<TcpClient>();
        private Dictionary<string, Lobby> lobbies = new Dictionary<string, Lobby>();

        public void Start()
        {
            server = new TcpListener(IPAddress.Any, 5555);
            server.Start();
            Logger.Log("Server started on port 5555.");

            while (true)
            {
                TcpClient client = server.AcceptTcpClient();
                connectedClients.Add(client);
                Logger.Log("Client connected.");

                Thread clientThread = new Thread(() => HandleClient(client));
                clientThread.Start();
            }
        }

        private void HandleClient(TcpClient client)
        {
            NetworkStream stream = client.GetStream();
            byte[] buffer = new byte[1024];

            try
            {
                while (true)
                {
                    int bytesRead = stream.Read(buffer, 0, buffer.Length);
                    if (bytesRead > 0)
                    {
                        string encryptedMessage = Encoding.ASCII.GetString(buffer, 0, bytesRead);
                        string jsonMessage = EncryptionHelper.Decrypt(encryptedMessage);

                        GameMessage message = GameMessage.FromJson(jsonMessage);

                        // Handle various message types
                        switch (message.Type)
                        {
                            case MessageType.Chat:
                                HandleChatMessage(message, client);
                                break;
                            case MessageType.Reconnect:
                                HandleReconnect(message, client);
                                break;
                            case MessageType.AdminKick:
                                HandleAdminKick(message);
                                break;
                            default:
                                if (ValidateMessage(message))
                                {
                                    Logger.Log($"Received {message.Type} from {message.PlayerId}");
                                    BroadcastMessage(jsonMessage, client);
                                }
                                else
                                {
                                    Logger.Log($"Invalid message from {message.PlayerId}: {message.Type}");
                                }
                                break;
                        }
                    }
                }
            }
            catch (Exception ex)
            {
                Logger.Log($"Client disconnected: {ex.Message}");
            }
            finally
            {
                connectedClients.Remove(client);
                client.Close();
            }
        }

        private void BroadcastMessage(string jsonMessage, TcpClient senderClient)
        {
            string encryptedMessage = EncryptionHelper.Encrypt(jsonMessage);
            byte[] messageBuffer = Encoding.ASCII.GetBytes(encryptedMessage);

            foreach (var client in connectedClients)
            {
                if (client != senderClient && client.Connected)
                {
                    NetworkStream stream = client.GetStream();
                    stream.Write(messageBuffer, 0, messageBuffer.Length);
                }
            }
        }

        private bool ValidateMessage(GameMessage message)
        {
            switch (message.Type)
            {
                case MessageType.Position:
                case MessageType.Inventory:
                case MessageType.Combat:
                case MessageType.Health:
                    return true;
                default:
                    return false;
            }
        }

        private void HandleChatMessage(GameMessage message, TcpClient senderClient)
        {
            if (lobbies.TryGetValue(message.LobbyId, out var lobby))
            {
                lobby.BroadcastChatMessage($"[{message.PlayerId}]: {message.Data}", senderClient);
            }
        }

        private void HandleReconnect(GameMessage message, TcpClient client)
        {
            if (lobbies.TryGetValue(message.LobbyId, out var lobby))
            {
                lobby.ReconnectPlayer(message.PlayerId, client);
                Logger.Log($"Player {message.PlayerId} reconnected to lobby {message.LobbyId}");
            }
        }

        private void HandleAdminKick(GameMessage message)
        {
            if (lobbies.TryGetValue(message.LobbyId, out var lobby))
            {
                lobby.KickPlayer(message.PlayerId);
                Logger.Log($"Player {message.PlayerId} was kicked from lobby {message.LobbyId}");
            }
        }

        public void CreateLobby(string lobbyId, bool isPrivate, string password, int maxPlayers)
        {
            if (!lobbies.ContainsKey(lobbyId))
            {
                lobbies[lobbyId] = new Lobby(lobbyId, isPrivate, password, maxPlayers);
                Logger.Log($"Lobby {lobbyId} created.");
            }
        }

        public bool JoinLobby(string lobbyId, TcpClient client, string password = "")
        {
            if (lobbies.ContainsKey(lobbyId) && lobbies[lobbyId].CanJoin(password))
            {
                lobbies[lobbyId].AddPlayer(client);
                Logger.Log($"Client joined lobby {lobbyId}.");
                return true;
            }
            return false;
        }
    }
}
