using System;
using System.Collections.Generic;
using KenshiMultiplayer.Utility;
using KenshiMultiplayer.Data;
using KenshiMultiplayer.Networking;

namespace KenshiMultiplayer.Networking
{
    /// <summary>
    /// Extensions to EnhancedClient for spawn functionality and game commands
    /// </summary>
    public static class ClientExtensions
    {
        /// <summary>
        /// Send spawn request to server
        /// </summary>
        public static void SendSpawnRequest(this EnhancedClient client, string locationName)
        {
            if (!client.IsConnected)
            {
                Console.WriteLine("ERROR: Not connected to server");
                return;
            }

            try
            {
                var message = new GameMessage
                {
                    Type = MessageType.SpawnRequest,
                    PlayerId = client.PlayerId,
                    SessionId = client.SessionId,
                    Timestamp = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds(),
                    Data = new Dictionary<string, object>
                    {
                        { "location", locationName }
                    }
                };

                client.SendMessage(message);
            }
            catch (Exception ex)
            {
                Console.WriteLine($"ERROR sending spawn request: {ex.Message}");
            }
        }

        /// <summary>
        /// Send group spawn request to server
        /// </summary>
        public static void SendGroupSpawnRequest(this EnhancedClient client, List<string> playerIds, string locationName)
        {
            if (!client.IsConnected)
            {
                Console.WriteLine("ERROR: Not connected to server");
                return;
            }

            try
            {
                var message = new GameMessage
                {
                    Type = MessageType.GroupSpawnRequest,
                    PlayerId = client.PlayerId,
                    SessionId = client.SessionId,
                    Timestamp = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds(),
                    Data = new Dictionary<string, object>
                    {
                        { "playerIds", playerIds },
                        { "location", locationName }
                    }
                };

                client.SendMessage(message);
            }
            catch (Exception ex)
            {
                Console.WriteLine($"ERROR sending group spawn request: {ex.Message}");
            }
        }

        /// <summary>
        /// Signal ready for group spawn
        /// </summary>
        public static void SendGroupSpawnReady(this EnhancedClient client, string groupId)
        {
            if (!client.IsConnected)
            {
                Console.WriteLine("ERROR: Not connected to server");
                return;
            }

            try
            {
                var message = new GameMessage
                {
                    Type = MessageType.GroupSpawnReady,
                    PlayerId = client.PlayerId,
                    SessionId = client.SessionId,
                    Timestamp = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds(),
                    Data = new Dictionary<string, object>
                    {
                        { "groupId", groupId }
                    }
                };

                client.SendMessage(message);
            }
            catch (Exception ex)
            {
                Console.WriteLine($"ERROR sending group spawn ready: {ex.Message}");
            }
        }

        /// <summary>
        /// Send player movement command
        /// </summary>
        public static void SendMoveCommand(this EnhancedClient client, float x, float y, float z)
        {
            if (!client.IsConnected)
            {
                Console.WriteLine("ERROR: Not connected to server");
                return;
            }

            try
            {
                var message = new GameMessage
                {
                    Type = MessageType.MoveCommand,
                    PlayerId = client.PlayerId,
                    SessionId = client.SessionId,
                    Timestamp = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds(),
                    Data = new Dictionary<string, object>
                    {
                        { "x", x },
                        { "y", y },
                        { "z", z }
                    }
                };

                client.SendMessage(message);
            }
            catch (Exception ex)
            {
                Console.WriteLine($"ERROR sending move command: {ex.Message}");
            }
        }

        /// <summary>
        /// Send attack command
        /// </summary>
        public static void SendAttackCommand(this EnhancedClient client, string targetId)
        {
            if (!client.IsConnected)
            {
                Console.WriteLine("ERROR: Not connected to server");
                return;
            }

            try
            {
                var message = new GameMessage
                {
                    Type = MessageType.AttackCommand,
                    PlayerId = client.PlayerId,
                    SessionId = client.SessionId,
                    Timestamp = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds(),
                    Data = new Dictionary<string, object>
                    {
                        { "targetId", targetId }
                    }
                };

                client.SendMessage(message);
            }
            catch (Exception ex)
            {
                Console.WriteLine($"ERROR sending attack command: {ex.Message}");
            }
        }

        /// <summary>
        /// Send chat message
        /// </summary>
        public static void SendChatMessage(this EnhancedClient client, string chatMessage)
        {
            if (!client.IsConnected)
            {
                Console.WriteLine("ERROR: Not connected to server");
                return;
            }

            try
            {
                var message = new GameMessage
                {
                    Type = MessageType.ChatMessage,
                    PlayerId = client.PlayerId,
                    SessionId = client.SessionId,
                    Timestamp = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds(),
                    Data = new Dictionary<string, object>
                    {
                        { "message", chatMessage },
                        { "sender", client.CurrentUsername }
                    }
                };

                client.SendMessage(message);
            }
            catch (Exception ex)
            {
                Console.WriteLine($"ERROR sending chat message: {ex.Message}");
            }
        }

        /// <summary>
        /// Send follow command
        /// </summary>
        public static void SendFollowCommand(this EnhancedClient client, string targetPlayerId)
        {
            if (!client.IsConnected)
            {
                Console.WriteLine("ERROR: Not connected to server");
                return;
            }

            try
            {
                var message = new GameMessage
                {
                    Type = MessageType.FollowCommand,
                    PlayerId = client.PlayerId,
                    SessionId = client.SessionId,
                    Timestamp = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds(),
                    Data = new Dictionary<string, object>
                    {
                        { "targetId", targetPlayerId }
                    }
                };

                client.SendMessage(message);
            }
            catch (Exception ex)
            {
                Console.WriteLine($"ERROR sending follow command: {ex.Message}");
            }
        }

        /// <summary>
        /// Send pickup item command
        /// </summary>
        public static void SendPickupCommand(this EnhancedClient client, string itemId)
        {
            if (!client.IsConnected)
            {
                Console.WriteLine("ERROR: Not connected to server");
                return;
            }

            try
            {
                var message = new GameMessage
                {
                    Type = MessageType.PickupCommand,
                    PlayerId = client.PlayerId,
                    SessionId = client.SessionId,
                    Timestamp = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds(),
                    Data = new Dictionary<string, object>
                    {
                        { "itemId", itemId }
                    }
                };

                client.SendMessage(message);
            }
            catch (Exception ex)
            {
                Console.WriteLine($"ERROR sending pickup command: {ex.Message}");
            }
        }
    }
}
