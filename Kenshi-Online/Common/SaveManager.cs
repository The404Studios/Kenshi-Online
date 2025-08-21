using KenshiMultiplayer.Common;
using System;
using System.Collections.Generic;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Text;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;
using KenshiMultiplayer.Auth;
using KenshiMultiplayer.Networking;
using KenshiMultiplayer.Utility;

namespace KenshiMultiplayer.Common
{
    /// <summary>
    /// Manages saving and loading of multiplayer game states
    /// </summary>
    public class SaveManager
    {
        private readonly string saveDirectory;
        private readonly string autoSaveDirectory;
        private readonly string cloudSaveDirectory;

        // Save configuration
        private SaveConfig config;
        private Timer autoSaveTimer;
        private bool isSaving = false;

        // Save versioning
        private const int SAVE_VERSION = 1;
        private const string SAVE_EXTENSION = ".kmp";
        private const string BACKUP_EXTENSION = ".bak";

        // Events
        public event Action<SaveInfo> OnSaveStarted;
        public event Action<SaveInfo> OnSaveCompleted;
        public event Action<string> OnSaveFailed;
        public event Action<SaveInfo> OnLoadStarted;
        public event Action<SaveInfo> OnLoadCompleted;
        public event Action<string> OnLoadFailed;

        public SaveManager()
        {
            var baseDir = Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.MyDocuments),
                "Kenshi",
                "Multiplayer",
                "Saves"
            );

            saveDirectory = Path.Combine(baseDir, "Manual");
            autoSaveDirectory = Path.Combine(baseDir, "Auto");
            cloudSaveDirectory = Path.Combine(baseDir, "Cloud");

            EnsureDirectories();
            LoadConfig();
        }

        /// <summary>
        /// Initialize save manager
        /// </summary>
        public void Initialize()
        {
            if (config.EnableAutoSave)
            {
                StartAutoSave();
            }

            if (config.EnableCloudSaves)
            {
                InitializeCloudSync();
            }

            CleanupOldSaves();
        }

        /// <summary>
        /// Save game state
        /// </summary>
        public async Task<SaveResult> SaveGame(string saveName, GameState state, SaveType type = SaveType.Manual)
        {
            if (isSaving)
            {
                return new SaveResult { Success = false, Message = "Save already in progress" };
            }

            isSaving = true;
            var saveInfo = new SaveInfo
            {
                Name = saveName,
                Type = type,
                Version = SAVE_VERSION,
                Timestamp = DateTime.UtcNow,
                GameVersion = GetGameVersion(),
                ModVersion = "1.0.0"
            };

            try
            {
                OnSaveStarted?.Invoke(saveInfo);

                // Determine save path
                var directory = type == SaveType.Auto ? autoSaveDirectory : saveDirectory;
                var fileName = SanitizeFileName(saveName) + SAVE_EXTENSION;
                var savePath = Path.Combine(directory, fileName);

                // Create backup if file exists
                if (File.Exists(savePath) && config.EnableBackups)
                {
                    CreateBackup(savePath);
                }

                // Prepare save data
                var saveData = new SaveData
                {
                    Info = saveInfo,
                    State = state,
                    Players = GetPlayerData(),
                    ServerSettings = GetServerSettings(),
                    ModData = GetModData()
                };

                // Validate save data
                if (!ValidateSaveData(saveData))
                {
                    throw new Exception("Save data validation failed");
                }

                // Serialize and compress
                var serialized = SerializeSaveData(saveData);
                var compressed = await CompressData(serialized);

                // Add checksum
                var checksum = CalculateChecksum(compressed);
                var finalData = new byte[compressed.Length + 32];
                Array.Copy(Encoding.UTF8.GetBytes(checksum), 0, finalData, 0, 32);
                Array.Copy(compressed, 0, finalData, 32, compressed.Length);

                // Write to disk
                await File.WriteAllBytesAsync(savePath, finalData);

                // Update save index
                UpdateSaveIndex(saveInfo, savePath);

                // Cloud sync if enabled
                if (config.EnableCloudSaves)
                {
                    await SyncToCloud(savePath);
                }

                OnSaveCompleted?.Invoke(saveInfo);

                Logger.Log($"Game saved successfully: {saveName}");
                return new SaveResult { Success = true, SavePath = savePath };
            }
            catch (Exception ex)
            {
                OnSaveFailed?.Invoke(ex.Message);
                Logger.LogError($"Failed to save game: {saveName}", ex);
                return new SaveResult { Success = false, Message = ex.Message };
            }
            finally
            {
                isSaving = false;
            }
        }

        /// <summary>
        /// Load game state
        /// </summary>
        public async Task<LoadResult> LoadGame(string savePath)
        {
            var saveInfo = GetSaveInfo(savePath);
            if (saveInfo == null)
            {
                return new LoadResult { Success = false, Message = "Invalid save file" };
            }

            try
            {
                OnLoadStarted?.Invoke(saveInfo);

                // Read file
                var fileData = await File.ReadAllBytesAsync(savePath);

                // Verify checksum
                var checksum = Encoding.UTF8.GetString(fileData, 0, 32);
                var compressed = new byte[fileData.Length - 32];
                Array.Copy(fileData, 32, compressed, 0, compressed.Length);

                if (CalculateChecksum(compressed) != checksum)
                {
                    throw new Exception("Save file corrupted (checksum mismatch)");
                }

                // Decompress
                var decompressed = await DecompressData(compressed);

                // Deserialize
                var saveData = DeserializeSaveData(decompressed);

                // Validate version compatibility
                if (!IsVersionCompatible(saveData.Info.Version))
                {
                    throw new Exception($"Incompatible save version: {saveData.Info.Version}");
                }

                // Validate mod compatibility
                if (!ValidateModCompatibility(saveData.ModData))
                {
                    throw new Exception("Mod incompatibility detected");
                }

                // Migrate if needed
                if (saveData.Info.Version < SAVE_VERSION)
                {
                    saveData = MigrateSaveData(saveData);
                }

                OnLoadCompleted?.Invoke(saveInfo);

                Logger.Log($"Game loaded successfully: {saveInfo.Name}");
                return new LoadResult
                {
                    Success = true,
                    SaveData = saveData,
                    State = saveData.State
                };
            }
            catch (Exception ex)
            {
                OnLoadFailed?.Invoke(ex.Message);
                Logger.LogError($"Failed to load game: {savePath}", ex);
                return new LoadResult { Success = false, Message = ex.Message };
            }
        }

        /// <summary>
        /// Quick save
        /// </summary>
        public async Task<SaveResult> QuickSave()
        {
            var saveName = $"QuickSave_{DateTime.Now:yyyyMMdd_HHmmss}";
            return await SaveGame(saveName, GetCurrentGameState(), SaveType.Quick);
        }

        /// <summary>
        /// Quick load
        /// </summary>
        public async Task<LoadResult> QuickLoad()
        {
            var latestQuickSave = GetLatestSave(SaveType.Quick);
            if (latestQuickSave == null)
            {
                return new LoadResult { Success = false, Message = "No quick save found" };
            }

            return await LoadGame(latestQuickSave);
        }

        /// <summary>
        /// Get list of available saves
        /// </summary>
        public List<SaveInfo> GetSaveList()
        {
            var saves = new List<SaveInfo>();

            // Get manual saves
            saves.AddRange(GetSavesFromDirectory(saveDirectory, SaveType.Manual));

            // Get auto saves
            saves.AddRange(GetSavesFromDirectory(autoSaveDirectory, SaveType.Auto));

            // Get cloud saves if enabled
            if (config.EnableCloudSaves)
            {
                saves.AddRange(GetSavesFromDirectory(cloudSaveDirectory, SaveType.Cloud));
            }

            // Sort by date
            saves.Sort((a, b) => b.Timestamp.CompareTo(a.Timestamp));

            return saves;
        }

        /// <summary>
        /// Delete save
        /// </summary>
        public bool DeleteSave(string savePath)
        {
            try
            {
                if (File.Exists(savePath))
                {
                    // Delete backup too
                    var backupPath = savePath + BACKUP_EXTENSION;
                    if (File.Exists(backupPath))
                    {
                        File.Delete(backupPath);
                    }

                    File.Delete(savePath);
                    RemoveFromIndex(savePath);

                    Logger.Log($"Deleted save: {savePath}");
                    return true;
                }

                return false;
            }
            catch (Exception ex)
            {
                Logger.LogError($"Failed to delete save: {savePath}", ex);
                return false;
            }
        }

        /// <summary>
        /// Start auto-save timer
        /// </summary>
        private void StartAutoSave()
        {
            var interval = TimeSpan.FromSeconds(config.AutoSaveInterval);
            autoSaveTimer = new Timer(async _ => await AutoSave(), null, interval, interval);
        }

        /// <summary>
        /// Perform auto-save
        /// </summary>
        private async Task AutoSave()
        {
            try
            {
                var saveName = $"AutoSave_{DateTime.Now:yyyyMMdd_HHmmss}";
                await SaveGame(saveName, GetCurrentGameState(), SaveType.Auto);

                // Cleanup old auto-saves
                CleanupAutoSaves();
            }
            catch (Exception ex)
            {
                Logger.LogError("Auto-save failed", ex);
            }
        }

        /// <summary>
        /// Initialize cloud synchronization
        /// </summary>
        private void InitializeCloudSync()
        {
            // Initialize cloud storage provider (Steam Cloud, custom server, etc.)
            Task.Run(async () =>
            {
                try
                {
                    await SyncFromCloud();
                }
                catch (Exception ex)
                {
                    Logger.LogError("Cloud sync initialization failed", ex);
                }
            });
        }

        /// <summary>
        /// Sync save to cloud
        /// </summary>
        private async Task SyncToCloud(string savePath)
        {
            try
            {
                var fileName = Path.GetFileName(savePath);
                var cloudPath = Path.Combine(cloudSaveDirectory, fileName);

                await File.Copy(savePath, cloudPath, true);

                // Upload to cloud service
                // This would integrate with Steam Cloud or custom service

                Logger.Log($"Synced to cloud: {fileName}");
            }
            catch (Exception ex)
            {
                Logger.LogError("Cloud sync failed", ex);
            }
        }

        /// <summary>
        /// Sync saves from cloud
        /// </summary>
        private async Task SyncFromCloud()
        {
            // Download saves from cloud service
            // This would integrate with Steam Cloud or custom service
        }

        /// <summary>
        /// Create backup of existing save
        /// </summary>
        private void CreateBackup(string savePath)
        {
            try
            {
                var backupPath = savePath + BACKUP_EXTENSION;
                File.Copy(savePath, backupPath, true);

                // Keep only N backups
                CleanupBackups(savePath);
            }
            catch (Exception ex)
            {
                Logger.LogError($"Failed to create backup: {savePath}", ex);
            }
        }

        /// <summary>
        /// Cleanup old backups
        /// </summary>
        private void CleanupBackups(string savePath)
        {
            var directory = Path.GetDirectoryName(savePath);
            var baseName = Path.GetFileNameWithoutExtension(savePath);

            var backups = Directory.GetFiles(directory, $"{baseName}*.bak")
                .OrderByDescending(f => File.GetCreationTime(f))
                .Skip(config.MaxBackupsPerSave)
                .ToList();

            foreach (var backup in backups)
            {
                try
                {
                    File.Delete(backup);
                }
                catch { }
            }
        }

        /// <summary>
        /// Cleanup old auto-saves
        /// </summary>
        private void CleanupAutoSaves()
        {
            try
            {
                var autoSaves = Directory.GetFiles(autoSaveDirectory, "*.kmp")
                    .Select(f => new { Path = f, Created = File.GetCreationTime(f) })
                    .OrderByDescending(f => f.Created)
                    .Skip(config.MaxAutoSaves)
                    .ToList();

                foreach (var save in autoSaves)
                {
                    File.Delete(save.Path);
                }
            }
            catch (Exception ex)
            {
                Logger.LogError("Failed to cleanup auto-saves", ex);
            }
        }

        /// <summary>
        /// Cleanup old saves based on age
        /// </summary>
        private void CleanupOldSaves()
        {
            if (!config.EnableSaveCleanup) return;

            try
            {
                var cutoffDate = DateTime.Now.AddDays(-config.SaveRetentionDays);

                var oldSaves = Directory.GetFiles(saveDirectory, "*.kmp")
                    .Where(f => File.GetCreationTime(f) < cutoffDate)
                    .ToList();

                foreach (var save in oldSaves)
                {
                    DeleteSave(save);
                }

                Logger.Log($"Cleaned up {oldSaves.Count} old saves");
            }
            catch (Exception ex)
            {
                Logger.LogError("Save cleanup failed", ex);
            }
        }

        /// <summary>
        /// Serialize save data
        /// </summary>
        private byte[] SerializeSaveData(SaveData data)
        {
            var options = new JsonSerializerOptions
            {
                WriteIndented = false,
                IncludeFields = true
            };

            var json = JsonSerializer.Serialize(data, options);
            return Encoding.UTF8.GetBytes(json);
        }

        /// <summary>
        /// Deserialize save data
        /// </summary>
        private SaveData DeserializeSaveData(byte[] data)
        {
            var json = Encoding.UTF8.GetString(data);
            return JsonSerializer.Deserialize<SaveData>(json);
        }

        /// <summary>
        /// Compress data
        /// </summary>
        private async Task<byte[]> CompressData(byte[] data)
        {
            using (var output = new MemoryStream())
            {
                using (var compressor = new GZipStream(output, CompressionLevel.Optimal))
                {
                    await compressor.WriteAsync(data, 0, data.Length);
                }
                return output.ToArray();
            }
        }

        /// <summary>
        /// Decompress data
        /// </summary>
        private async Task<byte[]> DecompressData(byte[] data)
        {
            using (var input = new MemoryStream(data))
            using (var output = new MemoryStream())
            {
                using (var decompressor = new GZipStream(input, CompressionMode.Decompress))
                {
                    await decompressor.CopyToAsync(output);
                }
                return output.ToArray();
            }
        }

        /// <summary>
        /// Calculate checksum
        /// </summary>
        private string CalculateChecksum(byte[] data)
        {
            using (var sha256 = System.Security.Cryptography.SHA256.Create())
            {
                var hash = sha256.ComputeHash(data);
                return Convert.ToBase64String(hash).Substring(0, 32);
            }
        }

        /// <summary>
        /// Validate save data
        /// </summary>
        private bool ValidateSaveData(SaveData data)
        {
            if (data == null || data.State == null)
                return false;

            if (string.IsNullOrEmpty(data.Info.Name))
                return false;

            // Additional validation
            return true;
        }

        /// <summary>
        /// Check version compatibility
        /// </summary>
        private bool IsVersionCompatible(int version)
        {
            return version <= SAVE_VERSION && version >= 1;
        }

        /// <summary>
        /// Validate mod compatibility
        /// </summary>
        private bool ValidateModCompatibility(Dictionary<string, ModInfo> modData)
        {
            // Check if required mods are present
            // Check mod versions
            return true;
        }

        /// <summary>
        /// Migrate old save data
        /// </summary>
        private SaveData MigrateSaveData(SaveData oldData)
        {
            // Perform migration based on version
            Logger.Log($"Migrating save from version {oldData.Info.Version} to {SAVE_VERSION}");

            // Migration logic here

            oldData.Info.Version = SAVE_VERSION;
            return oldData;
        }

        /// <summary>
        /// Get current game state
        /// </summary>
        private GameState GetCurrentGameState()
        {
            // Get from state manager
            return new GameState();
        }

        /// <summary>
        /// Get player data
        /// </summary>
        private List<PlayerSaveData> GetPlayerData()
        {
            return new List<PlayerSaveData>();
        }

        /// <summary>
        /// Get server settings
        /// </summary>
        private ServerSettings GetServerSettings()
        {
            return new ServerSettings();
        }

        /// <summary>
        /// Get mod data
        /// </summary>
        private Dictionary<string, ModInfo> GetModData()
        {
            return new Dictionary<string, ModInfo>();
        }

        /// <summary>
        /// Get game version
        /// </summary>
        private string GetGameVersion()
        {
            return "0.98.50";
        }

        /// <summary>
        /// Get save info from file
        /// </summary>
        private SaveInfo GetSaveInfo(string savePath)
        {
            try
            {
                // Read header without loading entire file
                using (var stream = File.OpenRead(savePath))
                {
                    // Skip checksum
                    stream.Seek(32, SeekOrigin.Begin);

                    // Read and decompress header
                    var buffer = new byte[1024];
                    stream.Read(buffer, 0, buffer.Length);

                    // Parse header for save info
                    // This is simplified - would need proper header parsing
                    return new SaveInfo
                    {
                        Name = Path.GetFileNameWithoutExtension(savePath),
                        Timestamp = File.GetCreationTime(savePath)
                    };
                }
            }
            catch
            {
                return null;
            }
        }

        /// <summary>
        /// Get saves from directory
        /// </summary>
        private List<SaveInfo> GetSavesFromDirectory(string directory, SaveType type)
        {
            var saves = new List<SaveInfo>();

            if (!Directory.Exists(directory))
                return saves;

            try
            {
                var files = Directory.GetFiles(directory, "*" + SAVE_EXTENSION);

                foreach (var file in files)
                {
                    var info = GetSaveInfo(file);
                    if (info != null)
                    {
                        info.Type = type;
                        info.FilePath = file;
                        saves.Add(info);
                    }
                }
            }
            catch (Exception ex)
            {
                Logger.LogError($"Failed to get saves from {directory}", ex);
            }

            return saves;
        }

        /// <summary>
        /// Get latest save of type
        /// </summary>
        private string GetLatestSave(SaveType type)
        {
            var directory = type == SaveType.Auto ? autoSaveDirectory : saveDirectory;

            if (!Directory.Exists(directory))
                return null;

            return Directory.GetFiles(directory, "*" + SAVE_EXTENSION)
                .OrderByDescending(f => File.GetCreationTime(f))
                .FirstOrDefault();
        }

        /// <summary>
        /// Update save index
        /// </summary>
        private void UpdateSaveIndex(SaveInfo info, string path)
        {
            // Update index file for quick access
        }

        /// <summary>
        /// Remove from save index
        /// </summary>
        private void RemoveFromIndex(string path)
        {
            // Remove from index file
        }

        /// <summary>
        /// Sanitize file name
        /// </summary>
        private string SanitizeFileName(string fileName)
        {
            var invalid = Path.GetInvalidFileNameChars();
            return string.Join("_", fileName.Split(invalid, StringSplitOptions.RemoveEmptyEntries));
        }

        /// <summary>
        /// Ensure directories exist
        /// </summary>
        private void EnsureDirectories()
        {
            Directory.CreateDirectory(saveDirectory);
            Directory.CreateDirectory(autoSaveDirectory);
            Directory.CreateDirectory(cloudSaveDirectory);
        }

        /// <summary>
        /// Load configuration
        /// </summary>
        private void LoadConfig()
        {
            config = new SaveConfig
            {
                EnableAutoSave = true,
                AutoSaveInterval = 300, // 5 minutes
                MaxAutoSaves = 10,
                EnableBackups = true,
                MaxBackupsPerSave = 3,
                EnableCloudSaves = false,
                EnableCompression = true,
                EnableSaveCleanup = true,
                SaveRetentionDays = 30
            };
        }
    }

    // Data structures
    public class SaveData
    {
        public SaveInfo Info { get; set; }
        public GameState State { get; set; }
        public List<PlayerSaveData> Players { get; set; }
        public ServerSettings ServerSettings { get; set; }
        public Dictionary<string, ModInfo> ModData { get; set; }
    }

    public class SaveInfo
    {
        public string Name { get; set; }
        public SaveType Type { get; set; }
        public int Version { get; set; }
        public DateTime Timestamp { get; set; }
        public string GameVersion { get; set; }
        public string ModVersion { get; set; }
        public string FilePath { get; set; }
        public long FileSize { get; set; }
        public string Description { get; set; }
    }

    public class PlayerSaveData
    {
        public string PlayerId { get; set; }
        public string PlayerName { get; set; }
        public List<string> CharacterIds { get; set; }
        public Dictionary<string, object> CustomData { get; set; }
    }

    public class ServerSettings
    {
        public string ServerName { get; set; }
        public int MaxPlayers { get; set; }
        public bool PvPEnabled { get; set; }
        public Dictionary<string, float> Multipliers { get; set; }
        public List<string> EnabledMods { get; set; }
    }

    public class ModInfo
    {
        public string Name { get; set; }
        public string Version { get; set; }
        public string Author { get; set; }
        public bool Required { get; set; }
    }

    public class SaveConfig
    {
        public bool EnableAutoSave { get; set; }
        public int AutoSaveInterval { get; set; }
        public int MaxAutoSaves { get; set; }
        public bool EnableBackups { get; set; }
        public int MaxBackupsPerSave { get; set; }
        public bool EnableCloudSaves { get; set; }
        public bool EnableCompression { get; set; }
        public bool EnableSaveCleanup { get; set; }
        public int SaveRetentionDays { get; set; }
    }

    public class SaveResult
    {
        public bool Success { get; set; }
        public string SavePath { get; set; }
        public string Message { get; set; }
    }

    public class LoadResult
    {
        public bool Success { get; set; }
        public SaveData SaveData { get; set; }
        public GameState State { get; set; }
        public string Message { get; set; }
    }

    public enum SaveType
    {
        Manual,
        Auto,
        Quick,
        Cloud,
        Backup
    }
}