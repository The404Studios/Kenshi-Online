using System;
using System.Collections.Generic;
using System.IO;
using System.Net.Http;
using System.Text.Json;
using System.Text.Json.Serialization;
using System.Threading.Tasks;
using System.Security.Cryptography;
using System.Text;

namespace KenshiMultiplayer.Game
{
    /// <summary>
    /// Provides online offset fetching capabilities for Kenshi game memory addresses.
    /// Supports multiple offset sources with fallback chain: Online -> Cache -> Hardcoded
    /// </summary>
    public class OnlineOffsetProvider
    {
        private static readonly HttpClient _httpClient = new HttpClient
        {
            Timeout = TimeSpan.FromSeconds(10)
        };

        private static OnlineOffsetProvider? _instance;
        public static OnlineOffsetProvider Instance => _instance ??= new OnlineOffsetProvider();

        // Default offset server URLs (can be configured)
        private readonly List<string> _offsetServers = new()
        {
            "https://raw.githubusercontent.com/The404Studios/Kenshi-Online/main/offsets/kenshi_offsets.json",
            "https://kenshi-online.the404studios.com/api/offsets",
            "https://pastebin.com/raw/PLACEHOLDER" // Backup pastebin URL
        };

        private const string CACHE_FILE = "kenshi_offsets_cache.json";
        private const string VERSION_FILE = "kenshi_version.txt";

        private OnlineOffsetDatabase? _cachedOffsets;
        private bool _isInitialized;

        /// <summary>
        /// Event fired when offsets are successfully loaded
        /// </summary>
        public event Action<OnlineOffsetDatabase>? OnOffsetsLoaded;

        /// <summary>
        /// Event fired when offset loading fails
        /// </summary>
        public event Action<string>? OnOffsetLoadFailed;

        /// <summary>
        /// Initialize the offset provider and attempt to fetch offsets
        /// </summary>
        public async Task<bool> InitializeAsync(string? gameVersion = null)
        {
            if (_isInitialized && _cachedOffsets != null)
                return true;

            gameVersion ??= DetectGameVersion();

            // Try loading in order: Online -> Cache -> Hardcoded
            _cachedOffsets = await TryLoadOffsetsAsync(gameVersion);

            if (_cachedOffsets != null)
            {
                ApplyOffsetsToKenshiMemory(_cachedOffsets);
                _isInitialized = true;
                OnOffsetsLoaded?.Invoke(_cachedOffsets);
                return true;
            }

            // Fall back to hardcoded offsets
            Console.WriteLine("[OffsetProvider] All sources failed, using hardcoded offsets");
            OnOffsetLoadFailed?.Invoke("All offset sources failed, using hardcoded fallback");
            return false;
        }

        /// <summary>
        /// Synchronous initialization wrapper
        /// </summary>
        public bool Initialize(string? gameVersion = null)
        {
            return InitializeAsync(gameVersion).GetAwaiter().GetResult();
        }

        /// <summary>
        /// Try to load offsets from available sources
        /// </summary>
        private async Task<OnlineOffsetDatabase?> TryLoadOffsetsAsync(string gameVersion)
        {
            // 1. Try online sources
            foreach (var server in _offsetServers)
            {
                try
                {
                    var offsets = await FetchOffsetsFromUrlAsync(server, gameVersion);
                    if (offsets != null && ValidateOffsets(offsets))
                    {
                        Console.WriteLine($"[OffsetProvider] Successfully loaded offsets from: {server}");
                        await SaveToCacheAsync(offsets);
                        return offsets;
                    }
                }
                catch (Exception ex)
                {
                    Console.WriteLine($"[OffsetProvider] Failed to fetch from {server}: {ex.Message}");
                }
            }

            // 2. Try local cache
            var cached = await LoadFromCacheAsync();
            if (cached != null && (cached.GameVersion == gameVersion || cached.IsUniversal))
            {
                Console.WriteLine("[OffsetProvider] Loaded offsets from local cache");
                return cached;
            }

            // 3. Return null to trigger hardcoded fallback
            return null;
        }

        /// <summary>
        /// Fetch offsets from a URL
        /// </summary>
        private async Task<OnlineOffsetDatabase?> FetchOffsetsFromUrlAsync(string url, string gameVersion)
        {
            // Add version parameter if URL supports it
            var requestUrl = url.Contains("?") ? $"{url}&version={gameVersion}" : $"{url}?version={gameVersion}";

            try
            {
                var response = await _httpClient.GetStringAsync(requestUrl);
                var database = JsonSerializer.Deserialize<OnlineOffsetDatabase>(response, new JsonSerializerOptions
                {
                    PropertyNameCaseInsensitive = true
                });

                return database;
            }
            catch (HttpRequestException)
            {
                // Try without version parameter
                var response = await _httpClient.GetStringAsync(url);
                return JsonSerializer.Deserialize<OnlineOffsetDatabase>(response, new JsonSerializerOptions
                {
                    PropertyNameCaseInsensitive = true
                });
            }
        }

        /// <summary>
        /// Validate downloaded offsets
        /// </summary>
        private bool ValidateOffsets(OnlineOffsetDatabase offsets)
        {
            // Basic validation
            if (offsets == null) return false;
            if (string.IsNullOrEmpty(offsets.GameVersion) && !offsets.IsUniversal) return false;
            if (offsets.GameOffsets == null) return false;

            // Verify checksum if provided
            if (!string.IsNullOrEmpty(offsets.Checksum))
            {
                var calculated = CalculateChecksum(offsets);
                if (calculated != offsets.Checksum)
                {
                    Console.WriteLine("[OffsetProvider] Checksum mismatch!");
                    return false;
                }
            }

            // Verify minimum required offsets exist
            if (offsets.GameOffsets.WorldInstance == 0 &&
                offsets.GameOffsets.PlayerSquadList == 0 &&
                offsets.GameOffsets.AllCharactersList == 0)
            {
                Console.WriteLine("[OffsetProvider] Missing critical offsets");
                return false;
            }

            return true;
        }

        /// <summary>
        /// Calculate checksum for validation
        /// </summary>
        private string CalculateChecksum(OnlineOffsetDatabase offsets)
        {
            var data = JsonSerializer.Serialize(offsets.GameOffsets);
            using var sha256 = SHA256.Create();
            var hash = sha256.ComputeHash(Encoding.UTF8.GetBytes(data));
            return Convert.ToHexString(hash).ToLower();
        }

        /// <summary>
        /// Save offsets to local cache
        /// </summary>
        private async Task SaveToCacheAsync(OnlineOffsetDatabase offsets)
        {
            try
            {
                offsets.CachedAt = DateTime.UtcNow;
                var json = JsonSerializer.Serialize(offsets, new JsonSerializerOptions
                {
                    WriteIndented = true
                });
                await File.WriteAllTextAsync(CACHE_FILE, json);
            }
            catch (Exception ex)
            {
                Console.WriteLine($"[OffsetProvider] Failed to save cache: {ex.Message}");
            }
        }

        /// <summary>
        /// Load offsets from local cache
        /// </summary>
        private async Task<OnlineOffsetDatabase?> LoadFromCacheAsync()
        {
            try
            {
                if (!File.Exists(CACHE_FILE))
                    return null;

                var json = await File.ReadAllTextAsync(CACHE_FILE);
                var cached = JsonSerializer.Deserialize<OnlineOffsetDatabase>(json, new JsonSerializerOptions
                {
                    PropertyNameCaseInsensitive = true
                });

                // Check if cache is too old (7 days)
                if (cached?.CachedAt != null && (DateTime.UtcNow - cached.CachedAt.Value).TotalDays > 7)
                {
                    Console.WriteLine("[OffsetProvider] Cache is stale (>7 days old)");
                    return null;
                }

                return cached;
            }
            catch
            {
                return null;
            }
        }

        /// <summary>
        /// Apply loaded offsets to KenshiMemory static class
        /// </summary>
        private void ApplyOffsetsToKenshiMemory(OnlineOffsetDatabase database)
        {
            var offsets = database.GameOffsets;
            if (offsets == null) return;

            // Update base address
            if (offsets.BaseAddress != 0)
                KenshiMemory.BaseAddress = offsets.BaseAddress;

            // The actual offset values will be used by creating a new accessor
            // Since KenshiMemory uses const values, we provide runtime accessors
            Console.WriteLine($"[OffsetProvider] Applied offsets for Kenshi {database.GameVersion}");
            Console.WriteLine($"  - WorldInstance: 0x{offsets.WorldInstance:X}");
            Console.WriteLine($"  - PlayerSquadList: 0x{offsets.PlayerSquadList:X}");
            Console.WriteLine($"  - AllCharactersList: 0x{offsets.AllCharactersList:X}");
        }

        /// <summary>
        /// Detect the game version from executable
        /// </summary>
        private string DetectGameVersion()
        {
            try
            {
                // Try to read from cached version file
                if (File.Exists(VERSION_FILE))
                {
                    return File.ReadAllText(VERSION_FILE).Trim();
                }

                // Default to latest known version
                return "1.0.64";
            }
            catch
            {
                return "1.0.64";
            }
        }

        /// <summary>
        /// Get current offsets (returns cached or hardcoded)
        /// </summary>
        public GameOffsetsData GetCurrentOffsets()
        {
            if (_cachedOffsets?.GameOffsets != null)
                return _cachedOffsets.GameOffsets;

            // Return hardcoded defaults
            return GameOffsetsData.CreateHardcodedDefaults();
        }

        /// <summary>
        /// Force refresh offsets from online
        /// </summary>
        public async Task<bool> RefreshOffsetsAsync()
        {
            _isInitialized = false;
            _cachedOffsets = null;

            // Delete cache to force re-fetch
            try
            {
                File.Delete(CACHE_FILE);
            }
            catch (Exception ex)
            {
                Logger.Log($"[OnlineOffsetProvider] Failed to delete cache file: {ex.Message}");
            }

            return await InitializeAsync();
        }

        /// <summary>
        /// Add a custom offset server URL
        /// </summary>
        public void AddOffsetServer(string url)
        {
            if (!_offsetServers.Contains(url))
                _offsetServers.Insert(0, url);
        }

        /// <summary>
        /// Clear all offset servers and set a single source
        /// </summary>
        public void SetOffsetServer(string url)
        {
            _offsetServers.Clear();
            _offsetServers.Add(url);
        }
    }

    #region Data Models

    /// <summary>
    /// Online offset database structure
    /// </summary>
    public class OnlineOffsetDatabase
    {
        [JsonPropertyName("version")]
        public int SchemaVersion { get; set; } = 1;

        [JsonPropertyName("gameVersion")]
        public string GameVersion { get; set; } = "";

        [JsonPropertyName("isUniversal")]
        public bool IsUniversal { get; set; }

        [JsonPropertyName("lastUpdated")]
        public DateTime LastUpdated { get; set; }

        [JsonPropertyName("checksum")]
        public string? Checksum { get; set; }

        [JsonPropertyName("author")]
        public string? Author { get; set; }

        [JsonPropertyName("notes")]
        public string? Notes { get; set; }

        [JsonPropertyName("offsets")]
        public GameOffsetsData? GameOffsets { get; set; }

        [JsonPropertyName("patterns")]
        public PatternSignaturesData? Patterns { get; set; }

        [JsonPropertyName("structureOffsets")]
        public StructureOffsetsData? StructureOffsets { get; set; }

        [JsonPropertyName("functionOffsets")]
        public FunctionOffsetsData? FunctionOffsets { get; set; }

        // Internal use
        [JsonIgnore]
        public DateTime? CachedAt { get; set; }
    }

    /// <summary>
    /// Core game pointer offsets
    /// </summary>
    public class GameOffsetsData
    {
        [JsonPropertyName("baseAddress")]
        public long BaseAddress { get; set; }

        // Game Core
        [JsonPropertyName("worldInstance")]
        public long WorldInstance { get; set; }

        [JsonPropertyName("gameState")]
        public long GameState { get; set; }

        [JsonPropertyName("gameTime")]
        public long GameTime { get; set; }

        [JsonPropertyName("gameDay")]
        public long GameDay { get; set; }

        // Characters
        [JsonPropertyName("playerSquadList")]
        public long PlayerSquadList { get; set; }

        [JsonPropertyName("playerSquadCount")]
        public long PlayerSquadCount { get; set; }

        [JsonPropertyName("allCharactersList")]
        public long AllCharactersList { get; set; }

        [JsonPropertyName("allCharactersCount")]
        public long AllCharactersCount { get; set; }

        [JsonPropertyName("selectedCharacter")]
        public long SelectedCharacter { get; set; }

        // Factions
        [JsonPropertyName("factionList")]
        public long FactionList { get; set; }

        [JsonPropertyName("factionCount")]
        public long FactionCount { get; set; }

        [JsonPropertyName("playerFaction")]
        public long PlayerFaction { get; set; }

        [JsonPropertyName("relationMatrix")]
        public long RelationMatrix { get; set; }

        // World
        [JsonPropertyName("buildingList")]
        public long BuildingList { get; set; }

        [JsonPropertyName("buildingCount")]
        public long BuildingCount { get; set; }

        [JsonPropertyName("worldItemsList")]
        public long WorldItemsList { get; set; }

        [JsonPropertyName("worldItemsCount")]
        public long WorldItemsCount { get; set; }

        [JsonPropertyName("weatherSystem")]
        public long WeatherSystem { get; set; }

        // Engine
        [JsonPropertyName("physicsWorld")]
        public long PhysicsWorld { get; set; }

        [JsonPropertyName("camera")]
        public long Camera { get; set; }

        [JsonPropertyName("cameraTarget")]
        public long CameraTarget { get; set; }

        [JsonPropertyName("renderer")]
        public long Renderer { get; set; }

        // Input
        [JsonPropertyName("inputHandler")]
        public long InputHandler { get; set; }

        [JsonPropertyName("commandQueue")]
        public long CommandQueue { get; set; }

        [JsonPropertyName("selectedUnits")]
        public long SelectedUnits { get; set; }

        [JsonPropertyName("uiState")]
        public long UIState { get; set; }

        public static GameOffsetsData CreateHardcodedDefaults()
        {
            return new GameOffsetsData
            {
                BaseAddress = 0x140000000,
                WorldInstance = 0x24D8F40,
                GameState = 0x24D8F48,
                GameTime = 0x24D8F50,
                GameDay = 0x24D8F58,
                PlayerSquadList = 0x24C5A20,
                PlayerSquadCount = 0x24C5A28,
                AllCharactersList = 0x24C5B00,
                AllCharactersCount = 0x24C5B08,
                SelectedCharacter = 0x24C5A30,
                FactionList = 0x24D2100,
                FactionCount = 0x24D2108,
                PlayerFaction = 0x24D2110,
                RelationMatrix = 0x24D2200,
                BuildingList = 0x24E1000,
                BuildingCount = 0x24E1008,
                WorldItemsList = 0x24E1100,
                WorldItemsCount = 0x24E1108,
                WeatherSystem = 0x24E7000,
                PhysicsWorld = 0x24F0000,
                Camera = 0x24E7C20,
                CameraTarget = 0x24E7C38,
                Renderer = 0x24F5000,
                InputHandler = 0x24F2D80,
                CommandQueue = 0x24F2D90,
                SelectedUnits = 0x24F2DA0,
                UIState = 0x24F3000
            };
        }
    }

    /// <summary>
    /// Function address offsets
    /// </summary>
    public class FunctionOffsetsData
    {
        [JsonPropertyName("spawnCharacter")]
        public long SpawnCharacter { get; set; }

        [JsonPropertyName("despawnCharacter")]
        public long DespawnCharacter { get; set; }

        [JsonPropertyName("addToSquad")]
        public long AddToSquad { get; set; }

        [JsonPropertyName("removeFromSquad")]
        public long RemoveFromSquad { get; set; }

        [JsonPropertyName("addItemToInventory")]
        public long AddItemToInventory { get; set; }

        [JsonPropertyName("removeItemFromInventory")]
        public long RemoveItemFromInventory { get; set; }

        [JsonPropertyName("setCharacterState")]
        public long SetCharacterState { get; set; }

        [JsonPropertyName("issueCommand")]
        public long IssueCommand { get; set; }

        [JsonPropertyName("createFaction")]
        public long CreateFaction { get; set; }

        [JsonPropertyName("setFactionRelation")]
        public long SetFactionRelation { get; set; }

        [JsonPropertyName("pathfindRequest")]
        public long PathfindRequest { get; set; }

        [JsonPropertyName("combatAttack")]
        public long CombatAttack { get; set; }

        [JsonPropertyName("characterUpdate")]
        public long CharacterUpdate { get; set; }

        [JsonPropertyName("aiUpdate")]
        public long AIUpdate { get; set; }

        public static FunctionOffsetsData CreateHardcodedDefaults()
        {
            return new FunctionOffsetsData
            {
                SpawnCharacter = 0x8B3C80,
                DespawnCharacter = 0x8B4120,
                AddToSquad = 0x8B4500,
                RemoveFromSquad = 0x8B4600,
                AddItemToInventory = 0x9C2100,
                RemoveItemFromInventory = 0x9C2200,
                SetCharacterState = 0x8C1000,
                IssueCommand = 0x8D5000,
                CreateFaction = 0x7A2000,
                SetFactionRelation = 0x7A2500,
                PathfindRequest = 0x7B1000,
                CombatAttack = 0x8E2000
            };
        }
    }

    /// <summary>
    /// Character structure field offsets
    /// </summary>
    public class StructureOffsetsData
    {
        [JsonPropertyName("character")]
        public CharacterStructureOffsets? Character { get; set; }

        [JsonPropertyName("squad")]
        public SquadStructureOffsets? Squad { get; set; }

        [JsonPropertyName("faction")]
        public FactionStructureOffsets? Faction { get; set; }

        [JsonPropertyName("item")]
        public ItemStructureOffsets? Item { get; set; }
    }

    public class CharacterStructureOffsets
    {
        [JsonPropertyName("position")]
        public int Position { get; set; } = 0x70;

        [JsonPropertyName("rotation")]
        public int Rotation { get; set; } = 0x7C;

        [JsonPropertyName("health")]
        public int Health { get; set; } = 0xC0;

        [JsonPropertyName("maxHealth")]
        public int MaxHealth { get; set; } = 0xC4;

        [JsonPropertyName("blood")]
        public int Blood { get; set; } = 0xC8;

        [JsonPropertyName("hunger")]
        public int Hunger { get; set; } = 0xD0;

        [JsonPropertyName("inventory")]
        public int Inventory { get; set; } = 0xF0;

        [JsonPropertyName("equipment")]
        public int Equipment { get; set; } = 0xF8;

        [JsonPropertyName("ai")]
        public int AI { get; set; } = 0x110;

        [JsonPropertyName("state")]
        public int State { get; set; } = 0x118;

        [JsonPropertyName("faction")]
        public int Faction { get; set; } = 0x158;

        [JsonPropertyName("squad")]
        public int Squad { get; set; } = 0x168;

        [JsonPropertyName("animState")]
        public int AnimState { get; set; } = 0x140;

        [JsonPropertyName("body")]
        public int Body { get; set; } = 0xB8;
    }

    public class SquadStructureOffsets
    {
        [JsonPropertyName("members")]
        public int Members { get; set; } = 0x20;

        [JsonPropertyName("memberCount")]
        public int MemberCount { get; set; } = 0x28;

        [JsonPropertyName("leader")]
        public int Leader { get; set; } = 0x30;

        [JsonPropertyName("factionId")]
        public int FactionId { get; set; } = 0x38;
    }

    public class FactionStructureOffsets
    {
        [JsonPropertyName("relations")]
        public int Relations { get; set; } = 0x20;

        [JsonPropertyName("members")]
        public int Members { get; set; } = 0x30;

        [JsonPropertyName("leader")]
        public int Leader { get; set; } = 0x40;
    }

    public class ItemStructureOffsets
    {
        [JsonPropertyName("name")]
        public int Name { get; set; } = 0x10;

        [JsonPropertyName("category")]
        public int Category { get; set; } = 0x18;

        [JsonPropertyName("value")]
        public int Value { get; set; } = 0x20;

        [JsonPropertyName("weight")]
        public int Weight { get; set; } = 0x24;

        [JsonPropertyName("stackCount")]
        public int StackCount { get; set; } = 0x28;
    }

    /// <summary>
    /// Pattern signatures for dynamic scanning
    /// </summary>
    public class PatternSignaturesData
    {
        [JsonPropertyName("gameWorld")]
        public PatternDefinition? GameWorld { get; set; }

        [JsonPropertyName("playerSquadList")]
        public PatternDefinition? PlayerSquadList { get; set; }

        [JsonPropertyName("allCharactersList")]
        public PatternDefinition? AllCharactersList { get; set; }

        [JsonPropertyName("factionManager")]
        public PatternDefinition? FactionManager { get; set; }

        [JsonPropertyName("weatherSystem")]
        public PatternDefinition? WeatherSystem { get; set; }

        [JsonPropertyName("inputHandler")]
        public PatternDefinition? InputHandler { get; set; }

        [JsonPropertyName("cameraController")]
        public PatternDefinition? CameraController { get; set; }

        [JsonPropertyName("spawnCharacter")]
        public PatternDefinition? SpawnCharacter { get; set; }

        [JsonPropertyName("characterUpdate")]
        public PatternDefinition? CharacterUpdate { get; set; }

        [JsonPropertyName("combatSystem")]
        public PatternDefinition? CombatSystem { get; set; }
    }

    public class PatternDefinition
    {
        [JsonPropertyName("name")]
        public string Name { get; set; } = "";

        [JsonPropertyName("pattern")]
        public string Pattern { get; set; } = "";

        [JsonPropertyName("mask")]
        public string Mask { get; set; } = "";

        [JsonPropertyName("offset")]
        public int Offset { get; set; }

        [JsonPropertyName("isRelative")]
        public bool IsRelative { get; set; }

        [JsonPropertyName("relativeBase")]
        public int RelativeBase { get; set; }
    }

    #endregion

    #region Runtime Offset Accessor

    /// <summary>
    /// Runtime offset accessor that provides current offsets (online or hardcoded)
    /// Use this instead of KenshiMemory.Game.* for runtime offset access
    /// </summary>
    public static class RuntimeOffsets
    {
        private static GameOffsetsData? _offsets;
        private static FunctionOffsetsData? _functions;
        private static StructureOffsetsData? _structures;

        public static bool IsInitialized { get; private set; }

        /// <summary>
        /// Initialize runtime offsets from online provider
        /// </summary>
        public static async Task<bool> InitializeAsync()
        {
            var provider = OnlineOffsetProvider.Instance;
            var success = await provider.InitializeAsync();

            if (success)
            {
                var db = provider.GetCurrentOffsets();
                _offsets = db;
                _functions = FunctionOffsetsData.CreateHardcodedDefaults(); // TODO: Load from online
                _structures = new StructureOffsetsData
                {
                    Character = new CharacterStructureOffsets()
                };
            }
            else
            {
                // Use hardcoded defaults
                _offsets = GameOffsetsData.CreateHardcodedDefaults();
                _functions = FunctionOffsetsData.CreateHardcodedDefaults();
                _structures = new StructureOffsetsData
                {
                    Character = new CharacterStructureOffsets()
                };
            }

            IsInitialized = true;
            return success;
        }

        /// <summary>
        /// Synchronous initialization
        /// </summary>
        public static bool Initialize()
        {
            return InitializeAsync().GetAwaiter().GetResult();
        }

        // Accessors
        public static long BaseAddress => _offsets?.BaseAddress ?? 0x140000000;

        // Game Core
        public static long WorldInstance => _offsets?.WorldInstance ?? KenshiMemory.Game.WorldInstance;
        public static long GameState => _offsets?.GameState ?? KenshiMemory.Game.GameState;
        public static long GameTime => _offsets?.GameTime ?? KenshiMemory.Game.GameTime;
        public static long GameDay => _offsets?.GameDay ?? KenshiMemory.Game.GameDay;

        // Characters
        public static long PlayerSquadList => _offsets?.PlayerSquadList ?? KenshiMemory.Characters.PlayerSquadList;
        public static long PlayerSquadCount => _offsets?.PlayerSquadCount ?? KenshiMemory.Characters.PlayerSquadCount;
        public static long AllCharactersList => _offsets?.AllCharactersList ?? KenshiMemory.Characters.AllCharactersList;
        public static long AllCharactersCount => _offsets?.AllCharactersCount ?? KenshiMemory.Characters.AllCharactersCount;
        public static long SelectedCharacter => _offsets?.SelectedCharacter ?? KenshiMemory.Characters.SelectedCharacter;

        // Factions
        public static long FactionList => _offsets?.FactionList ?? KenshiMemory.Factions.FactionList;
        public static long FactionCount => _offsets?.FactionCount ?? KenshiMemory.Factions.FactionCount;
        public static long PlayerFaction => _offsets?.PlayerFaction ?? KenshiMemory.Factions.PlayerFaction;

        // World
        public static long BuildingList => _offsets?.BuildingList ?? KenshiMemory.World.BuildingList;
        public static long BuildingCount => _offsets?.BuildingCount ?? KenshiMemory.World.BuildingCount;
        public static long WeatherSystem => _offsets?.WeatherSystem ?? KenshiMemory.World.WeatherSystem;

        // Engine
        public static long PhysicsWorld => _offsets?.PhysicsWorld ?? KenshiMemory.Engine.PhysicsWorld;
        public static long Camera => _offsets?.Camera ?? KenshiMemory.Engine.Camera;
        public static long InputHandler => _offsets?.InputHandler ?? KenshiMemory.Input.InputHandler;

        // Functions
        public static long FnSpawnCharacter => _functions?.SpawnCharacter ?? KenshiMemory.Functions.SpawnCharacter;
        public static long FnDespawnCharacter => _functions?.DespawnCharacter ?? KenshiMemory.Functions.DespawnCharacter;
        public static long FnAddToSquad => _functions?.AddToSquad ?? KenshiMemory.Functions.AddToSquad;
        public static long FnRemoveFromSquad => _functions?.RemoveFromSquad ?? KenshiMemory.Functions.RemoveFromSquad;
        public static long FnAddItemToInventory => _functions?.AddItemToInventory ?? KenshiMemory.Functions.AddItemToInventory;
        public static long FnRemoveItemFromInventory => _functions?.RemoveItemFromInventory ?? KenshiMemory.Functions.RemoveItemFromInventory;
        public static long FnSetCharacterState => _functions?.SetCharacterState ?? KenshiMemory.Functions.SetCharacterState;
        public static long FnIssueCommand => _functions?.IssueCommand ?? KenshiMemory.Functions.IssueCommand;
        public static long FnSetFactionRelation => _functions?.SetFactionRelation ?? KenshiMemory.Functions.SetFactionRelation;
        public static long FnPathfindRequest => _functions?.PathfindRequest ?? KenshiMemory.Functions.PathfindRequest;
        public static long FnCombatAttack => _functions?.CombatAttack ?? KenshiMemory.Functions.CombatAttack;

        // Character structure offsets
        public static class Character
        {
            public static int Position => _structures?.Character?.Position ?? KenshiMemory.CharacterOffsets.Position;
            public static int Rotation => _structures?.Character?.Rotation ?? KenshiMemory.CharacterOffsets.Rotation;
            public static int Health => _structures?.Character?.Health ?? KenshiMemory.CharacterOffsets.Health;
            public static int MaxHealth => _structures?.Character?.MaxHealth ?? KenshiMemory.CharacterOffsets.MaxHealth;
            public static int Blood => _structures?.Character?.Blood ?? KenshiMemory.CharacterOffsets.Blood;
            public static int Hunger => _structures?.Character?.Hunger ?? KenshiMemory.CharacterOffsets.Hunger;
            public static int Inventory => _structures?.Character?.Inventory ?? KenshiMemory.CharacterOffsets.Inventory;
            public static int Equipment => _structures?.Character?.Equipment ?? KenshiMemory.CharacterOffsets.Equipment;
            public static int AI => _structures?.Character?.AI ?? KenshiMemory.CharacterOffsets.AI;
            public static int State => _structures?.Character?.State ?? KenshiMemory.CharacterOffsets.State;
            public static int Faction => _structures?.Character?.Faction ?? KenshiMemory.CharacterOffsets.Faction;
            public static int Squad => _structures?.Character?.Squad ?? KenshiMemory.CharacterOffsets.Squad;
            public static int AnimState => _structures?.Character?.AnimState ?? KenshiMemory.CharacterOffsets.AnimState;
            public static int Body => _structures?.Character?.Body ?? KenshiMemory.CharacterOffsets.Body;
        }

        /// <summary>
        /// Get absolute address (base + offset)
        /// </summary>
        public static long GetAbsolute(long offset) => BaseAddress + offset;

        /// <summary>
        /// Export current offsets as JSON
        /// </summary>
        public static string ExportAsJson()
        {
            var db = new OnlineOffsetDatabase
            {
                SchemaVersion = 1,
                GameVersion = "1.0.64",
                LastUpdated = DateTime.UtcNow,
                IsUniversal = false,
                GameOffsets = _offsets ?? GameOffsetsData.CreateHardcodedDefaults(),
                FunctionOffsets = _functions ?? FunctionOffsetsData.CreateHardcodedDefaults(),
                StructureOffsets = _structures ?? new StructureOffsetsData { Character = new CharacterStructureOffsets() }
            };

            return JsonSerializer.Serialize(db, new JsonSerializerOptions { WriteIndented = true });
        }
    }

    #endregion
}
