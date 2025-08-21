using System;
using System.Collections.Generic;
using System.Collections.Concurrent;
using System.Linq;
using System.Text.Json;
using System.IO;
using System.Threading.Tasks;
using KenshiMultiplayer.Common;

namespace KenshiMultiplayer
{
    /// <summary>
    /// Synchronizes base building across all clients with validation and conflict resolution
    /// </summary>
    public class BaseBuildingSync
    {
        // Building state management
        private readonly ConcurrentDictionary<string, BuildingState> worldBuildings = new ConcurrentDictionary<string, BuildingState>();
        private readonly ConcurrentDictionary<string, PlayerBase> playerBases = new ConcurrentDictionary<string, PlayerBase>();
        private readonly ConcurrentDictionary<string, BuildingTemplate> buildingTemplates = new ConcurrentDictionary<string, BuildingTemplate>();

        // Collision detection
        private readonly CollisionSystem collisionSystem = new CollisionSystem();
        private readonly NavMeshUpdater navMeshUpdater = new NavMeshUpdater();

        // Resource tracking
        private readonly ResourceManager resourceManager = new ResourceManager();

        // Interior management
        private readonly InteriorSpaceManager interiorManager = new InteriorSpaceManager();

        // Configuration
        private readonly BuildingConfig config;
        private readonly float maxBuildDistance = 100.0f; // 100m from player
        private readonly int maxBuildingsPerPlayer = 200;

        public BaseBuildingSync(BuildingConfig configuration = null)
        {
            config = configuration ?? new BuildingConfig();
            InitializeBuildingTemplates();
        }

        /// <summary>
        /// Initialize building templates from Kenshi data
        /// </summary>
        private void InitializeBuildingTemplates()
        {
            // Small buildings
            buildingTemplates["small_shack"] = new BuildingTemplate
            {
                Id = "small_shack",
                Name = "Small Shack",
                Category = "Housing",
                Size = new Vector3(4, 4, 3),
                Footprint = new Vector2(4, 4),
                MaxFloors = 1,
                RequiredResources = new Dictionary<string, int>
                {
                    { "building_materials", 5 },
                    { "iron_plates", 2 }
                },
                BuildTime = 30000, // 30 seconds
                PowerConsumption = 0,
                WorkerSlots = 0,
                Storage = 20
            };

            // Medium buildings
            buildingTemplates["house"] = new BuildingTemplate
            {
                Id = "house",
                Name = "House",
                Category = "Housing",
                Size = new Vector3(8, 8, 4),
                Footprint = new Vector2(8, 8),
                MaxFloors = 2,
                RequiredResources = new Dictionary<string, int>
                {
                    { "building_materials", 15 },
                    { "iron_plates", 5 },
                    { "fabric", 3 }
                },
                BuildTime = 60000,
                PowerConsumption = 0,
                WorkerSlots = 0,
                Storage = 50
            };

            // Production buildings
            buildingTemplates["stone_mine"] = new BuildingTemplate
            {
                Id = "stone_mine",
                Name = "Stone Mine",
                Category = "Production",
                Size = new Vector3(6, 6, 3),
                Footprint = new Vector2(6, 6),
                MaxFloors = 1,
                RequiredResources = new Dictionary<string, int>
                {
                    { "building_materials", 10 },
                    { "iron_plates", 8 }
                },
                BuildTime = 45000,
                PowerConsumption = 0,
                WorkerSlots = 2,
                ProductionRate = 0.5f,
                ProducedResource = "raw_stone"
            };

            buildingTemplates["iron_refinery"] = new BuildingTemplate
            {
                Id = "iron_refinery",
                Name = "Iron Refinery",
                Category = "Production",
                Size = new Vector3(8, 8, 5),
                Footprint = new Vector2(8, 8),
                MaxFloors = 1,
                RequiredResources = new Dictionary<string, int>
                {
                    { "building_materials", 20 },
                    { "iron_plates", 15 },
                    { "electrical_components", 5 }
                },
                BuildTime = 90000,
                PowerConsumption = 10,
                WorkerSlots = 3,
                ProductionRate = 0.3f,
                ProducedResource = "iron_plates"
            };

            // Farming
            buildingTemplates["small_farm"] = new BuildingTemplate
            {
                Id = "small_farm",
                Name = "Small Farm",
                Category = "Farming",
                Size = new Vector3(10, 10, 0.5f),
                Footprint = new Vector2(10, 10),
                MaxFloors = 1,
                RequiredResources = new Dictionary<string, int>
                {
                    { "building_materials", 3 },
                    { "water", 10 }
                },
                BuildTime = 20000,
                PowerConsumption = 0,
                WorkerSlots = 1,
                ProductionRate = 0.2f,
                ProducedResource = "wheatstraw"
            };

            // Defense
            buildingTemplates["wall"] = new BuildingTemplate
            {
                Id = "wall",
                Name = "Wall",
                Category = "Defense",
                Size = new Vector3(4, 0.5f, 4),
                Footprint = new Vector2(4, 0.5f),
                MaxFloors = 1,
                RequiredResources = new Dictionary<string, int>
                {
                    { "building_materials", 2 }
                },
                BuildTime = 10000,
                PowerConsumption = 0,
                WorkerSlots = 0,
                IsWall = true,
                Defense = 100
            };

            buildingTemplates["turret"] = new BuildingTemplate
            {
                Id = "turret",
                Name = "Harpoon Turret",
                Category = "Defense",
                Size = new Vector3(2, 2, 3),
                Footprint = new Vector2(2, 2),
                MaxFloors = 1,
                RequiredResources = new Dictionary<string, int>
                {
                    { "building_materials", 8 },
                    { "iron_plates", 10 },
                    { "harpoons", 20 }
                },
                BuildTime = 60000,
                PowerConsumption = 5,
                WorkerSlots = 1,
                IsTurret = true,
                TurretRange = 50,
                TurretDamage = 80
            };

            // Power
            buildingTemplates["wind_generator"] = new BuildingTemplate
            {
                Id = "wind_generator",
                Name = "Wind Generator",
                Category = "Power",
                Size = new Vector3(3, 3, 8),
                Footprint = new Vector2(3, 3),
                MaxFloors = 1,
                RequiredResources = new Dictionary<string, int>
                {
                    { "building_materials", 5 },
                    { "iron_plates", 8 },
                    { "electrical_components", 3 }
                },
                BuildTime = 40000,
                PowerGeneration = 20,
                WorkerSlots = 0
            };

            Logger.Log($"Initialized {buildingTemplates.Count} building templates");
        }

        /// <summary>
        /// Validate and process building placement request
        /// </summary>
        public BuildResult PlaceBuilding(string playerId, BuildingRequest request)
        {
            // Validate request
            var validation = ValidateBuildingRequest(playerId, request);
            if (!validation.IsValid)
            {
                return new BuildResult
                {
                    Success = false,
                    Reason = validation.Reason
                };
            }

            // Get template
            if (!buildingTemplates.TryGetValue(request.BuildingType, out var template))
            {
                return new BuildResult
                {
                    Success = false,
                    Reason = "Invalid building type"
                };
            }

            // Check resources
            if (!resourceManager.HasResources(playerId, template.RequiredResources))
            {
                return new BuildResult
                {
                    Success = false,
                    Reason = "Insufficient resources"
                };
            }

            // Check collision
            if (!CheckBuildingPlacement(request.Position, request.Rotation, template))
            {
                return new BuildResult
                {
                    Success = false,
                    Reason = "Invalid placement - collision detected"
                };
            }

            // Check terrain
            if (!CheckTerrainSuitability(request.Position, template))
            {
                return new BuildResult
                {
                    Success = false,
                    Reason = "Unsuitable terrain"
                };
            }

            // Consume resources
            resourceManager.ConsumeResources(playerId, template.RequiredResources);

            // Create building
            var building = CreateBuilding(playerId, request, template);

            // Add to world
            worldBuildings[building.Id] = building;

            // Add to player's base
            var playerBase = playerBases.GetOrAdd(playerId, new PlayerBase { OwnerId = playerId });
            playerBase.Buildings.Add(building.Id);

            // Update navmesh
            navMeshUpdater.UpdateForBuilding(building);

            // Create interior if applicable
            if (template.MaxFloors > 0 && !template.IsWall)
            {
                interiorManager.CreateInterior(building);
            }

            // Broadcast placement
            BroadcastBuildingPlacement(building);

            return new BuildResult
            {
                Success = true,
                BuildingId = building.Id,
                CompletionTime = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds() + template.BuildTime
            };
        }

        /// <summary>
        /// Validate building request
        /// </summary>
        private BuildingValidation ValidateBuildingRequest(string playerId, BuildingRequest request)
        {
            var validation = new BuildingValidation { IsValid = true };

            // Check player exists
            if (string.IsNullOrEmpty(playerId))
            {
                validation.IsValid = false;
                validation.Reason = "Invalid player ID";
                return validation;
            }

            // Check building limit
            var playerBase = playerBases.GetOrAdd(playerId, new PlayerBase { OwnerId = playerId });
            if (playerBase.Buildings.Count >= maxBuildingsPerPlayer)
            {
                validation.IsValid = false;
                validation.Reason = "Building limit reached";
                return validation;
            }

            // Check build distance from player
            // Would need actual player position
            // For now, simplified check

            // Check if in claimed territory
            if (!IsInClaimedTerritory(playerId, request.Position))
            {
                validation.IsValid = false;
                validation.Reason = "Not in claimed territory";
                return validation;
            }

            return validation;
        }

        /// <summary>
        /// Check if position is in player's claimed territory
        /// </summary>
        private bool IsInClaimedTerritory(string playerId, Vector3 position)
        {
            var playerBase = playerBases.GetOrAdd(playerId, new PlayerBase { OwnerId = playerId });

            // Check if near any of player's buildings
            foreach (var buildingId in playerBase.Buildings)
            {
                if (worldBuildings.TryGetValue(buildingId, out var building))
                {
                    var distance = Vector3.Distance(building.Position, position);
                    if (distance < config.TerritoryRadius)
                        return true;
                }
            }

            // First building can be placed anywhere
            return playerBase.Buildings.Count == 0;
        }

        /// <summary>
        /// Check building collision
        /// </summary>
        private bool CheckBuildingPlacement(Vector3 position, float rotation, BuildingTemplate template)
        {
            // Calculate building bounds
            var bounds = CalculateBuildingBounds(position, rotation, template);

            // Check against existing buildings
            foreach (var building in worldBuildings.Values)
            {
                if (building.State == BuildingState.Destroyed)
                    continue;

                var existingBounds = CalculateBuildingBounds(
                    building.Position,
                    building.Rotation,
                    buildingTemplates[building.TemplateId]
                );

                if (BoundsIntersect(bounds, existingBounds))
                {
                    // Allow walls to connect
                    if (template.IsWall && buildingTemplates[building.TemplateId].IsWall)
                    {
                        if (AreWallsConnecting(bounds, existingBounds))
                            continue;
                    }

                    return false;
                }
            }

            return true;
        }

        /// <summary>
        /// Check terrain suitability
        /// </summary>
        private bool CheckTerrainSuitability(Vector3 position, BuildingTemplate template)
        {
            // Check terrain height variation
            var heightVariation = GetTerrainHeightVariation(position, template.Footprint);

            if (heightVariation > config.MaxTerrainSlope)
                return false;

            // Check if on water
            if (IsOnWater(position))
                return false;

            // Check if on road (allow only certain buildings)
            if (IsOnRoad(position) && template.Category != "Defense")
                return false;

            return true;
        }

        /// <summary>
        /// Create building instance
        /// </summary>
        private BuildingState CreateBuilding(string playerId, BuildingRequest request, BuildingTemplate template)
        {
            return new BuildingState
            {
                Id = Guid.NewGuid().ToString(),
                OwnerId = playerId,
                TemplateId = template.Id,
                Position = request.Position,
                Rotation = request.Rotation,
                State = BuildingConstructionState.Foundation,
                ConstructionProgress = 0,
                Health = template.MaxHealth,
                MaxHealth = template.MaxHealth,
                PlacedAt = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds(),
                CompletionTime = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds() + template.BuildTime,
                CurrentFloor = 0,
                MaxFloors = template.MaxFloors,
                Workers = new List<string>(),
                Storage = new Dictionary<string, int>()
            };
        }

        /// <summary>
        /// Update building construction progress
        /// </summary>
        public void UpdateConstruction(string buildingId, float deltaTime)
        {
            if (!worldBuildings.TryGetValue(buildingId, out var building))
                return;

            if (building.State != BuildingConstructionState.Foundation &&
                building.State != BuildingConstructionState.Construction)
                return;

            var template = buildingTemplates[building.TemplateId];

            // Calculate construction speed based on workers
            float constructionSpeed = 1.0f + building.Workers.Count * 0.5f;

            // Update progress
            building.ConstructionProgress += (deltaTime / template.BuildTime) * constructionSpeed * 100;

            // Check state transitions
            if (building.State == BuildingConstructionState.Foundation && building.ConstructionProgress >= 25)
            {
                building.State = BuildingConstructionState.Construction;
            }
            else if (building.State == BuildingConstructionState.Construction && building.ConstructionProgress >= 100)
            {
                building.State = BuildingConstructionState.Completed;
                building.ConstructionProgress = 100;

                // Activate building
                ActivateBuilding(building);
            }
        }

        /// <summary>
        /// Activate completed building
        /// </summary>
        private void ActivateBuilding(BuildingState building)
        {
            var template = buildingTemplates[building.TemplateId];

            // Start production if applicable
            if (!string.IsNullOrEmpty(template.ProducedResource))
            {
                building.IsProducing = true;
                building.LastProductionTime = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();
            }

            // Start power generation
            if (template.PowerGeneration > 0)
            {
                UpdatePowerGrid(building.OwnerId);
            }

            Logger.Log($"Building {building.Id} completed and activated");
        }

        /// <summary>
        /// Process building damage
        /// </summary>
        public void DamageBuilding(string buildingId, int damage, string damageSource)
        {
            if (!worldBuildings.TryGetValue(buildingId, out var building))
                return;

            building.Health -= damage;

            if (building.Health <= 0)
            {
                DestroyBuilding(buildingId, damageSource);
            }
            else if (building.Health < building.MaxHealth * 0.3f)
            {
                building.State = BuildingConstructionState.Damaged;
            }

            // Broadcast damage
            BroadcastBuildingDamage(buildingId, damage, building.Health);
        }

        /// <summary>
        /// Destroy building
        /// </summary>
        private void DestroyBuilding(string buildingId, string reason)
        {
            if (!worldBuildings.TryGetValue(buildingId, out var building))
                return;

            building.State = BuildingConstructionState.Destroyed;
            building.Health = 0;

            // Remove from player's base
            if (playerBases.TryGetValue(building.OwnerId, out var playerBase))
            {
                playerBase.Buildings.Remove(buildingId);
            }

            // Update navmesh
            navMeshUpdater.RemoveBuilding(building);

            // Remove interior
            interiorManager.RemoveInterior(buildingId);

            // Drop stored items
            DropStoredItems(building);

            // Update power grid if needed
            var template = buildingTemplates[building.TemplateId];
            if (template.PowerGeneration > 0 || template.PowerConsumption > 0)
            {
                UpdatePowerGrid(building.OwnerId);
            }

            Logger.Log($"Building {buildingId} destroyed: {reason}");

            // Broadcast destruction
            BroadcastBuildingDestruction(buildingId, reason);
        }

        /// <summary>
        /// Update building production
        /// </summary>
        public void UpdateProduction(string buildingId, float deltaTime)
        {
            if (!worldBuildings.TryGetValue(buildingId, out var building))
                return;

            if (!building.IsProducing || building.State != BuildingConstructionState.Completed)
                return;

            var template = buildingTemplates[building.TemplateId];

            if (string.IsNullOrEmpty(template.ProducedResource))
                return;

            // Check power requirement
            if (template.PowerConsumption > 0)
            {
                var playerBase = playerBases.GetOrAdd(building.OwnerId, new PlayerBase { OwnerId = building.OwnerId });
                if (playerBase.PowerBalance < 0)
                    return; // Not enough power
            }

            // Check workers
            if (template.WorkerSlots > 0 && building.Workers.Count == 0)
                return; // No workers

            // Calculate production
            var timeSinceLastProduction = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds() - building.LastProductionTime;
            var productionInterval = 60000 / template.ProductionRate; // Items per minute

            if (timeSinceLastProduction >= productionInterval)
            {
                // Produce resource
                ProduceResource(building, template.ProducedResource, 1);
                building.LastProductionTime = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();
            }
        }

        /// <summary>
        /// Produce resource
        /// </summary>
        private void ProduceResource(BuildingState building, string resource, int amount)
        {
            if (!building.Storage.ContainsKey(resource))
                building.Storage[resource] = 0;

            building.Storage[resource] += amount;

            // Check storage limit
            var template = buildingTemplates[building.TemplateId];
            var totalStorage = building.Storage.Values.Sum();

            if (totalStorage > template.Storage)
            {
                // Storage full, stop production
                building.IsProducing = false;
            }

            Logger.Log($"Building {building.Id} produced {amount} {resource}");
        }

        /// <summary>
        /// Assign worker to building
        /// </summary>
        public bool AssignWorker(string buildingId, string workerId)
        {
            if (!worldBuildings.TryGetValue(buildingId, out var building))
                return false;

            var template = buildingTemplates[building.TemplateId];

            if (building.Workers.Count >= template.WorkerSlots)
                return false;

            if (building.Workers.Contains(workerId))
                return false;

            building.Workers.Add(workerId);

            // Start production if was stopped
            if (!building.IsProducing && !string.IsNullOrEmpty(template.ProducedResource))
            {
                building.IsProducing = true;
            }

            return true;
        }

        /// <summary>
        /// Update power grid for player
        /// </summary>
        private void UpdatePowerGrid(string playerId)
        {
            var playerBase = playerBases.GetOrAdd(playerId, new PlayerBase { OwnerId = playerId });

            int totalGeneration = 0;
            int totalConsumption = 0;

            foreach (var buildingId in playerBase.Buildings)
            {
                if (worldBuildings.TryGetValue(buildingId, out var building))
                {
                    if (building.State != BuildingConstructionState.Completed)
                        continue;

                    var template = buildingTemplates[building.TemplateId];

                    totalGeneration += template.PowerGeneration;

                    if (building.IsProducing)
                        totalConsumption += template.PowerConsumption;
                }
            }

            playerBase.PowerGeneration = totalGeneration;
            playerBase.PowerConsumption = totalConsumption;
            playerBase.PowerBalance = totalGeneration - totalConsumption;

            // Disable production for buildings without power
            if (playerBase.PowerBalance < 0)
            {
                DisableLowPriorityProduction(playerId);
            }
        }

        /// <summary>
        /// Disable low priority production when power is insufficient
        /// </summary>
        private void DisableLowPriorityProduction(string playerId)
        {
            var playerBase = playerBases[playerId];

            // Sort buildings by priority (defense > production > other)
            var buildingsByPriority = playerBase.Buildings
                .Select(id => worldBuildings.GetValueOrDefault(id))
                .Where(b => b != null && b.IsProducing)
                .OrderBy(b => GetBuildingPriority(b))
                .ToList();

            int powerDeficit = Math.Abs(playerBase.PowerBalance);

            foreach (var building in buildingsByPriority)
            {
                var template = buildingTemplates[building.TemplateId];

                if (template.PowerConsumption > 0)
                {
                    building.IsProducing = false;
                    powerDeficit -= template.PowerConsumption;

                    if (powerDeficit <= 0)
                        break;
                }
            }
        }

        private int GetBuildingPriority(BuildingState building)
        {
            var template = buildingTemplates[building.TemplateId];

            if (template.IsTurret) return 0; // Highest priority
            if (template.Category == "Production") return 1;
            if (template.Category == "Farming") return 2;
            return 3; // Lowest priority
        }

        /// <summary>
        /// Calculate building bounds
        /// </summary>
        private BuildingBounds CalculateBuildingBounds(Vector3 position, float rotation, BuildingTemplate template)
        {
            var bounds = new BuildingBounds
            {
                Center = position,
                Size = template.Size,
                Rotation = rotation
            };

            // Calculate corners
            float cos = (float)Math.Cos(rotation);
            float sin = (float)Math.Sin(rotation);

            float halfWidth = template.Size.X / 2;
            float halfDepth = template.Size.Y / 2;

            bounds.Corners = new Vector3[]
            {
                new Vector3(
                    position.X + halfWidth * cos - halfDepth * sin,
                    position.Y + halfWidth * sin + halfDepth * cos,
                    position.Z
                ),
                new Vector3(
                    position.X - halfWidth * cos - halfDepth * sin,
                    position.Y - halfWidth * sin + halfDepth * cos,
                    position.Z
                ),
                new Vector3(
                    position.X - halfWidth * cos + halfDepth * sin,
                    position.Y - halfWidth * sin - halfDepth * cos,
                    position.Z
                ),
                new Vector3(
                    position.X + halfWidth * cos + halfDepth * sin,
                    position.Y + halfWidth * sin - halfDepth * cos,
                    position.Z
                )
            };

            return bounds;
        }

        private bool BoundsIntersect(BuildingBounds a, BuildingBounds b)
        {
            // Simplified AABB check
            // Would use SAT (Separating Axis Theorem) for rotated bounds

            float aMinX = a.Corners.Min(c => c.X);
            float aMaxX = a.Corners.Max(c => c.X);
            float aMinY = a.Corners.Min(c => c.Y);
            float aMaxY = a.Corners.Max(c => c.Y);

            float bMinX = b.Corners.Min(c => c.X);
            float bMaxX = b.Corners.Max(c => c.X);
            float bMinY = b.Corners.Min(c => c.Y);
            float bMaxY = b.Corners.Max(c => c.Y);

            return !(aMaxX < bMinX || aMinX > bMaxX || aMaxY < bMinY || aMinY > bMaxY);
        }

        private bool AreWallsConnecting(BuildingBounds a, BuildingBounds b)
        {
            // Check if walls are adjacent and aligned
            // Simplified - would need proper wall connection logic
            var distance = Vector3.Distance(a.Center, b.Center);
            return distance < 5.0f; // Within connection range
        }

        private float GetTerrainHeightVariation(Vector3 position, Vector2 footprint)
        {
            // Would sample terrain heights
            return 0.1f; // Placeholder
        }

        private bool IsOnWater(Vector3 position)
        {
            // Would check water map
            return false; // Placeholder
        }

        private bool IsOnRoad(Vector3 position)
        {
            // Would check road map
            return false; // Placeholder
        }

        private void DropStoredItems(BuildingState building)
        {
            // Drop items at building location
            foreach (var item in building.Storage)
            {
                // Create item drop in world
                Logger.Log($"Dropped {item.Value} {item.Key} at {building.Position}");
            }
        }

        /// <summary>
        /// Broadcast building placement
        /// </summary>
        private void BroadcastBuildingPlacement(BuildingState building)
        {
            var message = new GameMessage
            {
                Type = "building_placed",
                Data = new Dictionary<string, object>
                {
                    { "building", JsonSerializer.Serialize(building) }
                }
            };

            // Send through network
        }

        private void BroadcastBuildingDamage(string buildingId, int damage, int remainingHealth)
        {
            var message = new GameMessage
            {
                Type = "building_damaged",
                Data = new Dictionary<string, object>
                {
                    { "buildingId", buildingId },
                    { "damage", damage },
                    { "health", remainingHealth }
                }
            };

            // Send through network
        }

        private void BroadcastBuildingDestruction(string buildingId, string reason)
        {
            var message = new GameMessage
            {
                Type = "building_destroyed",
                Data = new Dictionary<string, object>
                {
                    { "buildingId", buildingId },
                    { "reason", reason }
                }
            };

            // Send through network
        }

        /// <summary>
        /// Save building state to disk
        /// </summary>
        public void SaveBuildingState()
        {
            try
            {
                var saveData = new BuildingSaveData
                {
                    Buildings = worldBuildings.Values.ToList(),
                    PlayerBases = playerBases.Values.ToList()
                };

                string json = JsonSerializer.Serialize(saveData, new JsonSerializerOptions { WriteIndented = true });
                File.WriteAllText("buildings.json", json);

                Logger.Log($"Saved {worldBuildings.Count} buildings");
            }
            catch (Exception ex)
            {
                Logger.Log($"Error saving building state: {ex.Message}");
            }
        }

        /// <summary>
        /// Load building state from disk
        /// </summary>
        public void LoadBuildingState()
        {
            try
            {
                if (File.Exists("buildings.json"))
                {
                    string json = File.ReadAllText("buildings.json");
                    var saveData = JsonSerializer.Deserialize<BuildingSaveData>(json);

                    foreach (var building in saveData.Buildings)
                    {
                        worldBuildings[building.Id] = building;
                    }

                    foreach (var playerBase in saveData.PlayerBases)
                    {
                        playerBases[playerBase.OwnerId] = playerBase;
                    }

                    Logger.Log($"Loaded {worldBuildings.Count} buildings");
                }
            }
            catch (Exception ex)
            {
                Logger.Log($"Error loading building state: {ex.Message}");
            }
        }
    }

    // Supporting classes

    public class BuildingState
    {
        public string Id { get; set; }
        public string OwnerId { get; set; }
        public string TemplateId { get; set; }
        public Vector3 Position { get; set; }
        public float Rotation { get; set; }
        public BuildingConstructionState State { get; set; }
        public float ConstructionProgress { get; set; }
        public int Health { get; set; }
        public int MaxHealth { get; set; }
        public long PlacedAt { get; set; }
        public long CompletionTime { get; set; }
        public int CurrentFloor { get; set; }
        public int MaxFloors { get; set; }
        public List<string> Workers { get; set; }
        public Dictionary<string, int> Storage { get; set; }
        public bool IsProducing { get; set; }
        public long LastProductionTime { get; set; }
    }

    public enum BuildingConstructionState
    {
        Foundation,
        Construction,
        Completed,
        Damaged,
        Destroyed
    }

    public class BuildingTemplate
    {
        public string Id { get; set; }
        public string Name { get; set; }
        public string Category { get; set; }
        public Vector3 Size { get; set; }
        public Vector2 Footprint { get; set; }
        public int MaxFloors { get; set; }
        public Dictionary<string, int> RequiredResources { get; set; }
        public long BuildTime { get; set; }
        public int PowerConsumption { get; set; }
        public int PowerGeneration { get; set; }
        public int WorkerSlots { get; set; }
        public float ProductionRate { get; set; }
        public string ProducedResource { get; set; }
        public int Storage { get; set; }
        public int MaxHealth { get; set; } = 1000;
        public bool IsWall { get; set; }
        public bool IsTurret { get; set; }
        public float TurretRange { get; set; }
        public int TurretDamage { get; set; }
        public int Defense { get; set; }
    }

    public class PlayerBase
    {
        public string OwnerId { get; set; }
        public List<string> Buildings { get; set; } = new List<string>();
        public int PowerGeneration { get; set; }
        public int PowerConsumption { get; set; }
        public int PowerBalance { get; set; }
        public Dictionary<string, int> TotalResources { get; set; } = new Dictionary<string, int>();
    }

    public class BuildingRequest
    {
        public string BuildingType { get; set; }
        public Vector3 Position { get; set; }
        public float Rotation { get; set; }
    }

    public class BuildResult
    {
        public bool Success { get; set; }
        public string Reason { get; set; }
        public string BuildingId { get; set; }
        public long CompletionTime { get; set; }
    }

    public class BuildingValidation
    {
        public bool IsValid { get; set; }
        public string Reason { get; set; }
    }

    public class BuildingBounds
    {
        public Vector3 Center { get; set; }
        public Vector3 Size { get; set; }
        public float Rotation { get; set; }
        public Vector3[] Corners { get; set; }
    }

    public class BuildingSaveData
    {
        public List<BuildingState> Buildings { get; set; }
        public List<PlayerBase> PlayerBases { get; set; }
    }

    public class BuildingConfig
    {
        public float TerritoryRadius { get; set; } = 200.0f;
        public float MaxTerrainSlope { get; set; } = 0.3f;
    }

    public class CollisionSystem
    {
        // Collision detection implementation
    }

    public class NavMeshUpdater
    {
        public void UpdateForBuilding(BuildingState building)
        {
            // Update navmesh around building
        }

        public void RemoveBuilding(BuildingState building)
        {
            // Remove building from navmesh
        }
    }

    public class ResourceManager
    {
        private readonly ConcurrentDictionary<string, Dictionary<string, int>> playerResources =
            new ConcurrentDictionary<string, Dictionary<string, int>>();

        public bool HasResources(string playerId, Dictionary<string, int> required)
        {
            var resources = playerResources.GetOrAdd(playerId, new Dictionary<string, int>());

            foreach (var requirement in required)
            {
                if (!resources.ContainsKey(requirement.Key) || resources[requirement.Key] < requirement.Value)
                    return false;
            }

            return true;
        }

        public void ConsumeResources(string playerId, Dictionary<string, int> resources)
        {
            var playerRes = playerResources.GetOrAdd(playerId, new Dictionary<string, int>());

            foreach (var resource in resources)
            {
                playerRes[resource.Key] -= resource.Value;
            }
        }
    }

    public class InteriorSpaceManager
    {
        private readonly ConcurrentDictionary<string, InteriorSpace> interiors =
            new ConcurrentDictionary<string, InteriorSpace>();

        public void CreateInterior(BuildingState building)
        {
            var interior = new InteriorSpace
            {
                BuildingId = building.Id,
                Floors = new List<Floor>()
            };

            for (int i = 0; i < building.MaxFloors; i++)
            {
                interior.Floors.Add(new Floor
                {
                    Level = i,
                    Furniture = new List<Furniture>()
                });
            }

            interiors[building.Id] = interior;
        }

        public void RemoveInterior(string buildingId)
        {
            interiors.TryRemove(buildingId, out _);
        }
    }

    public class InteriorSpace
    {
        public string BuildingId { get; set; }
        public List<Floor> Floors { get; set; }
    }

    public class Floor
    {
        public int Level { get; set; }
        public List<Furniture> Furniture { get; set; }
    }

    public class Furniture
    {
        public string Type { get; set; }
        public Vector3 Position { get; set; }
        public float Rotation { get; set; }
    }

    public struct Vector2
    {
        public float X { get; set; }
        public float Y { get; set; }

        public Vector2(float x, float y)
        {
            X = x;
            Y = y;
        }
    }
}