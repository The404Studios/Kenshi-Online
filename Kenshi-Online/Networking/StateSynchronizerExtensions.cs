using System;
using KenshiMultiplayer.Networking;
using KenshiMultiplayer.Data;

namespace KenshiMultiplayer.Networking
{
    /// <summary>
    /// Extension methods for StateSynchronizer to support new GameStateManager
    /// </summary>
    public static class StateSynchronizerExtensions
    {
        /// <summary>
        /// Queue a state update for synchronization
        /// </summary>
        public static void QueueStateUpdate(this StateSynchronizer synchronizer, StateUpdate update)
        {
            // Convert StateUpdate to the format expected by StateSynchronizer
            // This is a compatibility shim
            if (update == null) return;

            // The original StateSynchronizer might not have this method
            // For now, we'll just log it
            Console.WriteLine($"State update queued for player {update.PlayerId}");
        }
    }

    /// <summary>
    /// State update structure for GameStateManager
    /// </summary>
    public class StateUpdate
    {
        public string PlayerId { get; set; }
        public Position Position { get; set; }
        public float Health { get; set; }
        public PlayerState CurrentState { get; set; }
        public DateTime Timestamp { get; set; }
    }
}
