using System;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;

namespace KenshiMultiplayer.Core
{
    /// <summary>
    /// PlayerIdentity represents a player's identity across sessions.
    ///
    /// This is NOT gameplay data (that's in EntityState).
    /// This is WHO the player is and WHETHER they can connect.
    /// </summary>
    public class PlayerIdentity
    {
        /// <summary>
        /// Unique player identifier (UUID format).
        /// Persists across sessions. Used for saves, bans, friends.
        /// </summary>
        public string PlayerId { get; set; }

        /// <summary>
        /// Display name shown to other players.
        /// Can be changed; PlayerId is the true identifier.
        /// </summary>
        public string DisplayName { get; set; }

        /// <summary>
        /// JWT token for current session authentication.
        /// Expires and must be refreshed.
        /// </summary>
        public string AuthToken { get; set; }

        /// <summary>
        /// Client's Kenshi game version.
        /// Must match server for connection.
        /// </summary>
        public string KenshiVersion { get; set; }

        /// <summary>
        /// Client's mod version.
        /// Major.Minor must match server.
        /// </summary>
        public string ModVersion { get; set; }

        /// <summary>
        /// Network protocol version.
        /// Must match exactly.
        /// </summary>
        public string ProtocolVersion { get; set; } = "1";

        /// <summary>
        /// When this identity was created.
        /// </summary>
        public long CreatedAt { get; set; }

        /// <summary>
        /// Last time this player was seen online.
        /// </summary>
        public long LastSeen { get; set; }

        /// <summary>
        /// Total time played across all sessions (seconds).
        /// </summary>
        public long TotalPlayTime { get; set; }

        /// <summary>
        /// Create a new player identity.
        /// </summary>
        public static PlayerIdentity Create(string displayName, string kenshiVersion, string modVersion)
        {
            return new PlayerIdentity
            {
                PlayerId = Guid.NewGuid().ToString(),
                DisplayName = displayName,
                KenshiVersion = kenshiVersion,
                ModVersion = modVersion,
                ProtocolVersion = "1",
                CreatedAt = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds(),
                LastSeen = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds()
            };
        }

        /// <summary>
        /// Update last seen timestamp.
        /// </summary>
        public void UpdateLastSeen()
        {
            LastSeen = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();
        }

        /// <summary>
        /// Add play time.
        /// </summary>
        public void AddPlayTime(long seconds)
        {
            TotalPlayTime += seconds;
        }

        /// <summary>
        /// Validate identity fields.
        /// </summary>
        public IdentityValidationResult Validate()
        {
            if (string.IsNullOrWhiteSpace(PlayerId))
                return IdentityValidationResult.MissingPlayerId;

            if (string.IsNullOrWhiteSpace(DisplayName))
                return IdentityValidationResult.MissingDisplayName;

            if (DisplayName.Length < 2 || DisplayName.Length > 32)
                return IdentityValidationResult.InvalidDisplayName;

            if (string.IsNullOrWhiteSpace(KenshiVersion))
                return IdentityValidationResult.MissingKenshiVersion;

            if (string.IsNullOrWhiteSpace(ModVersion))
                return IdentityValidationResult.MissingModVersion;

            return IdentityValidationResult.Valid;
        }

        /// <summary>
        /// Get public info (no auth token).
        /// </summary>
        public PlayerInfo GetPublicInfo()
        {
            return new PlayerInfo
            {
                PlayerId = PlayerId,
                DisplayName = DisplayName,
                LastSeen = LastSeen,
                TotalPlayTime = TotalPlayTime
            };
        }

        public string ToJson()
        {
            return JsonSerializer.Serialize(this);
        }

        public static PlayerIdentity FromJson(string json)
        {
            return JsonSerializer.Deserialize<PlayerIdentity>(json);
        }
    }

    /// <summary>
    /// Result of identity validation.
    /// </summary>
    public enum IdentityValidationResult
    {
        Valid,
        MissingPlayerId,
        MissingDisplayName,
        InvalidDisplayName,
        MissingKenshiVersion,
        MissingModVersion
    }

    /// <summary>
    /// Public player info (safe to share with other players).
    /// </summary>
    public class PlayerInfo
    {
        public string PlayerId { get; set; }
        public string DisplayName { get; set; }
        public long LastSeen { get; set; }
        public long TotalPlayTime { get; set; }
    }

    /// <summary>
    /// Connection state for a connected player.
    /// </summary>
    public class PlayerConnection
    {
        public string PlayerId { get; set; }
        public string SessionId { get; set; }
        public PlayerIdentity Identity { get; set; }
        public ConnectionState State { get; set; }
        public long ConnectedAt { get; set; }
        public long LastMessageAt { get; set; }
        public ulong LastAcknowledgedTick { get; set; }
        public int Latency { get; set; } // milliseconds
        public int MessagesSent { get; set; }
        public int MessagesReceived { get; set; }

        /// <summary>
        /// Is this connection healthy?
        /// </summary>
        public bool IsHealthy
        {
            get
            {
                var now = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();
                var sinceLastMessage = now - LastMessageAt;
                return sinceLastMessage < 5000 && Latency < 1000;
            }
        }

        /// <summary>
        /// Create a new connection for a player.
        /// </summary>
        public static PlayerConnection Create(PlayerIdentity identity, string sessionId)
        {
            return new PlayerConnection
            {
                PlayerId = identity.PlayerId,
                SessionId = sessionId,
                Identity = identity,
                State = ConnectionState.Connected,
                ConnectedAt = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds(),
                LastMessageAt = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds()
            };
        }

        /// <summary>
        /// Record a received message.
        /// </summary>
        public void RecordMessage()
        {
            LastMessageAt = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();
            MessagesReceived++;
        }

        /// <summary>
        /// Update latency measurement.
        /// </summary>
        public void UpdateLatency(int latencyMs)
        {
            // Exponential moving average
            Latency = (Latency * 3 + latencyMs) / 4;
        }
    }

    /// <summary>
    /// Connection state enum.
    /// </summary>
    public enum ConnectionState
    {
        Connecting,     // Initial handshake
        Connected,      // Fully connected and playing
        Disconnecting,  // Graceful disconnect in progress
        Disconnected,   // Connection lost
        Reconnecting,   // Attempting to reconnect
        Banned          // Connection rejected due to ban
    }

    /// <summary>
    /// Disconnected player tracking for reconnection.
    /// </summary>
    public class DisconnectedPlayer
    {
        public string PlayerId { get; set; }
        public string SessionId { get; set; }
        public long DisconnectedAt { get; set; }
        public EntityState LastState { get; set; }
        public string AuthToken { get; set; }

        /// <summary>
        /// Grace period in milliseconds (5 minutes).
        /// </summary>
        public const long GRACE_PERIOD_MS = 5 * 60 * 1000;

        /// <summary>
        /// Is the grace period still active?
        /// </summary>
        public bool IsGracePeriodActive
        {
            get
            {
                var now = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();
                return now - DisconnectedAt < GRACE_PERIOD_MS;
            }
        }

        /// <summary>
        /// Time remaining in grace period (milliseconds).
        /// </summary>
        public long GraceTimeRemaining
        {
            get
            {
                var now = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();
                var remaining = GRACE_PERIOD_MS - (now - DisconnectedAt);
                return Math.Max(0, remaining);
            }
        }

        /// <summary>
        /// Create from a connected player.
        /// </summary>
        public static DisconnectedPlayer Create(PlayerConnection connection, EntityState lastState)
        {
            return new DisconnectedPlayer
            {
                PlayerId = connection.PlayerId,
                SessionId = connection.SessionId,
                DisconnectedAt = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds(),
                LastState = lastState?.Clone(),
                AuthToken = connection.Identity.AuthToken
            };
        }
    }
}
