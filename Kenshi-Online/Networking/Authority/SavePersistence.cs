using System;
using System.Collections.Generic;
using System.Collections.Concurrent;
using System.IO;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;
using KenshiMultiplayer.Utility;
using KenshiMultiplayer.Data;

namespace KenshiMultiplayer.Networking.Authority
{
    /// <summary>
    /// Save Persistence Contract:
    ///
    /// RULE: Server owns ALL saves. Clients are mirrors only.
    ///
    /// This ensures:
    /// 1. Single source of truth - no conflicting game states
    /// 2. Anti-cheat - clients cannot manipulate save files
    /// 3. Consistency - all players see the same world state
    /// 4. Recovery - server can restore any client's state
    /// </summary>
    public static class SavePersistenceContract
    {
        /// <summary>
        /// The definitive owner of all save data
        /// </summary>
        public const string SAVE_OWNER = "SERVER";

        /// <summary>
        /// Clients can only read saves, never write
        /// </summary>
        public const bool CLIENT_CAN_WRITE = false;

        /// <summary>
        /// Server must validate all state changes before persisting
        /// </summary>
        public const bool REQUIRE_VALIDATION = true;
    }

    /// <summary>
    /// Server-side save manager - the single source of truth for all persistent data
    /// </summary>
    public class ServerSaveManager : IDisposable
    {
        private readonly string savePath;
        private readonly ConcurrentDictionary<string, PlayerSaveData> playerSaves = new();
        private readonly ConcurrentDictionary<string, WorldSaveData> worldSaves = new();
        private readonly SemaphoreSlim saveLock = new(1, 1);
        private readonly Timer autoSaveTimer;
        private readonly int autoSaveIntervalMs = 60000; // 1 minute auto-save

        // Save versioning for conflict detection
        private long globalSaveVersion = 0;
        private readonly object versionLock = new object();

        public event Action<string, PlayerSaveData> OnPlayerSaved;
        public event Action<string, WorldSaveData> OnWorldSaved;
        public event Action<string> OnSaveError;

        public ServerSaveManager(string basePath)
        {
            savePath = Path.Combine(basePath, "saves");
            Directory.CreateDirectory(savePath);
            Directory.CreateDirectory(Path.Combine(savePath, "players"));
            Directory.CreateDirectory(Path.Combine(savePath, "worlds"));
            Directory.CreateDirectory(Path.Combine(savePath, "backups"));

            // Start auto-save timer
            autoSaveTimer = new Timer(AutoSaveCallback, null, autoSaveIntervalMs, autoSaveIntervalMs);
        }

        /// <summary>
        /// Load a player's save data (server-side only)
        /// </summary>
        public async Task<PlayerSaveData> LoadPlayerSave(string playerId)
        {
            string playerPath = Path.Combine(savePath, "players", $"{playerId}.json");

            if (!File.Exists(playerPath))
            {
                // Create new save for new player
                var newSave = CreateNewPlayerSave(playerId);
                await SavePlayerData(playerId, newSave);
                return newSave;
            }

            try
            {
                string json = await File.ReadAllTextAsync(playerPath);
                var save = JsonSerializer.Deserialize<PlayerSaveData>(json);
                playerSaves[playerId] = save;
                return save;
            }
            catch (Exception ex)
            {
                OnSaveError?.Invoke($"Failed to load player save {playerId}: {ex.Message}");
                return CreateNewPlayerSave(playerId);
            }
        }

        /// <summary>
        /// Save player data (server-side only)
        /// </summary>
        public async Task<bool> SavePlayerData(string playerId, PlayerSaveData data)
        {
            await saveLock.WaitAsync();
            try
            {
                // Update version and timestamp
                lock (versionLock)
                {
                    data.SaveVersion = ++globalSaveVersion;
                }
                data.LastSaved = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();

                string playerPath = Path.Combine(savePath, "players", $"{playerId}.json");
                string backupPath = Path.Combine(savePath, "backups", $"{playerId}_{data.SaveVersion}.json");

                // Create backup of existing save
                if (File.Exists(playerPath))
                {
                    File.Copy(playerPath, backupPath, true);
                }

                // Save new data
                var options = new JsonSerializerOptions { WriteIndented = true };
                string json = JsonSerializer.Serialize(data, options);
                await File.WriteAllTextAsync(playerPath, json);

                playerSaves[playerId] = data;
                OnPlayerSaved?.Invoke(playerId, data);

                return true;
            }
            catch (Exception ex)
            {
                OnSaveError?.Invoke($"Failed to save player data {playerId}: {ex.Message}");
                return false;
            }
            finally
            {
                saveLock.Release();
            }
        }

        /// <summary>
        /// Load world save data
        /// </summary>
        public async Task<WorldSaveData> LoadWorldSave(string worldId)
        {
            string worldPath = Path.Combine(savePath, "worlds", $"{worldId}.json");

            if (!File.Exists(worldPath))
            {
                var newWorld = CreateNewWorldSave(worldId);
                await SaveWorldData(worldId, newWorld);
                return newWorld;
            }

            try
            {
                string json = await File.ReadAllTextAsync(worldPath);
                var save = JsonSerializer.Deserialize<WorldSaveData>(json);
                worldSaves[worldId] = save;
                return save;
            }
            catch (Exception ex)
            {
                OnSaveError?.Invoke($"Failed to load world save {worldId}: {ex.Message}");
                return CreateNewWorldSave(worldId);
            }
        }

        /// <summary>
        /// Save world data
        /// </summary>
        public async Task<bool> SaveWorldData(string worldId, WorldSaveData data)
        {
            await saveLock.WaitAsync();
            try
            {
                lock (versionLock)
                {
                    data.SaveVersion = ++globalSaveVersion;
                }
                data.LastSaved = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();

                string worldPath = Path.Combine(savePath, "worlds", $"{worldId}.json");
                string backupPath = Path.Combine(savePath, "backups", $"world_{worldId}_{data.SaveVersion}.json");

                if (File.Exists(worldPath))
                {
                    File.Copy(worldPath, backupPath, true);
                }

                var options = new JsonSerializerOptions { WriteIndented = true };
                string json = JsonSerializer.Serialize(data, options);
                await File.WriteAllTextAsync(worldPath, json);

                worldSaves[worldId] = data;
                OnWorldSaved?.Invoke(worldId, data);

                return true;
            }
            catch (Exception ex)
            {
                OnSaveError?.Invoke($"Failed to save world data {worldId}: {ex.Message}");
                return false;
            }
            finally
            {
                saveLock.Release();
            }
        }

        /// <summary>
        /// Update persistent state for a player (validated by server)
        /// </summary>
        public async Task<bool> UpdatePlayerPersistentState(string playerId, string property, object value)
        {
            if (!playerSaves.TryGetValue(playerId, out var save))
            {
                save = await LoadPlayerSave(playerId);
            }

            // Validate and apply the change
            if (!ValidateStateChange(save, property, value))
            {
                return false;
            }

            ApplyStateChange(save, property, value);
            save.IsDirty = true;

            return true;
        }

        /// <summary>
        /// Validate a state change before applying
        /// </summary>
        private bool ValidateStateChange(PlayerSaveData save, string property, object value)
        {
            // Add validation rules here
            switch (property)
            {
                case "Health":
                    if (value is float health)
                        return health >= 0 && health <= save.MaxHealth;
                    return false;

                case "Experience":
                    if (value is int exp)
                        return exp >= 0;
                    return false;

                case "Money":
                    if (value is int money)
                        return money >= 0;
                    return false;

                default:
                    return true;
            }
        }

        /// <summary>
        /// Apply a validated state change
        /// </summary>
        private void ApplyStateChange(PlayerSaveData save, string property, object value)
        {
            switch (property)
            {
                case "Health":
                    save.Health = Convert.ToSingle(value);
                    break;
                case "MaxHealth":
                    save.MaxHealth = Convert.ToSingle(value);
                    break;
                case "Experience":
                    save.Experience = Convert.ToInt32(value);
                    break;
                case "Level":
                    save.Level = Convert.ToInt32(value);
                    break;
                case "Money":
                    save.Money = Convert.ToInt32(value);
                    break;
                case "Position":
                    if (value is SavedPosition pos)
                    {
                        save.Position = pos;
                    }
                    break;
                case "Inventory":
                    if (value is Dictionary<string, int> inv)
                    {
                        save.Inventory = inv;
                    }
                    break;
                case "Skills":
                    if (value is Dictionary<string, float> skills)
                    {
                        save.Skills = skills;
                    }
                    break;
            }
        }

        /// <summary>
        /// Get a player's save data for mirroring to client
        /// </summary>
        public PlayerSaveData GetPlayerMirror(string playerId)
        {
            playerSaves.TryGetValue(playerId, out var save);
            return save;
        }

        /// <summary>
        /// Create snapshot for client synchronization
        /// </summary>
        public SaveSnapshot CreateClientSnapshot(string playerId)
        {
            if (!playerSaves.TryGetValue(playerId, out var playerSave))
                return null;

            return new SaveSnapshot
            {
                PlayerId = playerId,
                SaveVersion = playerSave.SaveVersion,
                Timestamp = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds(),
                PlayerData = playerSave,
                IsAuthoritative = true // Server's snapshot is always authoritative
            };
        }

        private PlayerSaveData CreateNewPlayerSave(string playerId)
        {
            return new PlayerSaveData
            {
                PlayerId = playerId,
                CreatedAt = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds(),
                Health = 100f,
                MaxHealth = 100f,
                Level = 1,
                Experience = 0,
                Money = 100,
                Position = new SavedPosition { X = 0, Y = 0, Z = 0 },
                Inventory = new Dictionary<string, int>(),
                Skills = new Dictionary<string, float>(),
                FactionRelations = new Dictionary<string, int>(),
                QuestProgress = new Dictionary<string, QuestSaveData>()
            };
        }

        private WorldSaveData CreateNewWorldSave(string worldId)
        {
            return new WorldSaveData
            {
                WorldId = worldId,
                CreatedAt = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds(),
                Buildings = new List<BuildingSaveData>(),
                NPCStates = new Dictionary<string, NPCSaveData>(),
                WorldEvents = new List<WorldEventSaveData>()
            };
        }

        private async void AutoSaveCallback(object state)
        {
            try
            {
                await SaveAllDirty();
            }
            catch (Exception ex)
            {
                // Log but don't throw - async void methods must handle all exceptions
                Console.WriteLine($"[SavePersistence] Auto-save error: {ex.Message}");
            }
        }

        /// <summary>
        /// Save all dirty player data
        /// </summary>
        public async Task SaveAllDirty()
        {
            foreach (var kvp in playerSaves)
            {
                if (kvp.Value.IsDirty)
                {
                    await SavePlayerData(kvp.Key, kvp.Value);
                    kvp.Value.IsDirty = false;
                }
            }

            foreach (var kvp in worldSaves)
            {
                if (kvp.Value.IsDirty)
                {
                    await SaveWorldData(kvp.Key, kvp.Value);
                    kvp.Value.IsDirty = false;
                }
            }
        }

        /// <summary>
        /// Clean up old backups
        /// </summary>
        public void CleanupOldBackups(int keepCount = 10)
        {
            string backupDir = Path.Combine(savePath, "backups");
            var files = Directory.GetFiles(backupDir)
                .Select(f => new FileInfo(f))
                .OrderByDescending(f => f.CreationTime)
                .Skip(keepCount);

            foreach (var file in files)
            {
                try { file.Delete(); } catch { }
            }
        }

        public void Dispose()
        {
            autoSaveTimer?.Dispose();
            try
            {
                SaveAllDirty().GetAwaiter().GetResult();
            }
            catch (Exception ex)
            {
                Logger.Log($"[ServerSaveManager] Error during dispose save: {ex.Message}");
            }
            saveLock?.Dispose();
        }
    }

    /// <summary>
    /// Client-side save mirror - read-only view of server save data
    /// </summary>
    public class ClientSaveMirror
    {
        private PlayerSaveData cachedSave;
        private long lastSyncVersion = 0;
        private readonly object syncLock = new object();

        public event Action<PlayerSaveData> OnSaveUpdated;

        /// <summary>
        /// Apply server snapshot to client mirror
        /// </summary>
        public void ApplyServerSnapshot(SaveSnapshot snapshot)
        {
            if (!snapshot.IsAuthoritative)
            {
                // Only accept authoritative snapshots from server
                return;
            }

            lock (syncLock)
            {
                if (snapshot.SaveVersion <= lastSyncVersion)
                {
                    // Already have newer data
                    return;
                }

                cachedSave = snapshot.PlayerData;
                lastSyncVersion = snapshot.SaveVersion;
            }

            OnSaveUpdated?.Invoke(cachedSave);
        }

        /// <summary>
        /// Get current mirrored save (read-only)
        /// </summary>
        public PlayerSaveData GetMirror()
        {
            lock (syncLock)
            {
                return cachedSave;
            }
        }

        /// <summary>
        /// Check if save needs sync
        /// </summary>
        public bool NeedsSync(long serverVersion)
        {
            return serverVersion > lastSyncVersion;
        }

        /// <summary>
        /// Request save sync from server
        /// </summary>
        public GameMessage CreateSyncRequest(string playerId)
        {
            return new GameMessage(MessageType.PlayerState, playerId)
                .WithData("request_type", "save_sync")
                .WithData("client_version", lastSyncVersion);
        }
    }

    #region Save Data Structures

    public class PlayerSaveData
    {
        public string PlayerId { get; set; }
        public long SaveVersion { get; set; }
        public long CreatedAt { get; set; }
        public long LastSaved { get; set; }
        public bool IsDirty { get; set; }

        // Character stats
        public float Health { get; set; }
        public float MaxHealth { get; set; }
        public int Level { get; set; }
        public int Experience { get; set; }
        public int Money { get; set; }

        // Position (last saved location)
        public SavedPosition Position { get; set; }

        // Inventory
        public Dictionary<string, int> Inventory { get; set; }

        // Skills
        public Dictionary<string, float> Skills { get; set; }

        // Faction relations
        public Dictionary<string, int> FactionRelations { get; set; }

        // Quest progress
        public Dictionary<string, QuestSaveData> QuestProgress { get; set; }

        // Equipment
        public Dictionary<string, string> Equipment { get; set; }

        // Limb health (Kenshi-specific)
        public Dictionary<string, int> LimbHealth { get; set; }
    }

    public class SavedPosition
    {
        public float X { get; set; }
        public float Y { get; set; }
        public float Z { get; set; }
        public float Rotation { get; set; }
    }

    public class QuestSaveData
    {
        public string QuestId { get; set; }
        public string Status { get; set; }
        public int Progress { get; set; }
        public Dictionary<string, object> Data { get; set; }
    }

    public class WorldSaveData
    {
        public string WorldId { get; set; }
        public long SaveVersion { get; set; }
        public long CreatedAt { get; set; }
        public long LastSaved { get; set; }
        public bool IsDirty { get; set; }

        // Buildings placed by players
        public List<BuildingSaveData> Buildings { get; set; }

        // NPC states
        public Dictionary<string, NPCSaveData> NPCStates { get; set; }

        // World events
        public List<WorldEventSaveData> WorldEvents { get; set; }
    }

    public class BuildingSaveData
    {
        public string BuildingId { get; set; }
        public string OwnerId { get; set; }
        public string BuildingType { get; set; }
        public SavedPosition Position { get; set; }
        public int Health { get; set; }
        public Dictionary<string, object> Data { get; set; }
    }

    public class NPCSaveData
    {
        public string NPCId { get; set; }
        public string State { get; set; }
        public SavedPosition Position { get; set; }
        public int Health { get; set; }
        public Dictionary<string, int> Inventory { get; set; }
    }

    public class WorldEventSaveData
    {
        public string EventId { get; set; }
        public string EventType { get; set; }
        public long Timestamp { get; set; }
        public Dictionary<string, object> Data { get; set; }
    }

    public class SaveSnapshot
    {
        public string PlayerId { get; set; }
        public long SaveVersion { get; set; }
        public long Timestamp { get; set; }
        public PlayerSaveData PlayerData { get; set; }
        public bool IsAuthoritative { get; set; }
    }

    #endregion
}
