using KenshiMultiplayer.Common;
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.Json;
using System.Text.Json.Serialization;
using KenshiMultiplayer.Auth;
using KenshiMultiplayer.Networking;

namespace KenshiMultiplayer.Common
{
    /// <summary>
    /// Manages all configuration settings for the multiplayer mod
    /// </summary>
    public class ConfigurationManager
    {
        private static ConfigurationManager instance;
        public static ConfigurationManager Instance => instance ??= new ConfigurationManager();

        // Configuration file paths
        private readonly string configDirectory;
        private readonly string mainConfigPath;
        private readonly string serverConfigPath;
        private readonly string clientConfigPath;
        private readonly string keybindingsPath;

        // Loaded configurations
        public MainConfig Main { get; private set; }
        public ServerConfig Server { get; private set; }
        public ClientConfig Client { get; private set; }
        public KeybindingsConfig Keybindings { get; private set; }

        // Events
        public event Action<string> OnConfigChanged;
        public event Action<string> OnConfigSaved;

        private ConfigurationManager()
        {
            configDirectory = Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
                "KenshiMultiplayer",
                "Config"
            );

            mainConfigPath = Path.Combine(configDirectory, "main.json");
            serverConfigPath = Path.Combine(configDirectory, "server.json");
            clientConfigPath = Path.Combine(configDirectory, "client.json");
            keybindingsPath = Path.Combine(configDirectory, "keybindings.json");

            EnsureConfigDirectory();
        }

        /// <summary>
        /// Initialize and load all configurations
        /// </summary>
        public bool Initialize()
        {
            try
            {
                LoadOrCreateConfigs();
                ValidateConfigs();
                ApplyConfigs();

                Logger.Log("Configuration manager initialized");
                return true;
            }
            catch (Exception ex)
            {
                Logger.LogError("Failed to initialize configuration manager", ex);
                return false;
            }
        }

        /// <summary>
        /// Load or create default configurations
        /// </summary>
        private void LoadOrCreateConfigs()
        {
            Main = LoadOrCreate<MainConfig>(mainConfigPath, CreateDefaultMainConfig);
            Server = LoadOrCreate<ServerConfig>(serverConfigPath, CreateDefaultServerConfig);
            Client = LoadOrCreate<ClientConfig>(clientConfigPath, CreateDefaultClientConfig);
            Keybindings = LoadOrCreate<KeybindingsConfig>(keybindingsPath, CreateDefaultKeybindings);
        }

        /// <summary>
        /// Generic load or create method
        /// </summary>
        private T LoadOrCreate<T>(string path, Func<T> createDefault) where T : class
        {
            try
            {
                if (File.Exists(path))
                {
                    var json = File.ReadAllText(path);
                    return JsonSerializer.Deserialize<T>(json, GetJsonOptions());
                }
            }
            catch (Exception ex)
            {
                Logger.Log($"Failed to load config from {path}: {ex.Message}");
            }

            var defaultConfig = createDefault();
            Save(path, defaultConfig);
            return defaultConfig;
        }

        /// <summary>
        /// Save configuration
        /// </summary>
        private void Save<T>(string path, T config)
        {
            try
            {
                var json = JsonSerializer.Serialize(config, GetJsonOptions());
                File.WriteAllText(path, json);
                OnConfigSaved?.Invoke(path);
            }
            catch (Exception ex)
            {
                Logger.LogError($"Failed to save config to {path}", ex);
            }
        }

        /// <summary>
        /// Save all configurations
        /// </summary>
        public void SaveAll()
        {
            Save(mainConfigPath, Main);
            Save(serverConfigPath, Server);
            Save(clientConfigPath, Client);
            Save(keybindingsPath, Keybindings);
        }

        /// <summary>
        /// Validate configurations
        /// </summary>
        private void ValidateConfigs()
        {
            // Validate main config
            Main.MaxPlayers = Math.Clamp(Main.MaxPlayers, 2, 32);
            Main.NetworkPort = Math.Clamp(Main.NetworkPort, 1024, 65535);
            Main.TickRate = Math.Clamp(Main.TickRate, 10, 120);

            // Validate server config
            Server.MaxBandwidthPerClient = Math.Max(1024, Server.MaxBandwidthPerClient);
            Server.HeartbeatInterval = Math.Clamp(Server.HeartbeatInterval, 1000, 30000);
            Server.SessionTimeout = Math.Max(Server.HeartbeatInterval * 3, Server.SessionTimeout);

            // Validate client config
            Client.InterpolationDelay = Math.Clamp(Client.InterpolationDelay, 0, 500);
            Client.PredictionWindow = Math.Clamp(Client.PredictionWindow, 0, 1000);
            Client.MaxChatMessages = Math.Clamp(Client.MaxChatMessages, 10, 500);
        }

        /// <summary>
        /// Apply configurations to the system
        /// </summary>
        private void ApplyConfigs()
        {
            // Apply network settings
            if (Main.EnableEncryption)
            {
                Logger.Log("Encryption enabled");
            }

            if (Main.EnableCompression)
            {
                Logger.Log("Compression enabled");
            }

            // Apply performance settings
            if (Main.EnableMultithreading)
            {
                Logger.Log($"Multithreading enabled with {Main.WorkerThreads} worker threads");
            }
        }

        /// <summary>
        /// Update configuration value
        /// </summary>
        public void UpdateValue<T>(string category, string key, T value)
        {
            try
            {
                switch (category.ToLower())
                {
                    case "main":
                        UpdateMainConfig(key, value);
                        Save(mainConfigPath, Main);
                        break;

                    case "server":
                        UpdateServerConfig(key, value);
                        Save(serverConfigPath, Server);
                        break;

                    case "client":
                        UpdateClientConfig(key, value);
                        Save(clientConfigPath, Client);
                        break;

                    case "keybindings":
                        UpdateKeybinding(key, value.ToString());
                        Save(keybindingsPath, Keybindings);
                        break;
                }

                OnConfigChanged?.Invoke($"{category}.{key}");
            }
            catch (Exception ex)
            {
                Logger.LogError($"Failed to update config {category}.{key}", ex);
            }
        }

        /// <summary>
        /// Update main config property
        /// </summary>
        private void UpdateMainConfig<T>(string key, T value)
        {
            var property = typeof(MainConfig).GetProperty(key);
            if (property != null && property.CanWrite)
            {
                property.SetValue(Main, Convert.ChangeType(value, property.PropertyType));
            }
        }

        /// <summary>
        /// Update server config property
        /// </summary>
        private void UpdateServerConfig<T>(string key, T value)
        {
            var property = typeof(ServerConfig).GetProperty(key);
            if (property != null && property.CanWrite)
            {
                property.SetValue(Server, Convert.ChangeType(value, property.PropertyType));
            }
        }

        /// <summary>
        /// Update client config property
        /// </summary>
        private void UpdateClientConfig<T>(string key, T value)
        {
            var property = typeof(ClientConfig).GetProperty(key);
            if (property != null && property.CanWrite)
            {
                property.SetValue(Client, Convert.ChangeType(value, property.PropertyType));
            }
        }

        /// <summary>
        /// Update keybinding
        /// </summary>
        private void UpdateKeybinding(string action, string key)
        {
            if (Keybindings.Bindings.ContainsKey(action))
            {
                Keybindings.Bindings[action] = key;
            }
        }

        /// <summary>
        /// Reset to defaults
        /// </summary>
        public void ResetToDefaults(string category = null)
        {
            if (category == null || category.ToLower() == "main")
            {
                Main = CreateDefaultMainConfig();
                Save(mainConfigPath, Main);
            }

            if (category == null || category.ToLower() == "server")
            {
                Server = CreateDefaultServerConfig();
                Save(serverConfigPath, Server);
            }

            if (category == null || category.ToLower() == "client")
            {
                Client = CreateDefaultClientConfig();
                Save(clientConfigPath, Client);
            }

            if (category == null || category.ToLower() == "keybindings")
            {
                Keybindings = CreateDefaultKeybindings();
                Save(keybindingsPath, Keybindings);
            }

            OnConfigChanged?.Invoke(category ?? "all");
        }

        /// <summary>
        /// Ensure config directory exists
        /// </summary>
        private void EnsureConfigDirectory()
        {
            if (!Directory.Exists(configDirectory))
            {
                Directory.CreateDirectory(configDirectory);
            }
        }

        /// <summary>
        /// Get JSON serializer options
        /// </summary>
        private JsonSerializerOptions GetJsonOptions()
        {
            return new JsonSerializerOptions
            {
                WriteIndented = true,
                PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
                Converters = { new JsonStringEnumConverter() }
            };
        }

        // Default configuration creators
        private MainConfig CreateDefaultMainConfig()
        {
            return new MainConfig
            {
                // Network
                NetworkMode = NetworkMode.Client,
                NetworkPort = 27015,
                MaxPlayers = 16,
                TickRate = 30,

                // Security
                EnableEncryption = true,
                EnableCompression = true,
                EnableAntiCheat = true,
                RequirePasswordForHost = false,
                ServerPassword = "",

                // Performance
                EnableMultithreading = true,
                WorkerThreads = 4,
                MaxMemoryUsage = 2048, // MB
                EnablePathCaching = true,
                PathCacheSize = 10000,

                // Gameplay
                EnablePvP = true,
                EnableFriendlyFire = false,
                SyncNPCs = true,
                SyncWeather = true,
                SyncTime = true,
                PauseWhenEmpty = false,

                // Debug
                EnableDebugMode = false,
                EnableVerboseLogging = false,
                LogLevel = LogLevel.Info
            };
        }

        private ServerConfig CreateDefaultServerConfig()
        {
            return new ServerConfig
            {
                // Server settings
                ServerName = "Kenshi Multiplayer Server",
                ServerDescription = "A multiplayer Kenshi experience",
                ServerRegion = "US",
                PublicServer = false,

                // Network settings
                MaxBandwidthPerClient = 100000, // bytes/sec
                MaxBandwidthTotal = 1000000,
                HeartbeatInterval = 5000, // ms
                SessionTimeout = 15000,

                // Game rules
                MaxSquadSize = 30,
                MaxBaseSize = 100,
                AllowMods = true,
                RequiredMods = new List<string>(),
                BannedMods = new List<string>(),

                // Admin
                AdminPassword = "",
                AdminCommands = true,
                AutoSaveInterval = 300, // seconds
                BackupCount = 5,

                // Anti-grief
                BuildingProtection = true,
                ProtectionRadius = 500,
                AllowRaiding = true,
                RaidProtectionTime = 3600, // seconds after raid

                // Resource settings
                ResourceMultiplier = 1.0f,
                XPMultiplier = 1.0f,
                DamageMultiplier = 1.0f,
                HungerMultiplier = 1.0f
            };
        }

        private ClientConfig CreateDefaultClientConfig()
        {
            return new ClientConfig
            {
                // Display
                PlayerName = Environment.UserName,
                ShowPlayerNames = true,
                ShowHealthBars = true,
                ShowPing = true,
                ShowFPS = true,
                UIScale = 1.0f,

                // Network
                InterpolationDelay = 100, // ms
                PredictionWindow = 200,
                EnableClientPrediction = true,
                EnableInterpolation = true,

                // Chat
                EnableChat = true,
                MaxChatMessages = 100,
                ChatFadeTime = 10.0f,
                ChatOpacity = 0.8f,

                // Audio
                EnableVoiceChat = false,
                VoiceChatVolume = 0.7f,
                PushToTalk = true,
                VoiceChatKey = "V",

                // Graphics
                EnablePlayerMarkers = true,
                MarkerDistance = 1000,
                EnableMinimapPlayers = true,

                // Auto-connect
                AutoConnectEnabled = false,
                AutoConnectAddress = "",
                AutoConnectPort = 27015
            };
        }

        private KeybindingsConfig CreateDefaultKeybindings()
        {
            return new KeybindingsConfig
            {
                Bindings = new Dictionary<string, string>
                {
                    ["OpenMenu"] = "F1",
                    ["OpenChat"] = "Return",
                    ["TeamChat"] = "Y",
                    ["TogglePlayerList"] = "Tab",
                    ["ToggleMap"] = "M",
                    ["ToggleStats"] = "F3",
                    ["QuickSave"] = "F5",
                    ["QuickLoad"] = "F9",
                    ["VoiceChat"] = "V",
                    ["Screenshot"] = "F12",
                    ["ToggleUI"] = "F11",
                    ["EmoteMenu"] = "G",
                    ["Ping"] = "MiddleMouse",
                    ["Mark"] = "X",
                    ["Ready"] = "R"
                }
            };
        }
    }

    // Configuration classes
    public class MainConfig
    {
        // Network
        public NetworkMode NetworkMode { get; set; }
        public int NetworkPort { get; set; }
        public int MaxPlayers { get; set; }
        public int TickRate { get; set; }

        // Security
        public bool EnableEncryption { get; set; }
        public bool EnableCompression { get; set; }
        public bool EnableAntiCheat { get; set; }
        public bool RequirePasswordForHost { get; set; }
        public string ServerPassword { get; set; }

        // Performance
        public bool EnableMultithreading { get; set; }
        public int WorkerThreads { get; set; }
        public int MaxMemoryUsage { get; set; }
        public bool EnablePathCaching { get; set; }
        public int PathCacheSize { get; set; }

        // Gameplay
        public bool EnablePvP { get; set; }
        public bool EnableFriendlyFire { get; set; }
        public bool SyncNPCs { get; set; }
        public bool SyncWeather { get; set; }
        public bool SyncTime { get; set; }
        public bool PauseWhenEmpty { get; set; }

        // Debug
        public bool EnableDebugMode { get; set; }
        public bool EnableVerboseLogging { get; set; }
        public LogLevel LogLevel { get; set; }
    }

    public class ServerConfig
    {
        // Server info
        public string ServerName { get; set; }
        public string ServerDescription { get; set; }
        public string ServerRegion { get; set; }
        public bool PublicServer { get; set; }

        // Network
        public int MaxBandwidthPerClient { get; set; }
        public int MaxBandwidthTotal { get; set; }
        public int HeartbeatInterval { get; set; }
        public int SessionTimeout { get; set; }

        // Game rules
        public int MaxSquadSize { get; set; }
        public int MaxBaseSize { get; set; }
        public bool AllowMods { get; set; }
        public List<string> RequiredMods { get; set; }
        public List<string> BannedMods { get; set; }

        // Admin
        public string AdminPassword { get; set; }
        public bool AdminCommands { get; set; }
        public int AutoSaveInterval { get; set; }
        public int BackupCount { get; set; }

        // Anti-grief
        public bool BuildingProtection { get; set; }
        public float ProtectionRadius { get; set; }
        public bool AllowRaiding { get; set; }
        public int RaidProtectionTime { get; set; }

        // Multipliers
        public float ResourceMultiplier { get; set; }
        public float XPMultiplier { get; set; }
        public float DamageMultiplier { get; set; }
        public float HungerMultiplier { get; set; }
    }

    public class ClientConfig
    {
        // Display
        public string PlayerName { get; set; }
        public bool ShowPlayerNames { get; set; }
        public bool ShowHealthBars { get; set; }
        public bool ShowPing { get; set; }
        public bool ShowFPS { get; set; }
        public float UIScale { get; set; }

        // Network
        public int InterpolationDelay { get; set; }
        public int PredictionWindow { get; set; }
        public bool EnableClientPrediction { get; set; }
        public bool EnableInterpolation { get; set; }

        // Chat
        public bool EnableChat { get; set; }
        public int MaxChatMessages { get; set; }
        public float ChatFadeTime { get; set; }
        public float ChatOpacity { get; set; }

        // Audio
        public bool EnableVoiceChat { get; set; }
        public float VoiceChatVolume { get; set; }
        public bool PushToTalk { get; set; }
        public string VoiceChatKey { get; set; }

        // Graphics
        public bool EnablePlayerMarkers { get; set; }
        public float MarkerDistance { get; set; }
        public bool EnableMinimapPlayers { get; set; }

        // Auto-connect
        public bool AutoConnectEnabled { get; set; }
        public string AutoConnectAddress { get; set; }
        public int AutoConnectPort { get; set; }
    }

    public class KeybindingsConfig
    {
        public Dictionary<string, string> Bindings { get; set; }
    }

    public enum LogLevel
    {
        Debug,
        Info,
        Warning,
        Error,
        Critical
    }
}