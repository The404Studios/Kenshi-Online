using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;
using KenshiMultiplayer.Data;
using KenshiMultiplayer.Networking;
using KenshiMultiplayer.Utility;

namespace KenshiMultiplayer.Game
{
    /// <summary>
    /// Manages player spawning and multiplayer spawn coordination
    /// </summary>
    public class SpawnManager
    {
        private readonly KenshiGameBridge gameBridge;
        private readonly PlayerController playerController;
        private readonly Dictionary<string, SpawnRequest> pendingSpawns = new Dictionary<string, SpawnRequest>();
        private readonly Dictionary<string, string> playerSpawnLocations = new Dictionary<string, string>();
        private const string LOG_PREFIX = "[SpawnManager] ";

        // Predefined spawn locations from Kenshi world
        private static readonly Dictionary<string, SpawnLocation> SpawnLocations = new Dictionary<string, SpawnLocation>
        {
            // Major cities and safe zones
            { "Hub", new SpawnLocation("The Hub", -4200, 150, 18500, "Safe trading hub") },
            { "Squin", new SpawnLocation("Squin", 15800, 180, 24300, "United Cities outpost") },
            { "Sho-Battai", new SpawnLocation("Sho-Battai", 32500, 200, -12400, "Shek Kingdom capital") },
            { "Heng", new SpawnLocation("Heng", 52800, 160, -2100, "Major United Cities city") },
            { "Stack", new SpawnLocation("Stack", -11200, 170, 8900, "Neutral tech hunters") },
            { "Admag", new SpawnLocation("Admag", 72400, 190, 15600, "Holy Nation city") },
            { "BadTeeth", new SpawnLocation("Bad Teeth", 38900, 175, 29800, "Border zone outpost") },
            { "Bark", new SpawnLocation("Bark", 22100, 165, 33700, "Swamp village") },
            { "Stoat", new SpawnLocation("Stoat", -8600, 155, -16200, "Desert village") },
            { "WorldsEnd", new SpawnLocation("World's End", -88500, 250, 51200, "Northern outpost") },
            { "FlatsLagoon", new SpawnLocation("Flats Lagoon", 68900, 145, 48700, "Coastal city") },
            { "Shark", new SpawnLocation("Shark", 18700, 160, 8200, "Swamp base") },
            { "MudTown", new SpawnLocation("Mud Town", 27800, 170, 15500, "Swamp settlement") },
            { "Mongrel", new SpawnLocation("Mongrel", -18900, 180, 72400, "Fogmen territory outpost") },
            { "Catun", new SpawnLocation("Catun", 45200, 175, 41800, "Nomad city") },
            { "Spring", new SpawnLocation("Spring", 62100, 165, 28900, "Holy Nation village") },

            // Special spawn locations
            { "Default", new SpawnLocation("Default Start", -4200, 150, 18500, "Default game start location") },
            { "Random", new SpawnLocation("Random", 0, 0, 0, "Random safe location") }
        };

        public SpawnManager(KenshiGameBridge gameBridge, PlayerController playerController)
        {
            this.gameBridge = gameBridge ?? throw new ArgumentNullException(nameof(gameBridge));
            this.playerController = playerController ?? throw new ArgumentNullException(nameof(playerController));
            Logger.Log(LOG_PREFIX + "SpawnManager initialized");
        }

        #region Single Player Spawning

        /// <summary>
        /// Spawn a player at a specific location
        /// </summary>
        public async Task<bool> SpawnPlayer(string playerId, PlayerData playerData, string locationName = "Default")
        {
            try
            {
                Logger.Log(LOG_PREFIX + $"Spawning player {playerId} ({playerData.DisplayName}) at {locationName}");

                // Get spawn location
                if (!SpawnLocations.TryGetValue(locationName, out var spawnLocation))
                {
                    Logger.Log(LOG_PREFIX + $"Unknown spawn location '{locationName}', using default");
                    spawnLocation = SpawnLocations["Default"];
                }

                // Handle random spawn
                if (locationName == "Random")
                {
                    spawnLocation = GetRandomSafeLocation();
                }

                // Create position
                Position spawnPos = new Position(
                    spawnLocation.X,
                    spawnLocation.Y,
                    spawnLocation.Z
                );

                // Update player data
                playerData.Position = spawnPos;
                playerData.CurrentState = PlayerState.Idle;

                // Register with controller
                if (!playerController.RegisterPlayer(playerId, playerData))
                {
                    Logger.Log(LOG_PREFIX + $"ERROR: Failed to register player {playerId}");
                    return false;
                }

                // Spawn in game
                bool spawned = gameBridge.SpawnPlayer(playerId, playerData, spawnPos);

                if (spawned)
                {
                    playerSpawnLocations[playerId] = locationName;
                    Logger.Log(LOG_PREFIX + $"Successfully spawned player {playerId} at {spawnLocation.Name}");
                    OnPlayerSpawned?.Invoke(playerId, spawnPos);
                }
                else
                {
                    Logger.Log(LOG_PREFIX + $"ERROR: Failed to spawn player {playerId} in game");
                }

                return spawned;
            }
            catch (Exception ex)
            {
                Logger.Log(LOG_PREFIX + $"ERROR spawning player: {ex.Message}");
                return false;
            }
        }

        /// <summary>
        /// Spawn player at custom coordinates
        /// </summary>
        public async Task<bool> SpawnPlayerAt(string playerId, PlayerData playerData, float x, float y, float z)
        {
            try
            {
                Logger.Log(LOG_PREFIX + $"Spawning player {playerId} at custom coords ({x}, {y}, {z})");

                Position spawnPos = new Position(x, y, z);
                playerData.Position = spawnPos;
                playerData.CurrentState = PlayerState.Idle;

                if (!playerController.RegisterPlayer(playerId, playerData))
                {
                    Logger.Log(LOG_PREFIX + $"ERROR: Failed to register player {playerId}");
                    return false;
                }

                bool spawned = gameBridge.SpawnPlayer(playerId, playerData, spawnPos);

                if (spawned)
                {
                    playerSpawnLocations[playerId] = $"Custom_{x}_{y}_{z}";
                    Logger.Log(LOG_PREFIX + $"Successfully spawned player {playerId} at custom location");
                    OnPlayerSpawned?.Invoke(playerId, spawnPos);
                }

                return spawned;
            }
            catch (Exception ex)
            {
                Logger.Log(LOG_PREFIX + $"ERROR spawning player: {ex.Message}");
                return false;
            }
        }

        #endregion

        #region Multiplayer Group Spawning

        /// <summary>
        /// Request to spawn with friends - coordinates spawning multiple players together
        /// </summary>
        public string RequestGroupSpawn(List<string> playerIds, string locationName = "Default")
        {
            try
            {
                string groupId = Guid.NewGuid().ToString();
                Logger.Log(LOG_PREFIX + $"Creating group spawn request {groupId} for {playerIds.Count} players at {locationName}");

                // Get spawn location
                if (!SpawnLocations.TryGetValue(locationName, out var spawnLocation))
                {
                    spawnLocation = SpawnLocations["Default"];
                }

                // Handle random
                if (locationName == "Random")
                {
                    spawnLocation = GetRandomSafeLocation();
                }

                // Create spawn request
                var request = new SpawnRequest
                {
                    GroupId = groupId,
                    PlayerIds = playerIds,
                    SpawnLocation = spawnLocation,
                    RequestTime = DateTime.UtcNow,
                    IsGroupSpawn = true,
                    ReadyPlayers = new HashSet<string>()
                };

                pendingSpawns[groupId] = request;

                Logger.Log(LOG_PREFIX + $"Group spawn {groupId} created, waiting for {playerIds.Count} players to ready up");
                OnGroupSpawnRequested?.Invoke(groupId, playerIds, spawnLocation.Name);

                return groupId;
            }
            catch (Exception ex)
            {
                Logger.Log(LOG_PREFIX + $"ERROR creating group spawn: {ex.Message}");
                return null;
            }
        }

        /// <summary>
        /// Player signals they're ready to spawn
        /// </summary>
        public bool PlayerReadyToSpawn(string groupId, string playerId)
        {
            try
            {
                if (!pendingSpawns.TryGetValue(groupId, out var request))
                {
                    Logger.Log(LOG_PREFIX + $"ERROR: Group spawn {groupId} not found");
                    return false;
                }

                if (!request.PlayerIds.Contains(playerId))
                {
                    Logger.Log(LOG_PREFIX + $"ERROR: Player {playerId} not in group {groupId}");
                    return false;
                }

                request.ReadyPlayers.Add(playerId);
                Logger.Log(LOG_PREFIX + $"Player {playerId} ready for group spawn {groupId} ({request.ReadyPlayers.Count}/{request.PlayerIds.Count})");

                // Check if all players are ready
                if (request.ReadyPlayers.Count == request.PlayerIds.Count)
                {
                    Logger.Log(LOG_PREFIX + $"All players ready! Executing group spawn {groupId}");
                    _ = ExecuteGroupSpawn(groupId);
                }

                return true;
            }
            catch (Exception ex)
            {
                Logger.Log(LOG_PREFIX + $"ERROR setting player ready: {ex.Message}");
                return false;
            }
        }

        /// <summary>
        /// Execute the group spawn - spawns all players together
        /// </summary>
        private async Task<bool> ExecuteGroupSpawn(string groupId)
        {
            try
            {
                if (!pendingSpawns.TryGetValue(groupId, out var request))
                {
                    Logger.Log(LOG_PREFIX + $"ERROR: Group spawn {groupId} not found");
                    return false;
                }

                Logger.Log(LOG_PREFIX + $"Executing group spawn {groupId} for {request.PlayerIds.Count} players");

                if (request.PlayerIds.Count == 0)
                {
                    Logger.Log(LOG_PREFIX + $"ERROR: No players in group spawn {groupId}");
                    return false;
                }

                var spawnLocation = request.SpawnLocation;
                var tasks = new List<Task<bool>>();

                // Calculate spawn positions in a circle around the location
                float radius = 5.0f; // 5 meters apart
                float angleStep = 360.0f / request.PlayerIds.Count;

                for (int i = 0; i < request.PlayerIds.Count; i++)
                {
                    string playerId = request.PlayerIds[i];
                    float angle = angleStep * i * (float)Math.PI / 180.0f;

                    // Position players in a circle
                    float offsetX = radius * (float)Math.Cos(angle);
                    float offsetZ = radius * (float)Math.Sin(angle);

                    float spawnX = spawnLocation.X + offsetX;
                    float spawnY = spawnLocation.Y;
                    float spawnZ = spawnLocation.Z + offsetZ;

                    Logger.Log(LOG_PREFIX + $"Spawning player {playerId} at offset ({offsetX:F2}, 0, {offsetZ:F2})");

                    // Get player data (would need to be provided)
                    var playerData = new PlayerData
                    {
                        PlayerId = playerId,
                        DisplayName = $"Player_{playerId.Substring(0, 8)}",
                        Position = new Position(spawnX, spawnY, spawnZ),
                        Health = 100,
                        MaxHealth = 100,
                        CurrentState = PlayerState.Idle
                    };

                    tasks.Add(SpawnPlayerAt(playerId, playerData, spawnX, spawnY, spawnZ));
                }

                // Wait for all spawns to complete
                var results = await Task.WhenAll(tasks);
                bool allSuccessful = results.All(r => r);

                if (allSuccessful)
                {
                    Logger.Log(LOG_PREFIX + $"Group spawn {groupId} completed successfully!");
                    OnGroupSpawnCompleted?.Invoke(groupId, request.PlayerIds);
                }
                else
                {
                    Logger.Log(LOG_PREFIX + $"Group spawn {groupId} completed with some failures");
                }

                // Clean up
                pendingSpawns.Remove(groupId);

                return allSuccessful;
            }
            catch (Exception ex)
            {
                Logger.Log(LOG_PREFIX + $"ERROR executing group spawn: {ex.Message}");
                return false;
            }
        }

        /// <summary>
        /// Cancel a pending group spawn
        /// </summary>
        public bool CancelGroupSpawn(string groupId)
        {
            if (pendingSpawns.Remove(groupId))
            {
                Logger.Log(LOG_PREFIX + $"Cancelled group spawn {groupId}");
                OnGroupSpawnCancelled?.Invoke(groupId);
                return true;
            }
            return false;
        }

        #endregion

        #region Respawning

        /// <summary>
        /// Respawn a dead player
        /// </summary>
        public async Task<bool> RespawnPlayer(string playerId, string locationName = null)
        {
            try
            {
                Logger.Log(LOG_PREFIX + $"Respawning player {playerId}");

                // Get player data
                var playerData = playerController.GetPlayerData(playerId);
                if (playerData == null)
                {
                    Logger.Log(LOG_PREFIX + $"ERROR: Player {playerId} not found");
                    return false;
                }

                // Despawn first if already spawned
                gameBridge.DespawnPlayer(playerId);
                await Task.Delay(100); // Small delay

                // Use last spawn location if not specified
                if (string.IsNullOrEmpty(locationName))
                {
                    if (playerSpawnLocations.TryGetValue(playerId, out var lastLocation))
                    {
                        locationName = lastLocation;
                    }
                    else
                    {
                        locationName = "Default";
                    }
                }

                // Reset player state
                playerData.Health = playerData.MaxHealth;
                playerData.CurrentState = PlayerState.Idle;

                // Respawn
                bool success = await SpawnPlayer(playerId, playerData, locationName);

                if (success)
                {
                    Logger.Log(LOG_PREFIX + $"Successfully respawned player {playerId}");
                    OnPlayerRespawned?.Invoke(playerId);
                }

                return success;
            }
            catch (Exception ex)
            {
                Logger.Log(LOG_PREFIX + $"ERROR respawning player: {ex.Message}");
                return false;
            }
        }

        #endregion

        #region Despawning

        /// <summary>
        /// Despawn a player from the game
        /// </summary>
        public bool DespawnPlayer(string playerId)
        {
            try
            {
                Logger.Log(LOG_PREFIX + $"Despawning player {playerId}");

                // Unregister from controller
                playerController.UnregisterPlayer(playerId);

                // Despawn from game
                bool success = gameBridge.DespawnPlayer(playerId);

                if (success)
                {
                    playerSpawnLocations.Remove(playerId);
                    Logger.Log(LOG_PREFIX + $"Successfully despawned player {playerId}");
                    OnPlayerDespawned?.Invoke(playerId);
                }

                return success;
            }
            catch (Exception ex)
            {
                Logger.Log(LOG_PREFIX + $"ERROR despawning player: {ex.Message}");
                return false;
            }
        }

        #endregion

        #region Helpers

        private SpawnLocation GetRandomSafeLocation()
        {
            var safeLocations = SpawnLocations.Values
                .Where(loc => loc.Name != "Random" && loc.Name != "Default")
                .ToList();

            var random = new Random();
            return safeLocations[random.Next(safeLocations.Count)];
        }

        public List<string> GetAvailableSpawnLocations()
        {
            return SpawnLocations.Keys
                .Where(k => k != "Default" && k != "Random")
                .OrderBy(k => k)
                .ToList();
        }

        public SpawnLocation GetSpawnLocationInfo(string locationName)
        {
            return SpawnLocations.TryGetValue(locationName, out var location) ? location : null;
        }

        #endregion

        #region Events

        public event Action<string, Position> OnPlayerSpawned;
        public event Action<string> OnPlayerDespawned;
        public event Action<string> OnPlayerRespawned;
        public event Action<string, List<string>, string> OnGroupSpawnRequested;
        public event Action<string, List<string>> OnGroupSpawnCompleted;
        public event Action<string> OnGroupSpawnCancelled;

        #endregion

        #region Helper Classes

        public class SpawnLocation
        {
            public string Name { get; set; }
            public float X { get; set; }
            public float Y { get; set; }
            public float Z { get; set; }
            public string Description { get; set; }

            public SpawnLocation(string name, float x, float y, float z, string description)
            {
                Name = name;
                X = x;
                Y = y;
                Z = z;
                Description = description;
            }
        }

        private class SpawnRequest
        {
            public string GroupId { get; set; }
            public List<string> PlayerIds { get; set; }
            public SpawnLocation SpawnLocation { get; set; }
            public DateTime RequestTime { get; set; }
            public bool IsGroupSpawn { get; set; }
            public HashSet<string> ReadyPlayers { get; set; }
        }

        #endregion
    }
}
