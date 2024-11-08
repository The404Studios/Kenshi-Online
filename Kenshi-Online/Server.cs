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

        public void Start()
        {
            server = new TcpListener(IPAddress.Any, 5555);
            server.Start();
            Console.WriteLine("Server started on port 5555.");

            while (true)
            {
                TcpClient client = server.AcceptTcpClient();
                connectedClients.Add(client);
                Console.WriteLine("Client connected.");

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
                            Console.WriteLine($"Received {message.Type} from {message.PlayerId}");
                            BroadcastMessage(jsonMessage, client);
                        }
                        else
                        {
                            Console.WriteLine($"Invalid message from {message.PlayerId}: {message.Type}");
                        }
                    }
                }
            }
            catch (Exception ex)
            {
                Console.WriteLine("Client disconnected: " + ex.Message);
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

        // Simple validation logic
        private bool ValidateMessage(GameMessage message)
        {
            switch (message.Type)
            {
                case MessageType.Position:
                    return ValidatePositionMessage(message);
                case MessageType.Inventory:
                    return ValidateInventoryMessage(message);
                case MessageType.Combat:
                    return ValidateCombatMessage(message);
                case MessageType.Health:
                    return ValidateHealthMessage(message);
                default:
                    return false;
            }
        }

        private bool ValidatePositionMessage(GameMessage message)
        {
            // Add position-specific validation
            return true;
        }

        private bool ValidateInventoryMessage(GameMessage message)
        {
            // Add inventory-specific validation
            return true;
        }

        private bool ValidateCombatMessage(GameMessage message)
        {
            // Add combat-specific validation
            return true;
        }

        private bool ValidateHealthMessage(GameMessage message)
        {
            // Add health-specific validation
            return true;
        }
    }
}
