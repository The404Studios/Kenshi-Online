using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Text.Json;
using System.Text.RegularExpressions;

namespace KenshiMultiplayer.Common
{
    public class GameModManager
    {
        private readonly string kenshiRootPath;
        private readonly string modsPath;
        private readonly string gameDataPath;
        private readonly string modListFile;
        private readonly string userModListFile;
        private List<ModInfo> detectedMods = new List<ModInfo>();
        private List<string> activeModIds = new List<string>();

        public GameModManager(string kenshiRootPath)
        {
            this.kenshiRootPath = kenshiRootPath;
            modsPath = Path.Combine(kenshiRootPath, "mods");
            gameDataPath = Path.Combine(kenshiRootPath, "data");
            modListFile = Path.Combine(kenshiRootPath, "data", "mods.cfg");
            userModListFile = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.MyDocuments), "Kenshi", "mods.cfg");

            // Ensure the paths exist before proceeding
            if (!ValidateKenshiInstallation())
            {
                Logger.Log($"Invalid Kenshi installation path: {kenshiRootPath}");
                throw new DirectoryNotFoundException($"Invalid Kenshi installation path: {kenshiRootPath}");
            }

            // Initialize
            ScanForMods();
            LoadActiveModList();
        }

        public bool ValidateKenshiInstallation()
        {
            // Check basic requirements for a valid Kenshi installation
            if (!Directory.Exists(kenshiRootPath)) return false;

            // Check for key directories and files that should exist in a Kenshi installation
            string executablePath = Path.Combine(kenshiRootPath, "kenshi_x64.exe");

            bool hasExecutable = File.Exists(executablePath);
            bool hasDataFolder = Directory.Exists(gameDataPath);
            bool hasModsFolder = Directory.Exists(modsPath);

            return hasExecutable && hasDataFolder;
        }

        public List<ModInfo> GetAllMods()
        {
            return new List<ModInfo>(detectedMods);
        }

        public List<ModInfo> GetActiveMods()
        {
            return detectedMods.Where(mod => activeModIds.Contains(mod.Id)).ToList();
        }

        public bool IsModeActive(string modId)
        {
            return activeModIds.Contains(modId);
        }

        public ModInfo GetModById(string modId)
        {
            return detectedMods.FirstOrDefault(mod => mod.Id == modId);
        }

        public ModInfo GetModByName(string modName)
        {
            return detectedMods.FirstOrDefault(mod => mod.Name.Equals(modName, StringComparison.OrdinalIgnoreCase));
        }

        private void ScanForMods()
        {
            detectedMods.Clear();

            try
            {
                if (!Directory.Exists(modsPath))
                {
                    Logger.Log($"Mods directory not found: {modsPath}");
                    return;
                }

                // Scan official mods in data folder
                ScanOfficialMods();

                // Scan regular mods in mods folder
                ScanUserMods();

                Logger.Log($"Detected {detectedMods.Count} mods");
            }
            catch (Exception ex)
            {
                Logger.Log($"Error scanning for mods: {ex.Message}");
            }
        }

        private void ScanOfficialMods()
        {
            try
            {
                // Check for mod directories in the data folder (official mods)
                string[] officialModDirs = Directory.GetDirectories(gameDataPath);

                foreach (string modDir in officialModDirs)
                {
                    // Skip non-mod directories that are part of the base game
                    string dirName = Path.GetFileName(modDir);
                    if (IsBaseGameDirectory(dirName)) continue;

                    ModInfo mod = ParseModInfo(modDir, true);
                    if (mod != null)
                    {
                        detectedMods.Add(mod);
                    }
                }
            }
            catch (Exception ex)
            {
                Logger.Log($"Error scanning official mods: {ex.Message}");
            }
        }

        private void ScanUserMods()
        {
            try
            {
                // Check for mod directories in the mods folder (user mods)
                string[] userModDirs = Directory.GetDirectories(modsPath);

                foreach (string modDir in userModDirs)
                {
                    ModInfo mod = ParseModInfo(modDir, false);
                    if (mod != null)
                    {
                        detectedMods.Add(mod);
                    }
                }

                // Also check for mod files (*.mod) in the mods folder
                string[] modFiles = Directory.GetFiles(modsPath, "*.mod");

                foreach (string modFile in modFiles)
                {
                    ModInfo mod = ParseModFile(modFile);
                    if (mod != null)
                    {
                        detectedMods.Add(mod);
                    }
                }
            }
            catch (Exception ex)
            {
                Logger.Log($"Error scanning user mods: {ex.Message}");
            }
        }

        private ModInfo ParseModInfo(string modDir, bool isOfficial)
        {
            try
            {
                string dirName = Path.GetFileName(modDir);
                string infoFile = Path.Combine(modDir, "info.json");
                string metadataFile = Path.Combine(modDir, "mod_info.json");
                string modDescriptionFile = Path.Combine(modDir, "description.txt");

                // Create default mod info
                ModInfo mod = new ModInfo
                {
                    Id = dirName,
                    Name = FormatModName(dirName),
                    Path = modDir,
                    IsOfficial = isOfficial,
                    IsEnabled = false
                };

                // Try to load info from info.json or mod_info.json if they exist
                if (File.Exists(infoFile))
                {
                    try
                    {
                        string jsonContent = File.ReadAllText(infoFile);
                        var modJson = JsonSerializer.Deserialize<Dictionary<string, object>>(jsonContent);

                        if (modJson.TryGetValue("name", out object nameObj))
                            mod.Name = nameObj.ToString();

                        if (modJson.TryGetValue("author", out object authorObj))
                            mod.Author = authorObj.ToString();

                        if (modJson.TryGetValue("description", out object descObj))
                            mod.Description = descObj.ToString();

                        if (modJson.TryGetValue("version", out object versionObj))
                            mod.Version = versionObj.ToString();
                    }
                    catch (Exception ex)
                    {
                        Logger.Log($"Failed to parse info.json for mod {dirName}: {ex.Message}");
                    }
                }
                else if (File.Exists(metadataFile))
                {
                    try
                    {
                        string jsonContent = File.ReadAllText(metadataFile);
                        var modJson = JsonSerializer.Deserialize<Dictionary<string, object>>(jsonContent);

                        if (modJson.TryGetValue("name", out object nameObj))
                            mod.Name = nameObj.ToString();

                        if (modJson.TryGetValue("author", out object authorObj))
                            mod.Author = authorObj.ToString();

                        if (modJson.TryGetValue("description", out object descObj))
                            mod.Description = descObj.ToString();

                        if (modJson.TryGetValue("version", out object versionObj))
                            mod.Version = versionObj.ToString();
                    }
                    catch (Exception ex)
                    {
                        Logger.Log($"Failed to parse mod_info.json for mod {dirName}: {ex.Message}");
                    }
                }

                // Try to load description from description.txt if it exists
                if (File.Exists(modDescriptionFile) && string.IsNullOrEmpty(mod.Description))
                {
                    try
                    {
                        string description = File.ReadAllText(modDescriptionFile);
                        mod.Description = description.Trim();
                    }
                    catch (Exception ex)
                    {
                        Logger.Log($"Failed to read description.txt for mod {dirName}: {ex.Message}");
                    }
                }

                return mod;
            }
            catch (Exception ex)
            {
                Logger.Log($"Error parsing mod info for {modDir}: {ex.Message}");
                return null;
            }
        }

        private ModInfo ParseModFile(string modFile)
        {
            try
            {
                string fileName = Path.GetFileNameWithoutExtension(modFile);

                // Create default mod info
                ModInfo mod = new ModInfo
                {
                    Id = fileName,
                    Name = FormatModName(fileName),
                    Path = modFile,
                    IsOfficial = false,
                    IsEnabled = false
                };

                // Try to extract metadata from the .mod file
                // Kenshi mod files are typically binary files, but some might have a header with info
                try
                {
                    using (var stream = new FileStream(modFile, FileMode.Open, FileAccess.Read))
                    using (var reader = new BinaryReader(stream))
                    {
                        // Read first bytes to try to identify the mod format
                        byte[] header = reader.ReadBytes(16);

                        // Implementation would depend on the actual format of .mod files
                        // This is a placeholder that would need to be expanded based on the format
                    }
                }
                catch (Exception ex)
                {
                    Logger.Log($"Failed to parse .mod file for {fileName}: {ex.Message}");
                }

                return mod;
            }
            catch (Exception ex)
            {
                Logger.Log($"Error parsing mod file {modFile}: {ex.Message}");
                return null;
            }
        }

        private void LoadActiveModList()
        {
            activeModIds.Clear();

            try
            {
                // Check user mod list first (takes precedence)
                string activeModsFile = userModListFile;
                if (!File.Exists(activeModsFile))
                {
                    // Fall back to game directory mod list
                    activeModsFile = modListFile;
                    if (!File.Exists(activeModsFile))
                    {
                        Logger.Log("No mods.cfg file found");
                        return;
                    }
                }

                string[] lines = File.ReadAllLines(activeModsFile);

                foreach (string line in lines)
                {
                    string trimmedLine = line.Trim();
                    if (string.IsNullOrEmpty(trimmedLine) || trimmedLine.StartsWith("#")) continue;

                    // Kenshi mod.cfg typically has mod IDs or paths listed one per line
                    // Some lines might have additional metadata - extract just the mod ID/path
                    string modId = ExtractModIdFromConfigLine(trimmedLine);

                    if (!string.IsNullOrEmpty(modId))
                    {
                        activeModIds.Add(modId);

                        // Mark mod as enabled in our list
                        var mod = detectedMods.FirstOrDefault(m => m.Id == modId);
                        if (mod != null)
                        {
                            mod.IsEnabled = true;
                        }
                    }
                }

                Logger.Log($"Loaded {activeModIds.Count} active mods from config");
            }
            catch (Exception ex)
            {
                Logger.Log($"Error loading active mod list: {ex.Message}");
            }
        }

        private string ExtractModIdFromConfigLine(string line)
        {
            // Strip off any comments
            int commentIndex = line.IndexOf('#');
            if (commentIndex >= 0)
            {
                line = line.Substring(0, commentIndex).Trim();
            }

            // Extract the mod ID - logic depends on the format of mods.cfg
            // This is a simplified approach - might need to be adjusted
            string[] parts = line.Split(new[] { ':', '=', ' ', '\t' }, StringSplitOptions.RemoveEmptyEntries);

            if (parts.Length > 0)
            {
                // The mod ID might be a path, just the directory name, or some other format
                string modId = parts[0].Trim();

                // If it's a path, extract just the directory name
                if (modId.Contains('/') || modId.Contains('\\'))
                {
                    modId = Path.GetFileName(modId.Replace('/', Path.DirectorySeparatorChar).Replace('\\', Path.DirectorySeparatorChar));
                }

                return modId;
            }

            return null;
        }

        private bool IsBaseGameDirectory(string dirName)
        {
            // List of directories that are part of the base game and not mods
            string[] baseGameDirs = new[]
            {
                "ai",
                "backup",
                "biomes",
                "build",
                "characters",
                "effects",
                "env",
                "fcs",
                "gui",
                "items",
                "lights",
                "meshes",
                "particles",
                "physics",
                "props",
                "savedata",
                "shaders",
                "sound",
                "textures",
                "weather"
            };

            return baseGameDirs.Contains(dirName.ToLower());
        }

        private string FormatModName(string rawName)
        {
            // Convert directory naming conventions to readable names
            // e.g. "reactive_world" -> "Reactive World"

            // Replace underscores and hyphens with spaces
            string name = rawName.Replace('_', ' ').Replace('-', ' ');

            // Add spaces before capital letters (e.g. "DarkUI" -> "Dark UI")
            name = Regex.Replace(name, "([a-z])([A-Z])", "$1 $2");

            // Capitalize first letter of each word
            TextInfo textInfo = new CultureInfo("en-US", false).TextInfo;
            name = textInfo.ToTitleCase(name.ToLower());

            return name;
        }

        // Try to locate the Kenshi installation path if none was provided
        public static string FindKenshiInstallation()
        {
            // Common installation paths to check
            List<string> commonPaths = new List<string>
            {
                // Steam default path
                Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86), "Steam", "steamapps", "common", "Kenshi"),
                Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles), "Steam", "steamapps", "common", "Kenshi"),
                
                // GOG default path
                Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86), "GOG Galaxy", "Games", "Kenshi"),
                Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles), "GOG Galaxy", "Games", "Kenshi"),
                
                // Direct installation path
                Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86), "Kenshi"),
                Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles), "Kenshi")
            };

            // Check each path
            foreach (string path in commonPaths)
            {
                if (Directory.Exists(path) && File.Exists(Path.Combine(path, "kenshi_x64.exe")))
                {
                    return path;
                }
            }

            // Try to find via Steam registry entries
            try
            {
                // This would use registry access to find Steam installation and then Kenshi
                // Implementation depends on platform and access to registry
                // Microsoft.Win32.Registry could be used here
            }
            catch (Exception ex)
            {
                Logger.Log($"Error finding Kenshi via registry: {ex.Message}");
            }

            return null;
        }
    }

    public class ModInfo
    {
        public string Id { get; set; }
        public string Name { get; set; }
        public string Path { get; set; }
        public string Author { get; set; }
        public string Description { get; set; }
        public string Version { get; set; }
        public bool IsOfficial { get; set; }
        public bool IsEnabled { get; set; }
        public List<string> Dependencies { get; set; } = new List<string>();
        public DateTime? LastUpdated { get; set; }
        public long FileSize { get; set; }
    }
}