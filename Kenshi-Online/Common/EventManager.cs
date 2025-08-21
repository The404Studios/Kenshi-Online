using KenshiMultiplayer.Common;
using KenshiMultiplayer.Networking.Player;
using KenshiMultiplayer.Networking;
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.Json;
using System.Threading.Tasks;

namespace KenshiMultiplayer
{
    public enum EventType
    {
        Raid,
        BaseDefense,
        TradingCaravan,
        BountyHunt,
        ResourceGathering,
        Exploration,
        Training,
        Custom
    }

    public enum EventStatus
    {
        Planned,
        Starting,
        InProgress,
        Completed,
        Cancelled,
        Failed
    }

    public class EventParticipant
    {
        public string Username { get; set; }
        public bool IsReady { get; set; }
        public string Role { get; set; } // Tank, DPS, Scout, Medic, etc.
        public DateTime JoinedAt { get; set; }
        public Dictionary<string, int> ContributionStats { get; set; } = new Dictionary<string, int>();
        public bool HasCompletedObjectives { get; set; }
    }

    public class EventObjective
    {
        public string Id { get; set; } = Guid.NewGuid().ToString();
        public string Name { get; set; }
        public string Description { get; set; }
        public bool IsCompleted { get; set; }
        public bool IsRequired { get; set; } = true;
        public Dictionary<string, object> Requirements { get; set; } = new Dictionary<string, object>();
        public Dictionary<string, object> Progress { get; set; } = new Dictionary<string, object>();
        public List<string> CompletedBy { get; set; } = new List<string>();
    }

    public class EventReward
    {
        public string ItemId { get; set; }
        public string ItemName { get; set; }
        public int Quantity { get; set; }
        public float DropChance { get; set; } = 1.0f;
        public string RecipientUsername { get; set; } // Null for shared loot
    }

    public class WorldEvent
    {
        public string Id { get; set; } = Guid.NewGuid().ToString();
        public string Name { get; set; }
        public string Description { get; set; }
        public EventType Type { get; set; }
        public EventStatus Status { get; set; } = EventStatus.Planned;
        public string CreatorUsername { get; set; }
        public DateTime CreatedAt { get; set; } = DateTime.UtcNow;
        public DateTime ScheduledStartTime { get; set; }
        public DateTime? ActualStartTime { get; set; }
        public DateTime? EndTime { get; set; }

        // Location
        public Position Location { get; set; }
        public string LocationName { get; set; }
        public float Radius { get; set; } = 50f;

        // Participants
        public Dictionary<string, EventParticipant> Participants { get; set; } = new Dictionary<string, EventParticipant>();
        public int MinParticipants { get; set; } = 1;
        public int MaxParticipants { get; set; } = 20;
        public List<string> InvitedPlayers { get; set; } = new List<string>();
        public bool IsPublic { get; set; } = true;

        // Requirements
        public int MinLevel { get; set; } = 1;
        public Dictionary<string, float> RequiredSkills { get; set; } = new Dictionary<string, float>();
        public List<string> RequiredItems { get; set; } = new List<string>();

        // Objectives
        public List<EventObjective> Objectives { get; set; } = new List<EventObjective>();

        // Rewards
        public List<EventReward> Rewards { get; set; } = new List<EventReward>();
        public int ExperienceReward { get; set; }
        public int CurrencyReward { get; set; }

        // Event-specific data
        public Dictionary<string, object> CustomData { get; set; } = new Dictionary<string, object>();

        // Statistics
        public int TotalDamageDealt { get; set; }
        public int TotalDamageTaken { get; set; }
        public int TotalHealing { get; set; }
        public Dictionary<string, int> EnemiesKilled { get; set; } = new Dictionary<string, int>();
        public List<string> DeathLog { get; set; } = new List<string>();
    }

    public class EventManager
    {
        private readonly Dictionary<string, WorldEvent> activeEvents = new Dictionary<string, WorldEvent>();
        private readonly Dictionary<string, List<WorldEvent>> completedEvents = new Dictionary<string, List<WorldEvent>>();
        private readonly Dictionary<string, List<string>> playerEventHistory = new Dictionary<string, List<string>>();

        private readonly string dataFilePath;
        private readonly EnhancedClient client;
        private readonly EnhancedServer server;

        // Events
        public event EventHandler<WorldEvent> EventCreated;
        public event EventHandler<WorldEvent> EventStarted;
        public event EventHandler<WorldEvent> EventCompleted;
        public event EventHandler<WorldEvent> EventCancelled;
        public event EventHandler<(WorldEvent, EventParticipant)> ParticipantJoined;
        public event EventHandler<(WorldEvent, string)> ParticipantLeft;
        public event EventHandler<(WorldEvent, EventObjective)> ObjectiveCompleted;

        // Client-side constructor
        public EventManager(EnhancedClient clientInstance, string dataDirectory = "data")
        {
            client = clientInstance;
            dataFilePath = Path.Combine(dataDirectory, "events.json");
            Directory.CreateDirectory(dataDirectory);

            LoadData(GetCompletedEvents(), GetPlayerEventHistory());

            if (client != null)
            {
                client.MessageReceived += OnMessageReceived;
            }

            // Start periodic check for scheduled events
            Task.Run(async () => {
                while (true)
                {
                    await Task.Delay(TimeSpan.FromSeconds(30));
                    CheckScheduledEvents();
                }
            });
        }

        // Server-side constructor
        public EventManager(EnhancedServer serverInstance, string dataDirectory = "data")
        {
            server = serverInstance;
            dataFilePath = Path.Combine(dataDirectory, "events.json");
            Directory.CreateDirectory(dataDirectory);

            LoadData(GetCompletedEvents(), GetPlayerEventHistory());
        }

        private Dictionary<string, List<WorldEvent>> GetCompletedEvents()
        {
            return completedEvents;
        }

        private Dictionary<string, List<string>> GetPlayerEventHistory()
        {
            return playerEventHistory;
        }

        private void LoadData(Dictionary<string, List<WorldEvent>> completedEvents, Dictionary<string, List<string>> playerEventHistory)
        {
            try
            {
                if (File.Exists(dataFilePath))
                {
                    string json = File.ReadAllText(dataFilePath);
                    var data = JsonSerializer.Deserialize<EventData>(json);

                    if (data != null)
                    {
                        foreach (var evt in data.ActiveEvents)
                        {
                            activeEvents[evt.Id] = evt;
                        }

                        completedEvents = data.CompletedEvents ?? new Dictionary<string, List<WorldEvent>>();
                        playerEventHistory = data.PlayerEventHistory ?? new Dictionary<string, List<string>>();
                    }

                    Logger.Log($"Loaded {activeEvents.Count} active events");
                }
            }
            catch (Exception ex)
            {
                Logger.Log($"Error loading event data: {ex.Message}");
            }
        }

        private void SaveData()
        {
            try
            {
                var data = new EventData
                {
                    ActiveEvents = activeEvents.Values.ToList(),
                    CompletedEvents = completedEvents,
                    PlayerEventHistory = playerEventHistory
                };

                string json = JsonSerializer.Serialize(data, new JsonSerializerOptions { WriteIndented = true });
                File.WriteAllText(dataFilePath, json);
            }
            catch (Exception ex)
            {
                Logger.Log($"Error saving event data: {ex.Message}");
            }
        }

        // Create a new event
        public WorldEvent CreateEvent(string name, string description, EventType type, DateTime scheduledStart, Position location)
        {
            var worldEvent = new WorldEvent
            {
                Name = name,
                Description = description,
                Type = type,
                CreatorUsername = client.CurrentUsername,
                ScheduledStartTime = scheduledStart,
                Location = location
            };

            // Set default objectives based on event type
            switch (type)
            {
                case EventType.Raid:
                    worldEvent.Objectives.Add(new EventObjective
                    {
                        Name = "Defeat Enemy Leader",
                        Description = "Eliminate the enemy faction leader",
                        IsRequired = true
                    });
                    worldEvent.Objectives.Add(new EventObjective
                    {
                        Name = "Minimize Casualties",
                        Description = "Complete the raid with less than 3 deaths",
                        IsRequired = false
                    });
                    break;

                case EventType.BaseDefense:
                    worldEvent.Objectives.Add(new EventObjective
                    {
                        Name = "Defend the Base",
                        Description = "Prevent enemies from breaching the base",
                        IsRequired = true
                    });
                    worldEvent.Objectives.Add(new EventObjective
                    {
                        Name = "Protect Civilians",
                        Description = "Ensure no civilian casualties",
                        IsRequired = false
                    });
                    break;

                case EventType.TradingCaravan:
                    worldEvent.Objectives.Add(new EventObjective
                    {
                        Name = "Reach Destination",
                        Description = "Successfully deliver goods to the destination",
                        IsRequired = true
                    });
                    worldEvent.Objectives.Add(new EventObjective
                    {
                        Name = "Profit Margin",
                        Description = "Achieve at least 50% profit margin",
                        IsRequired = false
                    });
                    break;
            }

            activeEvents[worldEvent.Id] = worldEvent;

            // Send creation message
            var message = new GameMessage
            {
                Type = "event_create",
                PlayerId = client.CurrentUsername,
                Data = new Dictionary<string, object>
                {
                    { "event", JsonSerializer.Serialize(worldEvent) }
                },
                SessionId = client.AuthToken
            };

            client.SendMessageToServer(message);

            SaveData();
            EventCreated?.Invoke(this, worldEvent);

            return worldEvent;
        }

        // Join an event
        public bool JoinEvent(string eventId, string role = "")
        {
            if (!activeEvents.TryGetValue(eventId, out var worldEvent))
                return false;

            if (worldEvent.Status != EventStatus.Planned && worldEvent.Status != EventStatus.Starting)
                return false;

            if (worldEvent.Participants.Count >= worldEvent.MaxParticipants)
                return false;

            if (worldEvent.Participants.ContainsKey(client.CurrentUsername))
                return false;

            var message = new GameMessage
            {
                Type = "event_join",
                PlayerId = client.CurrentUsername,
                Data = new Dictionary<string, object>
                {
                    { "eventId", eventId },
                    { "role", role }
                },
                SessionId = client.AuthToken
            };

            client.SendMessageToServer(message);

            return true;
        }

        // Leave an event
        public bool LeaveEvent(string eventId)
        {
            if (!activeEvents.TryGetValue(eventId, out var worldEvent))
                return false;

            if (!worldEvent.Participants.ContainsKey(client.CurrentUsername))
                return false;

            var message = new GameMessage
            {
                Type = "event_leave",
                PlayerId = client.CurrentUsername,
                Data = new Dictionary<string, object>
                {
                    { "eventId", eventId }
                },
                SessionId = client.AuthToken
            };

            client.SendMessageToServer(message);

            return true;
        }

        // Set ready status
        public bool SetReadyStatus(string eventId, bool isReady)
        {
            if (!activeEvents.TryGetValue(eventId, out var worldEvent))
                return false;

            if (!worldEvent.Participants.ContainsKey(client.CurrentUsername))
                return false;

            var message = new GameMessage
            {
                Type = "event_ready",
                PlayerId = client.CurrentUsername,
                Data = new Dictionary<string, object>
                {
                    { "eventId", eventId },
                    { "isReady", isReady }
                },
                SessionId = client.AuthToken
            };

            client.SendMessageToServer(message);

            return true;
        }

        // Start event (creator only)
        public bool StartEvent(string eventId)
        {
            if (!activeEvents.TryGetValue(eventId, out var worldEvent))
                return false;

            if (worldEvent.CreatorUsername != client.CurrentUsername)
                return false;

            if (worldEvent.Status != EventStatus.Planned)
                return false;

            var message = new GameMessage
            {
                Type = "event_start",
                PlayerId = client.CurrentUsername,
                Data = new Dictionary<string, object>
                {
                    { "eventId", eventId }
                },
                SessionId = client.AuthToken
            };

            client.SendMessageToServer(message);

            return true;
        }

        // Complete an objective
        public bool CompleteObjective(string eventId, string objectiveId)
        {
            if (!activeEvents.TryGetValue(eventId, out var worldEvent))
                return false;

            if (worldEvent.Status != EventStatus.InProgress)
                return false;

            if (!worldEvent.Participants.ContainsKey(client.CurrentUsername))
                return false;

            var message = new GameMessage
            {
                Type = "event_objective_complete",
                PlayerId = client.CurrentUsername,
                Data = new Dictionary<string, object>
                {
                    { "eventId", eventId },
                    { "objectiveId", objectiveId }
                },
                SessionId = client.AuthToken
            };

            client.SendMessageToServer(message);

            return true;
        }

        // Report event statistics
        public bool ReportEventStats(string eventId, Dictionary<string, object> stats)
        {
            if (!activeEvents.TryGetValue(eventId, out var worldEvent))
                return false;

            if (!worldEvent.Participants.ContainsKey(client.CurrentUsername))
                return false;

            var message = new GameMessage
            {
                Type = "event_stats",
                PlayerId = client.CurrentUsername,
                Data = new Dictionary<string, object>
                {
                    { "eventId", eventId },
                    { "stats", stats }
                },
                SessionId = client.AuthToken
            };

            client.SendMessageToServer(message);

            return true;
        }

        // Get active events
        public List<WorldEvent> GetActiveEvents()
        {
            return activeEvents.Values
                .Where(e => e.Status == EventStatus.Planned || e.Status == EventStatus.Starting)
                .OrderBy(e => e.ScheduledStartTime)
                .ToList();
        }

        // Get events in progress
        public List<WorldEvent> GetEventsInProgress()
        {
            return activeEvents.Values
                .Where(e => e.Status == EventStatus.InProgress)
                .ToList();
        }

        // Get my events
        public List<WorldEvent> GetMyEvents()
        {
            return activeEvents.Values
                .Where(e => e.Participants.ContainsKey(client.CurrentUsername))
                .ToList();
        }

        // Get event history
        public List<WorldEvent> GetEventHistory(int limit = 10)
        {
            if (!completedEvents.TryGetValue(client.CurrentUsername, out var events))
                return new List<WorldEvent>();

            return events
                .OrderByDescending(e => e.EndTime)
                .Take(limit)
                .ToList();
        }

        // Check if player meets event requirements
        public bool MeetsRequirements(string eventId, PlayerData playerData)
        {
            if (!activeEvents.TryGetValue(eventId, out var worldEvent))
                return false;

            // Check level
            if (playerData.Level < worldEvent.MinLevel)
                return false;

            // Check skills
            foreach (var skill in worldEvent.RequiredSkills)
            {
                if (!playerData.Skills.TryGetValue(skill.Key, out float playerSkill) || playerSkill < skill.Value)
                    return false;
            }

            // Check items
            foreach (var item in worldEvent.RequiredItems)
            {
                if (!playerData.HasItem(item))
                    return false;
            }

            return true;
        }

        // Check scheduled events
        private void CheckScheduledEvents()
        {
            var now = DateTime.UtcNow;
            var eventsToStart = activeEvents.Values
                .Where(e => e.Status == EventStatus.Planned &&
                           e.ScheduledStartTime <= now &&
                           e.Participants.Count >= e.MinParticipants)
                .ToList();

            foreach (var evt in eventsToStart)
            {
                // Auto-start events that are past their scheduled time
                if (evt.CreatorUsername == client.CurrentUsername)
                {
                    StartEvent(evt.Id);
                }
            }
        }

        // Message handlers
        private void OnMessageReceived(object sender, GameMessage message)
        {
            switch (message.Type)
            {
                case "event_update":
                    HandleEventUpdate(message);
                    break;
                case "event_started":
                    HandleEventStarted(message);
                    break;
                case "event_completed":
                    HandleEventCompleted(message);
                    break;
                case "event_cancelled":
                    HandleEventCancelled(message);
                    break;
                case "event_participant_joined":
                    HandleParticipantJoined(message);
                    break;
                case "event_participant_left":
                    HandleParticipantLeft(message);
                    break;
                case "event_objective_completed":
                    HandleObjectiveCompleted(message);
                    break;
            }
        }

        private void HandleEventUpdate(GameMessage message)
        {
            if (message.Data.TryGetValue("event", out var eventObj))
            {
                var worldEvent = JsonSerializer.Deserialize<WorldEvent>(eventObj.ToString());
                activeEvents[worldEvent.Id] = worldEvent;
                SaveData();
            }
        }

        private void HandleEventStarted(GameMessage message)
        {
            if (message.Data.TryGetValue("eventId", out var eventIdObj))
            {
                string eventId = eventIdObj.ToString();
                if (activeEvents.TryGetValue(eventId, out var worldEvent))
                {
                    worldEvent.Status = EventStatus.InProgress;
                    worldEvent.ActualStartTime = DateTime.UtcNow;
                    SaveData();
                    EventStarted?.Invoke(this, worldEvent);
                }
            }
        }

        private void HandleEventCompleted(GameMessage message)
        {
            if (message.Data.TryGetValue("eventId", out var eventIdObj))
            {
                string eventId = eventIdObj.ToString();
                if (activeEvents.TryGetValue(eventId, out var worldEvent))
                {
                    worldEvent.Status = EventStatus.Completed;
                    worldEvent.EndTime = DateTime.UtcNow;

                    // Move to completed events
                    foreach (var participant in worldEvent.Participants.Keys)
                    {
                        if (!completedEvents.ContainsKey(participant))
                            completedEvents[participant] = new List<WorldEvent>();

                        completedEvents[participant].Add(worldEvent);

                        if (!playerEventHistory.ContainsKey(participant))
                            playerEventHistory[participant] = new List<string>();

                        playerEventHistory[participant].Add(eventId);
                    }

                    activeEvents.Remove(eventId);
                    SaveData();
                    EventCompleted?.Invoke(this, worldEvent);
                }
            }
        }

        private void HandleEventCancelled(GameMessage message)
        {
            if (message.Data.TryGetValue("eventId", out var eventIdObj))
            {
                string eventId = eventIdObj.ToString();
                if (activeEvents.TryGetValue(eventId, out var worldEvent))
                {
                    worldEvent.Status = EventStatus.Cancelled;
                    activeEvents.Remove(eventId);
                    SaveData();
                    EventCancelled?.Invoke(this, worldEvent);
                }
            }
        }

        private void HandleParticipantJoined(GameMessage message)
        {
            if (message.Data.TryGetValue("eventId", out var eventIdObj) &&
                message.Data.TryGetValue("participant", out var participantObj))
            {
                string eventId = eventIdObj.ToString();
                var participant = JsonSerializer.Deserialize<EventParticipant>(participantObj.ToString());

                if (activeEvents.TryGetValue(eventId, out var worldEvent))
                {
                    worldEvent.Participants[participant.Username] = participant;
                    SaveData();
                    ParticipantJoined?.Invoke(this, (worldEvent, participant));
                }
            }
        }

        private void HandleParticipantLeft(GameMessage message)
        {
            if (message.Data.TryGetValue("eventId", out var eventIdObj) &&
                message.Data.TryGetValue("username", out var usernameObj))
            {
                string eventId = eventIdObj.ToString();
                string username = usernameObj.ToString();

                if (activeEvents.TryGetValue(eventId, out var worldEvent))
                {
                    worldEvent.Participants.Remove(username);
                    SaveData();
                    ParticipantLeft?.Invoke(this, (worldEvent, username));
                }
            }
        }

        private void HandleObjectiveCompleted(GameMessage message)
        {
            if (message.Data.TryGetValue("eventId", out var eventIdObj) &&
                message.Data.TryGetValue("objectiveId", out var objectiveIdObj) &&
                message.Data.TryGetValue("completedBy", out var completedByObj))
            {
                string eventId = eventIdObj.ToString();
                string objectiveId = objectiveIdObj.ToString();
                string completedBy = completedByObj.ToString();

                if (activeEvents.TryGetValue(eventId, out var worldEvent))
                {
                    var objective = worldEvent.Objectives.FirstOrDefault(o => o.Id == objectiveId);
                    if (objective != null)
                    {
                        objective.IsCompleted = true;
                        objective.CompletedBy.Add(completedBy);
                        SaveData();
                        ObjectiveCompleted?.Invoke(this, (worldEvent, objective));
                    }
                }
            }
        }
    }

    public class EventData
    {
        public List<WorldEvent> ActiveEvents { get; set; } = new List<WorldEvent>();
        public Dictionary<string, List<WorldEvent>> CompletedEvents { get; set; } = new Dictionary<string, List<WorldEvent>>();
        public Dictionary<string, List<string>> PlayerEventHistory { get; set; } = new Dictionary<string, List<string>>();
    }
}