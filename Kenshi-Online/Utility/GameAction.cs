using System;
using KenshiMultiplayer.Networking;
using KenshiMultiplayer.Managers;
using KenshiMultiplayer.Data;

namespace KenshiMultiplayer.Utility
{
    /// <summary>
    /// Represents a game action to be executed
    /// </summary>
    public class GameAction
    {
        public string Type { get; set; } = string.Empty;
        public string PlayerId { get; set; } = string.Empty;
        public string Data { get; set; } = string.Empty;
        public long Timestamp { get; set; }
        public int Priority { get; set; }
    }
}
