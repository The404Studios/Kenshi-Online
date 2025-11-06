using System;
using System.Diagnostics;
using System.IO;
using System.Security.Cryptography;

namespace KenshiMultiplayer.Game
{
    /// <summary>
    /// Detects Kenshi game version and provides appropriate memory offsets
    /// </summary>
    public class GameVersionDetector
    {
        // Known game version hashes
        private static readonly string KENSHI_098_50_HASH = "A1B2C3D4E5F6"; // Example - replace with actual
        private static readonly string KENSHI_098_49_HASH = "F6E5D4C3B2A1"; // Example - replace with actual

        public enum KenshiVersion
        {
            Unknown,
            Version_098_49,
            Version_098_50,
            Version_098_51
        }

        public static KenshiVersion DetectVersion(Process kenshiProcess)
        {
            try
            {
                if (kenshiProcess == null || kenshiProcess.HasExited)
                    return KenshiVersion.Unknown;

                string exePath = kenshiProcess.MainModule?.FileName;
                if (string.IsNullOrEmpty(exePath) || !File.Exists(exePath))
                    return KenshiVersion.Unknown;

                // Get file hash
                string fileHash = GetFileHash(exePath);

                // Match against known versions
                if (fileHash == KENSHI_098_50_HASH)
                    return KenshiVersion.Version_098_50;
                else if (fileHash == KENSHI_098_49_HASH)
                    return KenshiVersion.Version_098_49;

                // Check file version info
                FileVersionInfo versionInfo = FileVersionInfo.GetVersionInfo(exePath);
                if (versionInfo.FileVersion != null)
                {
                    if (versionInfo.FileVersion.Contains("0.98.50"))
                        return KenshiVersion.Version_098_50;
                    if (versionInfo.FileVersion.Contains("0.98.49"))
                        return KenshiVersion.Version_098_49;
                    if (versionInfo.FileVersion.Contains("0.98.51"))
                        return KenshiVersion.Version_098_51;
                }

                return KenshiVersion.Unknown;
            }
            catch (Exception ex)
            {
                Console.WriteLine($"Error detecting game version: {ex.Message}");
                return KenshiVersion.Unknown;
            }
        }

        private static string GetFileHash(string filePath)
        {
            try
            {
                using var md5 = MD5.Create();
                using var stream = File.OpenRead(filePath);
                byte[] hash = md5.ComputeHash(stream);
                return BitConverter.ToString(hash).Replace("-", "");
            }
            catch
            {
                return string.Empty;
            }
        }

        /// <summary>
        /// Gets memory offsets for detected game version
        /// </summary>
        public static KenshiOffsets GetOffsetsForVersion(KenshiVersion version)
        {
            return version switch
            {
                KenshiVersion.Version_098_50 => new KenshiOffsets
                {
                    BaseAddress = 0x140000000,
                    HavokPathfindOffset = 0x1A3B240,
                    NavMeshQueryOffset = 0x1A3B580,
                    CharacterControllerOffset = 0x249BCB0,
                    WorldStateOffset = 0x2A1C3E0,
                    PlayerArrayOffset = 0x2B3F120,
                    NPCArrayOffset = 0x2C1A450,
                    FactionArrayOffset = 0x2D2B580
                },
                KenshiVersion.Version_098_49 => new KenshiOffsets
                {
                    BaseAddress = 0x140000000,
                    HavokPathfindOffset = 0x1A3A100,  // Different offsets for older version
                    NavMeshQueryOffset = 0x1A3A440,
                    CharacterControllerOffset = 0x249AB70,
                    WorldStateOffset = 0x2A1A2A0,
                    PlayerArrayOffset = 0x2B3CFE0,
                    NPCArrayOffset = 0x2C18310,
                    FactionArrayOffset = 0x2D29440
                },
                _ => throw new NotSupportedException($"Game version {version} is not supported. Please update the mod or use a compatible game version.")
            };
        }
    }

    /// <summary>
    /// Memory offsets for Kenshi game structures
    /// </summary>
    public class KenshiOffsets
    {
        public long BaseAddress { get; set; }
        public long HavokPathfindOffset { get; set; }
        public long NavMeshQueryOffset { get; set; }
        public long CharacterControllerOffset { get; set; }
        public long WorldStateOffset { get; set; }
        public long PlayerArrayOffset { get; set; }
        public long NPCArrayOffset { get; set; }
        public long FactionArrayOffset { get; set; }
    }
}
