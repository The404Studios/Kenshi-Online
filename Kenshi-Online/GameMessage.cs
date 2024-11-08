using System.Text.Json;

namespace KenshiMultiplayer
{
    public class GameMessage
    {
        public string Type { get; set; }
        public string PlayerId { get; set; }
        public string LobbyId { get; set; } // Add LobbyId here
        public object Data { get; set; }

        public string ToJson() => JsonSerializer.Serialize(this);
        public static GameMessage FromJson(string json) => JsonSerializer.Deserialize<GameMessage>(json);
    }
}
