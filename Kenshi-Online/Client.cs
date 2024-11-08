using System;
using System.Net.Sockets;
using System.Text;
using System.Text.Json;
using System.Threading;

namespace KenshiMultiplayer
{
    public class Client
    {
        private TcpClient client;
        private NetworkStream stream;
        private float lastX, lastY;

        public void Connect()
        {
            try
            {
                client = new TcpClient("127.0.0.1", 5555);
                stream = client.GetStream();
                Console.WriteLine("Connected to server.");

                Thread readThread = new Thread(ListenForServerMessages);
                readThread.Start();

                while (true)
                {
                    string message = Console.ReadLine();
                    var gameMessage = new GameMessage { Type = MessageType.Chat, PlayerId = "Player1", Data = message };
                    SendMessageToServer(gameMessage);
                }
            }
            catch (Exception ex)
            {
                Console.WriteLine("Failed to connect to server: " + ex.Message);
            }
        }

        private void ListenForServerMessages()
        {
            byte[] buffer = new byte[1024];
            while (true)
            {
                int bytesRead = stream.Read(buffer, 0, buffer.Length);
                if (bytesRead > 0)
                {
                    string jsonMessage = Encoding.ASCII.GetString(buffer, 0, bytesRead);
                    GameMessage message = GameMessage.FromJson(jsonMessage);
                    HandleGameMessage(message);
                }
            }
        }

        private void HandleGameMessage(GameMessage message)
        {
            switch (message.Type)
            {
                case MessageType.Position:
                    var position = JsonSerializer.Deserialize<Position>(message.Data.ToString());
                    Console.WriteLine($"Player {message.PlayerId} moved to ({position.X}, {position.Y})");
                    break;

                case MessageType.Inventory:
                    var item = JsonSerializer.Deserialize<InventoryItem>(message.Data.ToString());
                    Console.WriteLine($"Player {message.PlayerId} has item: {item.ItemName} x{item.Quantity}");
                    break;

                case MessageType.Combat:
                    var combatAction = JsonSerializer.Deserialize<CombatAction>(message.Data.ToString());
                    Console.WriteLine($"Player {message.PlayerId} performs {combatAction.Action} on {combatAction.TargetId}");
                    break;

                case MessageType.Health:
                    var health = JsonSerializer.Deserialize<HealthStatus>(message.Data.ToString());
                    Console.WriteLine($"Player {message.PlayerId} health: {health.CurrentHealth}/{health.MaxHealth}");
                    break;
            }
        }

        public void UpdatePosition(float newX, float newY)
        {
            float threshold = 0.5f;
            if (Math.Abs(lastX - newX) > threshold || Math.Abs(lastY - newY) > threshold)
            {
                var position = new Position { X = newX, Y = newY };
                var message = new GameMessage
                {
                    Type = MessageType.Position,
                    PlayerId = "Player1",
                    Data = position
                };

                SendMessageToServer(message);
                lastX = newX;
                lastY = newY;
            }
        }

        public void PerformCombatAction(string targetId, string actionType)
        {
            var combatAction = new CombatAction { TargetId = targetId, Action = actionType };
            var message = new GameMessage
            {
                Type = MessageType.Combat,
                PlayerId = "Player1",
                Data = combatAction
            };

            SendMessageToServer(message);
        }

        public void UpdateInventory(string itemName, int quantity)
        {
            var inventoryItem = new InventoryItem { ItemName = itemName, Quantity = quantity };
            var message = new GameMessage
            {
                Type = MessageType.Inventory,
                PlayerId = "Player1",
                Data = inventoryItem
            };

            SendMessageToServer(message);
        }

        public void UpdateHealth(int currentHealth, int maxHealth)
        {
            var healthStatus = new HealthStatus { CurrentHealth = currentHealth, MaxHealth = maxHealth };
            var message = new GameMessage
            {
                Type = MessageType.Health,
                PlayerId = "Player1",
                Data = healthStatus
            };

            SendMessageToServer(message);
        }

        private void SendMessageToServer(GameMessage message)
        {
            string jsonMessage = message.ToJson();
            byte[] messageBuffer = Encoding.ASCII.GetBytes(jsonMessage);
            stream.Write(messageBuffer, 0, messageBuffer.Length);
        }
    }
}
