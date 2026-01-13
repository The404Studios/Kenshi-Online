using System;
using System.Collections.Generic;
using System.IO;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;

namespace KenshiMultiplayer.Core
{
    /// <summary>
    /// Server-side save manager.
    ///
    /// Key principle: SERVER OWNS ALL SAVES.
    /// Clients are read-only mirrors. They cannot modify saves.
    ///
    /// This prevents:
    /// - Save file manipulation
    /// - Item duplication via save editing
    /// - Progress manipulation
    /// </summary>
    public class SaveManager
    {
        private readonly string _basePath;
        private readonly object _saveLock = new();
        private readonly SemaphoreSlim _asyncLock = new(1, 1);

        /// <summary>
        /// Auto-save interval in milliseconds (default: 60 seconds).
        /// </summary>
        public int AutoSaveIntervalMs { get; set; } = 60_000;

        /// <summary>
        /// Maximum number of backups to keep per session.
        /// </summary>
        public int MaxBackups { get; set; } = 10;

        public SaveManager(string basePath)
        {
            _basePath = basePath;
            EnsureDirectories();
        }

        private void EnsureDirectories()
        {
            Directory.CreateDirectory(Path.Combine(_basePath, "sessions"));
            Directory.CreateDirectory(Path.Combine(_basePath, "backups"));
        }

        /// <summary>
        /// Get path for a session's saves.
        /// </summary>
        private string GetSessionPath(string sessionId)
        {
            return Path.Combine(_basePath, "sessions", sessionId);
        }

        /// <summary>
        /// Initialize a new session's save directory.
        /// </summary>
        public void InitializeSession(string sessionId)
        {
            var sessionPath = GetSessionPath(sessionId);
            Directory.CreateDirectory(sessionPath);
            Directory.CreateDirectory(Path.Combine(sessionPath, "players"));
        }

        /// <summary>
        /// Save world state.
        /// </summary>
        public async Task SaveWorldAsync(string sessionId, WorldSave world)
        {
            await _asyncLock.WaitAsync();
            try
            {
                var path = Path.Combine(GetSessionPath(sessionId), "world.json");
                var json = JsonSerializer.Serialize(world, new JsonSerializerOptions { WriteIndented = true });
                await File.WriteAllTextAsync(path, json);

                // Update hash
                world.WorldHash = ComputeHash(json);
            }
            finally
            {
                _asyncLock.Release();
            }
        }

        /// <summary>
        /// Load world state.
        /// </summary>
        public async Task<WorldSave> LoadWorldAsync(string sessionId)
        {
            var path = Path.Combine(GetSessionPath(sessionId), "world.json");

            if (!File.Exists(path))
                return null;

            var json = await File.ReadAllTextAsync(path);
            return JsonSerializer.Deserialize<WorldSave>(json);
        }

        /// <summary>
        /// Save player data.
        /// </summary>
        public async Task SavePlayerAsync(string sessionId, PlayerSave player)
        {
            await _asyncLock.WaitAsync();
            try
            {
                var path = Path.Combine(GetSessionPath(sessionId), "players", $"{player.PlayerId}.json");
                var json = JsonSerializer.Serialize(player, new JsonSerializerOptions { WriteIndented = true });
                await File.WriteAllTextAsync(path, json);
            }
            finally
            {
                _asyncLock.Release();
            }
        }

        /// <summary>
        /// Load player data.
        /// </summary>
        public async Task<PlayerSave> LoadPlayerAsync(string sessionId, string playerId)
        {
            var path = Path.Combine(GetSessionPath(sessionId), "players", $"{playerId}.json");

            if (!File.Exists(path))
                return null;

            var json = await File.ReadAllTextAsync(path);
            return JsonSerializer.Deserialize<PlayerSave>(json);
        }

        /// <summary>
        /// Get all player saves for a session.
        /// </summary>
        public async Task<List<PlayerSave>> LoadAllPlayersAsync(string sessionId)
        {
            var players = new List<PlayerSave>();
            var playersPath = Path.Combine(GetSessionPath(sessionId), "players");

            if (!Directory.Exists(playersPath))
                return players;

            foreach (var file in Directory.GetFiles(playersPath, "*.json"))
            {
                var json = await File.ReadAllTextAsync(file);
                var player = JsonSerializer.Deserialize<PlayerSave>(json);
                if (player != null)
                    players.Add(player);
            }

            return players;
        }

        /// <summary>
        /// Create a backup of the current session state.
        /// </summary>
        public async Task CreateBackupAsync(string sessionId)
        {
            var sessionPath = GetSessionPath(sessionId);
            if (!Directory.Exists(sessionPath))
                return;

            var timestamp = DateTimeOffset.UtcNow.ToUnixTimeSeconds();
            var backupPath = Path.Combine(_basePath, "backups", sessionId, timestamp.ToString());

            Directory.CreateDirectory(backupPath);

            // Copy world
            var worldPath = Path.Combine(sessionPath, "world.json");
            if (File.Exists(worldPath))
            {
                File.Copy(worldPath, Path.Combine(backupPath, "world.json"));
            }

            // Copy players
            var playersPath = Path.Combine(sessionPath, "players");
            if (Directory.Exists(playersPath))
            {
                var backupPlayersPath = Path.Combine(backupPath, "players");
                Directory.CreateDirectory(backupPlayersPath);

                foreach (var file in Directory.GetFiles(playersPath, "*.json"))
                {
                    File.Copy(file, Path.Combine(backupPlayersPath, Path.GetFileName(file)));
                }
            }

            // Clean old backups
            await CleanOldBackupsAsync(sessionId);
        }

        /// <summary>
        /// Clean old backups beyond MaxBackups limit.
        /// </summary>
        private async Task CleanOldBackupsAsync(string sessionId)
        {
            await Task.Run(() =>
            {
                var backupPath = Path.Combine(_basePath, "backups", sessionId);
                if (!Directory.Exists(backupPath))
                    return;

                var backups = Directory.GetDirectories(backupPath);
                if (backups.Length <= MaxBackups)
                    return;

                // Sort by name (timestamp) and delete oldest
                Array.Sort(backups);
                var toDelete = backups.Length - MaxBackups;

                for (var i = 0; i < toDelete; i++)
                {
                    try
                    {
                        Directory.Delete(backups[i], true);
                    }
                    catch
                    {
                        // Ignore cleanup errors
                    }
                }
            });
        }

        /// <summary>
        /// Restore from a backup.
        /// </summary>
        public async Task RestoreBackupAsync(string sessionId, long timestamp)
        {
            var backupPath = Path.Combine(_basePath, "backups", sessionId, timestamp.ToString());
            if (!Directory.Exists(backupPath))
                throw new FileNotFoundException($"Backup not found: {timestamp}");

            var sessionPath = GetSessionPath(sessionId);

            // Clear current session
            if (Directory.Exists(sessionPath))
            {
                Directory.Delete(sessionPath, true);
            }

            // Copy backup to session
            await Task.Run(() => CopyDirectory(backupPath, sessionPath));
        }

        /// <summary>
        /// List available backups for a session.
        /// </summary>
        public List<BackupInfo> ListBackups(string sessionId)
        {
            var backups = new List<BackupInfo>();
            var backupPath = Path.Combine(_basePath, "backups", sessionId);

            if (!Directory.Exists(backupPath))
                return backups;

            foreach (var dir in Directory.GetDirectories(backupPath))
            {
                var name = Path.GetFileName(dir);
                if (long.TryParse(name, out var timestamp))
                {
                    var info = new DirectoryInfo(dir);
                    backups.Add(new BackupInfo
                    {
                        Timestamp = timestamp,
                        CreatedAt = DateTimeOffset.FromUnixTimeSeconds(timestamp).DateTime,
                        SizeBytes = GetDirectorySize(dir)
                    });
                }
            }

            backups.Sort((a, b) => b.Timestamp.CompareTo(a.Timestamp));
            return backups;
        }

        /// <summary>
        /// Compute SHA256 hash of content.
        /// </summary>
        public static string ComputeHash(string content)
        {
            using var sha = SHA256.Create();
            var bytes = Encoding.UTF8.GetBytes(content);
            var hash = sha.ComputeHash(bytes);
            return Convert.ToBase64String(hash);
        }

        /// <summary>
        /// Verify world hash matches.
        /// </summary>
        public async Task<bool> VerifyWorldHashAsync(string sessionId, string expectedHash)
        {
            var path = Path.Combine(GetSessionPath(sessionId), "world.json");

            if (!File.Exists(path))
                return string.IsNullOrEmpty(expectedHash);

            var content = await File.ReadAllTextAsync(path);
            var actualHash = ComputeHash(content);

            return actualHash == expectedHash;
        }

        /// <summary>
        /// Delete a session's saves.
        /// </summary>
        public void DeleteSession(string sessionId)
        {
            var sessionPath = GetSessionPath(sessionId);
            if (Directory.Exists(sessionPath))
            {
                Directory.Delete(sessionPath, true);
            }

            var backupPath = Path.Combine(_basePath, "backups", sessionId);
            if (Directory.Exists(backupPath))
            {
                Directory.Delete(backupPath, true);
            }
        }

        /// <summary>
        /// Check if a session exists.
        /// </summary>
        public bool SessionExists(string sessionId)
        {
            return Directory.Exists(GetSessionPath(sessionId));
        }

        private void CopyDirectory(string source, string destination)
        {
            Directory.CreateDirectory(destination);

            foreach (var file in Directory.GetFiles(source))
            {
                File.Copy(file, Path.Combine(destination, Path.GetFileName(file)));
            }

            foreach (var dir in Directory.GetDirectories(source))
            {
                CopyDirectory(dir, Path.Combine(destination, Path.GetFileName(dir)));
            }
        }

        private long GetDirectorySize(string path)
        {
            long size = 0;

            foreach (var file in Directory.GetFiles(path, "*", SearchOption.AllDirectories))
            {
                size += new FileInfo(file).Length;
            }

            return size;
        }
    }

    /// <summary>
    /// World save structure.
    /// </summary>
    public class WorldSave
    {
        public string SessionId { get; set; }
        public ulong LastTick { get; set; }
        public long SavedAt { get; set; }
        public string WorldHash { get; set; }

        public List<EntityState> Entities { get; set; } = new();
        public Dictionary<string, FactionState> Factions { get; set; } = new();
        public List<BuildingState> Buildings { get; set; } = new();

        public static WorldSave Create(string sessionId)
        {
            return new WorldSave
            {
                SessionId = sessionId,
                SavedAt = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds()
            };
        }
    }

    /// <summary>
    /// Player save structure.
    /// </summary>
    public class PlayerSave
    {
        public string PlayerId { get; set; }
        public string DisplayName { get; set; }

        // Position
        public float X { get; set; }
        public float Y { get; set; }
        public float Z { get; set; }

        // Vitals
        public float Health { get; set; }
        public float MaxHealth { get; set; }
        public float Hunger { get; set; }
        public float Blood { get; set; }

        // Inventory
        public List<InventorySlot> Inventory { get; set; } = new();
        public Dictionary<string, string> Equipment { get; set; } = new();

        // Progression
        public Dictionary<string, float> Skills { get; set; } = new();
        public int Money { get; set; }

        // Relations
        public string FactionId { get; set; }
        public Dictionary<string, int> FactionStanding { get; set; } = new();

        // Meta
        public long LastPlayed { get; set; }
        public int TotalPlayTimeSeconds { get; set; }

        public static PlayerSave Create(string playerId, string displayName)
        {
            return new PlayerSave
            {
                PlayerId = playerId,
                DisplayName = displayName,
                Health = 100,
                MaxHealth = 100,
                Hunger = 100,
                Blood = 100,
                LastPlayed = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds()
            };
        }

        /// <summary>
        /// Create from current entity state.
        /// </summary>
        public static PlayerSave FromEntityState(EntityState entity, PlayerSave existing = null)
        {
            var save = existing ?? new PlayerSave();

            save.PlayerId = entity.EntityId.Replace("player_", "");
            save.DisplayName = entity.Name;
            save.X = entity.X;
            save.Y = entity.Y;
            save.Z = entity.Z;
            save.Health = entity.Health;
            save.MaxHealth = entity.MaxHealth;
            save.LastPlayed = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();

            // Copy data fields
            if (entity.Data.TryGetValue("Faction", out var faction))
                save.FactionId = faction?.ToString();
            if (entity.Data.TryGetValue("Money", out var money) && money is int m)
                save.Money = m;
            if (entity.Data.TryGetValue("Hunger", out var hunger) && hunger is float h)
                save.Hunger = h;
            if (entity.Data.TryGetValue("Blood", out var blood) && blood is float b)
                save.Blood = b;

            return save;
        }

        /// <summary>
        /// Convert to entity state.
        /// </summary>
        public EntityState ToEntityState()
        {
            return new EntityState
            {
                EntityId = $"player_{PlayerId}",
                Type = SyncEntityType.Player,
                Name = DisplayName,
                X = X,
                Y = Y,
                Z = Z,
                Health = Health,
                MaxHealth = MaxHealth,
                OwnerId = PlayerId,
                Data = new Dictionary<string, object>
                {
                    ["Faction"] = FactionId,
                    ["Money"] = Money,
                    ["Hunger"] = Hunger,
                    ["Blood"] = Blood
                }
            };
        }
    }

    /// <summary>
    /// Inventory slot.
    /// </summary>
    public class InventorySlot
    {
        public string ItemId { get; set; }
        public string ItemType { get; set; }
        public int Quantity { get; set; }
        public int SlotIndex { get; set; }
    }

    /// <summary>
    /// Faction state.
    /// </summary>
    public class FactionState
    {
        public string FactionId { get; set; }
        public string Name { get; set; }
        public Dictionary<string, int> Relations { get; set; } = new();
    }

    /// <summary>
    /// Building state.
    /// </summary>
    public class BuildingState
    {
        public string BuildingId { get; set; }
        public string BuildingType { get; set; }
        public float X { get; set; }
        public float Y { get; set; }
        public float Z { get; set; }
        public float Health { get; set; }
        public string OwnerId { get; set; }
        public bool IsComplete { get; set; }
    }

    /// <summary>
    /// Backup info.
    /// </summary>
    public class BackupInfo
    {
        public long Timestamp { get; set; }
        public DateTime CreatedAt { get; set; }
        public long SizeBytes { get; set; }
    }
}
