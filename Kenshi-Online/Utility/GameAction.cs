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
        public string Type { get; set; }
        public string PlayerId { get; set; }
        public string Data { get; set; }
        public long Timestamp { get; set; }
        public int Priority { get; set; }
    }
}
