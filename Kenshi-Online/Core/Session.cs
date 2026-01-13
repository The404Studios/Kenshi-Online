using System;
using System.Collections.Generic;
using System.Text.Json;

namespace KenshiMultiplayer.Core
{
    /// <summary>
    /// Session state enum - what phase is the game session in?
    /// </summary>
    public enum SessionState
    {
        /// <summary>Lobby - waiting for players to join</summary>
        Lobby,

        /// <summary>Playing - game is active</summary>
        Playing,

        /// <summary>Paused - host paused the game</summary>
        Paused,

        /// <summary>Closed - session ended</summary>
        Closed
    }

    /// <summary>
    /// Session represents a multiplayer game instance.
    ///
    /// A session is:
    /// - Owned by a host (who can pause, kick, save)
    /// - Has a specific world state (save file)
    /// - Requires version matching to join
    /// - Tracks all connected players
    /// </summary>
    public class Session
    {
        /// <summary>
        /// Unique session identifier (UUID format).
        /// Used for reconnection and session lookup.
        /// </summary>
        public string SessionId { get; set; }

        /// <summary>
        /// Player ID who owns this session (the host).
        /// Host has special privileges: pause, kick, save.
        /// </summary>
        public string HostId { get; set; }

        /// <summary>
        /// Human-readable name for this session.
        /// Example: "Bob's Wasteland Adventure"
        /// </summary>
        public string WorldName { get; set; }

        /// <summary>
        /// SHA256 hash of the world save file.
        /// Clients must have matching hash to join (ensures same world).
        /// </summary>
        public string WorldHash { get; set; }

        /// <summary>
        /// Required Kenshi game version.
        /// Example: "1.0.64"
        /// </summary>
        public string KenshiVersion { get; set; }

        /// <summary>
        /// Required mod version.
        /// Example: "0.5.0"
        /// </summary>
        public string ModVersion { get; set; }

        /// <summary>
        /// Network protocol version.
        /// Must match exactly for compatibility.
        /// </summary>
        public string ProtocolVersion { get; set; } = "1";

        /// <summary>
        /// Current session state.
        /// </summary>
        public SessionState State { get; set; } = SessionState.Lobby;

        /// <summary>
        /// IDs of all connected players.
        /// </summary>
        public List<string> PlayerIds { get; set; } = new();

        /// <summary>
        /// Maximum number of players allowed.
        /// </summary>
        public int MaxPlayers { get; set; } = 8;

        /// <summary>
        /// Current tick number in this session.
        /// </summary>
        public ulong CurrentTick { get; set; }

        /// <summary>
        /// Unix timestamp when session was created.
        /// </summary>
        public long CreatedAt { get; set; }

        /// <summary>
        /// Unix timestamp of last activity.
        /// </summary>
        public long LastActivity { get; set; }

        /// <summary>
        /// Session password (null if public).
        /// Stored as hash, not plaintext.
        /// </summary>
        public string PasswordHash { get; set; }

        /// <summary>
        /// Is this session password-protected?
        /// </summary>
        public bool RequiresPassword => !string.IsNullOrEmpty(PasswordHash);

        /// <summary>
        /// Is the session full?
        /// </summary>
        public bool IsFull => PlayerIds.Count >= MaxPlayers;

        /// <summary>
        /// Is the session joinable?
        /// </summary>
        public bool CanJoin => State == SessionState.Lobby && !IsFull;

        /// <summary>
        /// Create a new session.
        /// </summary>
        public static Session Create(string hostId, string worldName, string kenshiVersion, string modVersion)
        {
            return new Session
            {
                SessionId = Guid.NewGuid().ToString(),
                HostId = hostId,
                WorldName = worldName,
                KenshiVersion = kenshiVersion,
                ModVersion = modVersion,
                State = SessionState.Lobby,
                PlayerIds = new List<string> { hostId },
                CreatedAt = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds(),
                LastActivity = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds()
            };
        }

        /// <summary>
        /// Add a player to the session.
        /// </summary>
        public SessionJoinResult AddPlayer(string playerId)
        {
            if (IsFull)
                return SessionJoinResult.SessionFull;

            if (State != SessionState.Lobby && State != SessionState.Playing)
                return SessionJoinResult.SessionNotJoinable;

            if (PlayerIds.Contains(playerId))
                return SessionJoinResult.AlreadyInSession;

            PlayerIds.Add(playerId);
            LastActivity = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();

            return SessionJoinResult.Success;
        }

        /// <summary>
        /// Remove a player from the session.
        /// </summary>
        public bool RemovePlayer(string playerId)
        {
            var removed = PlayerIds.Remove(playerId);

            // If host leaves, assign new host or close session
            if (removed && playerId == HostId)
            {
                if (PlayerIds.Count > 0)
                {
                    HostId = PlayerIds[0];
                }
                else
                {
                    State = SessionState.Closed;
                }
            }

            LastActivity = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();
            return removed;
        }

        /// <summary>
        /// Start the game (transition from Lobby to Playing).
        /// Only host can start.
        /// </summary>
        public bool Start(string requesterId)
        {
            if (requesterId != HostId)
                return false;

            if (State != SessionState.Lobby)
                return false;

            State = SessionState.Playing;
            LastActivity = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();
            return true;
        }

        /// <summary>
        /// Pause the game.
        /// Only host can pause.
        /// </summary>
        public bool Pause(string requesterId)
        {
            if (requesterId != HostId)
                return false;

            if (State != SessionState.Playing)
                return false;

            State = SessionState.Paused;
            LastActivity = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();
            return true;
        }

        /// <summary>
        /// Resume the game from pause.
        /// </summary>
        public bool Resume(string requesterId)
        {
            if (requesterId != HostId)
                return false;

            if (State != SessionState.Paused)
                return false;

            State = SessionState.Playing;
            LastActivity = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();
            return true;
        }

        /// <summary>
        /// Close the session.
        /// Only host can close.
        /// </summary>
        public bool Close(string requesterId)
        {
            if (requesterId != HostId)
                return false;

            State = SessionState.Closed;
            LastActivity = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();
            return true;
        }

        /// <summary>
        /// Check if a player can join this session.
        /// </summary>
        public SessionJoinResult CanPlayerJoin(PlayerIdentity player)
        {
            if (State == SessionState.Closed)
                return SessionJoinResult.SessionClosed;

            if (IsFull)
                return SessionJoinResult.SessionFull;

            if (PlayerIds.Contains(player.PlayerId))
                return SessionJoinResult.AlreadyInSession;

            // Version checks
            if (player.KenshiVersion != KenshiVersion)
                return SessionJoinResult.KenshiVersionMismatch;

            if (!IsModVersionCompatible(player.ModVersion, ModVersion))
                return SessionJoinResult.ModVersionMismatch;

            if (player.ProtocolVersion != ProtocolVersion)
                return SessionJoinResult.ProtocolMismatch;

            return SessionJoinResult.Success;
        }

        private bool IsModVersionCompatible(string clientVersion, string serverVersion)
        {
            // Major.Minor must match; Patch can differ
            var clientParts = clientVersion.Split('.');
            var serverParts = serverVersion.Split('.');

            if (clientParts.Length < 2 || serverParts.Length < 2)
                return clientVersion == serverVersion;

            return clientParts[0] == serverParts[0] &&
                   clientParts[1] == serverParts[1];
        }

        /// <summary>
        /// Get session info for display (no sensitive data).
        /// </summary>
        public SessionInfo GetInfo()
        {
            return new SessionInfo
            {
                SessionId = SessionId,
                WorldName = WorldName,
                HostName = HostId, // Should be resolved to display name by caller
                PlayerCount = PlayerIds.Count,
                MaxPlayers = MaxPlayers,
                State = State,
                RequiresPassword = RequiresPassword,
                KenshiVersion = KenshiVersion,
                ModVersion = ModVersion
            };
        }

        public string ToJson()
        {
            return JsonSerializer.Serialize(this);
        }

        public static Session FromJson(string json)
        {
            return JsonSerializer.Deserialize<Session>(json);
        }
    }

    /// <summary>
    /// Result of attempting to join a session.
    /// </summary>
    public enum SessionJoinResult
    {
        Success,
        SessionNotFound,
        SessionFull,
        SessionClosed,
        SessionNotJoinable,
        AlreadyInSession,
        KenshiVersionMismatch,
        ModVersionMismatch,
        ProtocolMismatch,
        WorldHashMismatch,
        PasswordRequired,
        WrongPassword,
        Banned
    }

    /// <summary>
    /// Session info for display (public, no sensitive data).
    /// </summary>
    public class SessionInfo
    {
        public string SessionId { get; set; }
        public string WorldName { get; set; }
        public string HostName { get; set; }
        public int PlayerCount { get; set; }
        public int MaxPlayers { get; set; }
        public SessionState State { get; set; }
        public bool RequiresPassword { get; set; }
        public string KenshiVersion { get; set; }
        public string ModVersion { get; set; }
    }

    /// <summary>
    /// Request to join a session.
    /// </summary>
    public class JoinRequest
    {
        public string SessionId { get; set; }
        public PlayerIdentity Player { get; set; }
        public string Password { get; set; }
        public string ClientWorldHash { get; set; }
    }

    /// <summary>
    /// Response to a join request.
    /// </summary>
    public class JoinResponse
    {
        public bool Success { get; set; }
        public SessionJoinResult Result { get; set; }
        public string ErrorMessage { get; set; }
        public Session Session { get; set; }
        public WorldTick InitialState { get; set; }
        public string AssignedSpawnPoint { get; set; }
    }
}
