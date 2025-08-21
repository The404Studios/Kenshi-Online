using KenshiMultiplayer.Auth;
using KenshiMultiplayer.Common.BaseManager;
using KenshiMultiplayer.Common.KenshiMultiplayer;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Net;
using System.Numerics;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;

namespace KenshiMultiplayer
{
    /// <summary>
    /// Main integration class that ties together all multiplayer components
    /// Handles initialization, server/client modes, and game state synchronization
    /// </summary>
    public class MultiplayerIntegration
    {
        // Core components
        private PathCache pathCache;
        private PathInjector pathInjector;
        private ActionProcessor actionProcessor;
        private NetworkManager networkManager;
        private StateManager stateManager;
        private MemoryInterface memoryInterface;

        // Server/Client state
        private bool isServer;
        private bool isInitialized;
        private string serverAddress;
        private int serverPort = 27015;

        // Game state tracking
        private Dictionary<string, KenshiCharacter> localCharacters;
        private Dictionary<string, KenshiCharacter> remoteCharacters;
        private Dictionary<string, Squad> squads;
        private Dictionary<string, Building> buildings;

        // Performance monitoring
        private PerformanceMonitor perfMonitor;
        private int targetTickRate = 30;
        private int currentTickRate;

        // Synchronization intervals (ms)
        private const int POSITION_SYNC_INTERVAL = 100;
        private const int INVENTORY_SYNC_INTERVAL = 500;
        private const int STATS_SYNC_INTERVAL = 1000;
        private const int WORLD_SYNC_INTERVAL = 5000;

        // Memory addresses for Kenshi v0.98.50
        private readonly IntPtr PLAYER_CONTROLLER_OFFSET = new IntPtr(0x249BCB0);
        private readonly IntPtr WORLD_STATE_OFFSET = new IntPtr(0x24617B8);
        private readonly IntPtr SQUAD_MANAGER_OFFSET = new IntPtr(0x2498FA0);
        private readonly IntPtr TIME_MANAGER_OFFSET = new IntPtr(0x245C890);

        public MultiplayerIntegration()
        {
            localCharacters = new Dictionary<string, KenshiCharacter>();
            remoteCharacters = new Dictionary<string, KenshiCharacter>();
            squads = new Dictionary<string, Squad>();
            buildings = new Dictionary<string, Building>();
            perfMonitor = new PerformanceMonitor();
        }

        /// <summary>
        /// Initialize the multiplayer system
        /// </summary>
        public async Task<bool> Initialize(bool asServer, string address = null)
        {
            try
            {
                Logger.Log("Initializing Kenshi Multiplayer System...");

                isServer = asServer;
                serverAddress = address ?? "127.0.0.1";

                // Initialize memory interface
                if (!InitializeMemoryInterface())
                {
                    Logger.Log("Failed to initialize memory interface");
                    return false;
                }

                // Initialize path cache system
                pathCache = new PathCache();
                if (asServer)
                {
                    Logger.Log("Server mode: Building path cache...");
                    await pathCache.Initialize();
                    await pathCache.PreBakeCommonPaths();
                }

                // Initialize path injection
                pathInjector = new PathInjector(pathCache);
                if (!pathInjector.Initialize())
                {
                    Logger.Log("Failed to initialize path injector");
                    return false;
                }

                // Hook into game pathfinding
                pathInjector.HookPathfinding();

                // Initialize action processor
                actionProcessor = new ActionProcessor();
                actionProcessor.OnActionProcessed += HandleProcessedAction;

                // Initialize network manager
                networkManager = new NetworkManager(isServer ? NetworkMode.Server : NetworkMode.Client);
                networkManager.OnMessageReceived += HandleNetworkMessage;
                networkManager.OnClientConnected += HandleClientConnected;
                networkManager.OnClientDisconnected += HandleClientDisconnected;

                // Start network
                if (isServer)
                {
                    if (!await networkManager.StartServer(serverPort))
                    {
                        Logger.Log("Failed to start server");
                        return false;
                    }
                    Logger.Log($"Server started on port {serverPort}");
                }
                else
                {
                    if (!await networkManager.ConnectToServer(serverAddress, serverPort))
                    {
                        Logger.Log($"Failed to connect to server at {serverAddress}:{serverPort}");
                        return false;
                    }
                    Logger.Log("Connected to server");

                    // Request path cache from server
                    await RequestPathCache();
                }

                // Initialize state manager
                stateManager = new StateManager(isServer);
                stateManager.OnStateChanged += HandleStateChange;

                // Start synchronization loops
                StartSynchronizationLoops();

                // Hook game events
                HookGameEvents();

                isInitialized = true;
                Logger.Log("Multiplayer system initialized successfully");
                return true;
            }
            catch (Exception ex)
            {
                Logger.Log($"Failed to initialize multiplayer: {ex.Message}");
                return false;
            }
        }

        /// <summary>
        /// Initialize memory interface for reading/writing game memory
        /// </summary>
        private bool InitializeMemoryInterface()
        {
            try
            {
                var process = Process.GetProcessesByName("kenshi_x64").FirstOrDefault();
                if (process == null)
                {
                    Logger.Log("Kenshi process not found");
                    return false;
                }

                memoryInterface = new MemoryInterface(process);
                return memoryInterface.Initialize();
            }
            catch (Exception ex)
            {
                Logger.Log($"Memory interface initialization failed: {ex.Message}");
                return false;
            }
        }

        /// <summary>
        /// Hook into game events for state tracking
        /// </summary>
        private void HookGameEvents()
        {
            // Hook character movement
            memoryInterface.HookFunction(PLAYER_CONTROLLER_OFFSET + 0x120, OnCharacterMove);

            // Hook combat events
            memoryInterface.HookFunction(PLAYER_CONTROLLER_OFFSET + 0x450, OnCombatAction);

            // Hook inventory changes
            memoryInterface.HookFunction(PLAYER_CONTROLLER_OFFSET + 0x780, OnInventoryChange);

            // Hook squad changes
            memoryInterface.HookFunction(SQUAD_MANAGER_OFFSET + 0x90, OnSquadChange);

            // Hook time changes
            memoryInterface.HookFunction(TIME_MANAGER_OFFSET + 0x40, OnTimeChange);
        }

        /// <summary>
        /// Start all synchronization loops
        /// </summary>
        private void StartSynchronizationLoops()
        {
            // Position sync loop
            Task.Run(async () =>
            {
                while (isInitialized)
                {
                    await SyncPositions();
                    await Task.Delay(POSITION_SYNC_INTERVAL);
                }
            });

            // Inventory sync loop
            Task.Run(async () =>
            {
                while (isInitialized)
                {
                    await SyncInventories();
                    await Task.Delay(INVENTORY_SYNC_INTERVAL);
                }
            });

            // Stats sync loop
            Task.Run(async () =>
            {
                while (isInitialized)
                {
                    await SyncCharacterStats();
                    await Task.Delay(STATS_SYNC_INTERVAL);
                }
            });

            // World sync loop (server only)
            if (isServer)
            {
                Task.Run(async () =>
                {
                    while (isInitialized)
                    {
                        await SyncWorldState();
                        await Task.Delay(WORLD_SYNC_INTERVAL);
                    }
                });
            }

            // Action processing loop
            Task.Run(async () =>
            {
                while (isInitialized)
                {
                    await actionProcessor.ProcessBatch();
                    await Task.Delay(1000 / targetTickRate);
                }
            });
        }

        /// <summary>
        /// Synchronize character positions
        /// </summary>
        private async Task SyncPositions()
        {
            try
            {
                var positions = GetLocalCharacterPositions();

                var positionData = new PositionSyncData
                {
                    Timestamp = GetGameTime(),
                    Positions = positions.Select(p => new CharacterPosition
                    {
                        CharacterId = p.Key,
                        X = p.Value.X,
                        Y = p.Value.Y,
                        Z = p.Value.Z,
                        Rotation = p.Value.W
                    }).ToList()
                };

                await networkManager.SendMessage(new NetworkMessage
                {
                    Type = MessageType.PositionSync,
                    Data = positionData
                });
            }
            catch (Exception ex)
            {
                Logger.Log($"Position sync failed: {ex.Message}");
            }
        }

        /// <summary>
        /// Synchronize inventories
        /// </summary>
        private async Task SyncInventories()
        {
            try
            {
                foreach (var character in localCharacters.Values)
                {
                    if (character.InventoryChanged)
                    {
                        var inventoryData = new InventorySyncData
                        {
                            CharacterId = character.ID.ToString(),
                            Items = character.Inventory.Select(i => new ItemData
                            {
                                ItemId = i.ID,
                                ItemType = i.Type,
                                Quantity = i.Quantity,
                                Quality = i.Quality,
                                SlotIndex = i.SlotIndex
                            }).ToList()
                        };

                        await networkManager.SendMessage(new NetworkMessage
                        {
                            Type = MessageType.InventorySync,
                            Data = inventoryData
                        });

                        character.InventoryChanged = false;
                    }
                }
            }
            catch (Exception ex)
            {
                Logger.Log($"Inventory sync failed: {ex.Message}");
            }
        }

        /// <summary>
        /// Synchronize character stats (health, skills, etc.)
        /// </summary>
        private async Task SyncCharacterStats()
        {
            try
            {
                foreach (var character in localCharacters.Values)
                {
                    if (character.StatsChanged)
                    {
                        var statsData = new CharacterStatsData
                        {
                            CharacterId = character.ID.ToString(),
                            LimbHealth = character.LimbHealth,
                            Skills = character.Skills,
                            Hunger = character.Hunger,
                            IsUnconscious = character.IsUnconscious,
                            IsDead = character.IsDead
                        };

                        await networkManager.SendMessage(new NetworkMessage
                        {
                            Type = MessageType.StatsSync,
                            Data = statsData
                        });

                        character.StatsChanged = false;
                    }
                }
            }
            catch (Exception ex)
            {
                Logger.Log($"Stats sync failed: {ex.Message}");
            }
        }

        /// <summary>
        /// Synchronize world state (server only)
        /// </summary>
        private async Task SyncWorldState()
        {
            if (!isServer) return;

            try
            {
                var worldState = new WorldStateData
                {
                    GameTime = GetGameTime(),
                    Weather = GetCurrentWeather(),
                    Factions = GetFactionRelations(),
                    Towns = GetTownStates(),
                    GlobalEvents = GetActiveEvents()
                };

                await networkManager.BroadcastMessage(new NetworkMessage
                {
                    Type = MessageType.WorldStateSync,
                    Data = worldState
                });
            }
            catch (Exception ex)
            {
                Logger.Log($"World sync failed: {ex.Message}");
            }
        }

        /// <summary>
        /// Handle processed actions from the action processor
        /// </summary>
        private async void HandleProcessedAction(ProcessedAction action)
        {
            try
            {
                // Apply action locally
                ApplyAction(action);

                // Send to network
                await networkManager.SendMessage(new NetworkMessage
                {
                    Type = MessageType.ActionExecuted,
                    Data = action
                });
            }
            catch (Exception ex)
            {
                Logger.Log($"Failed to handle processed action: {ex.Message}");
            }
        }

        /// <summary>
        /// Handle incoming network messages
        /// </summary>
        private async void HandleNetworkMessage(NetworkMessage message)
        {
            try
            {
                switch (message.Type)
                {
                    case MessageType.PositionSync:
                        HandlePositionSync((PositionSyncData)message.Data);
                        break;

                    case MessageType.ActionRequest:
                        var action = (PlayerAction)message.Data;
                        await actionProcessor.QueueAction(action);
                        break;

                    case MessageType.PathCacheRequest:
                        if (isServer)
                            await SendPathCache(message.SenderId);
                        break;

                    case MessageType.PathCacheData:
                        await ReceivePathCache((PathCacheData)message.Data);
                        break;

                    case MessageType.StateSync:
                        HandleStateSync((StateSyncData)message.Data);
                        break;

                    case MessageType.WorldStateSync:
                        HandleWorldStateSync((WorldStateData)message.Data);
                        break;
                }
            }
            catch (Exception ex)
            {
                Logger.Log($"Failed to handle network message: {ex.Message}");
            }
        }

        /// <summary>
        /// Handle client connection (server only)
        /// </summary>
        private async void HandleClientConnected(string clientId)
        {
            if (!isServer) return;

            Logger.Log($"Client connected: {clientId}");

            // Send initial state to new client
            await SendInitialState(clientId);

            // Send path cache
            await SendPathCache(clientId);

            // Notify other clients
            await networkManager.BroadcastMessage(new NetworkMessage
            {
                Type = MessageType.PlayerJoined,
                Data = new PlayerJoinedData { PlayerId = clientId }
            }, clientId);
        }

        /// <summary>
        /// Handle client disconnection
        /// </summary>
        private void HandleClientDisconnected(string clientId)
        {
            Logger.Log($"Client disconnected: {clientId}");

            // Remove client's characters
            var toRemove = remoteCharacters.Where(c => c.Value.PlayerId == clientId).Select(c => c.Key).ToList();
            foreach (var id in toRemove)
            {
                remoteCharacters.Remove(id);
            }

            // Notify other clients
            networkManager.BroadcastMessage(new NetworkMessage
            {
                Type = MessageType.PlayerLeft,
                Data = new PlayerLeftData { PlayerId = clientId }
            });
        }

        /// <summary>
        /// Request path cache from server (client only)
        /// </summary>
        private async Task RequestPathCache()
        {
            await networkManager.SendMessage(new NetworkMessage
            {
                Type = MessageType.PathCacheRequest
            });
        }

        /// <summary>
        /// Send path cache to client (server only)
        /// </summary>
        private async Task SendPathCache(string clientId)
        {
            var cacheData = await pathCache.SerializeCache();

            await networkManager.SendToClient(clientId, new NetworkMessage
            {
                Type = MessageType.PathCacheData,
                Data = new PathCacheData
                {
                    CompressedData = CompressData(cacheData),
                    Checksum = CalculateChecksum(cacheData)
                }
            });
        }

        /// <summary>
        /// Receive and load path cache (client only)
        /// </summary>
        private async Task ReceivePathCache(PathCacheData data)
        {
            var cacheData = DecompressData(data.CompressedData);

            if (CalculateChecksum(cacheData) != data.Checksum)
            {
                Logger.Log("Path cache checksum mismatch!");
                return;
            }

            await pathCache.LoadCache(cacheData);
            Logger.Log("Path cache loaded successfully");
        }

        /// <summary>
        /// Apply an action to the local game state
        /// </summary>
        private void ApplyAction(ProcessedAction action)
        {
            switch (action.Type)
            {
                case ActionType.Movement:
                    ApplyMovement(action);
                    break;

                case ActionType.Combat:
                    ApplyCombat(action);
                    break;

                case ActionType.Interaction:
                    ApplyInteraction(action);
                    break;

                case ActionType.Trade:
                    ApplyTrade(action);
                    break;

                case ActionType.Build:
                    ApplyBuild(action);
                    break;
            }
        }

        /// <summary>
        /// Game event callbacks
        /// </summary>
        private void OnCharacterMove(IntPtr args)
        {
            // Extract movement data from game memory
            var moveData = memoryInterface.ReadStruct<CharacterMoveData>(args);

            // Queue movement action
            actionProcessor.QueueAction(new PlayerAction
            {
                Type = ActionType.Movement,
                CharacterId = moveData.CharacterId.ToString(),
                TargetPosition = new Vector3(moveData.X, moveData.Y, moveData.Z),
                Timestamp = GetGameTime()
            });
        }

        private void OnCombatAction(IntPtr args)
        {
            var combatData = memoryInterface.ReadStruct<CombatActionData>(args);

            actionProcessor.QueueAction(new PlayerAction
            {
                Type = ActionType.Combat,
                CharacterId = combatData.AttackerId.ToString(),
                TargetId = combatData.TargetId.ToString(),
                ActionSubtype = combatData.AttackType,
                Timestamp = GetGameTime()
            });
        }

        private void OnInventoryChange(IntPtr args)
        {
            var character = GetCharacterFromPointer(args);
            if (character != null)
            {
                character.InventoryChanged = true;
            }
        }

        private void OnSquadChange(IntPtr args)
        {
            RefreshSquadData();
        }

        private void OnTimeChange(IntPtr args)
        {
            // Handle time synchronization
            if (isServer)
            {
                var timeData = memoryInterface.ReadStruct<TimeData>(args);
                stateManager.UpdateGameTime(timeData.GameTime, timeData.TimeMultiplier);
            }
        }

        /// <summary>
        /// Shutdown the multiplayer system
        /// </summary>
        public async Task Shutdown()
        {
            isInitialized = false;

            // Unhook pathfinding
            pathInjector?.UnhookPathfinding();

            // Disconnect network
            await networkManager?.Shutdown();

            // Clean up resources
            pathCache?.Dispose();
            memoryInterface?.Dispose();

            Logger.Log("Multiplayer system shut down");
        }

        // Helper methods
        private byte[] CompressData(byte[] data)
        {
            using (var output = new MemoryStream())
            {
                using (var gzip = new System.IO.Compression.GZipStream(output, System.IO.Compression.CompressionMode.Compress))
                {
                    gzip.Write(data, 0, data.Length);
                }
                return output.ToArray();
            }
        }

        private byte[] DecompressData(byte[] data)
        {
            using (var input = new MemoryStream(data))
            using (var output = new MemoryStream())
            {
                using (var gzip = new System.IO.Compression.GZipStream(input, System.IO.Compression.CompressionMode.Decompress))
                {
                    gzip.CopyTo(output);
                }
                return output.ToArray();
            }
        }

        private string CalculateChecksum(byte[] data)
        {
            using (var sha256 = System.Security.Cryptography.SHA256.Create())
            {
                var hash = sha256.ComputeHash(data);
                return BitConverter.ToString(hash).Replace("-", "");
            }
        }

        private double GetGameTime()
        {
            return memoryInterface.ReadDouble(TIME_MANAGER_OFFSET + 0x18);
        }

        private Dictionary<string, Vector4> GetLocalCharacterPositions()
        {
            var positions = new Dictionary<string, Vector4>();
            foreach (var character in localCharacters.Values)
            {
                positions[character.ID.ToString()] = new Vector4(
                    character.PosX,
                    character.PosY,
                    character.PosZ,
                    character.Rotation
                );
            }
            return positions;
        }
    }
}