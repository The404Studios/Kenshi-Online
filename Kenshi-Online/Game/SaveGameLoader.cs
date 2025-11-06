using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using KenshiMultiplayer.Networking;

namespace KenshiMultiplayer.Game
{
    /// <summary>
    /// Handles loading players into servers via save game mechanism
    /// Creates temporary save games for multiplayer sessions
    /// </summary>
    public class SaveGameLoader
    {
        private readonly string _kenshiSavePath;
        private readonly string _multiplayerSavePath;

        public SaveGameLoader(string kenshiInstallPath = null)
        {
            if (string.IsNullOrEmpty(kenshiInstallPath))
            {
                // Default Kenshi save location
                _kenshiSavePath = Path.Combine(
                    Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
                    "Kenshi", "save"
                );
            }
            else
            {
                _kenshiSavePath = Path.Combine(kenshiInstallPath, "save");
            }

            _multiplayerSavePath = Path.Combine(_kenshiSavePath, "multiplayer");

            // Ensure multiplayer save directory exists
            if (!Directory.Exists(_multiplayerSavePath))
            {
                Directory.CreateDirectory(_multiplayerSavePath);
            }
        }

        /// <summary>
        /// Create a multiplayer save game for joining a server
        /// </summary>
        public async Task<string> CreateMultiplayerSave(string serverName, string playerId, Position spawnPosition, string templateSaveName = null)
        {
            try
            {
                // Generate save game name
                string saveName = $"MP_{serverName}_{playerId}_{DateTime.Now:yyyyMMdd_HHmmss}";
                string savePath = Path.Combine(_multiplayerSavePath, saveName);

                // Create save directory
                Directory.CreateDirectory(savePath);

                // If template save is provided, copy it
                if (!string.IsNullOrEmpty(templateSaveName))
                {
                    string templatePath = Path.Combine(_kenshiSavePath, templateSaveName);
                    if (Directory.Exists(templatePath))
                    {
                        await CopyDirectory(templatePath, savePath);
                    }
                    else
                    {
                        // Create new save
                        await CreateNewSave(savePath, spawnPosition);
                    }
                }
                else
                {
                    // Create new save
                    await CreateNewSave(savePath, spawnPosition);
                }

                // Modify save game for multiplayer
                await ModifySaveForMultiplayer(savePath, playerId, spawnPosition);

                return saveName;
            }
            catch (Exception ex)
            {
                Console.WriteLine($"Failed to create multiplayer save: {ex.Message}");
                return null;
            }
        }

        /// <summary>
        /// Load a player into a server by creating and loading a save game
        /// </summary>
        public async Task<bool> LoadPlayerIntoServer(
            string serverName,
            string playerId,
            Position spawnPosition,
            Action<string> onSaveCreated = null)
        {
            try
            {
                // Create the save game
                string saveName = await CreateMultiplayerSave(serverName, playerId, spawnPosition);
                if (string.IsNullOrEmpty(saveName))
                {
                    return false;
                }

                // Notify callback
                onSaveCreated?.Invoke(saveName);

                // Signal game to load the save
                await SignalGameToLoadSave(saveName);

                return true;
            }
            catch (Exception ex)
            {
                Console.WriteLine($"Failed to load player into server: {ex.Message}");
                return false;
            }
        }

        /// <summary>
        /// Create a new save game
        /// </summary>
        private async Task CreateNewSave(string savePath, Position spawnPosition)
        {
            // Create basic save structure
            Directory.CreateDirectory(Path.Combine(savePath, "gamedata"));
            Directory.CreateDirectory(Path.Combine(savePath, "platoon"));
            Directory.CreateDirectory(Path.Combine(savePath, "zone"));

            // Create quicksave.save file (minimal save data)
            string saveFile = Path.Combine(savePath, "quicksave.save");

            // Generate minimal save data
            var saveData = GenerateMinimalSaveData(spawnPosition);

            await File.WriteAllBytesAsync(saveFile, saveData);

            // Create metadata file
            string metadataFile = Path.Combine(savePath, "metadata.json");
            var metadata = new
            {
                Version = "1.0.0",
                Created = DateTime.Now.ToString("o"),
                Multiplayer = true,
                SpawnPosition = new { spawnPosition.X, spawnPosition.Y, spawnPosition.Z }
            };

            await File.WriteAllTextAsync(metadataFile, Newtonsoft.Json.JsonConvert.SerializeObject(metadata, Newtonsoft.Json.Formatting.Indented));
        }

        /// <summary>
        /// Generate minimal save data for multiplayer
        /// </summary>
        private byte[] GenerateMinimalSaveData(Position spawnPosition)
        {
            // This is a simplified save format - you'll need to match Kenshi's actual format
            using (var ms = new MemoryStream())
            using (var writer = new BinaryWriter(ms))
            {
                // Header
                writer.Write(Encoding.ASCII.GetBytes("KENSHI_SAVE"));
                writer.Write((int)1); // Version

                // World state
                writer.Write((int)0); // Game time
                writer.Write((float)spawnPosition.X);
                writer.Write((float)spawnPosition.Y);
                writer.Write((float)spawnPosition.Z);

                // Minimal character data
                writer.Write((int)1); // Character count
                writer.Write(Encoding.UTF8.GetBytes("Player\0"));
                writer.Write((float)100.0f); // Health
                writer.Write((float)spawnPosition.X);
                writer.Write((float)spawnPosition.Y);
                writer.Write((float)spawnPosition.Z);

                return ms.ToArray();
            }
        }

        /// <summary>
        /// Modify save game for multiplayer compatibility
        /// </summary>
        private async Task ModifySaveForMultiplayer(string savePath, string playerId, Position spawnPosition)
        {
            // Inject multiplayer metadata
            string multiplayerConfig = Path.Combine(savePath, "multiplayer_config.json");

            var config = new
            {
                PlayerId = playerId,
                MultiplayerMode = true,
                SpawnPosition = new { spawnPosition.X, spawnPosition.Y, spawnPosition.Z },
                LastSync = DateTime.Now.ToString("o")
            };

            await File.WriteAllTextAsync(multiplayerConfig, Newtonsoft.Json.JsonConvert.SerializeObject(config, Newtonsoft.Json.Formatting.Indented));
        }

        /// <summary>
        /// Signal the game to load a save file
        /// </summary>
        private async Task SignalGameToLoadSave(string saveName)
        {
            // Create a signal file that the mod will detect
            string signalFile = Path.Combine(_multiplayerSavePath, "..", "load_save_signal.txt");

            await File.WriteAllTextAsync(signalFile, saveName);

            Console.WriteLine($"Signaled game to load save: {saveName}");
        }

        /// <summary>
        /// Copy directory recursively
        /// </summary>
        private async Task CopyDirectory(string sourceDir, string destDir)
        {
            // Create destination directory
            Directory.CreateDirectory(destDir);

            // Copy files
            foreach (string file in Directory.GetFiles(sourceDir))
            {
                string fileName = Path.GetFileName(file);
                string destFile = Path.Combine(destDir, fileName);
                File.Copy(file, destFile, true);
            }

            // Copy subdirectories
            foreach (string dir in Directory.GetDirectories(sourceDir))
            {
                string dirName = Path.GetFileName(dir);
                string destSubDir = Path.Combine(destDir, dirName);
                await CopyDirectory(dir, destSubDir);
            }
        }

        /// <summary>
        /// Clean up old multiplayer saves
        /// </summary>
        public void CleanupOldSaves(int maxAgeHours = 24)
        {
            try
            {
                var saves = Directory.GetDirectories(_multiplayerSavePath);
                var cutoffTime = DateTime.Now.AddHours(-maxAgeHours);

                foreach (var save in saves)
                {
                    var dirInfo = new DirectoryInfo(save);
                    if (dirInfo.CreationTime < cutoffTime)
                    {
                        Directory.Delete(save, true);
                        Console.WriteLine($"Cleaned up old save: {dirInfo.Name}");
                    }
                }
            }
            catch (Exception ex)
            {
                Console.WriteLine($"Failed to cleanup old saves: {ex.Message}");
            }
        }

        /// <summary>
        /// Get list of multiplayer saves
        /// </summary>
        public List<string> GetMultiplayerSaves()
        {
            try
            {
                return Directory.GetDirectories(_multiplayerSavePath)
                    .Select(Path.GetFileName)
                    .ToList();
            }
            catch
            {
                return new List<string>();
            }
        }
    }
}
