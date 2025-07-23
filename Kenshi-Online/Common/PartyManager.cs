using KenshiMultiplayer.Networking.Player;
using KenshiMultiplayer.Networking;
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.Json;

namespace KenshiMultiplayer.Common.PartyManager
{
    public enum PartyRole
    {
        Leader,
        Officer,
        Member
    }

    public class PartyMember
    {
        public string Username { get; set; }
        public PartyRole Role { get; set; }
        public DateTime JoinedAt { get; set; }
        public bool IsOnline { get; set; }
        public Position LastKnownPosition { get; set; }
        public int Health { get; set; }
        public int MaxHealth { get; set; }
        public PlayerState CurrentState { get; set; }
        public bool SharesLoot { get; set; } = true;
        public bool SharesExperience { get; set; } = true;
    }

    public class Party
    {
        public string Id { get; set; } = Guid.NewGuid().ToString();
        public string Name { get; set; }
        public Dictionary<string, PartyMember> Members { get; set; } = new Dictionary<string, PartyMember>();
        public int MaxSize { get; set; } = 8;
        public DateTime CreatedAt { get; set; } = DateTime.UtcNow;
        public string BaseLocationId { get; set; }
        public bool IsPublic { get; set; } = false;
        public string Description { get; set; }
        public Dictionary<string, object> SharedInventory { get; set; } = new Dictionary<string, object>();
        public List<string> SharedWaypoints { get; set; } = new List<string>();

        // Party settings
        public bool FriendlyFire { get; set; } = false;
        public bool SharedVision { get; set; } = true;
        public float ExperienceSharingRange { get; set; } = 100f;
        public bool AutoAcceptFriends { get; set; } = false;

        // Party statistics
        public int TotalKills { get; set; }
        public int TotalDeaths { get; set; }
        public long TotalExperienceGained { get; set; }
        public Dictionary<string, int> ResourcesGathered { get; set; } = new Dictionary<string, int>();
    }

    public class PartyManager
    {
        private readonly Dictionary<string, Party> parties = new Dictionary<string, Party>();
        private readonly Dictionary<string, string> playerToParty = new Dictionary<string, string>();
        private readonly string dataFilePath;
        private readonly EnhancedClient client;
        private readonly EnhancedServer server;

        // Events
        public event EventHandler<Party> PartyCreated;
        public event EventHandler<Party> PartyDisbanded;
        public event EventHandler<PartyMember> MemberJoined;
        public event EventHandler<PartyMember> MemberLeft;
        public event EventHandler<Party> PartyUpdated;

        // Client-side constructor
        public PartyManager(EnhancedClient clientInstance, string dataDirectory = "data")
        {
            client = clientInstance;
            dataFilePath = Path.Combine(dataDirectory, "parties.json");
            Directory.CreateDirectory(dataDirectory);

            LoadData();

            if (client != null)
            {
                client.MessageReceived += OnMessageReceived;
            }
        }

        // Server-side constructor
        public PartyManager(EnhancedServer serverInstance, string dataDirectory = "data")
        {
            server = serverInstance;
            dataFilePath = Path.Combine(dataDirectory, "parties.json");
            Directory.CreateDirectory(dataDirectory);

            LoadData();
        }

        private void LoadData()
        {
            try
            {
                if (File.Exists(dataFilePath))
                {
                    string json = File.ReadAllText(dataFilePath);
                    var data = JsonSerializer.Deserialize<PartyData>(json);

                    if (data != null)
                    {
                        foreach (var party in data.Parties)
                        {
                            parties[party.Id] = party;
                            foreach (var member in party.Members.Keys)
                            {
                                playerToParty[member] = party.Id;
                            }
                        }
                    }

                    Logger.Log($"Loaded {parties.Count} parties");
                }
            }
            catch (Exception ex)
            {
                Logger.Log($"Error loading party data: {ex.Message}");
            }
        }

        private void SaveData()
        {
            try
            {
                var data = new PartyData
                {
                    Parties = parties.Values.ToList()
                };

                string json = JsonSerializer.Serialize(data, new JsonSerializerOptions { WriteIndented = true });
                File.WriteAllText(dataFilePath, json);
            }
            catch (Exception ex)
            {
                Logger.Log($"Error saving party data: {ex.Message}");
            }
        }

        // Create a new party
        public Party CreateParty(string name, string description = "")
        {
            // Check if player is already in a party
            if (playerToParty.ContainsKey(client.CurrentUsername))
                return null;

            var party = new Party
            {
                Name = name,
                Description = description
            };

            // Add creator as leader
            var leader = new PartyMember
            {
                Username = client.CurrentUsername,
                Role = PartyRole.Leader,
                JoinedAt = DateTime.UtcNow,
                IsOnline = true
            };

            party.Members[client.CurrentUsername] = leader;
            parties[party.Id] = party;
            playerToParty[client.CurrentUsername] = party.Id;

            // Send creation message
            var message = new GameMessage
            {
                Type = "party_create",
                PlayerId = client.CurrentUsername,
                Data = new Dictionary<string, object>
                {
                    { "party", JsonSerializer.Serialize(party) }
                },
                SessionId = client.AuthToken
            };

            client.SendMessageToServer(message);

            SaveData();
            PartyCreated?.Invoke(this, party);

            return party;
        }

        // Join an existing party
        public bool JoinParty(string partyId, string password = "")
        {
            // Check if player is already in a party
            if (playerToParty.ContainsKey(client.CurrentUsername))
                return false;

            if (!parties.TryGetValue(partyId, out var party))
                return false;

            if (party.Members.Count >= party.MaxSize)
                return false;

            var message = new GameMessage
            {
                Type = "party_join",
                PlayerId = client.CurrentUsername,
                Data = new Dictionary<string, object>
                {
                    { "partyId", partyId },
                    { "password", password }
                },
                SessionId = client.AuthToken
            };

            client.SendMessageToServer(message);

            return true;
        }

        // Leave current party
        public bool LeaveParty()
        {
            if (!playerToParty.TryGetValue(client.CurrentUsername, out string partyId))
                return false;

            var message = new GameMessage
            {
                Type = "party_leave",
                PlayerId = client.CurrentUsername,
                Data = new Dictionary<string, object>
                {
                    { "partyId", partyId }
                },
                SessionId = client.AuthToken
            };

            client.SendMessageToServer(message);

            return true;
        }

        // Invite player to party
        public bool InviteToParty(string username)
        {
            if (!playerToParty.TryGetValue(client.CurrentUsername, out string partyId))
                return false;

            if (!parties.TryGetValue(partyId, out var party))
                return false;

            // Check if user has permission to invite
            var member = party.Members[client.CurrentUsername];
            if (member.Role != PartyRole.Leader && member.Role != PartyRole.Officer)
                return false;

            var message = new GameMessage
            {
                Type = "party_invite",
                PlayerId = client.CurrentUsername,
                Data = new Dictionary<string, object>
                {
                    { "partyId", partyId },
                    { "targetUsername", username }
                },
                SessionId = client.AuthToken
            };

            client.SendMessageToServer(message);

            return true;
        }

        // Promote/demote party member
        public bool SetMemberRole(string username, PartyRole newRole)
        {
            if (!playerToParty.TryGetValue(client.CurrentUsername, out string partyId))
                return false;

            if (!parties.TryGetValue(partyId, out var party))
                return false;

            // Only leader can change roles
            var member = party.Members[client.CurrentUsername];
            if (member.Role != PartyRole.Leader)
                return false;

            if (!party.Members.ContainsKey(username))
                return false;

            var message = new GameMessage
            {
                Type = "party_set_role",
                PlayerId = client.CurrentUsername,
                Data = new Dictionary<string, object>
                {
                    { "partyId", partyId },
                    { "targetUsername", username },
                    { "role", newRole.ToString() }
                },
                SessionId = client.AuthToken
            };

            client.SendMessageToServer(message);

            return true;
        }

        // Kick member from party
        public bool KickMember(string username)
        {
            if (!playerToParty.TryGetValue(client.CurrentUsername, out string partyId))
                return false;

            if (!parties.TryGetValue(partyId, out var party))
                return false;

            // Check permissions
            var member = party.Members[client.CurrentUsername];
            if (member.Role != PartyRole.Leader && member.Role != PartyRole.Officer)
                return false;

            var message = new GameMessage
            {
                Type = "party_kick",
                PlayerId = client.CurrentUsername,
                Data = new Dictionary<string, object>
                {
                    { "partyId", partyId },
                    { "targetUsername", username }
                },
                SessionId = client.AuthToken
            };

            client.SendMessageToServer(message);

            return true;
        }

        // Disband party (leader only)
        public bool DisbandParty()
        {
            if (!playerToParty.TryGetValue(client.CurrentUsername, out string partyId))
                return false;

            if (!parties.TryGetValue(partyId, out var party))
                return false;

            // Only leader can disband
            var member = party.Members[client.CurrentUsername];
            if (member.Role != PartyRole.Leader)
                return false;

            var message = new GameMessage
            {
                Type = "party_disband",
                PlayerId = client.CurrentUsername,
                Data = new Dictionary<string, object>
                {
                    { "partyId", partyId }
                },
                SessionId = client.AuthToken
            };

            client.SendMessageToServer(message);

            return true;
        }

        // Update party settings
        public bool UpdatePartySettings(Dictionary<string, object> settings)
        {
            if (!playerToParty.TryGetValue(client.CurrentUsername, out string partyId))
                return false;

            if (!parties.TryGetValue(partyId, out var party))
                return false;

            // Check permissions
            var member = party.Members[client.CurrentUsername];
            if (member.Role != PartyRole.Leader && member.Role != PartyRole.Officer)
                return false;

            var message = new GameMessage
            {
                Type = "party_update_settings",
                PlayerId = client.CurrentUsername,
                Data = new Dictionary<string, object>
                {
                    { "partyId", partyId },
                    { "settings", settings }
                },
                SessionId = client.AuthToken
            };

            client.SendMessageToServer(message);

            return true;
        }

        // Share waypoint with party
        public bool ShareWaypoint(float x, float y, float z, string name)
        {
            if (!playerToParty.TryGetValue(client.CurrentUsername, out string partyId))
                return false;

            var message = new GameMessage
            {
                Type = "party_waypoint",
                PlayerId = client.CurrentUsername,
                Data = new Dictionary<string, object>
                {
                    { "partyId", partyId },
                    { "x", x },
                    { "y", y },
                    { "z", z },
                    { "name", name }
                },
                SessionId = client.AuthToken
            };

            client.SendMessageToServer(message);

            return true;
        }

        // Get current party
        public Party GetCurrentParty()
        {
            if (!playerToParty.TryGetValue(client.CurrentUsername, out string partyId))
                return null;

            parties.TryGetValue(partyId, out var party);
            return party;
        }

        // Get all public parties
        public List<Party> GetPublicParties()
        {
            return parties.Values.Where(p => p.IsPublic && p.Members.Count < p.MaxSize).ToList();
        }

        // Get party member info
        public PartyMember GetMemberInfo(string username)
        {
            var party = GetCurrentParty();
            if (party == null)
                return null;

            party.Members.TryGetValue(username, out var member);
            return member;
        }

        // Check if player is in same party
        public bool IsInSameParty(string username)
        {
            if (!playerToParty.TryGetValue(client.CurrentUsername, out string myPartyId))
                return false;

            if (!playerToParty.TryGetValue(username, out string theirPartyId))
                return false;

            return myPartyId == theirPartyId;
        }

        // Message handlers
        private void OnMessageReceived(object sender, GameMessage message)
        {
            switch (message.Type)
            {
                case "party_update":
                    HandlePartyUpdate(message);
                    break;
                case "party_member_joined":
                    HandleMemberJoined(message);
                    break;
                case "party_member_left":
                    HandleMemberLeft(message);
                    break;
                case "party_disbanded":
                    HandlePartyDisbanded(message);
                    break;
                case "party_member_update":
                    HandleMemberUpdate(message);
                    break;
            }
        }

        private void HandlePartyUpdate(GameMessage message)
        {
            if (message.Data.TryGetValue("party", out var partyObj))
            {
                var party = JsonSerializer.Deserialize<Party>(partyObj.ToString());
                parties[party.Id] = party;

                // Update player mappings
                foreach (var member in party.Members.Keys)
                {
                    playerToParty[member] = party.Id;
                }

                SaveData();
                PartyUpdated?.Invoke(this, party);
            }
        }

        private void HandleMemberJoined(GameMessage message)
        {
            if (message.Data.TryGetValue("partyId", out var partyIdObj) &&
                message.Data.TryGetValue("member", out var memberObj))
            {
                string partyId = partyIdObj.ToString();
                var member = JsonSerializer.Deserialize<PartyMember>(memberObj.ToString());

                if (parties.TryGetValue(partyId, out var party))
                {
                    party.Members[member.Username] = member;
                    playerToParty[member.Username] = partyId;

                    SaveData();
                    MemberJoined?.Invoke(this, member);
                }
            }
        }

        private void HandleMemberLeft(GameMessage message)
        {
            if (message.Data.TryGetValue("partyId", out var partyIdObj) &&
                message.Data.TryGetValue("username", out var usernameObj))
            {
                string partyId = partyIdObj.ToString();
                string username = usernameObj.ToString();

                if (parties.TryGetValue(partyId, out var party))
                {
                    if (party.Members.TryGetValue(username, out var member))
                    {
                        party.Members.Remove(username);
                        playerToParty.Remove(username);

                        SaveData();
                        MemberLeft?.Invoke(this, member);
                    }
                }
            }
        }

        private void HandlePartyDisbanded(GameMessage message)
        {
            if (message.Data.TryGetValue("partyId", out var partyIdObj))
            {
                string partyId = partyIdObj.ToString();

                if (parties.TryGetValue(partyId, out var party))
                {
                    // Remove all player mappings
                    foreach (var member in party.Members.Keys)
                    {
                        playerToParty.Remove(member);
                    }

                    parties.Remove(partyId);

                    SaveData();
                    PartyDisbanded?.Invoke(this, party);
                }
            }
        }

        private void HandleMemberUpdate(GameMessage message)
        {
            if (message.Data.TryGetValue("partyId", out var partyIdObj) &&
                message.Data.TryGetValue("username", out var usernameObj) &&
                message.Data.TryGetValue("update", out var updateObj))
            {
                string partyId = partyIdObj.ToString();
                string username = usernameObj.ToString();
                var update = JsonSerializer.Deserialize<Dictionary<string, object>>(updateObj.ToString());

                if (parties.TryGetValue(partyId, out var party) &&
                    party.Members.TryGetValue(username, out var member))
                {
                    // Update member properties
                    if (update.ContainsKey("position"))
                    {
                        member.LastKnownPosition = JsonSerializer.Deserialize<Position>(update["position"].ToString());
                    }

                    if (update.ContainsKey("health"))
                    {
                        member.Health = Convert.ToInt32(update["health"]);
                    }

                    if (update.ContainsKey("state"))
                    {
                        member.CurrentState = Enum.Parse<PlayerState>(update["state"].ToString());
                    }

                    SaveData();
                }
            }
        }
    }

    public class PartyData
    {
        public List<Party> Parties { get; set; } = new List<Party>();
    }
}