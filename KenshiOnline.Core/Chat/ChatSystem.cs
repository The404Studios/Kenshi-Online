using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Linq;

namespace KenshiOnline.Core.Chat
{
    /// <summary>
    /// Chat channel types
    /// </summary>
    public enum ChatChannel
    {
        Global,     // Everyone can see
        Squad,      // Only squad members
        Proximity,  // Players nearby
        Whisper,    // Private message
        System      // System messages
    }

    /// <summary>
    /// Chat message
    /// </summary>
    public class ChatMessage
    {
        public Guid MessageId { get; set; }
        public string SenderId { get; set; }
        public string SenderName { get; set; }
        public ChatChannel Channel { get; set; }
        public string Content { get; set; }
        public string TargetId { get; set; } // For whispers
        public string SquadId { get; set; } // For squad chat
        public DateTime Timestamp { get; set; }
        public Dictionary<string, object> Metadata { get; set; }

        public ChatMessage()
        {
            MessageId = Guid.NewGuid();
            Timestamp = DateTime.UtcNow;
            Metadata = new Dictionary<string, object>();
        }

        public Dictionary<string, object> Serialize()
        {
            return new Dictionary<string, object>
            {
                ["messageId"] = MessageId.ToString(),
                ["senderId"] = SenderId ?? "",
                ["senderName"] = SenderName ?? "",
                ["channel"] = Channel.ToString(),
                ["content"] = Content ?? "",
                ["targetId"] = TargetId ?? "",
                ["squadId"] = SquadId ?? "",
                ["timestamp"] = Timestamp.ToString("o"),
                ["metadata"] = Metadata
            };
        }

        public static ChatMessage Deserialize(Dictionary<string, object> data)
        {
            var msg = new ChatMessage();

            if (data.TryGetValue("messageId", out var messageId))
                msg.MessageId = Guid.Parse(messageId.ToString());
            if (data.TryGetValue("senderId", out var senderId))
                msg.SenderId = senderId.ToString();
            if (data.TryGetValue("senderName", out var senderName))
                msg.SenderName = senderName.ToString();
            if (data.TryGetValue("channel", out var channel))
                msg.Channel = Enum.Parse<ChatChannel>(channel.ToString());
            if (data.TryGetValue("content", out var content))
                msg.Content = content.ToString();
            if (data.TryGetValue("targetId", out var targetId))
                msg.TargetId = targetId.ToString();
            if (data.TryGetValue("squadId", out var squadId))
                msg.SquadId = squadId.ToString();
            if (data.TryGetValue("timestamp", out var timestamp))
                msg.Timestamp = DateTime.Parse(timestamp.ToString());
            if (data.TryGetValue("metadata", out var metadata) && metadata is Dictionary<string, object> meta)
                msg.Metadata = meta;

            return msg;
        }
    }

    /// <summary>
    /// Chat system for multiplayer communication
    /// </summary>
    public class ChatSystem
    {
        private readonly ConcurrentQueue<ChatMessage> _pendingMessages;
        private readonly ConcurrentDictionary<Guid, ChatMessage> _recentMessages;
        private readonly object _lock = new object();

        // Chat settings
        public int MaxMessageLength { get; set; } = 500;
        public float MessageRetentionTime { get; set; } = 60.0f; // Keep for 60 seconds
        public bool EnableProfanityFilter { get; set; } = true;
        public bool EnableSpamProtection { get; set; } = true;

        // Spam protection
        private readonly ConcurrentDictionary<string, DateTime> _lastMessageTime;
        private const float MinMessageInterval = 1.0f; // 1 second between messages

        // Statistics
        public int TotalMessages { get; private set; }
        public int GlobalMessages { get; private set; }
        public int SquadMessages { get; private set; }
        public int ProximityMessages { get; private set; }
        public int WhisperMessages { get; private set; }

        // Events
        public event Action<ChatMessage> OnMessageReceived;

        public ChatSystem()
        {
            _pendingMessages = new ConcurrentQueue<ChatMessage>();
            _recentMessages = new ConcurrentDictionary<Guid, ChatMessage>();
            _lastMessageTime = new ConcurrentDictionary<string, DateTime>();
        }

        #region Message Sending

        /// <summary>
        /// Send chat message
        /// </summary>
        public bool SendMessage(string senderId, string senderName, ChatChannel channel, string content, string targetId = null, string squadId = null)
        {
            // Validate content
            if (string.IsNullOrWhiteSpace(content))
                return false;

            // Check message length
            if (content.Length > MaxMessageLength)
                content = content.Substring(0, MaxMessageLength);

            // Spam protection
            if (EnableSpamProtection && !CanSendMessage(senderId))
                return false;

            // Apply profanity filter
            if (EnableProfanityFilter)
                content = FilterProfanity(content);

            // Create message
            var message = new ChatMessage
            {
                SenderId = senderId,
                SenderName = senderName,
                Channel = channel,
                Content = content,
                TargetId = targetId,
                SquadId = squadId
            };

            // Queue message
            _pendingMessages.Enqueue(message);
            _recentMessages[message.MessageId] = message;

            // Update spam protection
            if (EnableSpamProtection)
                _lastMessageTime[senderId] = DateTime.UtcNow;

            // Update statistics
            TotalMessages++;
            switch (channel)
            {
                case ChatChannel.Global:
                    GlobalMessages++;
                    break;
                case ChatChannel.Squad:
                    SquadMessages++;
                    break;
                case ChatChannel.Proximity:
                    ProximityMessages++;
                    break;
                case ChatChannel.Whisper:
                    WhisperMessages++;
                    break;
            }

            // Trigger event
            OnMessageReceived?.Invoke(message);

            return true;
        }

        /// <summary>
        /// Send global message
        /// </summary>
        public bool SendGlobalMessage(string senderId, string senderName, string content)
        {
            return SendMessage(senderId, senderName, ChatChannel.Global, content);
        }

        /// <summary>
        /// Send squad message
        /// </summary>
        public bool SendSquadMessage(string senderId, string senderName, string squadId, string content)
        {
            return SendMessage(senderId, senderName, ChatChannel.Squad, content, null, squadId);
        }

        /// <summary>
        /// Send proximity message
        /// </summary>
        public bool SendProximityMessage(string senderId, string senderName, string content)
        {
            return SendMessage(senderId, senderName, ChatChannel.Proximity, content);
        }

        /// <summary>
        /// Send whisper message
        /// </summary>
        public bool SendWhisper(string senderId, string senderName, string targetId, string content)
        {
            return SendMessage(senderId, senderName, ChatChannel.Whisper, content, targetId);
        }

        /// <summary>
        /// Send system message
        /// </summary>
        public bool SendSystemMessage(string content)
        {
            return SendMessage("system", "System", ChatChannel.System, content);
        }

        #endregion

        #region Message Retrieval

        /// <summary>
        /// Get pending messages
        /// </summary>
        public IEnumerable<ChatMessage> GetPendingMessages()
        {
            var messages = new List<ChatMessage>();
            while (_pendingMessages.TryDequeue(out var message))
            {
                messages.Add(message);
            }
            return messages;
        }

        /// <summary>
        /// Get messages for player
        /// </summary>
        public IEnumerable<ChatMessage> GetMessagesForPlayer(string playerId, string squadId = null)
        {
            var messages = new List<ChatMessage>();

            foreach (var message in _recentMessages.Values)
            {
                // Check if player should receive this message
                switch (message.Channel)
                {
                    case ChatChannel.Global:
                    case ChatChannel.System:
                        messages.Add(message);
                        break;

                    case ChatChannel.Squad:
                        if (!string.IsNullOrEmpty(squadId) && message.SquadId == squadId)
                            messages.Add(message);
                        break;

                    case ChatChannel.Whisper:
                        if (message.TargetId == playerId || message.SenderId == playerId)
                            messages.Add(message);
                        break;

                    case ChatChannel.Proximity:
                        // Would need position check, for now include all
                        messages.Add(message);
                        break;
                }
            }

            return messages.OrderBy(m => m.Timestamp);
        }

        #endregion

        #region Spam Protection

        /// <summary>
        /// Check if player can send message (spam protection)
        /// </summary>
        private bool CanSendMessage(string playerId)
        {
            if (_lastMessageTime.TryGetValue(playerId, out var lastTime))
            {
                var timeSince = (DateTime.UtcNow - lastTime).TotalSeconds;
                return timeSince >= MinMessageInterval;
            }

            return true;
        }

        #endregion

        #region Profanity Filter

        // Simple profanity filter (can be expanded)
        private readonly string[] _profanityWords = new[]
        {
            // Add profanity words here if needed
        };

        /// <summary>
        /// Filter profanity from message
        /// </summary>
        private string FilterProfanity(string content)
        {
            foreach (var word in _profanityWords)
            {
                var replacement = new string('*', word.Length);
                content = content.Replace(word, replacement, StringComparison.OrdinalIgnoreCase);
            }

            return content;
        }

        #endregion

        #region Maintenance

        /// <summary>
        /// Update chat system (cleanup old messages)
        /// </summary>
        public void Update()
        {
            var now = DateTime.UtcNow;
            var toRemove = new List<Guid>();

            foreach (var kvp in _recentMessages)
            {
                if ((now - kvp.Value.Timestamp).TotalSeconds > MessageRetentionTime)
                {
                    toRemove.Add(kvp.Key);
                }
            }

            foreach (var id in toRemove)
            {
                _recentMessages.TryRemove(id, out _);
            }

            // Clean up spam protection
            var oldEntries = _lastMessageTime
                .Where(kvp => (now - kvp.Value).TotalSeconds > 60)
                .Select(kvp => kvp.Key)
                .ToList();

            foreach (var key in oldEntries)
            {
                _lastMessageTime.TryRemove(key, out _);
            }
        }

        /// <summary>
        /// Clear all messages
        /// </summary>
        public void Clear()
        {
            while (_pendingMessages.TryDequeue(out _)) { }
            _recentMessages.Clear();
            _lastMessageTime.Clear();
        }

        #endregion

        #region Statistics

        /// <summary>
        /// Get chat statistics
        /// </summary>
        public Dictionary<string, object> GetStatistics()
        {
            return new Dictionary<string, object>
            {
                ["totalMessages"] = TotalMessages,
                ["globalMessages"] = GlobalMessages,
                ["squadMessages"] = SquadMessages,
                ["proximityMessages"] = ProximityMessages,
                ["whisperMessages"] = WhisperMessages,
                ["recentMessageCount"] = _recentMessages.Count,
                ["activeUsers"] = _lastMessageTime.Count
            };
        }

        #endregion
    }
}
