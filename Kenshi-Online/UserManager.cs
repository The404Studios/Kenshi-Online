using System;
using System.Collections.Generic;
using System.IO;
using System.Text.Json;

namespace KenshiMultiplayer
{
    public static class UserManager
    {
        private static readonly string userFilePath = "users.json";
        private static readonly string dataFilePath = "playerData.json";
        private static Dictionary<string, string> users;
        private static Dictionary<string, PlayerData> playerData = new Dictionary<string, PlayerData>();

        static UserManager()
        {
            LoadUsers();
            LoadPlayerData();
        }

        private static void LoadUsers()
        {
            if (File.Exists(userFilePath))
            {
                var json = File.ReadAllText(userFilePath);
                users = JsonSerializer.Deserialize<Dictionary<string, string>>(json);
            }
            else
            {
                users = new Dictionary<string, string>();
            }
        }

        public static bool Authenticate(string username, string password)
        {
            return users.ContainsKey(username) && users[username] == password;
        }

        public static void RegisterUser(string username, string password)
        {
            if (!users.ContainsKey(username))
            {
                users[username] = password;
                File.WriteAllText(userFilePath, JsonSerializer.Serialize(users));
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

        public static void SavePlayerData(PlayerData data)
        {
            playerData[data.PlayerId] = data;
            File.WriteAllText(dataFilePath, JsonSerializer.Serialize(playerData));
        }

        public static PlayerData LoadPlayerData(string playerId)
        {
            return playerData.ContainsKey(playerId) ? playerData[playerId] : new PlayerData { PlayerId = playerId };
        }
    }
}
