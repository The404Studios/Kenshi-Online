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
                        string jsonMessage = Encoding.ASCII.GetString(buffer, 0, bytesRead);
                        GameMessage message = GameMessage.FromJson(jsonMessage);

                        // Validate message before broadcasting
                        if (ValidateMessage(message))
                        {
                            Logger.Log($"Received {message.Type} from {message.PlayerId}");
                            BroadcastMessage(jsonMessage, client);
                        }
                        else
                        {
                            Logger.Log($"Invalid message from {message.PlayerId}: {message.Type}");
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
            byte[] messageBuffer = Encoding.ASCII.GetBytes(jsonMessage);

            foreach (var client in connectedClients)
            {
                if (client != senderClient && client.Connected)
                {
                    NetworkStream stream = client.GetStream();
                    stream.Write(messageBuffer, 0, messageBuffer.Length);
                }
            }
        }

        // Validate message types for legitimacy
        private bool ValidateMessage(GameMessage message)
        {
            bool isValid = false;

            switch (message.Type)
            {
                case MessageType.Position:
                    isValid = ValidatePositionMessage(message);
                    break;
                case MessageType.Inventory:
                    isValid = ValidateInventoryMessage(message);
                    break;
                case MessageType.Combat:
                    isValid = ValidateCombatMessage(message);
                    break;
                case MessageType.Health:
                    isValid = ValidateHealthMessage(message);
                    break;
            }

            if (!isValid)
            {
                Logger.Log($"Invalid message from {message.PlayerId}: {message.Type}");
            }

            return isValid;
        }

        private bool ValidatePositionMessage(GameMessage message) => true;
        private bool ValidateInventoryMessage(GameMessage message) => true;
        private bool ValidateCombatMessage(GameMessage message) => true;
        private bool ValidateHealthMessage(GameMessage message) => true;

        public void CreateLobby(string lobbyId)
        {
            if (!lobbies.ContainsKey(lobbyId))
            {
                lobbies[lobbyId] = new Lobby(lobbyId);
                Logger.Log($"Lobby {lobbyId} created.");
            }
        }

        public void JoinLobby(string lobbyId, TcpClient client)
        {
            if (lobbies.ContainsKey(lobbyId))
            {
                lobbies[lobbyId].AddPlayer(client);
                Logger.Log($"Client joined lobby {lobbyId}.");
            }
        }
    }
}
