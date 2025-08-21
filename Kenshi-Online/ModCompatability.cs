using KenshiMultiplayer.Common;
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using System.Text.Json;
using System.Threading.Tasks;
using System.Xml.Linq;
using KenshiMultiplayer.Auth;
using KenshiMultiplayer.Networking;
using KenshiMultiplayer.Utility;

namespace KenshiMultiplayer.Common
{
    /// <summary>
    /// Manages mod compatibility and synchronization for multiplayer
    /// </summary>
    public class ModCompatibility
    {
        private readonly string modsDirectory;
        private readonly string modConfigPath;

        // Loaded mods
        private Dictionary<string, ModDefinition> loadedMods;
        private Dictionary<string, ModCompatibilityInfo> compatibilityDatabase;
        private List<ModConflict> detectedConflicts;

        // Mod categories
        private Dictionary<ModCategory, List<string>> categorizedMods;

        // Events
        public event Action<ModDefinition> OnModLoaded;
        public event Action<ModDefinition> OnModUnloaded;
        public event Action<ModConflict> OnConflictDetected;
        public event Action<ModSyncRequest> OnSyncRequired;

        // Multiplayer compatibility levels
        private Dictionary<string, CompatibilityLevel> modCompatibilityLevels;

        public ModCompatibility()
        {
            modsDirectory = Path.Combine(GetKenshiPath(), "mods");
            modConfigPath = Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
                "KenshiMultiplayer",
                "ModConfig"
            );

            loadedMods = new Dictionary<string, ModDefinition>();
            compatibilityDatabase = new Dictionary<string, ModCompatibilityInfo>();
            detectedConflicts = new List<ModConflict>();
            categorizedMods = new Dictionary<ModCategory, List<string>>();
            modCompatibilityLevels = new Dictionary<string, CompatibilityLevel>();

            Initialize();
        }

        /// <summary>
        /// Initialize mod system
        /// </summary>
        private void Initialize()
        {
            LoadCompatibilityDatabase();
            ScanInstalledMods();
            CategorIzeMods();
            CheckForConflicts();
        }

        /// <summary>
        /// Load mod compatibility database
        /// </summary>
        private void LoadCompatibilityDatabase()
        {
            // Load known mod compatibility information
            // This would be loaded from a file or server

            // Example entries
            compatibilityDatabase["genesis"] = new ModCompatibilityInfo
            {
                ModId = "genesis",
                Name = "Genesis",
                CompatibilityLevel = CompatibilityLevel.ClientSide,
                ConflictsWith = new List<string> { "reactive_world" },
                RequiresSameVersion = false,
                SyncRequired = new[] { "data/items.xml", "data/races.xml" }
            };

            compatibilityDatabase["reactive_world"] = new ModCompatibilityInfo
            {
                ModId = "reactive_world",
                Name = "Reactive World",
                CompatibilityLevel = CompatibilityLevel.ServerSide,
                ConflictsWith = new List<string> { "genesis" },
                RequiresSameVersion = true,
                SyncRequired = new[] { "data/world.xml", "data/factions.xml" }
            };

            compatibilityDatabase["hives_expanded"] = new ModCompatibilityInfo
            {
                ModId = "hives_expanded",
                Name = "Hives Expanded",
                CompatibilityLevel = CompatibilityLevel.FullyCompatible,
                ConflictsWith = new List<string>(),
                RequiresSameVersion = true,
                SyncRequired = new[] { "data/dialogue.xml" }
            };
        }

        /// <summary>
        /// Scan for installed mods
        /// </summary>
        private void ScanInstalledMods()
        {
            if (!Directory.Exists(modsDirectory))
            {
                Logger.Log("Mods directory not found");
                return;
            }

            var modFolders = Directory.GetDirectories(modsDirectory);

            foreach (var folder in modFolders)
            {
                try
                {
                    var modInfo = LoadModInfo(folder);
                    if (modInfo != null)
                    {
                        loadedMods[modInfo.Id] = modInfo;
                        OnModLoaded?.Invoke(modInfo);

                        Logger.Log($"Loaded mod: {modInfo.Name} v{modInfo.Version}");
                    }
                }
                catch (Exception ex)
                {
                    Logger.LogError($"Failed to load mod from {folder}", ex);
                }
            }

            Logger.Log($"Loaded {loadedMods.Count} mods");
        }

        /// <summary>
        /// Load mod information from folder
        /// </summary>
        private ModDefinition LoadModInfo(string modPath)
        {
            var modFile = Path.Combine(modPath, "mod.info");
            if (!File.Exists(modFile))
            {
                // Try alternative formats
                modFile = Path.Combine(modPath, "mod.xml");
                if (!File.Exists(modFile))
                {
                    return null;
                }
            }

            var mod = new ModDefinition
            {
                Id = Path.GetFileName(modPath),
                Path = modPath
            };

            // Parse mod info file
            if (modFile.EndsWith(".xml"))
            {
                var xml = XDocument.Load(modFile);
                mod.Name = xml.Root?.Element("name")?.Value ?? mod.Id;
                mod.Version = xml.Root?.Element("version")?.Value ?? "1.0.0";
                mod.Author = xml.Root?.Element("author")?.Value ?? "Unknown";
                mod.Description = xml.Root?.Element("description")?.Value ?? "";

                // Parse dependencies
                var deps = xml.Root?.Element("dependencies");
                if (deps != null)
                {
                    mod.Dependencies = deps.Elements("dependency")
                        .Select(d => d.Value)
                        .ToList();
                }

                // Parse files
                var files = xml.Root?.Element("files");
                if (files != null)
                {
                    mod.ModifiedFiles = files.Elements("file")
                        .Select(f => f.Value)
                        .ToList();
                }
            }
            else
            {
                // Parse text-based mod.info
                var lines = File.ReadAllLines(modFile);
                foreach (var line in lines)
                {
                    var parts = line.Split('=', 2);
                    if (parts.Length != 2) continue;

                    var key = parts[0].Trim();
                    var value = parts[1].Trim();

                    switch (key.ToLower())
                    {
                        case "name":
                            mod.Name = value;
                            break;
                        case "version":
                            mod.Version = value;
                            break;
                        case "author":
                            mod.Author = value;
                            break;
                        case "description":
                            mod.Description = value;
                            break;
                    }
                }
            }

            // Calculate mod hash for verification
            mod.Hash = CalculateModHash(modPath);

            // Determine category
            mod.Category = DetermineModCategory(mod);

            // Check compatibility
            if (compatibilityDatabase.TryGetValue(mod.Id, out var compatInfo))
            {
                mod.CompatibilityLevel = compatInfo.CompatibilityLevel;
            }
            else
            {
                mod.CompatibilityLevel = CompatibilityLevel.Unknown;
            }

            return mod;
        }

        /// <summary>
        /// Categorize mods
        /// </summary>
        private void CategorIzeMods()
        {
            foreach (ModCategory category in Enum.GetValues(typeof(ModCategory)))
            {
                categorizedMods[category] = new List<string>();
            }

            foreach (var mod in loadedMods.Values)
            {
                categorizedMods[mod.Category].Add(mod.Id);
            }
        }

        /// <summary>
        /// Determine mod category based on content
        /// </summary>
        private ModCategory DetermineModCategory(ModDefinition mod)
        {
            // Analyze mod files to determine category
            var files = mod.ModifiedFiles ?? new List<string>();

            if (files.Any(f => f.Contains("texture") || f.Contains("mesh")))
                return ModCategory.Graphics;

            if (files.Any(f => f.Contains("sound") || f.Contains("music")))
                return ModCategory.Audio;

            if (files.Any(f => f.Contains("ui") || f.Contains("interface")))
                return ModCategory.UI;

            if (files.Any(f => f.Contains("world") || f.Contains("town")))
                return ModCategory.World;

            if (files.Any(f => f.Contains("faction") || f.Contains("squad")))
                return ModCategory.Faction;

            if (files.Any(f => f.Contains("item") || f.Contains("weapon") || f.Contains("armor")))
                return ModCategory.Items;

            if (files.Any(f => f.Contains("race") || f.Contains("character")))
                return ModCategory.Races;

            if (files.Any(f => f.Contains("building") || f.Contains("furniture")))
                return ModCategory.Buildings;

            return ModCategory.Gameplay;
        }

        /// <summary>
        /// Check for mod conflicts
        /// </summary>
        private void CheckForConflicts()
        {
            detectedConflicts.Clear();

            // Check direct conflicts
            foreach (var mod in loadedMods.Values)
            {
                if (compatibilityDatabase.TryGetValue(mod.Id, out var compatInfo))
                {
                    foreach (var conflictId in compatInfo.ConflictsWith)
                    {
                        if (loadedMods.ContainsKey(conflictId))
                        {
                            var conflict = new ModConflict
                            {
                                Type = ConflictType.DirectConflict,
                                Mod1 = mod.Id,
                                Mod2 = conflictId,
                                Description = $"{mod.Name} conflicts with {loadedMods[conflictId].Name}",
                                Severity = ConflictSeverity.Critical
                            };

                            detectedConflicts.Add(conflict);
                            OnConflictDetected?.Invoke(conflict);
                        }
                    }
                }
            }

            // Check file conflicts
            var fileModMap = new Dictionary<string, List<string>>();

            foreach (var mod in loadedMods.Values)
            {
                foreach (var file in mod.ModifiedFiles ?? new List<string>())
                {
                    if (!fileModMap.ContainsKey(file))
                        fileModMap[file] = new List<string>();

                    fileModMap[file].Add(mod.Id);
                }
            }

            foreach (var kvp in fileModMap)
            {
                if (kvp.Value.Count > 1)
                {
                    var conflict = new ModConflict
                    {
                        Type = ConflictType.FileOverwrite,
                        Mod1 = kvp.Value[0],
                        Mod2 = kvp.Value[1],
                        Description = $"Multiple mods modify {kvp.Key}",
                        Severity = ConflictSeverity.Warning,
                        AffectedFile = kvp.Key
                    };

                    detectedConflicts.Add(conflict);
                    OnConflictDetected?.Invoke(conflict);
                }
            }

            Logger.Log($"Detected {detectedConflicts.Count} mod conflicts");
        }

        /// <summary>
        /// Validate mod compatibility for multiplayer
        /// </summary>
        public ValidationResult ValidateForMultiplayer(bool isServer)
        {
            var result = new ValidationResult { IsValid = true };

            // Check for incompatible mods
            foreach (var mod in loadedMods.Values)
            {
                switch (mod.CompatibilityLevel)
                {
                    case CompatibilityLevel.Incompatible:
                        result.IsValid = false;
                        result.Issues.Add($"{mod.Name} is incompatible with multiplayer");
                        break;

                    case CompatibilityLevel.ServerSide:
                        if (!isServer)
                        {
                            result.Warnings.Add($"{mod.Name} is server-side only");
                        }
                        break;

                    case CompatibilityLevel.ClientSide:
                        if (isServer)
                        {
                            result.Warnings.Add($"{mod.Name} is client-side only");
                        }
                        break;
                }
            }

            // Check for critical conflicts
            var criticalConflicts = detectedConflicts
                .Where(c => c.Severity == ConflictSeverity.Critical)
                .ToList();

            if (criticalConflicts.Any())
            {
                result.IsValid = false;
                foreach (var conflict in criticalConflicts)
                {
                    result.Issues.Add(conflict.Description);
                }
            }

            return result;
        }

        /// <summary>
        /// Sync mods with server
        /// </summary>
        public async Task<SyncResult> SyncWithServer(List<ModDefinition> serverMods)
        {
            var result = new SyncResult { Success = true };

            // Check for missing mods
            foreach (var serverMod in serverMods)
            {
                if (!loadedMods.ContainsKey(serverMod.Id))
                {
                    result.MissingMods.Add(serverMod);
                }
                else
                {
                    var localMod = loadedMods[serverMod.Id];

                    // Check version match if required
                    if (compatibilityDatabase.TryGetValue(serverMod.Id, out var compatInfo))
                    {
                        if (compatInfo.RequiresSameVersion && localMod.Version != serverMod.Version)
                        {
                            result.VersionMismatches.Add(new VersionMismatch
                            {
                                ModId = serverMod.Id,
                                LocalVersion = localMod.Version,
                                ServerVersion = serverMod.Version
                            });
                        }
                    }

                    // Check hash match
                    if (localMod.Hash != serverMod.Hash)
                    {
                        result.HashMismatches.Add(serverMod);
                    }
                }
            }

            // Check for extra mods
            foreach (var localMod in loadedMods.Values)
            {
                if (!serverMods.Any(m => m.Id == localMod.Id))
                {
                    // Check if mod is client-side only
                    if (localMod.CompatibilityLevel != CompatibilityLevel.ClientSide)
                    {
                        result.ExtraMods.Add(localMod);
                    }
                }
            }

            // Request sync if needed
            if (result.RequiresSync())
            {
                OnSyncRequired?.Invoke(new ModSyncRequest
                {
                    MissingMods = result.MissingMods,
                    UpdatedMods = result.HashMismatches
                });

                // Attempt auto-download if enabled
                if (ConfigurationManager.Instance.Main.EnableModAutoDownload)
                {
                    await DownloadMissingMods(result.MissingMods);
                }
            }

            result.Success = !result.HasCriticalIssues();
            return result;
        }

        /// <summary>
        /// Download missing mods
        /// </summary>
        private async Task DownloadMissingMods(List<ModDefinition> mods)
        {
            foreach (var mod in mods)
            {
                try
                {
                    Logger.Log($"Downloading mod: {mod.Name}");

                    // Download from server or workshop
                    var downloaded = await DownloadMod(mod);

                    if (downloaded)
                    {
                        // Extract and install
                        InstallMod(mod);

                        // Reload mod
                        var modInfo = LoadModInfo(Path.Combine(modsDirectory, mod.Id));
                        if (modInfo != null)
                        {
                            loadedMods[modInfo.Id] = modInfo;
                            OnModLoaded?.Invoke(modInfo);
                        }
                    }
                }
                catch (Exception ex)
                {
                    Logger.LogError($"Failed to download mod {mod.Name}", ex);
                }
            }
        }

        /// <summary>
        /// Download mod from server
        /// </summary>
        private async Task<bool> DownloadMod(ModDefinition mod)
        {
            // Implementation would download from server or Steam Workshop
            return false;
        }

        /// <summary>
        /// Install downloaded mod
        /// </summary>
        private void InstallMod(ModDefinition mod)
        {
            // Extract and install mod files
        }

        /// <summary>
        /// Calculate mod hash for verification
        /// </summary>
        private string CalculateModHash(string modPath)
        {
            using (var sha256 = SHA256.Create())
            {
                var files = Directory.GetFiles(modPath, "*", SearchOption.AllDirectories)
                    .Where(f => !f.EndsWith(".log") && !f.EndsWith(".tmp"))
                    .OrderBy(f => f)
                    .ToList();

                var combinedHash = new List<byte>();

                foreach (var file in files)
                {
                    var fileHash = sha256.ComputeHash(File.ReadAllBytes(file));
                    combinedHash.AddRange(fileHash);
                }

                var finalHash = sha256.ComputeHash(combinedHash.ToArray());
                return Convert.ToBase64String(finalHash);
            }
        }

        /// <summary>
        /// Get load order for mods
        /// </summary>
        public List<string> GetLoadOrder()
        {
            var order = new List<string>();
            var added = new HashSet<string>();

            // Sort by dependencies
            foreach (var mod in loadedMods.Values)
            {
                AddModToLoadOrder(mod.Id, order, added);
            }

            return order;
        }

        /// <summary>
        /// Add mod to load order recursively
        /// </summary>
        private void AddModToLoadOrder(string modId, List<string> order, HashSet<string> added)
        {
            if (added.Contains(modId))
                return;

            if (!loadedMods.TryGetValue(modId, out var mod))
                return;

            // Add dependencies first
            foreach (var dep in mod.Dependencies ?? new List<string>())
            {
                AddModToLoadOrder(dep, order, added);
            }

            // Add this mod
            order.Add(modId);
            added.Add(modId);
        }

        /// <summary>
        /// Apply mod patches
        /// </summary>
        public void ApplyPatches()
        {
            var loadOrder = GetLoadOrder();

            foreach (var modId in loadOrder)
            {
                if (loadedMods.TryGetValue(modId, out var mod))
                {
                    ApplyModPatches(mod);
                }
            }
        }

        /// <summary>
        /// Apply patches from a specific mod
        /// </summary>
        private void ApplyModPatches(ModDefinition mod)
        {
            // Load and apply mod patches to game data
            Logger.Log($"Applying patches from {mod.Name}");

            // This would actually patch game files/memory
        }

        /// <summary>
        /// Get Kenshi installation path
        /// </summary>
        private string GetKenshiPath()
        {
            // Check Steam installation
            var steamPath = @"C:\Program Files (x86)\Steam\steamapps\common\Kenshi";
            if (Directory.Exists(steamPath))
                return steamPath;

            // Check GOG installation
            var gogPath = @"C:\Program Files (x86)\GOG Galaxy\Games\Kenshi";
            if (Directory.Exists(gogPath))
                return gogPath;

            // Check registry
            // ...

            return @"C:\Program Files\Kenshi";
        }

        /// <summary>
        /// Export mod list
        /// </summary>
        public void ExportModList(string filePath)
        {
            var modList = loadedMods.Values.Select(m => new
            {
                m.Id,
                m.Name,
                m.Version,
                m.Author,
                m.Hash,
                m.CompatibilityLevel
            }).ToList();

            var json = JsonSerializer.Serialize(modList, new JsonSerializerOptions
            {
                WriteIndented = true
            });

            File.WriteAllText(filePath, json);
        }

        /// <summary>
        /// Import mod list
        /// </summary>
        public void ImportModList(string filePath)
        {
            // Import and validate mod list
        }
    }

    // Data structures
    public class ModDefinition
    {
        public string Id { get; set; }
        public string Name { get; set; }
        public string Version { get; set; }
        public string Author { get; set; }
        public string Description { get; set; }
        public string Path { get; set; }
        public string Hash { get; set; }
        public ModCategory Category { get; set; }
        public CompatibilityLevel CompatibilityLevel { get; set; }
        public List<string> Dependencies { get; set; }
        public List<string> ModifiedFiles { get; set; }
        public Dictionary<string, object> Metadata { get; set; }
    }

    public class ModCompatibilityInfo
    {
        public string ModId { get; set; }
        public string Name { get; set; }
        public CompatibilityLevel CompatibilityLevel { get; set; }
        public List<string> ConflictsWith { get; set; }
        public List<string> RequiredMods { get; set; }
        public bool RequiresSameVersion { get; set; }
        public string[] SyncRequired { get; set; }
        public Dictionary<string, string> Settings { get; set; }
    }

    public class ModConflict
    {
        public ConflictType Type { get; set; }
        public string Mod1 { get; set; }
        public string Mod2 { get; set; }
        public string Description { get; set; }
        public ConflictSeverity Severity { get; set; }
        public string AffectedFile { get; set; }
        public string Resolution { get; set; }
    }

    public class ValidationResult
    {
        public bool IsValid { get; set; }
        public List<string> Issues { get; set; } = new List<string>();
        public List<string> Warnings { get; set; } = new List<string>();
    }

    public class SyncResult
    {
        public bool Success { get; set; }
        public List<ModDefinition> MissingMods { get; set; } = new List<ModDefinition>();
        public List<ModDefinition> ExtraMods { get; set; } = new List<ModDefinition>();
        public List<ModDefinition> HashMismatches { get; set; } = new List<ModDefinition>();
        public List<VersionMismatch> VersionMismatches { get; set; } = new List<VersionMismatch>();

        public bool RequiresSync()
        {
            return MissingMods.Any() || HashMismatches.Any() || VersionMismatches.Any();
        }

        public bool HasCriticalIssues()
        {
            return MissingMods.Any() || VersionMismatches.Any();
        }
    }

    public class VersionMismatch
    {
        public string ModId { get; set; }
        public string LocalVersion { get; set; }
        public string ServerVersion { get; set; }
    }

    public class ModSyncRequest
    {
        public List<ModDefinition> MissingMods { get; set; }
        public List<ModDefinition> UpdatedMods { get; set; }
    }

    public enum ModCategory
    {
        Gameplay,
        Graphics,
        Audio,
        UI,
        World,
        Faction,
        Items,
        Races,
        Buildings,
        Animation,
        Balance,
        Quality,
        Other
    }

    public enum CompatibilityLevel
    {
        Unknown,
        FullyCompatible,
        ServerSide,
        ClientSide,
        PartiallyCompatible,
        Incompatible
    }

    public enum ConflictType
    {
        DirectConflict,
        FileOverwrite,
        LoadOrder,
        Version,
        Dependency
    }

    public enum ConflictSeverity
    {
        Info,
        Warning,
        Error,
        Critical
    }
}