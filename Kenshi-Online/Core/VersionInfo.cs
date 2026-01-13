using System;
using System.Collections.Generic;
using System.IO;
using System.Text.Json;

namespace KenshiMultiplayer.Core
{
    /// <summary>
    /// Version information for compatibility checking.
    /// </summary>
    public class VersionInfo
    {
        /// <summary>
        /// Kenshi game version (e.g., "1.0.64").
        /// </summary>
        public string KenshiVersion { get; set; }

        /// <summary>
        /// Mod version (e.g., "0.5.0").
        /// Follows semantic versioning: MAJOR.MINOR.PATCH
        /// </summary>
        public string ModVersion { get; set; }

        /// <summary>
        /// Network protocol version (e.g., "1").
        /// Must match exactly for connection.
        /// </summary>
        public string ProtocolVersion { get; set; }

        /// <summary>
        /// Offset table version (e.g., "2024-01-15").
        /// Used to determine memory offset compatibility.
        /// </summary>
        public string OffsetTableVersion { get; set; }

        /// <summary>
        /// Current mod version constant.
        /// Update this when releasing new versions.
        /// </summary>
        public const string CURRENT_MOD_VERSION = "0.5.0";

        /// <summary>
        /// Current protocol version constant.
        /// Increment when making breaking network changes.
        /// </summary>
        public const string CURRENT_PROTOCOL_VERSION = "1";

        /// <summary>
        /// Get current version info.
        /// </summary>
        public static VersionInfo GetCurrent(string kenshiVersion = null)
        {
            return new VersionInfo
            {
                KenshiVersion = kenshiVersion ?? "unknown",
                ModVersion = CURRENT_MOD_VERSION,
                ProtocolVersion = CURRENT_PROTOCOL_VERSION,
                OffsetTableVersion = OffsetTable.GetVersion()
            };
        }

        /// <summary>
        /// Check if this version is compatible with another.
        /// </summary>
        public VersionCompatibility CheckCompatibility(VersionInfo other)
        {
            var result = new VersionCompatibility { IsCompatible = true };

            // Protocol must match exactly
            if (ProtocolVersion != other.ProtocolVersion)
            {
                result.IsCompatible = false;
                result.Issues.Add($"Protocol mismatch: {ProtocolVersion} vs {other.ProtocolVersion}");
            }

            // Mod version: Major.Minor must match
            if (!IsModVersionCompatible(ModVersion, other.ModVersion))
            {
                result.IsCompatible = false;
                result.Issues.Add($"Mod version mismatch: {ModVersion} vs {other.ModVersion}");
            }

            // Kenshi version: Must have compatible offsets
            if (!OffsetTable.IsKenshiVersionSupported(other.KenshiVersion))
            {
                result.IsCompatible = false;
                result.Issues.Add($"Unsupported Kenshi version: {other.KenshiVersion}");
            }

            return result;
        }

        private bool IsModVersionCompatible(string v1, string v2)
        {
            var parts1 = v1.Split('.');
            var parts2 = v2.Split('.');

            if (parts1.Length < 2 || parts2.Length < 2)
                return v1 == v2;

            // Major and Minor must match; Patch can differ
            return parts1[0] == parts2[0] && parts1[1] == parts2[1];
        }

        public string ToJson()
        {
            return JsonSerializer.Serialize(this);
        }

        public static VersionInfo FromJson(string json)
        {
            return JsonSerializer.Deserialize<VersionInfo>(json);
        }
    }

    /// <summary>
    /// Result of version compatibility check.
    /// </summary>
    public class VersionCompatibility
    {
        public bool IsCompatible { get; set; }
        public List<string> Issues { get; set; } = new();

        public string GetErrorMessage()
        {
            return string.Join("; ", Issues);
        }
    }

    /// <summary>
    /// Offset table for Kenshi memory addresses.
    /// Different Kenshi versions have different memory layouts.
    /// </summary>
    public static class OffsetTable
    {
        private static Dictionary<string, KenshiOffsets> _offsets;
        private static string _version = "2024-01-15";

        /// <summary>
        /// Supported Kenshi versions and their offsets.
        /// </summary>
        static OffsetTable()
        {
            _offsets = new Dictionary<string, KenshiOffsets>
            {
                ["1.0.64"] = new KenshiOffsets
                {
                    Supported = true,
                    PlayerList = 0x24C5A20,
                    WorldInstance = 0x24D8F40,
                    GameTime = 0x24D8F50,
                    GameDay = 0x24D8F58,
                    AllCharacters = 0x24C5B00,
                    SelectedCharacter = 0x24C5A30,
                    FactionList = 0x24D2100,
                    SpawnFunction = 0x8B3C80,
                    DespawnFunction = 0x8B4120,
                    IssueCommand = 0x8D5000,
                    CombatAttack = 0x8E2000,
                    // Character field offsets (relative to character pointer)
                    CharacterName = 0x08,
                    CharacterPosition = 0x70,
                    CharacterHealth = 0xC0,
                    CharacterInventory = 0xF0,
                    CharacterAI = 0x110,
                    CharacterFaction = 0x158
                },
                ["1.0.65"] = new KenshiOffsets
                {
                    Supported = false,
                    Message = "Version 1.0.65 is not yet supported. Please use 1.0.64."
                }
            };
        }

        public static string GetVersion() => _version;

        /// <summary>
        /// Check if a Kenshi version is supported.
        /// </summary>
        public static bool IsKenshiVersionSupported(string version)
        {
            if (string.IsNullOrEmpty(version))
                return false;

            return _offsets.TryGetValue(version, out var offsets) && offsets.Supported;
        }

        /// <summary>
        /// Get offsets for a Kenshi version.
        /// </summary>
        public static KenshiOffsets GetOffsets(string version)
        {
            if (_offsets.TryGetValue(version, out var offsets))
                return offsets;
            return null;
        }

        /// <summary>
        /// Get list of supported versions.
        /// </summary>
        public static List<string> GetSupportedVersions()
        {
            var versions = new List<string>();
            foreach (var kvp in _offsets)
            {
                if (kvp.Value.Supported)
                    versions.Add(kvp.Key);
            }
            return versions;
        }

        /// <summary>
        /// Load offsets from external JSON file.
        /// Used for dynamic offset updates without rebuilding.
        /// </summary>
        public static bool LoadFromFile(string path)
        {
            try
            {
                if (!File.Exists(path))
                    return false;

                var json = File.ReadAllText(path);
                var data = JsonSerializer.Deserialize<OffsetTableData>(json);

                if (data?.Versions != null)
                {
                    _version = data.Version;
                    _offsets = data.Versions;
                    return true;
                }
            }
            catch
            {
                // Fall back to built-in offsets
            }
            return false;
        }

        /// <summary>
        /// Save current offsets to JSON file.
        /// </summary>
        public static void SaveToFile(string path)
        {
            var data = new OffsetTableData
            {
                Version = _version,
                Versions = _offsets
            };

            var json = JsonSerializer.Serialize(data, new JsonSerializerOptions { WriteIndented = true });
            File.WriteAllText(path, json);
        }
    }

    /// <summary>
    /// Memory offsets for a specific Kenshi version.
    /// </summary>
    public class KenshiOffsets
    {
        public bool Supported { get; set; }
        public string Message { get; set; }

        // Base addresses
        public long PlayerList { get; set; }
        public long WorldInstance { get; set; }
        public long GameTime { get; set; }
        public long GameDay { get; set; }
        public long AllCharacters { get; set; }
        public long SelectedCharacter { get; set; }
        public long FactionList { get; set; }

        // Function addresses
        public long SpawnFunction { get; set; }
        public long DespawnFunction { get; set; }
        public long IssueCommand { get; set; }
        public long CombatAttack { get; set; }

        // Character field offsets (relative)
        public int CharacterName { get; set; }
        public int CharacterPosition { get; set; }
        public int CharacterHealth { get; set; }
        public int CharacterInventory { get; set; }
        public int CharacterAI { get; set; }
        public int CharacterFaction { get; set; }
    }

    /// <summary>
    /// Offset table JSON format.
    /// </summary>
    public class OffsetTableData
    {
        public string Version { get; set; }
        public Dictionary<string, KenshiOffsets> Versions { get; set; }
    }

    /// <summary>
    /// Runtime version detection from Kenshi.exe.
    /// </summary>
    public static class KenshiVersionDetector
    {
        /// <summary>
        /// Detect Kenshi version from running process.
        /// </summary>
        public static string DetectVersion(string exePath)
        {
            try
            {
                if (!File.Exists(exePath))
                    return null;

                var versionInfo = System.Diagnostics.FileVersionInfo.GetVersionInfo(exePath);
                return versionInfo.FileVersion;
            }
            catch
            {
                return null;
            }
        }

        /// <summary>
        /// Validate that a Kenshi version is supported before injection.
        /// </summary>
        public static VersionValidationResult Validate(string exePath)
        {
            var result = new VersionValidationResult();

            var version = DetectVersion(exePath);
            if (version == null)
            {
                result.IsValid = false;
                result.Error = GameErrors.GameNotFound();
                return result;
            }

            result.DetectedVersion = version;

            if (!OffsetTable.IsKenshiVersionSupported(version))
            {
                result.IsValid = false;
                result.Error = GameErrors.UnsupportedKenshiVersion(version);
                return result;
            }

            result.IsValid = true;
            result.Offsets = OffsetTable.GetOffsets(version);
            return result;
        }
    }

    /// <summary>
    /// Result of version validation.
    /// </summary>
    public class VersionValidationResult
    {
        public bool IsValid { get; set; }
        public string DetectedVersion { get; set; }
        public KenshiOffsets Offsets { get; set; }
        public GameError Error { get; set; }
    }
}
