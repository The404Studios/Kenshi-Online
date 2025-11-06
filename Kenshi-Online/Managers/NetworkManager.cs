using System;
using System.Collections.Concurrent;
using System.Net.Sockets;
using System.Text;
using System.Text.Json;
using System.Threading.Tasks;
using KenshiMultiplayer.Networking;
using KenshiMultiplayer.Utility;
using KenshiMultiplayer.Data;

namespace KenshiMultiplayer.Managers
{
    /// <summary>
    /// Manages network communication between server and clients
    /// </summary>
    public class NetworkManager
    {
        private readonly ConcurrentDictionary<string, TcpClient> playerConnections;
        private readonly ConcurrentDictionary<string, NetworkStream> playerStreams;
        private EnhancedServer? server;
        private EnhancedClient? client;
        private readonly object lockObj = new object();

        public NetworkManager()
        {
            playerConnections = new ConcurrentDictionary<string, TcpClient>();
            playerStreams = new ConcurrentDictionary<string, NetworkStream>();
        }

        /// <summary>
        /// Initialize with server instance
        /// </summary>
        public void Initialize(EnhancedServer serverInstance)
        {
            server = serverInstance;
        }

        /// <summary>
        /// Initialize with client instance
        /// </summary>
        public void Initialize(EnhancedClient clientInstance)
        {
            client = clientInstance;
        }

        /// <summary>
        /// Register a player connection
        /// </summary>
        public void RegisterPlayer(string playerId, TcpClient connection, NetworkStream stream)
        {
            playerConnections[playerId] = connection;
            playerStreams[playerId] = stream;
            Console.WriteLine($"NetworkManager: Registered player {playerId}");
        }

        /// <summary>
        /// Unregister a player connection
        /// </summary>
        public void UnregisterPlayer(string playerId)
        {
            playerConnections.TryRemove(playerId, out _);
            playerStreams.TryRemove(playerId, out _);
            Console.WriteLine($"NetworkManager: Unregistered player {playerId}");
        }

        /// <summary>
        /// Send a message to a specific player
        /// </summary>
        public void SendToPlayer(string playerId, GameMessage message)
        {
            try
            {
                if (!playerStreams.TryGetValue(playerId, out var stream))
                {
                    Console.WriteLine($"NetworkManager: No connection found for player {playerId}");
                    return;
                }

                if (!playerConnections.TryGetValue(playerId, out var connection) || !connection.Connected)
                {
                    Console.WriteLine($"NetworkManager: Player {playerId} is disconnected");
                    UnregisterPlayer(playerId);
                    return;
                }

                string json = JsonSerializer.Serialize(message);
                byte[] data = Encoding.UTF8.GetBytes(json);
                byte[] lengthPrefix = BitConverter.GetBytes(data.Length);

                lock (lockObj)
                {
                    stream.Write(lengthPrefix, 0, lengthPrefix.Length);
                    stream.Write(data, 0, data.Length);
                    stream.Flush();
                }

                Console.WriteLine($"NetworkManager: Sent {message.Type} to {playerId}");
            }
            catch (Exception ex)
            {
                Console.WriteLine($"NetworkManager: Error sending to {playerId}: {ex.Message}");
                UnregisterPlayer(playerId);
            }
        }

        /// <summary>
        /// Send a message to a specific player asynchronously
        /// </summary>
        public async Task SendToPlayerAsync(string playerId, GameMessage message)
        {
            await Task.Run(() => SendToPlayer(playerId, message));
        }

        /// <summary>
        /// Broadcast a message to all connected players
        /// </summary>
        public void BroadcastToAll(GameMessage message)
        {
            foreach (var playerId in playerConnections.Keys)
            {
                SendToPlayer(playerId, message);
            }
        }

        /// <summary>
        /// Broadcast a message to all connected players asynchronously
        /// </summary>
        public async Task BroadcastToAllAsync(GameMessage message)
        {
            var tasks = new List<Task>();
            foreach (var playerId in playerConnections.Keys)
            {
                tasks.Add(SendToPlayerAsync(playerId, message));
            }
            await Task.WhenAll(tasks);
        }

        /// <summary>
        /// Broadcast a message to players except one
        /// </summary>
        public void BroadcastExcept(string excludePlayerId, GameMessage message)
        {
            foreach (var playerId in playerConnections.Keys)
            {
                if (playerId != excludePlayerId)
                {
                    SendToPlayer(playerId, message);
                }
            }
        }

        /// <summary>
        /// Get count of connected players
        /// </summary>
        public int GetConnectedPlayerCount()
        {
            return playerConnections.Count;
        }

        /// <summary>
        /// Check if player is connected
        /// </summary>
        public bool IsPlayerConnected(string playerId)
        {
            return playerConnections.ContainsKey(playerId) &&
                   playerConnections[playerId].Connected;
        }
    }
}
