using System;
using System.Collections.Generic;
using Kenshi_Online.Utility;
using Kenshi_Online.Data;
using Kenshi_Online.Networking;

namespace Kenshi_Online.Networking
{
    /// <summary>
    /// Extensions to EnhancedClient for spawn functionality
    /// </summary>
    public static class ClientExtensions
    {
        /// <summary>
        /// Send spawn request to server
        /// </summary>
        public static void SendSpawnRequest(this EnhancedClient client, string locationName)
        {
            try
            {
                var message = new GameMessage
                {
                    Type = MessageType.SpawnRequest,
                    PlayerId = client.GetPlayerId(),
                    SessionId = client.GetSessionId(),
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
            try
            {
                var message = new GameMessage
                {
                    Type = MessageType.GroupSpawnRequest,
                    PlayerId = client.GetPlayerId(),
                    SessionId = client.GetSessionId(),
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
            try
            {
                var message = new GameMessage
                {
                    Type = MessageType.GroupSpawnReady,
                    PlayerId = client.GetPlayerId(),
                    SessionId = client.GetSessionId(),
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
            try
            {
                var message = new GameMessage
                {
                    Type = MessageType.MoveCommand,
                    PlayerId = client.GetPlayerId(),
                    SessionId = client.GetSessionId(),
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
            try
            {
                var message = new GameMessage
                {
                    Type = MessageType.AttackCommand,
                    PlayerId = client.GetPlayerId(),
                    SessionId = client.GetSessionId(),
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
        /// Helper to get player ID from client
        /// </summary>
        private static string GetPlayerId(this EnhancedClient client)
        {
            // Use reflection to access private field if needed
            var field = client.GetType().GetField("playerId", System.Reflection.BindingFlags.NonPublic | System.Reflection.BindingFlags.Instance);
            return field?.GetValue(client)?.ToString() ?? "unknown";
        }

        /// <summary>
        /// Helper to get session ID from client
        /// </summary>
        private static string GetSessionId(this EnhancedClient client)
        {
            var field = client.GetType().GetField("sessionId", System.Reflection.BindingFlags.NonPublic | System.Reflection.BindingFlags.Instance);
            return field?.GetValue(client)?.ToString() ?? "";
        }

        /// <summary>
        /// Helper to send message
        /// </summary>
        private static void SendMessage(this EnhancedClient client, GameMessage message)
        {
            var method = client.GetType().GetMethod("SendMessage", System.Reflection.BindingFlags.NonPublic | System.Reflection.BindingFlags.Instance);
            method?.Invoke(client, new object[] { message });
        }
    }
}
