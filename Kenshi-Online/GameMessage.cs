using System;
using System.Collections.Generic;
using System.Text.Json;

namespace KenshiMultiplayer
{
    public class GameMessage
    {
        // Basic message properties
        public string Type { get; set; }
        public string PlayerId { get; set; }
        public string LobbyId { get; set; }
        public Dictionary<string, object> Data { get; set; } = new Dictionary<string, object>();

        // Security and tracking properties
        public string SessionId { get; set; }  // For authentication
        public long Timestamp { get; set; } = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();
        public string MessageId { get; set; } = Guid.NewGuid().ToString();
        public int SequenceNumber { get; set; } = 0;  // For detecting missing packets

        // Batching properties
        public bool IsBatch { get; set; } = false;
        public List<GameMessage> BatchedMessages { get; set; } = new List<GameMessage>();

        // Delta compression
        public bool IsDelta { get; set; } = false;
        public string BaseMessageId { get; set; }  // Reference to the full message this delta is based on

        // Acknowledgment
        public bool RequiresAck { get; set; } = false;
        public string AckId { get; set; }  // If this is an ACK, which message it acknowledges

        // Priority (higher numbers = more important)
        public int Priority { get; set; } = 1;

        // Constructor with minimal required data
        public GameMessage(string type, string playerId)
        {
            Type = type;
            PlayerId = playerId;
        }

        // Default constructor for deserialization
        public GameMessage()
        {
        }

        // Create a new message with updated data
        public GameMessage WithData(Dictionary<string, object> data)
        {
            Data = data;
            return this;
        }

        // Add a single data item
        public GameMessage WithData(string key, object value)
        {
            Data[key] = value;
            return this;
        }

        // Set session ID for authentication
        public GameMessage WithSession(string sessionId)
        {
            SessionId = sessionId;
            return this;
        }

        // Set lobby ID
        public GameMessage WithLobby(string lobbyId)
        {
            LobbyId = lobbyId;
            return this;
        }

        // Create a new acknowledgment message
        public GameMessage CreateAck()
        {
            return new GameMessage(MessageType.Acknowledgment, PlayerId)
            {
                AckId = MessageId,
                LobbyId = LobbyId,
                SessionId = SessionId
            };
        }

        // Create a batch message containing this message and others
        public GameMessage CreateBatch(List<GameMessage> additionalMessages)
        {
            GameMessage batch = new GameMessage(MessageType.Batch, PlayerId)
            {
                IsBatch = true,
                LobbyId = LobbyId,
                SessionId = SessionId
            };

            batch.BatchedMessages.Add(this);
            batch.BatchedMessages.AddRange(additionalMessages);

            return batch;
        }

        // Convert to JSON string
        public string ToJson() => JsonSerializer.Serialize(this);

        // Create from JSON string
        public static GameMessage FromJson(string json) => JsonSerializer.Deserialize<GameMessage>(json);

        // Validation
        public bool IsValid()
        {
            // Basic validation
            if (string.IsNullOrEmpty(Type) || string.IsNullOrEmpty(PlayerId))
                return false;

            // Batch validation
            if (IsBatch && (BatchedMessages == null || BatchedMessages.Count == 0))
                return false;

            // Delta validation
            if (IsDelta && string.IsNullOrEmpty(BaseMessageId))
                return false;

            // Acknowledgment validation
            if (Type == MessageType.Acknowledgment && string.IsNullOrEmpty(AckId))
                return false;

            return true;
        }
    }
}