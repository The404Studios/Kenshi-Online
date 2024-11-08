using System.Text.Json;
using System.Collections.Generic;

namespace KenshiMultiplayer
{
    public class GameMessage
    {
        public string Type { get; set; }
        public string PlayerId { get; set; }
        public string LobbyId { get; set; }
        public Dictionary<string, object> Data { get; set; } // Use Dictionary for structured data

        public string ToJson() => JsonSerializer.Serialize(this);
        public static GameMessage FromJson(string json) => JsonSerializer.Deserialize<GameMessage>(json);
    }
}
