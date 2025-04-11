using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.Json;
using System.Text.Json.Serialization;
using System.Threading.Tasks;

namespace KenshiMultiplayer
{
    public enum FriendshipStatus
    {
        Pending,
        Accepted,
        Blocked
    }

    public class FriendRelation
    {
        public string Username { get; set; }
        public FriendshipStatus Status { get; set; }
        public DateTime CreatedAt { get; set; }
        public DateTime? LastSeen { get; set; }

        [JsonIgnore]
        public bool IsOnline { get; set; }

        public FriendRelation()
        {
            CreatedAt = DateTime.UtcNow;
        }

        public FriendRelation(string username, FriendshipStatus status = FriendshipStatus.Pending)
        {
            Username = username;
            Status = status;
            CreatedAt = DateTime.UtcNow;
        }
    }

    public class FriendsManager
    {
        private readonly Dictionary<string, FriendRelation> friends = new Dictionary<string, FriendRelation>();
        private readonly Dictionary<string, List<string>> incomingRequests = new Dictionary<string, List<string>>();
        private readonly Dictionary<string, List<string>> outgoingRequests = new Dictionary<string, List<string>>();

        private readonly string dataFilePath;
        private readonly EnhancedClient client;
        private readonly EnhancedServer server;

        // Server-side constructor
        public FriendsManager(EnhancedServer serverInstance, string dataDirectory = "data")
        {
            server = serverInstance;
            dataFilePath = Path.Combine(dataDirectory, "friends.json");
            Directory.CreateDirectory(dataDirectory);

            LoadData();

            // Subscribe to user connection events 
            // (this would be implemented in the EnhancedServer class)
            // server.UserConnected += OnUserConnected;
            // server.UserDisconnected += OnUserDisconnected;
        }

        // Client-side constructor
        public FriendsManager(EnhancedClient clientInstance, string dataDirectory = "data")
        {
            client = clientInstance;
            dataFilePath = Path.Combine(dataDirectory, "friends.json");
            Directory.CreateDirectory(dataDirectory);

            LoadData();

            // Subscribe to client message events
            if (client != null)
            {
                client.MessageReceived += OnMessageReceived;
            }
        }

        private void LoadData()
        {
            try
            {
                if (File.Exists(dataFilePath))
                {
                    string json = File.ReadAllText(dataFilePath);
                    var data = JsonSerializer.Deserialize<FriendsData>(json);

                    if (data != null)
                    {
                        if (data.Friends != null)
                        {
                            foreach (var friend in data.Friends)
                            {
                                friends[friend.Username] = friend;
                            }
                        }

                        if (data.IncomingRequests != null)
                        {
                            incomingRequests = data.IncomingRequests;
                        }

                        if (data.OutgoingRequests != null)
                        {
                            outgoingRequests = data.OutgoingRequests;
                        }
                    }

                    Logger.Log($"Loaded {friends.Count} friends, {incomingRequests.Count} incoming requests, {outgoingRequests.Count} outgoing requests");
                }
            }
            catch (Exception ex)
            {
                Logger.Log($"Error loading friends data: {ex.Message}");
            }
        }

        private void SaveData()
        {
            try
            {
                var data = new FriendsData
                {
                    Friends = friends.Values.ToList(),
                    IncomingRequests = incomingRequests,
                    OutgoingRequests = outgoingRequests
                };

                string json = JsonSerializer.Serialize(data, new JsonSerializerOptions { WriteIndented = true });
                File.WriteAllText(dataFilePath, json);
            }
            catch (Exception ex)
            {
                Logger.Log($"Error saving friends data: {ex.Message}");
            }
        }

        // Client message handling
        private void OnMessageReceived(object sender, GameMessage message)
        {
            if (message.Type == MessageType.FriendRequest)
            {
                HandleFriendRequest(message);
            }
            else if (message.Type == MessageType.FriendAccept)
            {
                HandleFriendAccept(message);
            }
            else if (message.Type == MessageType.FriendDecline)
            {
                HandleFriendDecline(message);
            }
            else if (message.Type == MessageType.FriendRemove)
            {
                HandleFriendRemove(message);
            }
            else if (message.Type == MessageType.FriendStatus)
            {
                HandleFriendStatusUpdate(message);
            }
        }

        // Friend Request Methods

        // Send a friend request to another player
        public bool SendFriendRequest(string username)
        {
            if (string.IsNullOrWhiteSpace(username))
                return false;

            // Don't send request to yourself
            if (username == client.CurrentUsername)
                return false;

            // Check if already friends
            if (friends.ContainsKey(username))
                return false;

            // Check if already sent request
            if (IsOutgoingRequest(client.CurrentUsername, username))
                return false;

            // Create the request message
            var requestMessage = new GameMessage
            {
                Type = MessageType.FriendRequest,
                PlayerId = client.CurrentUsername,
                Data = new Dictionary<string, object>
                {
                    { "targetUsername", username }
                },
                SessionId = client.AuthToken
            };

            // Send the request to the server
            client.SendMessageToServer(requestMessage);

            // Track outgoing request locally
            if (!outgoingRequests.ContainsKey(client.CurrentUsername))
            {
                outgoingRequests[client.CurrentUsername] = new List<string>();
            }

            if (!outgoingRequests[client.CurrentUsername].Contains(username))
            {
                outgoingRequests[client.CurrentUsername].Add(username);
                SaveData();
            }

            return true;
        }

        // Accept a friend request from another player
        public bool AcceptFriendRequest(string username)
        {
            if (string.IsNullOrWhiteSpace(username))
                return false;

            // Check if we have an incoming request from this user
            if (!IsIncomingRequest(client.CurrentUsername, username))
                return false;

            // Create the accept message
            var acceptMessage = new GameMessage
            {
                Type = MessageType.FriendAccept,
                PlayerId = client.CurrentUsername,
                Data = new Dictionary<string, object>
                {
                    { "targetUsername", username }
                },
                SessionId = client.AuthToken
            };

            // Send the accept message to the server
            client.SendMessageToServer(acceptMessage);

            // Update local state
            RemoveIncomingRequest(client.CurrentUsername, username);

            // Add to friends
            friends[username] = new FriendRelation(username, FriendshipStatus.Accepted);
            SaveData();

            return true;
        }

        // Decline a friend request from another player
        public bool DeclineFriendRequest(string username)
        {
            if (string.IsNullOrWhiteSpace(username))
                return false;

            // Check if we have an incoming request from this user
            if (!IsIncomingRequest(client.CurrentUsername, username))
                return false;

            // Create the decline message
            var declineMessage = new GameMessage
            {
                Type = MessageType.FriendDecline,
                PlayerId = client.CurrentUsername,
                Data = new Dictionary<string, object>
                {
                    { "targetUsername", username }
                },
                SessionId = client.AuthToken
            };

            // Send the decline message to the server
            client.SendMessageToServer(declineMessage);

            // Update local state
            RemoveIncomingRequest(client.CurrentUsername, username);
            SaveData();

            return true;
        }

        // Remove a friend
        public bool RemoveFriend(string username)
        {
            if (string.IsNullOrWhiteSpace(username))
                return false;

            if (!friends.ContainsKey(username))
                return false;

            // Create the remove friend message
            var removeMessage = new GameMessage
            {
                Type = MessageType.FriendRemove,
                PlayerId = client.CurrentUsername,
                Data = new Dictionary<string, object>
                {
                    { "targetUsername", username }
                },
                SessionId = client.AuthToken
            };

            // Send the remove message to the server
            client.SendMessageToServer(removeMessage);

            // Update local state
            friends.Remove(username);
            SaveData();

            return true;
        }

        // Block a user
        public bool BlockUser(string username)
        {
            if (string.IsNullOrWhiteSpace(username))
                return false;

            // If already friends, update status to blocked
            if (friends.ContainsKey(username))
            {
                friends[username].Status = FriendshipStatus.Blocked;
            }
            else
            {
                // Add as blocked
                friends[username] = new FriendRelation(username, FriendshipStatus.Blocked);
            }

            // Remove any pending requests
            RemoveIncomingRequest(client.CurrentUsername, username);
            RemoveOutgoingRequest(client.CurrentUsername, username);

            // Create the block message
            var blockMessage = new GameMessage
            {
                Type = MessageType.FriendBlock,
                PlayerId = client.CurrentUsername,
                Data = new Dictionary<string, object>
                {
                    { "targetUsername", username }
                },
                SessionId = client.AuthToken
            };

            // Send the block message to the server
            client.SendMessageToServer(blockMessage);

            SaveData();
            return true;
        }

        // Get list of friends
        public List<FriendRelation> GetFriends()
        {
            return friends.Values
                .Where(f => f.Status == FriendshipStatus.Accepted)
                .ToList();
        }

        // Get list of blocked users
        public List<FriendRelation> GetBlockedUsers()
        {
            return friends.Values
                .Where(f => f.Status == FriendshipStatus.Blocked)
                .ToList();
        }

        // Get list of incoming friend requests
        public List<string> GetIncomingRequests(string username)
        {
            if (incomingRequests.TryGetValue(username, out var requests))
            {
                return requests;
            }
            return new List<string>();
        }

        // Get list of outgoing friend requests
        public List<string> GetOutgoingRequests(string username)
        {
            if (outgoingRequests.TryGetValue(username, out var requests))
            {
                return requests;
            }
            return new List<string>();
        }

        // Check if a user is blocked
        public bool IsBlocked(string username)
        {
            return friends.TryGetValue(username, out var relation) &&
                relation.Status == FriendshipStatus.Blocked;
        }

        // Check if we have an incoming request from a user
        private bool IsIncomingRequest(string receiver, string sender)
        {
            return incomingRequests.TryGetValue(receiver, out var requests) &&
                requests.Contains(sender);
        }

        // Check if we have sent a request to a user
        private bool IsOutgoingRequest(string sender, string receiver)
        {
            return outgoingRequests.TryGetValue(sender, out var requests) &&
                requests.Contains(receiver);
        }

        // Remove an incoming request
        private void RemoveIncomingRequest(string receiver, string sender)
        {
            if (incomingRequests.TryGetValue(receiver, out var requests))
            {
                requests.Remove(sender);
            }
        }

        // Remove an outgoing request
        private void RemoveOutgoingRequest(string sender, string receiver)
        {
            if (outgoingRequests.TryGetValue(sender, out var requests))
            {
                requests.Remove(receiver);
            }
        }

        // Message handlers
        private void HandleFriendRequest(GameMessage message)
        {
            string sender = message.PlayerId;

            // Add to incoming requests
            if (!incomingRequests.ContainsKey(client.CurrentUsername))
            {
                incomingRequests[client.CurrentUsername] = new List<string>();
            }

            if (!incomingRequests[client.CurrentUsername].Contains(sender))
            {
                incomingRequests[client.CurrentUsername].Add(sender);
                SaveData();
            }
        }

        private void HandleFriendAccept(GameMessage message)
        {
            string sender = message.PlayerId;

            // Remove from outgoing requests
            RemoveOutgoingRequest(client.CurrentUsername, sender);

            // Add to friends
            friends[sender] = new FriendRelation(sender, FriendshipStatus.Accepted);
            SaveData();
        }

        private void HandleFriendDecline(GameMessage message)
        {
            string sender = message.PlayerId;

            // Remove from outgoing requests
            RemoveOutgoingRequest(client.CurrentUsername, sender);
            SaveData();
        }

        private void HandleFriendRemove(GameMessage message)
        {
            string sender = message.PlayerId;

            // Remove from friends
            friends.Remove(sender);
            SaveData();
        }

        private void HandleFriendStatusUpdate(GameMessage message)
        {
            string username = message.PlayerId;
            bool isOnline = (bool)message.Data["isOnline"];

            if (friends.TryGetValue(username, out var friend))
            {
                friend.IsOnline = isOnline;

                if (!isOnline)
                {
                    friend.LastSeen = DateTime.UtcNow;
                }

                SaveData();
            }
        }
    }

    public class FriendsData
    {
        public List<FriendRelation> Friends { get; set; } = new List<FriendRelation>();
        public Dictionary<string, List<string>> IncomingRequests { get; set; } = new Dictionary<string, List<string>>();
        public Dictionary<string, List<string>> OutgoingRequests { get; set; } = new Dictionary<string, List<string>>();
    }
}