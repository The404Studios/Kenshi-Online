using System;
using System.Collections.Generic;
using System.IO;
using System.Security.Cryptography;
using System.Text;
using KenshiMultiplayer.Networking;
using KenshiMultiplayer.Utility;
using KenshiMultiplayer.Data;

namespace KenshiMultiplayer.Managers
{
    public class GameFileManager
    {
        private readonly string gameRootPath;
        private Dictionary<string, FileInfo> gameFiles = new Dictionary<string, FileInfo>();
        private Dictionary<string, string> fileHashes = new Dictionary<string, string>();

        public GameFileManager(string kenshiRootPath)
        {
            gameRootPath = kenshiRootPath;
            IndexGameFiles();
        }

        // Index all important game files for quick access
        private void IndexGameFiles()
        {
            // Index common Kenshi directories
            string[] dirsToIndex = new string[] {
                "data", "mods", "save", "screens",
                "dependencies", "locale", "logs"
            };

            foreach (var dir in dirsToIndex)
            {
                string dirPath = Path.Combine(gameRootPath, dir);
                if (Directory.Exists(dirPath))
                {
                    foreach (var file in Directory.GetFiles(dirPath, "*", SearchOption.AllDirectories))
                    {
                        string relativePath = file.Substring(gameRootPath.Length + 1);
                        gameFiles[relativePath] = new FileInfo(file);
                    }
                }
            }

            Logger.Log($"Indexed {gameFiles.Count} game files for serving");
        }

        // Calculate MD5 hash of a file for integrity verification
        private string CalculateFileHash(string filePath)
        {
            using (var md5 = MD5.Create())
            using (var stream = File.OpenRead(filePath))
            {
                byte[] hash = md5.ComputeHash(stream);
                return BitConverter.ToString(hash).Replace("-", "").ToLowerInvariant();
            }
        }

        // Stream a file to a client
        public byte[] GetFileData(string relativePath)
        {
            if (gameFiles.TryGetValue(relativePath, out var fileInfo))
            {
                // Ensure we have the hash calculated
                if (!fileHashes.ContainsKey(relativePath))
                {
                    fileHashes[relativePath] = CalculateFileHash(fileInfo.FullName);
                }

                return File.ReadAllBytes(fileInfo.FullName);
            }

            throw new FileNotFoundException($"Game file not found: {relativePath}");
        }

        // Get metadata about a file
        public GameFileInfo GetFileInfo(string relativePath)
        {
            if (gameFiles.TryGetValue(relativePath, out var fileInfo))
            {
                // Ensure we have the hash calculated
                if (!fileHashes.ContainsKey(relativePath))
                {
                    fileHashes[relativePath] = CalculateFileHash(fileInfo.FullName);
                }

                return new GameFileInfo
                {
                    RelativePath = relativePath,
                    Size = fileInfo.Length,
                    LastModified = fileInfo.LastWriteTime,
                    Hash = fileHashes[relativePath]
                };
            }

            throw new FileNotFoundException($"Game file not found: {relativePath}");
        }

        // Get a list of all available game files
        public List<GameFileInfo> GetAllFileInfo()
        {
            var result = new List<GameFileInfo>();

            foreach (var entry in gameFiles)
            {
                result.Add(GetFileInfo(entry.Key));
            }

            return result;
        }

        // Get a list of file infos for a specific directory
        public List<GameFileInfo> GetDirectoryContents(string relativeDirPath)
        {
            var result = new List<GameFileInfo>();

            foreach (var entry in gameFiles)
            {
                if (entry.Key.StartsWith(relativeDirPath))
                {
                    result.Add(GetFileInfo(entry.Key));
                }
            }

            return result;
        }
    }

    // File metadata for clients
    public class GameFileInfo
    {
        public string RelativePath { get; set; }
        public long Size { get; set; }
        public DateTime LastModified { get; set; }
        public string Hash { get; set; } // For integrity verification
    }
}