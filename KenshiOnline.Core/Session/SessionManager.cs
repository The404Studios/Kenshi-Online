using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Linq;
using System.Net;

namespace KenshiOnline.Core.Session
{
    /// <summary>
    /// Player session data
    /// </summary>
    public class PlayerSession
    {
        public string SessionId { get; set; }
        public string PlayerId { get; set; }
        public string PlayerName { get; set; }
        public string IPAddress { get; set; }
        public DateTime ConnectedAt { get; set; }
        public DateTime LastHeartbeat { get; set; }
        public bool IsAuthenticated { get; set; }
        public bool IsAdmin { get; set; }
        public int Ping { get; set; }
        public Dictionary<string, object> Metadata { get; set; }

        public PlayerSession()
        {
            SessionId = Guid.NewGuid().ToString();
            ConnectedAt = DateTime.UtcNow;
            LastHeartbeat = DateTime.UtcNow;
            Metadata = new Dictionary<string, object>();
        }

        public TimeSpan ConnectedDuration => DateTime.UtcNow - ConnectedAt;
        public TimeSpan TimeSinceLastHeartbeat => DateTime.UtcNow - LastHeartbeat;

        public Dictionary<string, object> Serialize()
        {
            return new Dictionary<string, object>
            {
                ["sessionId"] = SessionId,
                ["playerId"] = PlayerId ?? "",
                ["playerName"] = PlayerName ?? "",
                ["ipAddress"] = IPAddress ?? "",
                ["connectedAt"] = ConnectedAt.ToString("o"),
                ["lastHeartbeat"] = LastHeartbeat.ToString("o"),
                ["isAuthenticated"] = IsAuthenticated,
                ["isAdmin"] = IsAdmin,
                ["ping"] = Ping,
                ["metadata"] = Metadata
            };
        }
    }

    /// <summary>
    /// Server session info (for server browser)
    /// </summary>
    public class ServerInfo
    {
        public string ServerId { get; set; }
        public string ServerName { get; set; }
        public string Description { get; set; }
        public string Version { get; set; }
        public string Map { get; set; }
        public int MaxPlayers { get; set; }
        public int CurrentPlayers { get; set; }
        public bool HasPassword { get; set; }
        public string IPAddress { get; set; }
        public int Port { get; set; }
        public Dictionary<string, string> ServerTags { get; set; }

        public ServerInfo()
        {
            ServerId = Guid.NewGuid().ToString();
            ServerTags = new Dictionary<string, string>();
            Version = "1.0.0";
            MaxPlayers = 32;
        }

        public Dictionary<string, object> Serialize()
        {
            return new Dictionary<string, object>
            {
                ["serverId"] = ServerId,
                ["serverName"] = ServerName ?? "",
                ["description"] = Description ?? "",
                ["version"] = Version,
                ["map"] = Map ?? "",
                ["maxPlayers"] = MaxPlayers,
                ["currentPlayers"] = CurrentPlayers,
                ["hasPassword"] = HasPassword,
                ["ipAddress"] = IPAddress ?? "",
                ["port"] = Port,
                ["serverTags"] = ServerTags
            };
        }
    }

    /// <summary>
    /// Manages player sessions and server info
    /// </summary>
    public class SessionManager
    {
        private readonly ConcurrentDictionary<string, PlayerSession> _sessions; // SessionId -> Session
        private readonly ConcurrentDictionary<string, string> _playerIdToSessionId; // PlayerId -> SessionId
        private readonly object _lock = new object();

        // Server info
        private ServerInfo _serverInfo;

        // Session settings
        public TimeSpan SessionTimeout { get; set; } = TimeSpan.FromMinutes(5);
        public TimeSpan HeartbeatInterval { get; set; } = TimeSpan.FromSeconds(30);

        // Events
        public event Action<PlayerSession> OnPlayerJoined;
        public event Action<PlayerSession> OnPlayerLeft;
        public event Action<PlayerSession> OnPlayerAuthenticated;

        // Statistics
        public int TotalSessions => _sessions.Count;
        public int AuthenticatedSessions => _sessions.Values.Count(s => s.IsAuthenticated);
        public int TotalConnectionsAllTime { get; private set; }

        public SessionManager()
        {
            _sessions = new ConcurrentDictionary<string, PlayerSession>();
            _playerIdToSessionId = new ConcurrentDictionary<string, string>();
            _serverInfo = new ServerInfo();
        }

        #region Session Management

        /// <summary>
        /// Create new player session
        /// </summary>
        public PlayerSession CreateSession(string ipAddress)
        {
            var session = new PlayerSession
            {
                IPAddress = ipAddress
            };

            if (_sessions.TryAdd(session.SessionId, session))
            {
                TotalConnectionsAllTime++;
                UpdateServerInfo();
                OnPlayerJoined?.Invoke(session);
                return session;
            }

            return null;
        }

        /// <summary>
        /// Authenticate session
        /// </summary>
        public bool AuthenticateSession(string sessionId, string playerId, string playerName)
        {
            if (_sessions.TryGetValue(sessionId, out var session))
            {
                session.PlayerId = playerId;
                session.PlayerName = playerName;
                session.IsAuthenticated = true;
                session.LastHeartbeat = DateTime.UtcNow;

                _playerIdToSessionId[playerId] = sessionId;

                UpdateServerInfo();
                OnPlayerAuthenticated?.Invoke(session);
                return true;
            }

            return false;
        }

        /// <summary>
        /// Remove session
        /// </summary>
        public bool RemoveSession(string sessionId)
        {
            if (_sessions.TryRemove(sessionId, out var session))
            {
                if (!string.IsNullOrEmpty(session.PlayerId))
                {
                    _playerIdToSessionId.TryRemove(session.PlayerId, out _);
                }

                UpdateServerInfo();
                OnPlayerLeft?.Invoke(session);
                return true;
            }

            return false;
        }

        /// <summary>
        /// Get session by ID
        /// </summary>
        public PlayerSession GetSession(string sessionId)
        {
            _sessions.TryGetValue(sessionId, out var session);
            return session;
        }

        /// <summary>
        /// Get session by player ID
        /// </summary>
        public PlayerSession GetSessionByPlayerId(string playerId)
        {
            if (_playerIdToSessionId.TryGetValue(playerId, out var sessionId))
            {
                return GetSession(sessionId);
            }
            return null;
        }

        /// <summary>
        /// Get all sessions
        /// </summary>
        public IEnumerable<PlayerSession> GetAllSessions()
        {
            return _sessions.Values;
        }

        /// <summary>
        /// Get authenticated sessions
        /// </summary>
        public IEnumerable<PlayerSession> GetAuthenticatedSessions()
        {
            return _sessions.Values.Where(s => s.IsAuthenticated);
        }

        /// <summary>
        /// Update session heartbeat
        /// </summary>
        public bool UpdateHeartbeat(string sessionId, int ping = 0)
        {
            if (_sessions.TryGetValue(sessionId, out var session))
            {
                session.LastHeartbeat = DateTime.UtcNow;
                session.Ping = ping;
                return true;
            }

            return false;
        }

        /// <summary>
        /// Check for timed-out sessions
        /// </summary>
        public void Update()
        {
            var timedOut = new List<string>();

            foreach (var kvp in _sessions)
            {
                if (kvp.Value.TimeSinceLastHeartbeat > SessionTimeout)
                {
                    timedOut.Add(kvp.Key);
                }
            }

            foreach (var sessionId in timedOut)
            {
                RemoveSession(sessionId);
            }
        }

        #endregion

        #region Server Info

        /// <summary>
        /// Set server info
        /// </summary>
        public void SetServerInfo(ServerInfo info)
        {
            lock (_lock)
            {
                _serverInfo = info;
                UpdateServerInfo();
            }
        }

        /// <summary>
        /// Get server info
        /// </summary>
        public ServerInfo GetServerInfo()
        {
            lock (_lock)
            {
                return _serverInfo;
            }
        }

        /// <summary>
        /// Update server info (player count, etc.)
        /// </summary>
        private void UpdateServerInfo()
        {
            lock (_lock)
            {
                _serverInfo.CurrentPlayers = AuthenticatedSessions;
            }
        }

        /// <summary>
        /// Set server name
        /// </summary>
        public void SetServerName(string name)
        {
            lock (_lock)
            {
                _serverInfo.ServerName = name;
            }
        }

        /// <summary>
        /// Set server description
        /// </summary>
        public void SetServerDescription(string description)
        {
            lock (_lock)
            {
                _serverInfo.Description = description;
            }
        }

        /// <summary>
        /// Set max players
        /// </summary>
        public void SetMaxPlayers(int maxPlayers)
        {
            lock (_lock)
            {
                _serverInfo.MaxPlayers = maxPlayers;
            }
        }

        /// <summary>
        /// Check if server is full
        /// </summary>
        public bool IsServerFull()
        {
            lock (_lock)
            {
                return _serverInfo.CurrentPlayers >= _serverInfo.MaxPlayers;
            }
        }

        #endregion

        #region Admin Management

        /// <summary>
        /// Set player as admin
        /// </summary>
        public bool SetAdmin(string playerId, bool isAdmin)
        {
            var session = GetSessionByPlayerId(playerId);
            if (session != null)
            {
                session.IsAdmin = isAdmin;
                return true;
            }
            return false;
        }

        /// <summary>
        /// Check if player is admin
        /// </summary>
        public bool IsAdmin(string playerId)
        {
            var session = GetSessionByPlayerId(playerId);
            return session != null && session.IsAdmin;
        }

        /// <summary>
        /// Get all admin sessions
        /// </summary>
        public IEnumerable<PlayerSession> GetAdminSessions()
        {
            return _sessions.Values.Where(s => s.IsAdmin);
        }

        #endregion

        #region Kick/Ban

        /// <summary>
        /// Kick player by player ID
        /// </summary>
        public bool KickPlayer(string playerId, string reason = "")
        {
            var session = GetSessionByPlayerId(playerId);
            if (session != null)
            {
                // Store reason in metadata
                if (!string.IsNullOrEmpty(reason))
                {
                    session.Metadata["kickReason"] = reason;
                }

                return RemoveSession(session.SessionId);
            }
            return false;
        }

        /// <summary>
        /// Kick player by session ID
        /// </summary>
        public bool KickSession(string sessionId, string reason = "")
        {
            var session = GetSession(sessionId);
            if (session != null)
            {
                if (!string.IsNullOrEmpty(reason))
                {
                    session.Metadata["kickReason"] = reason;
                }

                return RemoveSession(sessionId);
            }
            return false;
        }

        #endregion

        #region Statistics

        /// <summary>
        /// Get session statistics
        /// </summary>
        public Dictionary<string, object> GetStatistics()
        {
            var stats = new Dictionary<string, object>
            {
                ["totalSessions"] = TotalSessions,
                ["authenticatedSessions"] = AuthenticatedSessions,
                ["totalConnectionsAllTime"] = TotalConnectionsAllTime,
                ["adminSessions"] = GetAdminSessions().Count(),
                ["averagePing"] = _sessions.Values.Any() ? _sessions.Values.Average(s => s.Ping) : 0
            };

            // Connection duration stats
            if (_sessions.Values.Any())
            {
                var durations = _sessions.Values.Select(s => s.ConnectedDuration.TotalMinutes).ToList();
                stats["averageConnectionDuration"] = durations.Average();
                stats["maxConnectionDuration"] = durations.Max();
            }

            return stats;
        }

        /// <summary>
        /// Get player list for server browser
        /// </summary>
        public List<Dictionary<string, object>> GetPlayerList()
        {
            return GetAuthenticatedSessions()
                .Select(s => new Dictionary<string, object>
                {
                    ["playerId"] = s.PlayerId,
                    ["playerName"] = s.PlayerName,
                    ["ping"] = s.Ping,
                    ["connectedDuration"] = s.ConnectedDuration.TotalMinutes,
                    ["isAdmin"] = s.IsAdmin
                })
                .ToList();
        }

        #endregion

        #region Bulk Operations

        /// <summary>
        /// Remove all sessions
        /// </summary>
        public void ClearAllSessions()
        {
            var sessionIds = _sessions.Keys.ToList();
            foreach (var sessionId in sessionIds)
            {
                RemoveSession(sessionId);
            }
        }

        /// <summary>
        /// Send message to all sessions (callback)
        /// </summary>
        public void BroadcastToAll(Action<PlayerSession> action)
        {
            foreach (var session in _sessions.Values)
            {
                action(session);
            }
        }

        /// <summary>
        /// Send message to authenticated sessions only
        /// </summary>
        public void BroadcastToAuthenticated(Action<PlayerSession> action)
        {
            foreach (var session in GetAuthenticatedSessions())
            {
                action(session);
            }
        }

        #endregion
    }
}
