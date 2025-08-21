using KenshiMultiplayer.Common;
using System;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using System.Text;

namespace KenshiMultiplayer.Common
{
    /// <summary>
    /// Manages encryption for secure network communication
    /// </summary>
    public class EncryptionManager
    {
        private readonly byte[] key;
        private readonly byte[] iv;
        private readonly Aes aes;

        // RSA for key exchange
        private RSACryptoServiceProvider rsa;
        private string publicKey;
        private string privateKey;

        // Session management
        private Dictionary<string, SessionKey> sessionKeys;
        private readonly object lockObject = new object();

        public EncryptionManager()
        {
            // Initialize AES
            aes = Aes.Create();
            aes.KeySize = 256;
            aes.Mode = CipherMode.CBC;
            aes.Padding = PaddingMode.PKCS7;

            // Generate default key and IV
            key = GenerateRandomBytes(32); // 256 bits
            iv = GenerateRandomBytes(16);  // 128 bits

            // Initialize RSA for key exchange
            rsa = new RSACryptoServiceProvider(2048);
            publicKey = rsa.ToXmlString(false);
            privateKey = rsa.ToXmlString(true);

            sessionKeys = new Dictionary<string, SessionKey>();
        }

        /// <summary>
        /// Encrypt data using AES
        /// </summary>
        public byte[] Encrypt(byte[] data)
        {
            lock (lockObject)
            {
                using (var encryptor = aes.CreateEncryptor(key, iv))
                using (var ms = new MemoryStream())
                {
                    // Write IV at the beginning
                    ms.Write(iv, 0, iv.Length);

                    using (var cs = new CryptoStream(ms, encryptor, CryptoStreamMode.Write))
                    {
                        cs.Write(data, 0, data.Length);
                        cs.FlushFinalBlock();
                    }

                    return ms.ToArray();
                }
            }
        }

        /// <summary>
        /// Decrypt data using AES
        /// </summary>
        public byte[] Decrypt(byte[] encryptedData, int length)
        {
            lock (lockObject)
            {
                // Extract IV from the beginning
                var extractedIv = new byte[16];
                Array.Copy(encryptedData, 0, extractedIv, 0, 16);

                using (var decryptor = aes.CreateDecryptor(key, extractedIv))
                using (var ms = new MemoryStream(encryptedData, 16, length - 16))
                using (var cs = new CryptoStream(ms, decryptor, CryptoStreamMode.Read))
                {
                    var decrypted = new byte[length - 16];
                    var bytesRead = cs.Read(decrypted, 0, decrypted.Length);

                    // Resize array to actual decrypted size
                    Array.Resize(ref decrypted, bytesRead);
                    return decrypted;
                }
            }
        }

        /// <summary>
        /// Encrypt with session key
        /// </summary>
        public byte[] EncryptWithSession(string sessionId, byte[] data)
        {
            if (!sessionKeys.TryGetValue(sessionId, out var sessionKey))
                return Encrypt(data); // Fallback to default

            using (var aesSession = Aes.Create())
            {
                aesSession.Key = sessionKey.Key;
                aesSession.IV = sessionKey.IV;

                using (var encryptor = aesSession.CreateEncryptor())
                using (var ms = new MemoryStream())
                {
                    // Write session ID hash
                    var sessionHash = ComputeHash(sessionId);
                    ms.Write(sessionHash, 0, 8); // First 8 bytes of hash

                    // Write IV
                    ms.Write(sessionKey.IV, 0, sessionKey.IV.Length);

                    using (var cs = new CryptoStream(ms, encryptor, CryptoStreamMode.Write))
                    {
                        cs.Write(data, 0, data.Length);
                        cs.FlushFinalBlock();
                    }

                    return ms.ToArray();
                }
            }
        }

        /// <summary>
        /// Generate RSA key pair
        /// </summary>
        public (string publicKey, string privateKey) GenerateKeyPair()
        {
            using (var rsa = new RSACryptoServiceProvider(2048))
            {
                return (rsa.ToXmlString(false), rsa.ToXmlString(true));
            }
        }

        /// <summary>
        /// Encrypt session key with RSA public key
        /// </summary>
        public byte[] EncryptSessionKey(string publicKeyXml, byte[] sessionKey)
        {
            using (var rsa = new RSACryptoServiceProvider())
            {
                rsa.FromXmlString(publicKeyXml);
                return rsa.Encrypt(sessionKey, true);
            }
        }

        /// <summary>
        /// Decrypt session key with RSA private key
        /// </summary>
        public byte[] DecryptSessionKey(byte[] encryptedSessionKey)
        {
            return rsa.Decrypt(encryptedSessionKey, true);
        }

        /// <summary>
        /// Create new session
        /// </summary>
        public string CreateSession(string clientId)
        {
            var sessionId = Guid.NewGuid().ToString();
            var sessionKey = new SessionKey
            {
                ClientId = clientId,
                Key = GenerateRandomBytes(32),
                IV = GenerateRandomBytes(16),
                CreatedAt = DateTime.UtcNow,
                LastUsed = DateTime.UtcNow
            };

            sessionKeys[sessionId] = sessionKey;
            return sessionId;
        }

        /// <summary>
        /// Validate and update session
        /// </summary>
        public bool ValidateSession(string sessionId)
        {
            if (!sessionKeys.TryGetValue(sessionId, out var sessionKey))
                return false;

            // Check session timeout (24 hours)
            if ((DateTime.UtcNow - sessionKey.CreatedAt).TotalHours > 24)
            {
                sessionKeys.Remove(sessionId);
                return false;
            }

            // Update last used time
            sessionKey.LastUsed = DateTime.UtcNow;
            return true;
        }

        /// <summary>
        /// Generate HMAC for message authentication
        /// </summary>
        public byte[] GenerateHMAC(byte[] data)
        {
            using (var hmac = new HMACSHA256(key))
            {
                return hmac.ComputeHash(data);
            }
        }

        /// <summary>
        /// Verify HMAC
        /// </summary>
        public bool VerifyHMAC(byte[] data, byte[] hmac)
        {
            var computedHmac = GenerateHMAC(data);
            return computedHmac.SequenceEqual(hmac);
        }

        /// <summary>
        /// Compute hash
        /// </summary>
        public byte[] ComputeHash(string input)
        {
            using (var sha256 = SHA256.Create())
            {
                return sha256.ComputeHash(Encoding.UTF8.GetBytes(input));
            }
        }

        /// <summary>
        /// Generate random bytes
        /// </summary>
        private byte[] GenerateRandomBytes(int length)
        {
            var bytes = new byte[length];
            using (var rng = RandomNumberGenerator.Create())
            {
                rng.GetBytes(bytes);
            }
            return bytes;
        }

        /// <summary>
        /// Clean up old sessions
        /// </summary>
        public void CleanupSessions()
        {
            var expiredSessions = sessionKeys
                .Where(s => (DateTime.UtcNow - s.Value.LastUsed).TotalHours > 1)
                .Select(s => s.Key)
                .ToList();

            foreach (var sessionId in expiredSessions)
            {
                sessionKeys.Remove(sessionId);
            }
        }

        /// <summary>
        /// Get public key for key exchange
        /// </summary>
        public string GetPublicKey()
        {
            return publicKey;
        }

        /// <summary>
        /// Session key information
        /// </summary>
        private class SessionKey
        {
            public string ClientId { get; set; }
            public byte[] Key { get; set; }
            public byte[] IV { get; set; }
            public DateTime CreatedAt { get; set; }
            public DateTime LastUsed { get; set; }
        }
    }

    /// <summary>
    /// Helper classes for network data structures
    /// </summary>
    public class HandshakeData
    {
        public string Version { get; set; }
        public string PlayerName { get; set; }
        public string ModVersion { get; set; }
        public string Checksum { get; set; }
        public string PublicKey { get; set; }
    }

    public class PositionSyncData
    {
        public double Timestamp { get; set; }
        public List<CharacterPosition> Positions { get; set; }
    }

    public class CharacterPosition
    {
        public string CharacterId { get; set; }
        public float X { get; set; }
        public float Y { get; set; }
        public float Z { get; set; }
        public float Rotation { get; set; }
    }

    public class InventorySyncData
    {
        public string CharacterId { get; set; }
        public List<ItemData> Items { get; set; }
    }

    public class ItemData
    {
        public string ItemId { get; set; }
        public string ItemType { get; set; }
        public int Quantity { get; set; }
        public float Quality { get; set; }
        public int SlotIndex { get; set; }
    }

    public class CharacterStatsData
    {
        public string CharacterId { get; set; }
        public Dictionary<string, int> LimbHealth { get; set; }
        public Dictionary<string, float> Skills { get; set; }
        public float Hunger { get; set; }
        public bool IsUnconscious { get; set; }
        public bool IsDead { get; set; }
    }

    public class WorldStateData
    {
        public double GameTime { get; set; }
        public string Weather { get; set; }
        public Dictionary<string, float> Factions { get; set; }
        public Dictionary<string, TownState> Towns { get; set; }
        public List<string> GlobalEvents { get; set; }
    }

    public class PathCacheData
    {
        public byte[] CompressedData { get; set; }
        public string Checksum { get; set; }
    }

    public class StateSyncData
    {
        public string ClientId { get; set; }
        public StateDelta Delta { get; set; }
        public double ServerTime { get; set; }
    }

    public class PlayerJoinedData
    {
        public string PlayerId { get; set; }
        public string PlayerName { get; set; }
        public List<string> CharacterIds { get; set; }
    }

    public class PlayerLeftData
    {
        public string PlayerId { get; set; }
    }

    /// <summary>
    /// Additional helper classes
    /// </summary>
    public class Squad
    {
        public string Id { get; set; }
        public string Name { get; set; }
        public List<KenshiCharacter> Members { get; set; } 
        public string LeaderId { get; set; }
    }

    public class Building
    {
        public string Id { get; set; }
        public string Type { get; set; }
        public Vector3 Position { get; set; }
        public float Health { get; set; }
        public string OwnerId { get; set; }
    }

    public class PerformanceMonitor
    {
        public double AverageFrameTime { get; set; }
        public int DroppedFrames { get; set; }
        public long MemoryUsage { get; set; }
        public double NetworkLatency { get; set; }

        public void Update()
        {
            // Update performance metrics
        }
    }

    /// <summary>
    /// Logger utility
    /// </summary>
    public static class Logger
    {
        private static readonly string LogPath = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
            "KenshiMultiplayer",
            "logs.txt"
        );

        private static readonly object LogLock = new object();

        static Logger()
        {
            var dir = Path.GetDirectoryName(LogPath);
            if (!Directory.Exists(dir))
                Directory.CreateDirectory(dir);
        }

        public static void Log(string message)
        {
            lock (LogLock)
            {
                try
                {
                    var timestamp = DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss.fff");
                    var logMessage = $"[{timestamp}] {message}{Environment.NewLine}";

                    File.AppendAllText(LogPath, logMessage);
                    Console.WriteLine(logMessage);
                }
                catch
                {
                    // Silently fail if logging fails
                }
            }
        }

        public static void LogError(string message, Exception ex)
        {
            Log($"ERROR: {message} - {ex.Message}");
            Log($"Stack: {ex.StackTrace}");
        }
    }
}