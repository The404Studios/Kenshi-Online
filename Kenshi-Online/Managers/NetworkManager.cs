using System;
using System.Collections.Concurrent;
using System.Threading.Tasks;
using KenshiMultiplayer.Networking;
using KenshiMultiplayer.Utility;
using KenshiMultiplayer.Data;

namespace KenshiMultiplayer.Managers
{
    /// <summary>
    /// Manages network communication between server and clients.
    /// This class provides a unified interface for sending messages to players.
    /// </summary>
    public class NetworkManager
    {
        private readonly ConcurrentDictionary<string, Action<GameMessage>> playerMessageHandlers;
        private Action<string, GameMessage> broadcastHandler;
        private Action<string, GameMessage> sendToPlayerHandler;

        public NetworkManager()
        {
            playerMessageHandlers = new ConcurrentDictionary<string, Action<GameMessage>>();
        }

        /// <summary>
        /// Configure the handler for sending messages to specific players
        /// </summary>
        public void SetSendHandler(Action<string, GameMessage> handler)
        {
            sendToPlayerHandler = handler;
        }

        /// <summary>
        /// Configure the handler for broadcasting messages to all players
        /// </summary>
        public void SetBroadcastHandler(Action<string, GameMessage> handler)
        {
            broadcastHandler = handler;
        }

        /// <summary>
        /// Register a message handler for a specific player
        /// </summary>
        public void RegisterPlayerHandler(string playerId, Action<GameMessage> handler)
        {
            playerMessageHandlers[playerId] = handler;
        }

        /// <summary>
        /// Unregister a player's message handler
        /// </summary>
        public void UnregisterPlayerHandler(string playerId)
        {
            playerMessageHandlers.TryRemove(playerId, out _);
        }

        /// <summary>
        /// Send a message to a specific player
        /// </summary>
        public void SendToPlayer(string playerId, GameMessage message)
        {
            if (sendToPlayerHandler != null)
            {
                sendToPlayerHandler(playerId, message);
                return;
            }

            // Fallback to registered handler if available
            if (playerMessageHandlers.TryGetValue(playerId, out var handler))
            {
                handler(message);
                return;
            }

            Logger.Log($"NetworkManager: No handler for player {playerId}, message type: {message.Type}");
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
        public void Broadcast(GameMessage message, string excludePlayerId = null)
        {
            if (broadcastHandler != null)
            {
                broadcastHandler(excludePlayerId, message);
                return;
            }

            // Fallback to sending to all registered handlers
            foreach (var kvp in playerMessageHandlers)
            {
                if (kvp.Key != excludePlayerId)
                {
                    kvp.Value(message);
                }
            }
        }

        /// <summary>
        /// Broadcast a message to all connected players asynchronously
        /// </summary>
        public async Task BroadcastAsync(GameMessage message, string excludePlayerId = null)
        {
            await Task.Run(() => Broadcast(message, excludePlayerId));
        }
    }
}
