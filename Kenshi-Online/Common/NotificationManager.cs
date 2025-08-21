using KenshiMultiplayer.Auth;
using KenshiMultiplayer.Networking;
using KenshiMultiplayer.Utility;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Text.Json;
using System.Threading.Tasks;

namespace KenshiMultiplayer.Common
{
    public enum NotificationType
    {
        Info,
        Success,
        Warning,
        Error,
        FriendRequest,
        PartyInvite,
        TradeRequest,
        EventInvite,
        Achievement,
        LevelUp,
        ItemReceived,
        QuestUpdate,
        CombatAlert,
        BaseAlert,
        SystemMessage
    }

    public enum NotificationPriority
    {
        Low,
        Normal,
        High,
        Critical
    }

    public class Notification
    {
        public string Id { get; set; } = Guid.NewGuid().ToString();
        public NotificationType Type { get; set; }
        public NotificationPriority Priority { get; set; } = NotificationPriority.Normal;
        public string Title { get; set; }
        public string Message { get; set; }
        public string Icon { get; set; }
        public DateTime Timestamp { get; set; } = DateTime.UtcNow;
        public bool IsRead { get; set; } = false;
        public bool IsPersistent { get; set; } = false;
        public TimeSpan? Duration { get; set; }
        public Dictionary<string, object> Data { get; set; } = new Dictionary<string, object>();
        public List<NotificationAction> Actions { get; set; } = new List<NotificationAction>();
        public string SoundEffect { get; set; }
        public bool ShowInGame { get; set; } = true;
        public bool ShowInUI { get; set; } = true;
    }

    public class NotificationAction
    {
        public string Id { get; set; }
        public string Label { get; set; }
        public string ActionType { get; set; } // accept, decline, view, goto, etc.
        public Dictionary<string, object> ActionData { get; set; } = new Dictionary<string, object>();
        public bool CloseOnAction { get; set; } = true;
    }

    public class NotificationPreferences
    {
        public bool EnableNotifications { get; set; } = true;
        public bool EnableSounds { get; set; } = true;
        public bool EnableDesktopNotifications { get; set;  } = false;
        public Dictionary<NotificationType, bool> TypeSettings { get; set; } = new Dictionary<NotificationType, bool>();
        public Dictionary<NotificationType, bool> SoundSettings { get; set; } = new Dictionary<NotificationType, bool>();
        public bool ShowInCombat { get; set; } = false;
        public bool GroupSimilar { get; set; } = true;
        public int MaxVisibleNotifications { get; set; } = 5;
    }

    public class NotificationManager
    {
        private readonly List<Notification> activeNotifications = new List<Notification>();
        private readonly List<Notification> notificationHistory = new List<Notification>();
        private readonly Dictionary<string, DateTime> cooldowns = new Dictionary<string, DateTime>();
        private readonly NotificationPreferences preferences = new NotificationPreferences();

        private readonly string dataFilePath;
        private readonly EnhancedClient client;
        private readonly EnhancedServer server;

        // Events
        public event EventHandler<Notification> NotificationReceived;
        public event EventHandler<Notification> NotificationRead;
        public event EventHandler<Notification> NotificationDismissed;
        public event EventHandler<(Notification, NotificationAction)> NotificationActionTaken;

        // Sound mappings
        private readonly Dictionary<NotificationType, string> defaultSounds = new Dictionary<NotificationType, string>
        {
            { NotificationType.Info, "notification_info.wav" },
            { NotificationType.Success, "notification_success.wav" },
            { NotificationType.Warning, "notification_warning.wav" },
            { NotificationType.Error, "notification_error.wav" },
            { NotificationType.FriendRequest, "notification_social.wav" },
            { NotificationType.PartyInvite, "notification_party.wav" },
            { NotificationType.TradeRequest, "notification_trade.wav" },
            { NotificationType.EventInvite, "notification_event.wav" },
            { NotificationType.Achievement, "notification_achievement.wav" },
            { NotificationType.LevelUp, "notification_levelup.wav" },
            { NotificationType.ItemReceived, "notification_item.wav" },
            { NotificationType.CombatAlert, "notification_combat.wav" },
            { NotificationType.BaseAlert, "notification_alert.wav" }
        };

        public NotificationManager(EnhancedClient clientInstance, string dataDirectory = "data")
        {
            client = clientInstance;
            dataFilePath = Path.Combine(dataDirectory, "notifications.json");
            Directory.CreateDirectory(dataDirectory);

            LoadPreferences();
            InitializeDefaultPreferences();

            if (client != null)
            {
                // Subscribe to various events that generate notifications
                SubscribeToClientEvents();
            }

            // Start cleanup timer
            Task.Run(async () => {
                while (true)
                {
                    await Task.Delay(TimeSpan.FromMinutes(1));
                    CleanupOldNotifications();
                }
            });
        }

        private void LoadPreferences()
        {
            try
            {
                string prefPath = Path.Combine(Path.GetDirectoryName(dataFilePath), "notification_preferences.json");
                if (File.Exists(prefPath))
                {
                    string json = File.ReadAllText(prefPath);
                    var loadedPrefs = JsonSerializer.Deserialize<NotificationPreferences>(json);
                    if (loadedPrefs != null)
                    {
                        // Copy loaded preferences
                        preferences.EnableNotifications = loadedPrefs.EnableNotifications;
                        preferences.EnableSounds = loadedPrefs.EnableSounds;
                        preferences.EnableDesktopNotifications = loadedPrefs.EnableDesktopNotifications;
                        preferences.ShowInCombat = loadedPrefs.ShowInCombat;
                        preferences.GroupSimilar = loadedPrefs.GroupSimilar;
                        preferences.MaxVisibleNotifications = loadedPrefs.MaxVisibleNotifications;

                        if (loadedPrefs.TypeSettings != null)
                            preferences.TypeSettings = loadedPrefs.TypeSettings;

                        if (loadedPrefs.SoundSettings != null)
                            preferences.SoundSettings = loadedPrefs.SoundSettings;
                    }
                }
            }
            catch (Exception ex)
            {
                Logger.Log($"Error loading notification preferences: {ex.Message}");
            }
        }

        private void SavePreferences()
        {
            try
            {
                string prefPath = Path.Combine(Path.GetDirectoryName(dataFilePath), "notification_preferences.json");
                string json = JsonSerializer.Serialize(preferences, new JsonSerializerOptions { WriteIndented = true });
                File.WriteAllText(prefPath, json);
            }
            catch (Exception ex)
            {
                Logger.Log($"Error saving notification preferences: {ex.Message}");
            }
        }

        private void InitializeDefaultPreferences()
        {
            // Initialize type settings if not already set
            foreach (NotificationType type in Enum.GetValues(typeof(NotificationType)))
            {
                if (!preferences.TypeSettings.ContainsKey(type))
                    preferences.TypeSettings[type] = true;

                if (!preferences.SoundSettings.ContainsKey(type))
                    preferences.SoundSettings[type] = true;
            }

            // Disable some notifications by default
            preferences.TypeSettings[NotificationType.Info] = false; // Too spammy
            preferences.SoundSettings[NotificationType.Info] = false;
        }

        private void SubscribeToClientEvents()
        {
            // Friend events
            client.MessageReceived += (sender, msg) => {
                switch (msg.Type)
                {
                    case Auth.MessageType.FriendRequest:
                        CreateNotification(
                            NotificationType.FriendRequest,
                            "Friend Request",
                            $"{msg.PlayerId} wants to be your friend",
                            new List<NotificationAction>
                            {
                                new NotificationAction
                                {
                                    Id = "accept",
                                    Label = "Accept",
                                    ActionType = "friend_accept",
                                    ActionData = new Dictionary<string, object> { { "username", msg.PlayerId } }
                                },
                                new NotificationAction
                                {
                                    Id = "decline",
                                    Label = "Decline",
                                    ActionType = "friend_decline",
                                    ActionData = new Dictionary<string, object> { { "username", msg.PlayerId } }
                                }
                            }
                        );
                        break;

                    case Auth.MessageType.PartyInvite:
                        if (msg.Data.TryGetValue("partyName", out var partyName))
                        {
                            CreateNotification(
                                NotificationType.PartyInvite,
                                "Party Invite",
                                $"{msg.PlayerId} invited you to join '{partyName}'",
                                new List<NotificationAction>
                                {
                                    new NotificationAction
                                    {
                                        Id = "join",
                                        Label = "Join Party",
                                        ActionType = "party_join",
                                        ActionData = new Dictionary<string, object> { { "partyId", msg.Data["partyId"] } }
                                    },
                                    new NotificationAction
                                    {
                                        Id = "decline",
                                        Label = "Decline",
                                        ActionType = "party_decline",
                                        ActionData = new Dictionary<string, object> { { "partyId", msg.Data["partyId"] } }
                                    }
                                }
                            );
                        }
                        break;

                    case MessageType.TradeRequest:
                        CreateNotification(
                            NotificationType.TradeRequest,
                            "Trade Request",
                            $"{msg.PlayerId} wants to trade with you",
                            new List<NotificationAction>
                            {
                                new NotificationAction
                                {
                                    Id = "accept",
                                    Label = "Accept Trade",
                                    ActionType = "trade_accept",
                                    ActionData = new Dictionary<string, object> { { "tradeId", msg.Data["tradeId"] } }
                                },
                                new NotificationAction
                                {
                                    Id = "decline",
                                    Label = "Decline",
                                    ActionType = "trade_decline",
                                    ActionData = new Dictionary<string, object> { { "tradeId", msg.Data["tradeId"] } }
                                }
                            },
                            priority: NotificationPriority.High
                        );
                        break;

                    case Auth.MessageType.CombatAction:
                        if (msg.Data.ContainsKey("targetId") && msg.Data["targetId"].ToString() == client.CurrentUsername)
                        {
                            CreateNotification(
                                NotificationType.CombatAlert,
                                "Under Attack!",
                                $"{msg.PlayerId} is attacking you!",
                                priority: NotificationPriority.Critical,
                                duration: TimeSpan.FromSeconds(5)
                            );
                        }
                        break;
                }
            };
        }

        public Notification CreateNotification(
            NotificationType type,
            string title,
            string message,
            List<NotificationAction> actions = null,
            NotificationPriority priority = NotificationPriority.Normal,
            TimeSpan? duration = null,
            Dictionary<string, object> data = null)
        {
            // Check if notifications are enabled
            if (!preferences.EnableNotifications || !preferences.TypeSettings.GetValueOrDefault(type, true))
                return null;

            // Check cooldown for spam prevention
            string cooldownKey = $"{type}:{title}";
            if (cooldowns.TryGetValue(cooldownKey, out DateTime lastTime))
            {
                if (DateTime.UtcNow - lastTime < TimeSpan.FromSeconds(5))
                    return null; // Too soon
            }

            var notification = new Notification
            {
                Type = type,
                Title = title,
                Message = message,
                Priority = priority,
                Duration = duration ?? GetDefaultDuration(type),
                Actions = actions ?? new List<NotificationAction>(),
                Data = data ?? new Dictionary<string, object>(),
                SoundEffect = defaultSounds.GetValueOrDefault(type)
            };

            // Add to active notifications
            activeNotifications.Add(notification);
            cooldowns[cooldownKey] = DateTime.UtcNow;

            // Limit active notifications
            if (activeNotifications.Count > preferences.MaxVisibleNotifications * 2)
            {
                // Remove oldest low-priority notifications
                var toRemove = activeNotifications
                    .Where(n => !n.IsPersistent && n.Priority == NotificationPriority.Low)
                    .OrderBy(n => n.Timestamp)
                    .Take(activeNotifications.Count - preferences.MaxVisibleNotifications)
                    .ToList();

                foreach (var n in toRemove)
                {
                    activeNotifications.Remove(n);
                }
            }

            // Play sound if enabled
            if (preferences.EnableSounds && preferences.SoundSettings.GetValueOrDefault(type, true))
            {
                PlayNotificationSound(notification.SoundEffect);
            }

            // Raise event
            NotificationReceived?.Invoke(this, notification);

            // Auto-dismiss after duration
            if (notification.Duration.HasValue && !notification.IsPersistent)
            {
                Task.Delay(notification.Duration.Value).ContinueWith(_ => {
                    DismissNotification(notification.Id);
                });
            }

            return notification;
        }

        public void MarkAsRead(string notificationId)
        {
            var notification = activeNotifications.FirstOrDefault(n => n.Id == notificationId);
            if (notification != null && !notification.IsRead)
            {
                notification.IsRead = true;
                NotificationRead?.Invoke(this, notification);
            }
        }

        public void DismissNotification(string notificationId)
        {
            var notification = activeNotifications.FirstOrDefault(n => n.Id == notificationId);
            if (notification != null)
            {
                activeNotifications.Remove(notification);
                notificationHistory.Add(notification);
                NotificationDismissed?.Invoke(this, notification);
            }
        }

        public void ExecuteAction(string notificationId, string actionId)
        {
            var notification = activeNotifications.FirstOrDefault(n => n.Id == notificationId);
            if (notification != null)
            {
                var action = notification.Actions.FirstOrDefault(a => a.Id == actionId);
                if (action != null)
                {
                    // Execute the action based on type
                    switch (action.ActionType)
                    {
                        case "friend_accept":
                            if (action.ActionData.TryGetValue("username", out var friendUsername))
                                client.AcceptFriendRequest(friendUsername.ToString());
                            break;

                        case "friend_decline":
                            if (action.ActionData.TryGetValue("username", out var declineUsername))
                                client.DeclineFriendRequest(declineUsername.ToString());
                            break;

                        case "party_join":
                            if (action.ActionData.TryGetValue("partyId", out var partyId))
                                client.JoinParty(partyId.ToString());
                            break;

                        case "trade_accept":
                            if (action.ActionData.TryGetValue("tradeId", out var tradeId))
                                client.AcceptTradeRequest(tradeId.ToString());
                            break;

                        case "teleport_to":
                            if (action.ActionData.TryGetValue("position", out var posObj))
                            {
                                var pos = JsonSerializer.Deserialize<Position>(posObj.ToString());
                                // Teleport logic would go here
                            }
                            break;
                    }

                    NotificationActionTaken?.Invoke(this, (notification, action));

                    if (action.CloseOnAction)
                    {
                        DismissNotification(notificationId);
                    }
                }
            }
        }

        public List<Notification> GetActiveNotifications()
        {
            return activeNotifications
                .OrderByDescending(n => n.Priority)
                .ThenByDescending(n => n.Timestamp)
                .Take(preferences.MaxVisibleNotifications)
                .ToList();
        }

        public List<Notification> GetNotificationHistory(int limit = 50)
        {
            return notificationHistory
                .OrderByDescending(n => n.Timestamp)
                .Take(limit)
                .ToList();
        }

        public int GetUnreadCount()
        {
            return activeNotifications.Count(n => !n.IsRead);
        }

        public void UpdatePreferences(NotificationPreferences newPreferences)
        {
            preferences.EnableNotifications = newPreferences.EnableNotifications;
            preferences.EnableSounds = newPreferences.EnableSounds;
            preferences.EnableDesktopNotifications = newPreferences.EnableDesktopNotifications;
            preferences.ShowInCombat = newPreferences.ShowInCombat;
            preferences.GroupSimilar = newPreferences.GroupSimilar;
            preferences.MaxVisibleNotifications = newPreferences.MaxVisibleNotifications;

            // Update type settings
            foreach (var kvp in newPreferences.TypeSettings)
            {
                preferences.TypeSettings[kvp.Key] = kvp.Value;
            }

            // Update sound settings
            foreach (var kvp in newPreferences.SoundSettings)
            {
                preferences.SoundSettings[kvp.Key] = kvp.Value;
            }

            SavePreferences();
        }

        private TimeSpan GetDefaultDuration(NotificationType type)
        {
            return type switch
            {
                NotificationType.Info => TimeSpan.FromSeconds(3),
                NotificationType.Success => TimeSpan.FromSeconds(3),
                NotificationType.Warning => TimeSpan.FromSeconds(5),
                NotificationType.Error => TimeSpan.FromSeconds(10),
                NotificationType.CombatAlert => TimeSpan.FromSeconds(5),
                NotificationType.Achievement => TimeSpan.FromSeconds(10),
                NotificationType.LevelUp => TimeSpan.FromSeconds(10),
                _ => TimeSpan.FromSeconds(5)
            };
        }

        private void PlayNotificationSound(string soundFile)
        {
            if (string.IsNullOrEmpty(soundFile))
                return;

            // Sound playing logic would go here
            // This could use NAudio or System.Media.SoundPlayer
            Logger.Log($"Playing notification sound: {soundFile}");
        }

        private void CleanupOldNotifications()
        {
            // Remove notifications older than 24 hours from history
            var cutoff = DateTime.UtcNow.AddHours(-24);
            notificationHistory.RemoveAll(n => n.Timestamp < cutoff);

            // Remove expired non-persistent notifications
            var expired = activeNotifications
                .Where(n => !n.IsPersistent &&
                           n.Duration.HasValue &&
                           n.Timestamp.Add(n.Duration.Value) < DateTime.UtcNow)
                .ToList();

            foreach (var n in expired)
            {
                DismissNotification(n.Id);
            }
        }

        // Helper methods for common notifications
        public void NotifyLevelUp(int newLevel)
        {
            CreateNotification(
                NotificationType.LevelUp,
                "Level Up!",
                $"Congratulations! You've reached level {newLevel}!",
                priority: NotificationPriority.High,
                duration: TimeSpan.FromSeconds(10)
            );
        }

        public void NotifyAchievement(string achievementName, string description)
        {
            CreateNotification(
                NotificationType.Achievement,
                "Achievement Unlocked!",
                $"{achievementName}: {description}",
                priority: NotificationPriority.High,
                duration: TimeSpan.FromSeconds(15)
            );
        }

        public void NotifyItemReceived(string itemName, int quantity)
        {
            CreateNotification(
                NotificationType.ItemReceived,
                "Item Received",
                quantity > 1 ? $"You received {itemName} x{quantity}" : $"You received {itemName}",
                priority: NotificationPriority.Normal,
                duration: TimeSpan.FromSeconds(5)
            );
        }

        public void NotifyBaseUnderAttack(string baseName, string attackerFaction)
        {
            CreateNotification(
                NotificationType.BaseAlert,
                "Base Under Attack!",
                $"{baseName} is under attack by {attackerFaction}!",
                new List<NotificationAction>
                {
                    new NotificationAction
                    {
                        Id = "teleport",
                        Label = "Teleport to Base",
                        ActionType = "teleport_base",
                        ActionData = new Dictionary<string, object> { { "baseName", baseName } }
                    }
                },
                priority: NotificationPriority.Critical
            );
        }
    }
}
