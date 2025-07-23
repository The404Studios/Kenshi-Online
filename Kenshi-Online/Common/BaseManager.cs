using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using KenshiMultiplayer


namespace KenshiMultiplayer.Common.BaseManager
{
        public enum BuildingType
        {
            House,
            Storage,
            Production,
            Defense,
            Farm,
            Mine,
            Power,
            Research,
            Training,
            Medical,
            Prison
        }

        public enum BuildingStatus
        {
            Blueprint,
            UnderConstruction,
            Operational,
            Damaged,
            Destroyed,
            Upgrading
        }

        public enum ResourceType
        {
            BuildingMaterials,
            Iron,
            Copper,
            Stone,
            Wood,
            Fabric,
            ElectricalComponents,
            Food,
            Water,
            Power
        }

        public class Building
        {
            public string Id { get; set; } = Guid.NewGuid().ToString();
            public string Name { get; set; }
            public BuildingType Type { get; set; }
            public BuildingStatus Status { get; set; } = BuildingStatus.Blueprint;
            public Position Position { get; set; }
            public float Rotation { get; set; }
            public string BaseId { get; set; }
            public string OwnerId { get; set; }
            public DateTime PlacedAt { get; set; } = DateTime.UtcNow;
            public DateTime? CompletedAt { get; set; }

            // Construction
            public Dictionary<ResourceType, int> RequiredResources { get; set; } = new Dictionary<ResourceType, int>();
            public Dictionary<ResourceType, int> CurrentResources { get; set; } = new Dictionary<ResourceType, int>();
            public float ConstructionProgress { get; set; } = 0f;
            public List<string> AssignedWorkers { get; set; } = new List<string>();

            // Operation
            public bool IsOperational { get; set; } = false;
            public Dictionary<ResourceType, float> ResourceProduction { get; set; } = new Dictionary<ResourceType, float>();
            public Dictionary<ResourceType, float> ResourceConsumption { get; set; } = new Dictionary<ResourceType, float>();
            public int WorkerSlots { get; set; } = 1;
            public List<string> CurrentWorkers { get; set; } = new List<string>();

            // Stats
            public int Health { get; set; } = 100;
            public int MaxHealth { get; set; } = 100;
            public int DefenseRating { get; set; } = 0;
            public Dictionary<string, object> CustomData { get; set; } = new Dictionary<string, object>();
        }

        public class PlayerBase
        {
            public string Id { get; set; } = Guid.NewGuid().ToString();
            public string Name { get; set; }
            public string OwnerId { get; set; }
            public Position CenterPosition { get; set; }
            public float Radius { get; set; } = 100f;
            public DateTime FoundedAt { get; set; } = DateTime.UtcNow;

            // Buildings
            public List<Building> Buildings { get; set; } = new List<Building>();
            public int MaxBuildings { get; set; } = 50;

            // Resources
            public Dictionary<ResourceType, float> StoredResources { get; set; } = new Dictionary<ResourceType, float>();
            public Dictionary<ResourceType, float> StorageCapacity { get; set; } = new Dictionary<ResourceType, float>();

            // Population
            public List<string> Members { get; set; } = new List<string>();
            public List<string> Visitors { get; set; } = new List<string>();
            public Dictionary<string, BasePermissions> Permissions { get; set; } = new Dictionary<string, BasePermissions>();

            // Defense
            public int DefenseLevel { get; set; } = 0;
            public List<string> DefenseStructures { get; set; } = new List<string>();
            public bool IsUnderAttack { get; set; } = false;
            public string AttackingFaction { get; set; }

            // Production stats
            public Dictionary<ResourceType, float> TotalProduction { get; set; } = new Dictionary<ResourceType, float>();
            public Dictionary<ResourceType, float> TotalConsumption { get; set; } = new Dictionary<ResourceType, float>();

            // Settings
            public bool IsPublic { get; set; } = false;
            public bool AllowVisitors { get; set; } = true;
            public bool AutoAssignWorkers { get; set; } = true;
            public Dictionary<string, object> Settings { get; set; } = new Dictionary<string, object>();
        }

        public class BasePermissions
        {
            public bool CanBuild { get; set; } = false;
            public bool CanDestroy { get; set; } = false;
            public bool CanManageWorkers { get; set; } = false;
            public bool CanAccessStorage { get; set; } = true;
            public bool CanInviteMembers { get; set; } = false;
            public bool CanKickMembers { get; set; } = false;
            public bool CanChangeSettings { get; set; } = false;
        }

        public class BaseAttack
        {
            public string Id { get; set; } = Guid.NewGuid().ToString();
            public string BaseId { get; set; }
            public string AttackingFaction { get; set; }
            public DateTime StartTime { get; set; } = DateTime.UtcNow;
            public DateTime? EndTime { get; set; }
            public int AttackerStrength { get; set; }
            public int DefenderStrength { get; set; }
            public bool IsActive { get; set; } = true;
            public string Result { get; set; } // "defended", "lost", "ongoing"
            public Dictionary<string, int> Casualties { get; set; } = new Dictionary<string, int>();
            public List<string> DestroyedBuildings { get; set; } = new List<string>();
            public Dictionary<ResourceType, int> LootedResources { get; set; } = new Dictionary<ResourceType, int>();
        }

        public class ConstructionTask
        {
            public string Id { get; set; } = Guid.NewGuid().ToString();
            public string BuildingId { get; set; }
            public string AssignedWorker { get; set; }
            public DateTime StartTime { get; set; }
            public float Progress { get; set; }
            public float WorkSpeed { get; set; } = 1.0f;
        }

        public class BaseManager
        {
            private readonly Dictionary<string, PlayerBase> bases = new Dictionary<string, PlayerBase>();
            private readonly Dictionary<string, List<BaseAttack>> baseAttackHistory = new Dictionary<string, List<BaseAttack>>();
            private readonly List<ConstructionTask> activeTasks = new List<ConstructionTask>();

            private readonly string dataFilePath;
            private readonly EnhancedClient client;
            private readonly NotificationManager notificationManager;
            private readonly ReputationManager reputationManager;

            // Events
            public event EventHandler<PlayerBase> BaseCreated;
            public event EventHandler<Building> BuildingPlaced;
            public event EventHandler<Building> BuildingCompleted;
            public event EventHandler<Building> BuildingDestroyed;
            public event EventHandler<BaseAttack> BaseUnderAttack;
            public event EventHandler<BaseAttack> BaseAttackEnded;

            // Building costs
            private readonly Dictionary<BuildingType, Dictionary<ResourceType, int>> buildingCosts;

            public BaseManager(EnhancedClient clientInstance, NotificationManager notificationManager,
                ReputationManager reputationManager, string dataDirectory = "data")
            {
                this.client = clientInstance;
                this.notificationManager = notificationManager;
                this.reputationManager = reputationManager;
                dataFilePath = Path.Combine(dataDirectory, "bases.json");
                Directory.CreateDirectory(dataDirectory);

                LoadData();
                InitializeBuildingCosts();

                if (client != null)
                {
                    client.MessageReceived += OnMessageReceived;
                }

                // Start construction update timer
                System.Threading.Tasks.Task.Run(async () => {
                    while (true)
                    {
                        await System.Threading.Tasks.Task.Delay(TimeSpan.FromSeconds(1));
                        UpdateConstruction();
                        UpdateProduction();
                    }
                });
            }

            private void LoadData()
            {
                try
                {
                    if (File.Exists(dataFilePath))
                    {
                        string json = File.ReadAllText(dataFilePath);
                        var data = JsonSerializer.Deserialize<BaseData>(json);

                        if (data != null)
                        {
                            foreach (var baseData in data.Bases)
                            {
                                bases[baseData.Id] = baseData;
                            }

                            baseAttackHistory = data.AttackHistory ?? new Dictionary<string, List<BaseAttack>>();
                        }

                        Logger.Log($"Loaded {bases.Count} player bases");
                    }
                }
                catch (Exception ex)
                {
                    Logger.Log($"Error loading base data: {ex.Message}");
                }
            }

            private void SaveData()
            {
                try
                {
                    var data = new BaseData
                    {
                        Bases = bases.Values.ToList(),
                        AttackHistory = baseAttackHistory
                    };

                    string json = JsonSerializer.Serialize(data, new JsonSerializerOptions { WriteIndented = true });
                    File.WriteAllText(dataFilePath, json);
                }
                catch (Exception ex)
                {
                    Logger.Log($"Error saving base data: {ex.Message}");
                }
            }

            private void InitializeBuildingCosts()
            {
                buildingCosts = new Dictionary<BuildingType, Dictionary<ResourceType, int>>
                {
                    [BuildingType.House] = new Dictionary<ResourceType, int>
                {
                    { ResourceType.BuildingMaterials, 10 },
                    { ResourceType.Wood, 5 }
                },
                    [BuildingType.Storage] = new Dictionary<ResourceType, int>
                {
                    { ResourceType.BuildingMaterials, 20 },
                    { ResourceType.Iron, 10 }
                },
                    [BuildingType.Production] = new Dictionary<ResourceType, int>
                {
                    { ResourceType.BuildingMaterials, 15 },
                    { ResourceType.Iron, 20 },
                    { ResourceType.ElectricalComponents, 5 }
                },
                    [BuildingType.Defense] = new Dictionary<ResourceType, int>
                {
                    { ResourceType.BuildingMaterials, 30 },
                    { ResourceType.Iron, 40 },
                    { ResourceType.Stone, 20 }
                },
                    [BuildingType.Farm] = new Dictionary<ResourceType, int>
                {
                    { ResourceType.BuildingMaterials, 5 },
                    { ResourceType.Wood, 10 }
                },
                    [BuildingType.Mine] = new Dictionary<ResourceType, int>
                {
                    { ResourceType.BuildingMaterials, 25 },
                    { ResourceType.Iron, 15 }
                },
                    [BuildingType.Power] = new Dictionary<ResourceType, int>
                {
                    { ResourceType.BuildingMaterials, 20 },
                    { ResourceType.ElectricalComponents, 20 },
                    { ResourceType.Copper, 30 }
                },
                    [BuildingType.Research] = new Dictionary<ResourceType, int>
                {
                    { ResourceType.BuildingMaterials, 30 },
                    { ResourceType.ElectricalComponents, 15 }
                },
                    [BuildingType.Training] = new Dictionary<ResourceType, int>
                {
                    { ResourceType.BuildingMaterials, 15 },
                    { ResourceType.Wood, 20 }
                },
                    [BuildingType.Medical] = new Dictionary<ResourceType, int>
                {
                    { ResourceType.BuildingMaterials, 20 },
                    { ResourceType.Fabric, 10 }
                },
                    [BuildingType.Prison] = new Dictionary<ResourceType, int>
                {
                    { ResourceType.BuildingMaterials, 25 },
                    { ResourceType.Iron, 30 }
                }
                };
            }

            // Base management
            public PlayerBase CreateBase(string name, Position position)
            {
                var playerBase = new PlayerBase
                {
                    Name = name,
                    OwnerId = client.CurrentUsername,
                    CenterPosition = position
                };

                // Initialize storage capacities
                playerBase.StorageCapacity[ResourceType.BuildingMaterials] = 100;
                playerBase.StorageCapacity[ResourceType.Food] = 100;
                playerBase.StorageCapacity[ResourceType.Water] = 100;

                // Add owner as member with full permissions
                playerBase.Members.Add(client.CurrentUsername);
                playerBase.Permissions[client.CurrentUsername] = new BasePermissions
                {
                    CanBuild = true,
                    CanDestroy = true,
                    CanManageWorkers = true,
                    CanAccessStorage = true,
                    CanInviteMembers = true,
                    CanKickMembers = true,
                    CanChangeSettings = true
                };

                bases[playerBase.Id] = playerBase;

                // Send to server
                var message = new GameMessage
                {
                    Type = "base_create",
                    PlayerId = client.CurrentUsername,
                    Data = new Dictionary<string, object>
                {
                    { "base", JsonSerializer.Serialize(playerBase) }
                },
                    SessionId = client.AuthToken
                };

                client.SendMessageToServer(message);

                SaveData();
                BaseCreated?.Invoke(this, playerBase);

                notificationManager?.NotifyAchievement(
                    "Base Founded!",
                    $"You have established {name}"
                );

                return playerBase;
            }

            // Building placement
            public Building PlaceBuilding(string baseId, string buildingName, BuildingType type, Position position, float rotation = 0)
            {
                if (!bases.TryGetValue(baseId, out var playerBase))
                    return null;

                // Check permissions
                if (!HasPermission(baseId, client.CurrentUsername, b => b.CanBuild))
                    return null;

                // Check building limit
                if (playerBase.Buildings.Count >= playerBase.MaxBuildings)
                {
                    notificationManager?.CreateNotification(
                        NotificationType.Warning,
                        "Building Limit Reached",
                        $"Base has reached maximum building limit ({playerBase.MaxBuildings})"
                    );
                    return null;
                }

                // Check if position is within base radius
                float distance = position.DistanceTo(playerBase.CenterPosition);
                if (distance > playerBase.Radius)
                {
                    notificationManager?.CreateNotification(
                        NotificationType.Warning,
                        "Too Far From Base",
                        "Building must be placed within base radius"
                    );
                    return null;
                }

                var building = new Building
                {
                    Name = buildingName,
                    Type = type,
                    Position = position,
                    Rotation = rotation,
                    BaseId = baseId,
                    OwnerId = client.CurrentUsername,
                    RequiredResources = buildingCosts.GetValueOrDefault(type, new Dictionary<ResourceType, int>())
                };

                // Set building-specific properties
                switch (type)
                {
                    case BuildingType.Storage:
                        building.CustomData["storageCapacity"] = 500;
                        break;
                    case BuildingType.Defense:
                        building.DefenseRating = 50;
                        building.MaxHealth = 200;
                        building.Health = 200;
                        break;
                    case BuildingType.Farm:
                        building.ResourceProduction[ResourceType.Food] = 10f;
                        building.WorkerSlots = 2;
                        break;
                    case BuildingType.Mine:
                        building.ResourceProduction[ResourceType.Iron] = 5f;
                        building.ResourceProduction[ResourceType.Stone] = 5f;
                        building.WorkerSlots = 3;
                        break;
                    case BuildingType.Power:
                        building.ResourceProduction[ResourceType.Power] = 20f;
                        break;
                }

                playerBase.Buildings.Add(building);

                // Send to server
                var message = new GameMessage
                {
                    Type = MessageType.BuildingPlace,
                    PlayerId = client.CurrentUsername,
                    Data = new Dictionary<string, object>
                {
                    { "baseId", baseId },
                    { "building", JsonSerializer.Serialize(building) }
                },
                    SessionId = client.AuthToken
                };

                client.SendMessageToServer(message);

                SaveData();
                BuildingPlaced?.Invoke(this, building);

                return building;
            }

            // Resource contribution
            public bool ContributeResources(string baseId, string buildingId, ResourceType resource, int amount)
            {
                if (!bases.TryGetValue(baseId, out var playerBase))
                    return false;

                var building = playerBase.Buildings.FirstOrDefault(b => b.Id == buildingId);
                if (building == null || building.Status != BuildingStatus.UnderConstruction)
                    return false;

                // Check if player has resources
                // In real implementation, this would check player inventory

                if (!building.CurrentResources.ContainsKey(resource))
                    building.CurrentResources[resource] = 0;

                building.CurrentResources[resource] += amount;

                // Update construction progress
                UpdateBuildingProgress(building);

                // Send to server
                var message = new GameMessage
                {
                    Type = "building_contribute",
                    PlayerId = client.CurrentUsername,
                    Data = new Dictionary<string, object>
                {
                    { "baseId", baseId },
                    { "buildingId", buildingId },
                    { "resource", resource.ToString() },
                    { "amount", amount }
                },
                    SessionId = client.AuthToken
                };

                client.SendMessageToServer(message);

                SaveData();

                return true;
            }

            // Assign worker to building
            public bool AssignWorker(string baseId, string buildingId, string workerId)
            {
                if (!bases.TryGetValue(baseId, out var playerBase))
                    return false;

                if (!HasPermission(baseId, client.CurrentUsername, p => p.CanManageWorkers))
                    return false;

                var building = playerBase.Buildings.FirstOrDefault(b => b.Id == buildingId);
                if (building == null || !building.IsOperational)
                    return false;

                if (building.CurrentWorkers.Count >= building.WorkerSlots)
                    return false;

                building.CurrentWorkers.Add(workerId);

                // Send to server
                var message = new GameMessage
                {
                    Type = "building_assign_worker",
                    PlayerId = client.CurrentUsername,
                    Data = new Dictionary<string, object>
                {
                    { "baseId", baseId },
                    { "buildingId", buildingId },
                    { "workerId", workerId }
                },
                    SessionId = client.AuthToken
                };

                client.SendMessageToServer(message);

                SaveData();

                return true;
            }

            // Base defense
            public void TriggerBaseAttack(string baseId, string attackingFaction, int attackStrength)
            {
                if (!bases.TryGetValue(baseId, out var playerBase))
                    return;

                var attack = new BaseAttack
                {
                    BaseId = baseId,
                    AttackingFaction = attackingFaction,
                    AttackerStrength = attackStrength,
                    DefenderStrength = CalculateDefenseStrength(playerBase)
                };

                playerBase.IsUnderAttack = true;
                playerBase.AttackingFaction = attackingFaction;

                // Add to history
                if (!baseAttackHistory.ContainsKey(baseId))
                    baseAttackHistory[baseId] = new List<BaseAttack>();

                baseAttackHistory[baseId].Add(attack);

                SaveData();
                BaseUnderAttack?.Invoke(this, attack);

                // Notify all base members
                notificationManager?.NotifyBaseUnderAttack(playerBase.Name, attackingFaction);
            }

            // Update methods
            private void UpdateConstruction()
            {
                foreach (var playerBase in bases.Values)
                {
                    foreach (var building in playerBase.Buildings.Where(b => b.Status == BuildingStatus.UnderConstruction))
                    {
                        if (building.AssignedWorkers.Count > 0)
                        {
                            // Progress construction
                            float progressPerSecond = 0.01f * building.AssignedWorkers.Count;
                            building.ConstructionProgress += progressPerSecond;

                            if (building.ConstructionProgress >= 1.0f)
                            {
                                CompleteConstruction(building);
                            }
                        }
                    }
                }
            }

            private void UpdateProduction()
            {
                foreach (var playerBase in bases.Values)
                {
                    // Reset totals
                    playerBase.TotalProduction.Clear();
                    playerBase.TotalConsumption.Clear();

                    foreach (var building in playerBase.Buildings.Where(b => b.IsOperational))
                    {
                        // Calculate production based on workers
                        float efficiency = building.CurrentWorkers.Count / (float)building.WorkerSlots;

                        foreach (var production in building.ResourceProduction)
                        {
                            float amount = production.Value * efficiency / 60f; // Per second

                            if (!playerBase.TotalProduction.ContainsKey(production.Key))
                                playerBase.TotalProduction[production.Key] = 0;

                            playerBase.TotalProduction[production.Key] += amount;

                            // Add to storage if there's capacity
                            if (playerBase.StoredResources.ContainsKey(production.Key))
                            {
                                float capacity = playerBase.StorageCapacity.GetValueOrDefault(production.Key, 0);
                                float current = playerBase.StoredResources[production.Key];

                                if (current < capacity)
                                {
                                    playerBase.StoredResources[production.Key] = Math.Min(capacity, current + amount);
                                }
                            }
                        }

                        // Handle consumption
                        foreach (var consumption in building.ResourceConsumption)
                        {
                            float amount = consumption.Value * efficiency / 60f;

                            if (!playerBase.TotalConsumption.ContainsKey(consumption.Key))
                                playerBase.TotalConsumption[consumption.Key] = 0;

                            playerBase.TotalConsumption[consumption.Key] += amount;

                            // Deduct from storage
                            if (playerBase.StoredResources.ContainsKey(consumption.Key))
                            {
                                playerBase.StoredResources[consumption.Key] =
                                    Math.Max(0, playerBase.StoredResources[consumption.Key] - amount);
                            }
                        }
                    }
                }
            }

            private void UpdateBuildingProgress(Building building)
            {
                // Calculate total progress
                float totalProgress = 0;
                int totalRequired = 0;

                foreach (var requirement in building.RequiredResources)
                {
                    int current = building.CurrentResources.GetValueOrDefault(requirement.Key, 0);
                    totalProgress += Math.Min(current, requirement.Value);
                    totalRequired += requirement.Value;
                }

                if (totalRequired > 0)
                {
                    building.ConstructionProgress = totalProgress / totalRequired;

                    if (building.ConstructionProgress >= 1.0f && building.Status == BuildingStatus.Blueprint)
                    {
                        building.Status = BuildingStatus.UnderConstruction;
                    }
                }
            }

            private void CompleteConstruction(Building building)
            {
                building.Status = BuildingStatus.Operational;
                building.IsOperational = true;
                building.CompletedAt = DateTime.UtcNow;
                building.ConstructionProgress = 1.0f;

                SaveData();
                BuildingCompleted?.Invoke(this, building);

                notificationManager?.CreateNotification(
                    NotificationType.Success,
                    "Building Completed!",
                    $"{building.Name} is now operational"
                );

                // Update base defense if it's a defense building
                if (building.Type == BuildingType.Defense)
                {
                    var playerBase = bases.Values.FirstOrDefault(b => b.Buildings.Contains(building));
                    if (playerBase != null)
                    {
                        playerBase.DefenseLevel = CalculateDefenseLevel(playerBase);
                    }
                }
            }

            // Helper methods
            private bool HasPermission(string baseId, string userId, Func<BasePermissions, bool> permissionCheck)
            {
                if (!bases.TryGetValue(baseId, out var playerBase))
                    return false;

                if (playerBase.OwnerId == userId)
                    return true;

                if (playerBase.Permissions.TryGetValue(userId, out var permissions))
                {
                    return permissionCheck(permissions);
                }

                return false;
            }

            private int CalculateDefenseStrength(PlayerBase playerBase)
            {
                int strength = playerBase.DefenseLevel * 10;

                // Add defense from buildings
                foreach (var building in playerBase.Buildings.Where(b => b.Type == BuildingType.Defense && b.IsOperational))
                {
                    strength += building.DefenseRating;
                }

                // Add bonus for online members
                strength += playerBase.Members.Count * 5;

                return strength;
            }

            private int CalculateDefenseLevel(PlayerBase playerBase)
            {
                return playerBase.Buildings.Count(b => b.Type == BuildingType.Defense && b.IsOperational);
            }

            // Getters
            public List<PlayerBase> GetMyBases()
            {
                return bases.Values.Where(b => b.OwnerId == client.CurrentUsername || b.Members.Contains(client.CurrentUsername)).ToList();
            }

            public PlayerBase GetBase(string baseId)
            {
                bases.TryGetValue(baseId, out var playerBase);
                return playerBase;
            }

            public List<Building> GetBaseBuildings(string baseId)
            {
                if (bases.TryGetValue(baseId, out var playerBase))
                {
                    return playerBase.Buildings;
                }
                return new List<Building>();
            }

            public Dictionary<ResourceType, float> GetBaseResources(string baseId)
            {
                if (bases.TryGetValue(baseId, out var playerBase))
                {
                    return new Dictionary<ResourceType, float>(playerBase.StoredResources);
                }
                return new Dictionary<ResourceType, float>();
            }

            public List<BaseAttack> GetBaseAttackHistory(string baseId)
            {
                return baseAttackHistory.GetValueOrDefault(baseId, new List<BaseAttack>());
            }

            // Message handlers
            private void OnMessageReceived(object sender, GameMessage message)
            {
                switch (message.Type)
                {
                    case "base_update":
                        HandleBaseUpdate(message);
                        break;
                    case "building_update":
                        HandleBuildingUpdate(message);
                        break;
                    case "base_attack":
                        HandleBaseAttack(message);
                        break;
                    case "base_attack_ended":
                        HandleBaseAttackEnded(message);
                        break;
                }
            }

            private void HandleBaseUpdate(GameMessage message)
            {
                if (message.Data.TryGetValue("base", out var baseObj))
                {
                    var playerBase = JsonSerializer.Deserialize<PlayerBase>(baseObj.ToString());
                    bases[playerBase.Id] = playerBase;
                    SaveData();
                }
            }

            private void HandleBuildingUpdate(GameMessage message)
            {
                if (message.Data.TryGetValue("baseId", out var baseIdObj) &&
                    message.Data.TryGetValue("building", out var buildingObj))
                {
                    string baseId = baseIdObj.ToString();
                    var building = JsonSerializer.Deserialize<Building>(buildingObj.ToString());

                    if (bases.TryGetValue(baseId, out var playerBase))
                    {
                        var existingBuilding = playerBase.Buildings.FirstOrDefault(b => b.Id == building.Id);
                        if (existingBuilding != null)
                        {
                            // Update existing building
                            int index = playerBase.Buildings.IndexOf(existingBuilding);
                            playerBase.Buildings[index] = building;
                        }
                        else
                        {
                            // Add new building
                            playerBase.Buildings.Add(building);
                        }

                        SaveData();
                    }
                }
            }

            private void HandleBaseAttack(GameMessage message)
            {
                if (message.Data.TryGetValue("attack", out var attackObj))
                {
                    var attack = JsonSerializer.Deserialize<BaseAttack>(attackObj.ToString());

                    if (bases.TryGetValue(attack.BaseId, out var playerBase))
                    {
                        playerBase.IsUnderAttack = true;
                        playerBase.AttackingFaction = attack.AttackingFaction;

                        if (!baseAttackHistory.ContainsKey(attack.BaseId))
                            baseAttackHistory[attack.BaseId] = new List<BaseAttack>();

                        baseAttackHistory[attack.BaseId].Add(attack);

                        SaveData();
                        BaseUnderAttack?.Invoke(this, attack);
                    }
                }
            }

            private void HandleBaseAttackEnded(GameMessage message)
            {
                if (message.Data.TryGetValue("attackId", out var attackIdObj) &&
                    message.Data.TryGetValue("result", out var resultObj))
                {
                    string attackId = attackIdObj.ToString();
                    string result = resultObj.ToString();

                    // Find the attack
                    foreach (var attacks in baseAttackHistory.Values)
                    {
                        var attack = attacks.FirstOrDefault(a => a.Id == attackId);
                        if (attack != null)
                        {
                            attack.IsActive = false;
                            attack.EndTime = DateTime.UtcNow;
                            attack.Result = result;

                            if (bases.TryGetValue(attack.BaseId, out var playerBase))
                            {
                                playerBase.IsUnderAttack = false;
                                playerBase.AttackingFaction = null;

                                // Handle casualties and damage
                                if (message.Data.TryGetValue("casualties", out var casualtiesObj))
                                {
                                    attack.Casualties = JsonSerializer.Deserialize<Dictionary<string, int>>(casualtiesObj.ToString());
                                }

                                if (message.Data.TryGetValue("destroyedBuildings", out var destroyedObj))
                                {
                                    var destroyedIds = JsonSerializer.Deserialize<List<string>>(destroyedObj.ToString());
                                    foreach (var buildingId in destroyedIds)
                                    {
                                        var building = playerBase.Buildings.FirstOrDefault(b => b.Id == buildingId);
                                        if (building != null)
                                        {
                                            building.Status = BuildingStatus.Destroyed;
                                            building.IsOperational = false;
                                            BuildingDestroyed?.Invoke(this, building);
                                        }
                                    }
                                }
                            }

                            SaveData();
                            BaseAttackEnded?.Invoke(this, attack);

                            // Notify about result
                            string resultMessage = result == "defended" ?
                                "Base successfully defended!" :
                                "Base was overrun by attackers!";

                            notificationManager?.CreateNotification(
                                result == "defended" ? NotificationType.Success : NotificationType.Error,
                                "Battle Result",
                                resultMessage,
                                priority: NotificationPriority.High
                            );

                            break;
                        }
                    }
                }
            }
        }

        public class BaseData
        {
            public List<PlayerBase> Bases { get; set; } = new List<PlayerBase>();
            public Dictionary<string, List<BaseAttack>> AttackHistory { get; set; } = new Dictionary<string, List<BaseAttack>>();
        }
    }