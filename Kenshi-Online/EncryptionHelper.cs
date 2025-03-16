using System;
using System.IO;
using System.Security.Cryptography;
using System.Text;

namespace KenshiMultiplayer
{
    public static class EncryptionHelper
    {
        // Configuration (these should be stored in a secure config file in production)
        private static readonly string configFilePath = "encryption_config.json";
        private static string encryptionKey;
        private static byte[] initVector;

        static EncryptionHelper()
        {
            // Initialize encryption key and IV
            InitializeEncryption();
        }

        private static void InitializeEncryption()
        {
            // Try to load from config file
            if (File.Exists(configFilePath))
            {
                try
                {
                    var config = System.Text.Json.JsonSerializer.Deserialize<EncryptionConfig>(File.ReadAllText(configFilePath));
                    encryptionKey = config.Key;
                    initVector = Convert.FromBase64String(config.IV);
                    return;
                }
                catch (Exception ex)
                {
                    Logger.Log($"Error loading encryption config: {ex.Message}. Generating new keys.");
                }
            }

            // Generate new keys if config doesn't exist or is invalid
            GenerateNewKeys();
        }

        private static void GenerateNewKeys()
        {
            // Generate a random encryption key
            using (var rng = new RNGCryptoServiceProvider())
            {
                var keyBytes = new byte[32]; // 256-bit key
                rng.GetBytes(keyBytes);
                encryptionKey = Convert.ToBase64String(keyBytes);

                // Generate a random IV
                initVector = new byte[16]; // 128-bit IV
                rng.GetBytes(initVector);
            }

            // Save to config file
            var config = new EncryptionConfig
            {
                Key = encryptionKey,
                IV = Convert.ToBase64String(initVector)
            };

            File.WriteAllText(configFilePath, System.Text.Json.JsonSerializer.Serialize(config));
            Logger.Log("New encryption keys generated and saved to config.");
        }

        public static string Encrypt(string text)
        {
            if (string.IsNullOrEmpty(text))
                return text;

            try
            {
                using (Aes aes = Aes.Create())
                {
                    aes.Key = Encoding.UTF8.GetBytes(encryptionKey);
                    aes.IV = initVector;
                    aes.Mode = CipherMode.CBC;
                    aes.Padding = PaddingMode.PKCS7;

                    using (var encryptor = aes.CreateEncryptor(aes.Key, aes.IV))
                    using (var ms = new MemoryStream())
                    {
                        using (var cs = new CryptoStream(ms, encryptor, CryptoStreamMode.Write))
                        using (var writer = new StreamWriter(cs))
                        {
                            writer.Write(text);
                        }
                        return Convert.ToBase64String(ms.ToArray());
                    }
                }
            }
            catch (Exception ex)
            {
                Logger.Log($"Encryption error: {ex.Message}");
                throw;
            }
        }

        public static string Decrypt(string encryptedText)
        {
            if (string.IsNullOrEmpty(encryptedText))
                return encryptedText;

            try
            {
                byte[] buffer = Convert.FromBase64String(encryptedText);

                using (Aes aes = Aes.Create())
                {
                    aes.Key = Encoding.UTF8.GetBytes(encryptionKey);
                    aes.IV = initVector;
                    aes.Mode = CipherMode.CBC;
                    aes.Padding = PaddingMode.PKCS7;

                    using (var decryptor = aes.CreateDecryptor(aes.Key, aes.IV))
                    using (var ms = new MemoryStream(buffer))
                    using (var cs = new CryptoStream(ms, decryptor, CryptoStreamMode.Read))
                    using (var reader = new StreamReader(cs))
                    {
                        return reader.ReadToEnd();
                    }
                }
            }
            catch (Exception ex)
            {
                Logger.Log($"Decryption error: {ex.Message}");
                throw;
            }
        }

        // Class to hold encryption configuration
        private class EncryptionConfig
        {
            public string Key { get; set; }
            public string IV { get; set; }
        }

        // Password hashing methods for user authentication
        public static (string hash, string salt) HashPassword(string password)
        {
            byte[] salt = new byte[16];
            using (var rng = new RNGCryptoServiceProvider())
            {
                rng.GetBytes(salt);
            }

            string saltString = Convert.ToBase64String(salt);
            string hash = ComputeHash(password, salt);

            return (hash, saltString);
        }

        public static bool VerifyPassword(string password, string storedHash, string storedSalt)
        {
            byte[] salt = Convert.FromBase64String(storedSalt);
            string computedHash = ComputeHash(password, salt);

            return computedHash == storedHash;
        }

        private static string ComputeHash(string password, byte[] salt)
        {
            using (var pbkdf2 = new Rfc2898DeriveBytes(password, salt, 10000))
            {
                byte[] hash = pbkdf2.GetBytes(32);
                return Convert.ToBase64String(hash);
            }
        }
    }
}