using System;
using System.Collections.Generic;
using System.IO;
using System.Text.Json;
using System.Linq;

namespace KenshiMultiplayer
{
    public static class UserManager
    {
        private static readonly string userFilePath = "users.json";
        private static readonly string dataFilePath = "playerData.json";
        private static readonly string sessionFilePath = "sessions.json";
        private static Dictionary<string, UserAccount> users;
        private static Dictionary<string, PlayerData> playerData = new Dictionary<string, PlayerData>();
        private static Dictionary<string, UserSession> activeSessions = new Dictionary<string, UserSession>();

        static UserManager()
        {
            LoadUsers();
            LoadPlayerData();
            LoadSessions();
        }

        private static void LoadUsers()
        {
            if (File.Exists(userFilePath))
            {
                var json = File.ReadAllText(userFilePath);
                users = JsonSerializer.Deserialize<Dictionary<string, UserAccount>>(json);
            }
            else
            {
                users = new Dictionary<string, UserAccount>();
            }
        }

        private static void LoadPlayerData()
        {
            if (File.Exists(dataFilePath))
            {
                var json = File.ReadAllText(dataFilePath);
                playerData = JsonSerializer.Deserialize<Dictionary<string, PlayerData>>(json);
            }
        }

        private static void LoadSessions()
        {
            if (File.Exists(sessionFilePath))
            {
                var json = File.ReadAllText(sessionFilePath);
                activeSessions = JsonSerializer.Deserialize<Dictionary<string, UserSession>>(json);

                // Clean up expired sessions
                var expiredSessions = activeSessions
                    .Where(s => s.Value.ExpiresAt < DateTime.UtcNow)
                    .Select(s => s.Key)
                    .ToList();

                foreach (var sessionId in expiredSessions)
                {
                    activeSessions.Remove(sessionId);
                }

                SaveSessions();
            }
            else
            {
                activeSessions = new Dictionary<string, UserSession>();
            }
        }

        public static (bool success, string sessionId, string errorMessage) Login(string username, string password)
        {
            if (!users.TryGetValue(username, out var account))
            {
                return (false, null, "User not found");
            }

            if (account.IsBanned)
            {
                if (account.BanExpiration.HasValue && account.BanExpiration.Value < DateTime.UtcNow)
                {
                    // Ban has expired, remove ban
                    account.IsBanned = false;
                    account.BanExpiration = null;
                    SaveUsers();
                }
                else
                {
                    string banMessage = account.BanExpiration.HasValue
                        ? $"Account banned until {account.BanExpiration.Value}"
                        : "Account permanently banned";
                    return (false, null, banMessage);
                }
            }

            if (!EncryptionHelper.VerifyPassword(password, account.PasswordHash, account.Salt))
            {
                return (false, null, "Invalid password");
            }

            // Create a new session
            string sessionId = Guid.NewGuid().ToString();
            var session = new UserSession
            {
                SessionId = sessionId,
                Username = username,
                CreatedAt = DateTime.UtcNow,
                ExpiresAt = DateTime.UtcNow.AddDays(1),
                LastActivity = DateTime.UtcNow,
                IpAddress = "0.0.0.0" // This should be passed in from higher level
            };

            activeSessions[sessionId] = session;
            SaveSessions();

            // Update last login time
            account.LastLogin = DateTime.UtcNow;
            SaveUsers();

            return (true, sessionId, null);
        }

        public static bool ValidateSession(string sessionId)
        {
            if (!activeSessions.TryGetValue(sessionId, out var session))
                return false;

            if (session.ExpiresAt < DateTime.UtcNow)
            {
                activeSessions.Remove(sessionId);
                SaveSessions();
                return false;
            }

            // Update last activity
            session.LastActivity = DateTime.UtcNow;
            // Extend session
            session.ExpiresAt = DateTime.UtcNow.AddDays(1);
            SaveSessions();

            return true;
        }

        public static void Logout(string sessionId)
        {
            if (activeSessions.ContainsKey(sessionId))
            {
                activeSessions.Remove(sessionId);
                SaveSessions();
            }
        }

        public static (bool success, string errorMessage) RegisterUser(string username, string password, string email)
        {
            if (string.IsNullOrWhiteSpace(username) || username.Length < 3)
                return (false, "Username must be at least 3 characters");

            if (string.IsNullOrWhiteSpace(password) || password.Length < 8)
                return (false, "Password must be at least 8 characters");

            if (string.IsNullOrWhiteSpace(email) || !email.Contains('@') || !email.Contains('.'))
                return (false, "Invalid email address");

            if (users.ContainsKey(username))
                return (false, "Username already exists");

            if (users.Values.Any(u => u.Email == email))
                return (false, "Email already registered");

            var (hash, salt) = EncryptionHelper.HashPassword(password);

            users[username] = new UserAccount
            {
                Username = username,
                PasswordHash = hash,
                Salt = salt,
                Email = email,
                CreatedAt = DateTime.UtcNow,
                LastLogin = DateTime.UtcNow,
                Roles = new List<string> { "player" }
            };

            SaveUsers();
            return (true, null);
        }

        public static bool IsAdmin(string username)
        {
            return users.TryGetValue(username, out var account) && account.Roles.Contains("admin");
        }

        public static bool BanUser(string adminUsername, string userToBan, TimeSpan? duration, string reason)
        {
            if (!IsAdmin(adminUsername))
                return false;

            if (!users.TryGetValue(userToBan, out var account))
                return false;

            account.IsBanned = true;
            account.BanReason = reason;
            account.BanExpiration = duration.HasValue ? DateTime.UtcNow.Add(duration.Value) : (DateTime?)null;

            // Remove all active sessions for this user
            var userSessions = activeSessions.Where(s => s.Value.Username == userToBan).Select(s => s.Key).ToList();
            foreach (var sessionId in userSessions)
            {
                activeSessions.Remove(sessionId);
            }

            SaveUsers();
            SaveSessions();

            Logger.Log($"User {userToBan} banned by {adminUsername}. Reason: {reason}");
            return true;
        }

        public static PlayerData LoadPlayerData(string username)
        {
            if (!playerData.TryGetValue(username, out var data))
            {
                data = new PlayerData
                {
                    PlayerId = username,
                    Health = 100,
                    Inventory = new Dictionary<string, int>(),
                    Skills = new Dictionary<string, float>(),
                    Level = 1,
                    Experience = 0,
                    ExperienceToNextLevel = 1000
                };
                playerData[username] = data;
            }
            return data;
        }

        public static void SavePlayerData(PlayerData data)
        {
            playerData[data.PlayerId] = data;
            SaveAllPlayerData();
        }

        private static void SaveUsers()
        {
            File.WriteAllText(userFilePath, JsonSerializer.Serialize(users));
        }

        private static void SaveSessions()
        {
            File.WriteAllText(sessionFilePath, JsonSerializer.Serialize(activeSessions));
        }

        private static void SaveAllPlayerData()
        {
            File.WriteAllText(dataFilePath, JsonSerializer.Serialize(playerData));
        }

        public static List<string> GetOnlineUsers()
        {
            // Clean expired sessions first
            var expiredSessions = activeSessions
                .Where(s => s.Value.ExpiresAt < DateTime.UtcNow)
                .Select(s => s.Key)
                .ToList();

            foreach (var sessionId in expiredSessions)
            {
                activeSessions.Remove(sessionId);
            }

            if (expiredSessions.Any())
            {
                SaveSessions();
            }

            // Return unique usernames with active sessions
            return activeSessions.Values
                .Select(s => s.Username)
                .Distinct()
                .ToList();
        }
    }

    public class UserAccount
    {
        public string Username { get; set; }
        public string PasswordHash { get; set; }
        public string Salt { get; set; }
        public string Email { get; set; }
        public DateTime CreatedAt { get; set; }
        public DateTime LastLogin { get; set; }
        public bool IsBanned { get; set; }
        public DateTime? BanExpiration { get; set; }
        public string BanReason { get; set; }
        public List<string> Roles { get; set; } = new List<string>();
    }

    public class UserSession
    {
        public string SessionId { get; set; }
        public string Username { get; set; }
        public DateTime CreatedAt { get; set; }
        public DateTime ExpiresAt { get; set; }
        public DateTime LastActivity { get; set; }
        public string IpAddress { get; set; }
    }
}