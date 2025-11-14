using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Linq;

namespace KenshiOnline.Core.Social
{
    /// <summary>
    /// Friend status
    /// </summary>
    public enum FriendStatus
    {
        Online,
        Offline,
        InGame,
        Away,
        Busy
    }

    /// <summary>
    /// Friend data
    /// </summary>
    public class Friend
    {
        public string PlayerId { get; set; }
        public string PlayerName { get; set; }
        public FriendStatus Status { get; set; }
        public DateTime AddedAt { get; set; }
        public DateTime LastOnline { get; set; }
        public string CurrentServer { get; set; }
        public Dictionary<string, object> Metadata { get; set; }

        public Friend()
        {
            AddedAt = DateTime.UtcNow;
            LastOnline = DateTime.UtcNow;
            Status = FriendStatus.Offline;
            Metadata = new Dictionary<string, object>();
        }

        public TimeSpan TimeSinceLastOnline => DateTime.UtcNow - LastOnline;

        public Dictionary<string, object> Serialize()
        {
            return new Dictionary<string, object>
            {
                ["playerId"] = PlayerId ?? "",
                ["playerName"] = PlayerName ?? "",
                ["status"] = Status.ToString(),
                ["addedAt"] = AddedAt.ToString("o"),
                ["lastOnline"] = LastOnline.ToString("o"),
                ["currentServer"] = CurrentServer ?? "",
                ["metadata"] = Metadata
            };
        }
    }

    /// <summary>
    /// Friend request
    /// </summary>
    public class FriendRequest
    {
        public string RequestId { get; set; }
        public string SenderId { get; set; }
        public string SenderName { get; set; }
        public string ReceiverId { get; set; }
        public DateTime CreatedAt { get; set; }
        public DateTime ExpiresAt { get; set; }
        public string Message { get; set; }

        public FriendRequest()
        {
            RequestId = Guid.NewGuid().ToString();
            CreatedAt = DateTime.UtcNow;
            ExpiresAt = CreatedAt.AddHours(24); // 24 hour expiration
        }

        public bool IsExpired => DateTime.UtcNow > ExpiresAt;

        public Dictionary<string, object> Serialize()
        {
            return new Dictionary<string, object>
            {
                ["requestId"] = RequestId,
                ["senderId"] = SenderId ?? "",
                ["senderName"] = SenderName ?? "",
                ["receiverId"] = ReceiverId ?? "",
                ["createdAt"] = CreatedAt.ToString("o"),
                ["expiresAt"] = ExpiresAt.ToString("o"),
                ["message"] = Message ?? "",
                ["isExpired"] = IsExpired
            };
        }
    }

    /// <summary>
    /// Friend system for managing player friendships
    /// </summary>
    public class FriendSystem
    {
        // PlayerId -> List of Friend PlayerIds
        private readonly ConcurrentDictionary<string, HashSet<string>> _friendships;

        // PlayerId -> Friend data
        private readonly ConcurrentDictionary<string, ConcurrentDictionary<string, Friend>> _friendData;

        // Friend requests
        private readonly ConcurrentDictionary<string, FriendRequest> _requests;

        private readonly object _lock = new object();

        // Settings
        public int MaxFriends { get; set; } = 100;
        public bool AllowFriendRequests { get; set; } = true;

        // Events
        public event Action<string, Friend> OnFriendAdded;
        public event Action<string, string> OnFriendRemoved;
        public event Action<FriendRequest> OnFriendRequestSent;
        public event Action<string, string, FriendStatus> OnFriendStatusChanged;

        // Statistics
        public int TotalFriendships { get; private set; }
        public int TotalRequests => _requests.Count;

        public FriendSystem()
        {
            _friendships = new ConcurrentDictionary<string, HashSet<string>>();
            _friendData = new ConcurrentDictionary<string, ConcurrentDictionary<string, Friend>>();
            _requests = new ConcurrentDictionary<string, FriendRequest>();
        }

        #region Friend Management

        /// <summary>
        /// Send friend request
        /// </summary>
        public FriendRequest SendFriendRequest(string senderId, string senderName, string receiverId, string message = null)
        {
            if (!AllowFriendRequests)
                return null;

            // Check if already friends
            if (AreFriends(senderId, receiverId))
                return null;

            // Check if request already exists
            if (_requests.Values.Any(r => r.SenderId == senderId && r.ReceiverId == receiverId && !r.IsExpired))
                return null;

            // Check friend limit
            if (GetFriendCount(senderId) >= MaxFriends)
                return null;

            // Create request
            var request = new FriendRequest
            {
                SenderId = senderId,
                SenderName = senderName,
                ReceiverId = receiverId,
                Message = message
            };

            _requests[request.RequestId] = request;

            OnFriendRequestSent?.Invoke(request);

            return request;
        }

        /// <summary>
        /// Accept friend request
        /// </summary>
        public bool AcceptFriendRequest(string requestId, string receiverName)
        {
            if (!_requests.TryGetValue(requestId, out var request))
                return false;

            // Check if expired
            if (request.IsExpired)
            {
                _requests.TryRemove(requestId, out _);
                return false;
            }

            // Check friend limits
            if (GetFriendCount(request.SenderId) >= MaxFriends || GetFriendCount(request.ReceiverId) >= MaxFriends)
                return false;

            // Add friendship (bidirectional)
            AddFriend(request.SenderId, request.ReceiverId, request.SenderName);
            AddFriend(request.ReceiverId, request.SenderId, receiverName);

            // Remove request
            _requests.TryRemove(requestId, out _);

            return true;
        }

        /// <summary>
        /// Decline friend request
        /// </summary>
        public bool DeclineFriendRequest(string requestId)
        {
            return _requests.TryRemove(requestId, out _);
        }

        /// <summary>
        /// Add friend (internal)
        /// </summary>
        private void AddFriend(string playerId, string friendId, string friendName)
        {
            // Add to friendship list
            var friendList = _friendships.GetOrAdd(playerId, _ => new HashSet<string>());
            lock (friendList)
            {
                friendList.Add(friendId);
            }

            // Add friend data
            var friendDataDict = _friendData.GetOrAdd(playerId, _ => new ConcurrentDictionary<string, Friend>());
            var friend = new Friend
            {
                PlayerId = friendId,
                PlayerName = friendName,
                Status = FriendStatus.Offline
            };

            friendDataDict[friendId] = friend;

            TotalFriendships++;
            OnFriendAdded?.Invoke(playerId, friend);
        }

        /// <summary>
        /// Remove friend
        /// </summary>
        public bool RemoveFriend(string playerId, string friendId)
        {
            bool removed = false;

            // Remove from both sides
            if (_friendships.TryGetValue(playerId, out var friendList1))
            {
                lock (friendList1)
                {
                    removed = friendList1.Remove(friendId);
                }
            }

            if (_friendships.TryGetValue(friendId, out var friendList2))
            {
                lock (friendList2)
                {
                    friendList2.Remove(playerId);
                }
            }

            // Remove friend data
            if (_friendData.TryGetValue(playerId, out var friendDataDict1))
            {
                friendDataDict1.TryRemove(friendId, out _);
            }

            if (_friendData.TryGetValue(friendId, out var friendDataDict2))
            {
                friendDataDict2.TryRemove(playerId, out _);
            }

            if (removed)
            {
                TotalFriendships--;
                OnFriendRemoved?.Invoke(playerId, friendId);
            }

            return removed;
        }

        /// <summary>
        /// Check if two players are friends
        /// </summary>
        public bool AreFriends(string playerId1, string playerId2)
        {
            if (_friendships.TryGetValue(playerId1, out var friendList))
            {
                lock (friendList)
                {
                    return friendList.Contains(playerId2);
                }
            }

            return false;
        }

        /// <summary>
        /// Get friend list
        /// </summary>
        public IEnumerable<Friend> GetFriends(string playerId)
        {
            if (_friendData.TryGetValue(playerId, out var friendDataDict))
            {
                return friendDataDict.Values.ToList();
            }

            return Enumerable.Empty<Friend>();
        }

        /// <summary>
        /// Get online friends
        /// </summary>
        public IEnumerable<Friend> GetOnlineFriends(string playerId)
        {
            return GetFriends(playerId).Where(f => f.Status == FriendStatus.Online || f.Status == FriendStatus.InGame);
        }

        /// <summary>
        /// Get friend count
        /// </summary>
        public int GetFriendCount(string playerId)
        {
            if (_friendships.TryGetValue(playerId, out var friendList))
            {
                lock (friendList)
                {
                    return friendList.Count;
                }
            }

            return 0;
        }

        #endregion

        #region Status Management

        /// <summary>
        /// Update friend status
        /// </summary>
        public void UpdateFriendStatus(string playerId, FriendStatus status, string serverName = null)
        {
            // Update status for all players who have this player as a friend
            foreach (var kvp in _friendData)
            {
                if (kvp.Value.TryGetValue(playerId, out var friend))
                {
                    friend.Status = status;
                    friend.LastOnline = DateTime.UtcNow;

                    if (!string.IsNullOrEmpty(serverName))
                    {
                        friend.CurrentServer = serverName;
                    }

                    OnFriendStatusChanged?.Invoke(kvp.Key, playerId, status);
                }
            }
        }

        /// <summary>
        /// Set player online
        /// </summary>
        public void SetPlayerOnline(string playerId, string serverName = null)
        {
            UpdateFriendStatus(playerId, FriendStatus.Online, serverName);
        }

        /// <summary>
        /// Set player offline
        /// </summary>
        public void SetPlayerOffline(string playerId)
        {
            UpdateFriendStatus(playerId, FriendStatus.Offline);
        }

        /// <summary>
        /// Set player in game
        /// </summary>
        public void SetPlayerInGame(string playerId, string serverName)
        {
            UpdateFriendStatus(playerId, FriendStatus.InGame, serverName);
        }

        #endregion

        #region Friend Requests

        /// <summary>
        /// Get pending friend requests for player
        /// </summary>
        public IEnumerable<FriendRequest> GetPendingRequests(string playerId)
        {
            return _requests.Values.Where(r => r.ReceiverId == playerId && !r.IsExpired);
        }

        /// <summary>
        /// Get sent friend requests
        /// </summary>
        public IEnumerable<FriendRequest> GetSentRequests(string playerId)
        {
            return _requests.Values.Where(r => r.SenderId == playerId && !r.IsExpired);
        }

        #endregion

        #region Suggestions

        /// <summary>
        /// Get friend suggestions (mutual friends)
        /// </summary>
        public IEnumerable<string> GetFriendSuggestions(string playerId, int maxSuggestions = 10)
        {
            var suggestions = new HashSet<string>();
            var myFriends = GetFriends(playerId).Select(f => f.PlayerId).ToHashSet();

            // Find friends of friends
            foreach (var friendId in myFriends)
            {
                var friendsOfFriend = GetFriends(friendId).Select(f => f.PlayerId);

                foreach (var suggestedId in friendsOfFriend)
                {
                    // Don't suggest self or existing friends
                    if (suggestedId != playerId && !myFriends.Contains(suggestedId))
                    {
                        suggestions.Add(suggestedId);

                        if (suggestions.Count >= maxSuggestions)
                            return suggestions;
                    }
                }
            }

            return suggestions;
        }

        #endregion

        #region Maintenance

        /// <summary>
        /// Update friend system (cleanup expired requests)
        /// </summary>
        public void Update()
        {
            // Remove expired requests
            var expired = _requests.Values
                .Where(r => r.IsExpired)
                .Select(r => r.RequestId)
                .ToList();

            foreach (var requestId in expired)
            {
                _requests.TryRemove(requestId, out _);
            }
        }

        /// <summary>
        /// Clear all data for player
        /// </summary>
        public void ClearPlayerData(string playerId)
        {
            // Remove all friendships
            if (_friendships.TryGetValue(playerId, out var friendList))
            {
                var friends = new List<string>();
                lock (friendList)
                {
                    friends.AddRange(friendList);
                }

                foreach (var friendId in friends)
                {
                    RemoveFriend(playerId, friendId);
                }
            }

            _friendships.TryRemove(playerId, out _);
            _friendData.TryRemove(playerId, out _);

            // Remove all requests
            var requestsToRemove = _requests.Values
                .Where(r => r.SenderId == playerId || r.ReceiverId == playerId)
                .Select(r => r.RequestId)
                .ToList();

            foreach (var requestId in requestsToRemove)
            {
                _requests.TryRemove(requestId, out _);
            }
        }

        #endregion

        #region Statistics

        /// <summary>
        /// Get friend system statistics
        /// </summary>
        public Dictionary<string, object> GetStatistics()
        {
            var totalPlayers = _friendships.Count;
            var totalFriendships = TotalFriendships;
            var avgFriendsPerPlayer = totalPlayers > 0 ? (float)totalFriendships / totalPlayers : 0;

            return new Dictionary<string, object>
            {
                ["totalPlayers"] = totalPlayers,
                ["totalFriendships"] = totalFriendships,
                ["totalRequests"] = TotalRequests,
                ["averageFriendsPerPlayer"] = avgFriendsPerPlayer,
                ["maxFriends"] = MaxFriends
            };
        }

        #endregion
    }
}
