using KenshiMultiplayer.Networking;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Text.Json;
using System.Threading.Tasks;

namespace KenshiMultiplayer.Common
{
    public enum WaypointType
    {
        Personal,
        Party,
        Public,
        Event,
        Base,
        Resource,
        Danger,
        Vendor,
        Quest,
        Custom
    }

    public enum MapMarkerIcon
    {
        Default,
        Flag,
        Cross,
        Circle,
        Triangle,
        Square,
        Star,
        Skull,
        Home,
        Sword,
        Shield,
        Pickaxe,
        Coin,
        Question,
        Exclamation
    }

    public class Waypoint
    {
        public string Id { get; set; } = Guid.NewGuid().ToString();
        public string Name { get; set; }
        public string Description { get; set; }
        public Position Position { get; set; }
        public WaypointType Type { get; set; }
        public MapMarkerIcon Icon { get; set; } = MapMarkerIcon.Default;
        public string Color { get; set; } = "#FFFFFF";
        public string CreatedBy { get; set; }
        public DateTime CreatedAt { get; set; } = DateTime.UtcNow;
        public DateTime? ExpiresAt { get; set; }
        public bool IsVisible { get; set; } = true;
        public List<string> SharedWith { get; set; } = new List<string>();
        public Dictionary<string, object> CustomData { get; set; } = new Dictionary<string, object>();
    }

    public class MapRegion
    {
        public string Id { get; set; }
        public string Name { get; set; }
        public float MinX { get; set; }
        public float MaxX { get; set; }
        public float MinY { get; set; }
        public float MaxY { get; set; }
        public string BiomeType { get; set; }
        public int DangerLevel { get; set; } // 1-10
        public List<string> ControllingFactions { get; set; } = new List<string>();
        public List<string> Resources { get; set; } = new List<string>();
        public Dictionary<string, object> Properties { get; set; } = new Dictionary<string, object>();
    }

    public class MapLayer
    {
        public string Id { get; set; } = Guid.NewGuid().ToString();
        public string Name { get; set; }
        public bool IsVisible { get; set; } = true;
        public float Opacity { get; set; } = 1.0f;
        public List<Waypoint> Waypoints { get; set; } = new List<Waypoint>();
        public List<MapPath> Paths { get; set; } = new List<MapPath>();
        public List<MapArea> Areas { get; set; } = new List<MapArea>();
    }

    public class MapPath
    {
        public string Id { get; set; } = Guid.NewGuid().ToString();
        public string Name { get; set; }
        public List<Position> Points { get; set; } = new List<Position>();
        public string Color { get; set; } = "#FFFFFF";
        public float Width { get; set; } = 2.0f;
        public bool IsDashed { get; set; } = false;
        public Dictionary<string, object> Properties { get; set; } = new Dictionary<string, object>();
    }

    public class MapArea
    {
        public string Id { get; set; } = Guid.NewGuid().ToString();
        public string Name { get; set; }
        public List<Position> Boundary { get; set; } = new List<Position>();
        public string FillColor { get; set; } = "#FFFFFF";
        public float FillOpacity { get; set; } = 0.3f;
        public string BorderColor { get; set; } = "#FFFFFF";
        public float BorderWidth { get; set; } = 2.0f;
        public Dictionary<string, object> Properties { get; set; } = new Dictionary<string, object>();
    }

    public class MapDiscovery
    {
        public string RegionId { get; set; }
        public string DiscoveredBy { get; set; }
        public DateTime DiscoveredAt { get; set; }
        public float ExplorationPercentage { get; set; }
        public List<string> DiscoveredLocations { get; set; } = new List<string>();
    }

    public class MapManager
    {
        private readonly Dictionary<string, Waypoint> waypoints = new Dictionary<string, Waypoint>();
        private readonly Dictionary<string, MapLayer> layers = new Dictionary<string, MapLayer>();
        private readonly Dictionary<string, MapRegion> regions = new Dictionary<string, MapRegion>();
        private readonly Dictionary<string, List<MapDiscovery>> discoveries = new Dictionary<string, List<MapDiscovery>>();

        // Kenshi-specific locations
        private readonly Dictionary<string, Waypoint> knownLocations = new Dictionary<string, Waypoint>();

        private readonly string dataFilePath;
        private readonly EnhancedClient client;
        private readonly NotificationManager notificationManager;

        // Events
        public event EventHandler<Waypoint> WaypointAdded;
        public event EventHandler<Waypoint> WaypointRemoved;
        public event EventHandler<Waypoint> WaypointUpdated;
        public event EventHandler<MapRegion> RegionDiscovered;
        public event EventHandler<MapLayer> LayerUpdated;

        // Map bounds (Kenshi world approximate size)
        private const float WorldMinX = -2000f;
        private const float WorldMaxX = 2000f;
        private const float WorldMinY = -2000f;
        private const float WorldMaxY = 2000f;

        public MapManager(EnhancedClient clientInstance, NotificationManager notificationManager, string dataDirectory = "data")
        {
            this.client = clientInstance;
            this.notificationManager = notificationManager;
            dataFilePath = Path.Combine(dataDirectory, "map_data.json");
            Directory.CreateDirectory(dataDirectory);

            LoadData();
            InitializeKenshiLocations();

            if (client != null)
            {
                client.MessageReceived += OnMessageReceived;
            }

            // Create default layers
            CreateDefaultLayers();
        }

        private void LoadData()
        {
            try
            {
                if (File.Exists(dataFilePath))
                {
                    string json = File.ReadAllText(dataFilePath);
                    var data = JsonSerializer.Deserialize<MapData>(json);

                    if (data != null)
                    {
                        foreach (var waypoint in data.Waypoints)
                        {
                            waypoints[waypoint.Id] = waypoint;
                        }

                        foreach (var layer in data.Layers)
                        {
                            layers[layer.Id] = layer;
                        }

                        foreach (var region in data.Regions)
                        {
                            regions[region.Id] = region;
                        }

                        discoveries = data.Discoveries ?? new Dictionary<string, List<MapDiscovery>>();
                    }

                    Logger.Log($"Loaded {waypoints.Count} waypoints, {regions.Count} regions");
                }
            }
            catch (Exception ex)
            {
                Logger.Log($"Error loading map data: {ex.Message}");
            }
        }

        private void SaveData()
        {
            try
            {
                var data = new MapData
                {
                    Waypoints = waypoints.Values.ToList(),
                    Layers = layers.Values.ToList(),
                    Regions = regions.Values.ToList(),
                    Discoveries = discoveries
                };

                string json = JsonSerializer.Serialize(data, new JsonSerializerOptions { WriteIndented = true });
                File.WriteAllText(dataFilePath, json);
            }
            catch (Exception ex)
            {
                Logger.Log($"Error saving map data: {ex.Message}");
            }
        }

        private void InitializeKenshiLocations()
        {
            // Major cities
            AddKnownLocation("The Hub", new Position(0, 0, 0), WaypointType.Base, "Neutral trading town");
            AddKnownLocation("Squin", new Position(-200, 150, 0), WaypointType.Base, "Shek capital");
            AddKnownLocation("Stack", new Position(300, -200, 0), WaypointType.Base, "Holy Nation city");
            AddKnownLocation("Heft", new Position(500, 300, 0), WaypointType.Base, "United Cities capital");
            AddKnownLocation("Heng", new Position(-400, -300, 0), WaypointType.Base, "UC trade city");
            AddKnownLocation("Black Desert City", new Position(800, -500, 0), WaypointType.Base, "Tech Hunter HQ");
            AddKnownLocation("World's End", new Position(-800, 600, 0), WaypointType.Base, "Tech Hunter outpost");

            // Dangerous areas
            AddKnownLocation("Venge", new Position(600, 0, 0), WaypointType.Danger, "Death lasers at night!");
            AddKnownLocation("Fog Islands", new Position(-600, -600, 0), WaypointType.Danger, "Fogmen territory");
            AddKnownLocation("Ashlands", new Position(1000, 1000, 0), WaypointType.Danger, "Skeleton territory");

            // Resource locations
            AddKnownLocation("Copper Mine (Hub)", new Position(50, 50, 0), WaypointType.Resource, "Copper ore");
            AddKnownLocation("Iron Mine (Squin)", new Position(-150, 180, 0), WaypointType.Resource, "Iron ore");
        }

        private void AddKnownLocation(string name, Position position, WaypointType type, string description)
        {
            var waypoint = new Waypoint
            {
                Name = name,
                Description = description,
                Position = position,
                Type = type,
                Icon = GetIconForType(type),
                Color = GetColorForType(type),
                CreatedBy = "System"
            };

            knownLocations[name] = waypoint;
        }

        private void CreateDefaultLayers()
        {
            // Personal layer
            if (!layers.ContainsKey("personal"))
            {
                layers["personal"] = new MapLayer
                {
                    Id = "personal",
                    Name = "Personal Waypoints",
                    IsVisible = true
                };
            }

            // Party layer
            if (!layers.ContainsKey("party"))
            {
                layers["party"] = new MapLayer
                {
                    Id = "party",
                    Name = "Party Waypoints",
                    IsVisible = true
                };
            }

            // Events layer
            if (!layers.ContainsKey("events"))
            {
                layers["events"] = new MapLayer
                {
                    Id = "events",
                    Name = "Active Events",
                    IsVisible = true
                };
            }
        }

        // Waypoint management
        public Waypoint CreateWaypoint(string name, Position position, WaypointType type = WaypointType.Personal, string description = "")
        {
            var waypoint = new Waypoint
            {
                Name = name,
                Description = description,
                Position = position,
                Type = type,
                Icon = GetIconForType(type),
                Color = GetColorForType(type),
                CreatedBy = client.CurrentUsername
            };

            // Add to appropriate layer
            string layerId = type == WaypointType.Personal ? "personal" :
                           type == WaypointType.Party ? "party" : "events";

            if (layers.TryGetValue(layerId, out var layer))
            {
                layer.Waypoints.Add(waypoint);
            }

            waypoints[waypoint.Id] = waypoint;

            // Send to server
            var message = new GameMessage
            {
                Type = "waypoint_create",
                PlayerId = client.CurrentUsername,
                Data = new Dictionary<string, object>
                {
                    { "waypoint", JsonSerializer.Serialize(waypoint) }
                },
                SessionId = client.AuthToken
            };

            client.SendMessageToServer(message);

            SaveData();
            WaypointAdded?.Invoke(this, waypoint);

            return waypoint;
        }

        public bool RemoveWaypoint(string waypointId)
        {
            if (!waypoints.TryGetValue(waypointId, out var waypoint))
                return false;

            // Only creator can remove
            if (waypoint.CreatedBy != client.CurrentUsername)
                return false;

            waypoints.Remove(waypointId);

            // Remove from layers
            foreach (var layer in layers.Values)
            {
                layer.Waypoints.RemoveAll(w => w.Id == waypointId);
            }

            // Send to server
            var message = new GameMessage
            {
                Type = "waypoint_remove",
                PlayerId = client.CurrentUsername,
                Data = new Dictionary<string, object>
                {
                    { "waypointId", waypointId }
                },
                SessionId = client.AuthToken
            };

            client.SendMessageToServer(message);

            SaveData();
            WaypointRemoved?.Invoke(this, waypoint);

            return true;
        }

        public bool ShareWaypoint(string waypointId, List<string> usernames)
        {
            if (!waypoints.TryGetValue(waypointId, out var waypoint))
                return false;

            waypoint.SharedWith.AddRange(usernames);

            // Send to server
            var message = new GameMessage
            {
                Type = "waypoint_share",
                PlayerId = client.CurrentUsername,
                Data = new Dictionary<string, object>
                {
                    { "waypointId", waypointId },
                    { "sharedWith", usernames }
                },
                SessionId = client.AuthToken
            };

            client.SendMessageToServer(message);

            SaveData();
            WaypointUpdated?.Invoke(this, waypoint);

            return true;
        }

        // Quick waypoint creation
        public Waypoint MarkCurrentLocation(string name = "")
        {
            if (string.IsNullOrEmpty(name))
                name = $"Waypoint {DateTime.Now:HH:mm}";

            // Get current position from game
            var currentPos = new Position(0, 0, 0); // This would come from the game

            return CreateWaypoint(name, currentPos, WaypointType.Personal);
        }

        public Waypoint MarkDangerousArea(Position position, string description, float radius = 50f)
        {
            var waypoint = CreateWaypoint($"Danger: {description}", position, WaypointType.Danger, description);

            // Create circular area around danger
            var area = new MapArea
            {
                Name = $"Danger Zone: {description}",
                FillColor = "#FF0000",
                FillOpacity = 0.3f,
                BorderColor = "#FF0000"
            };

            // Generate circle boundary
            for (int i = 0; i < 32; i++)
            {
                float angle = (float)(i * Math.PI * 2 / 32);
                float x = position.X + radius * (float)Math.Cos(angle);
                float y = position.Y + radius * (float)Math.Sin(angle);
                area.Boundary.Add(new Position(x, y, position.Z));
            }

            if (layers.TryGetValue("personal", out var layer))
            {
                layer.Areas.Add(area);
            }

            SaveData();
            return waypoint;
        }

        // Path recording
        private List<Position> recordingPath = null;
        private string recordingPathName = "";

        public void StartRecordingPath(string name)
        {
            recordingPath = new List<Position>();
            recordingPathName = name;
        }

        public void AddPathPoint(Position position)
        {
            if (recordingPath != null)
            {
                recordingPath.Add(position.Clone());
            }
        }

        public MapPath StopRecordingPath()
        {
            if (recordingPath == null || recordingPath.Count < 2)
                return null;

            var path = new MapPath
            {
                Name = recordingPathName,
                Points = recordingPath,
                Color = "#00FF00",
                Width = 3.0f
            };

            if (layers.TryGetValue("personal", out var layer))
            {
                layer.Paths.Add(path);
            }

            recordingPath = null;
            recordingPathName = "";

            SaveData();
            return path;
        }

        // Region discovery
        public void DiscoverRegion(string regionId, float explorationPercentage = 0.1f)
        {
            if (!regions.TryGetValue(regionId, out var region))
                return;

            if (!discoveries.TryGetValue(client.CurrentUsername, out var userDiscoveries))
            {
                userDiscoveries = new List<MapDiscovery>();
                discoveries[client.CurrentUsername] = userDiscoveries;
            }

            var discovery = userDiscoveries.FirstOrDefault(d => d.RegionId == regionId);
            if (discovery == null)
            {
                discovery = new MapDiscovery
                {
                    RegionId = regionId,
                    DiscoveredBy = client.CurrentUsername,
                    DiscoveredAt = DateTime.UtcNow,
                    ExplorationPercentage = explorationPercentage
                };
                userDiscoveries.Add(discovery);

                // First discovery notification
                notificationManager?.NotifyAchievement(
                    $"Region Discovered: {region.Name}",
                    $"You are the first to discover {region.Name}!"
                );

                RegionDiscovered?.Invoke(this, region);
            }
            else
            {
                // Update exploration percentage
                discovery.ExplorationPercentage = Math.Min(1.0f, discovery.ExplorationPercentage + explorationPercentage);

                if (discovery.ExplorationPercentage >= 1.0f)
                {
                    notificationManager?.NotifyAchievement(
                        $"Region Fully Explored: {region.Name}",
                        "You have fully explored this region!"
                    );
                }
            }

            SaveData();
        }

        // Getters
        public List<Waypoint> GetVisibleWaypoints()
        {
            var visibleWaypoints = new List<Waypoint>();

            // Add known locations
            visibleWaypoints.AddRange(knownLocations.Values);

            // Add waypoints from visible layers
            foreach (var layer in layers.Values.Where(l => l.IsVisible))
            {
                visibleWaypoints.AddRange(layer.Waypoints.Where(w => w.IsVisible));
            }

            // Add shared waypoints
            visibleWaypoints.AddRange(waypoints.Values.Where(w =>
                w.SharedWith.Contains(client.CurrentUsername) ||
                w.CreatedBy == client.CurrentUsername));

            return visibleWaypoints.Distinct().ToList();
        }

        public List<MapRegion> GetDiscoveredRegions()
        {
            if (!discoveries.TryGetValue(client.CurrentUsername, out var userDiscoveries))
                return new List<MapRegion>();

            return userDiscoveries
                .Select(d => regions.GetValueOrDefault(d.RegionId))
                .Where(r => r != null)
                .ToList();
        }

        public float GetExplorationPercentage()
        {
            int totalRegions = regions.Count;
            if (totalRegions == 0) return 0;

            if (!discoveries.TryGetValue(client.CurrentUsername, out var userDiscoveries))
                return 0;

            float totalExploration = userDiscoveries.Sum(d => d.ExplorationPercentage);
            return (totalExploration / totalRegions) * 100f;
        }

        // Helper methods
        private MapMarkerIcon GetIconForType(WaypointType type)
        {
            return type switch
            {
                WaypointType.Base => MapMarkerIcon.Home,
                WaypointType.Resource => MapMarkerIcon.Pickaxe,
                WaypointType.Danger => MapMarkerIcon.Skull,
                WaypointType.Vendor => MapMarkerIcon.Coin,
                WaypointType.Quest => MapMarkerIcon.Question,
                WaypointType.Event => MapMarkerIcon.Exclamation,
                _ => MapMarkerIcon.Default
            };
        }

        private string GetColorForType(WaypointType type)
        {
            return type switch
            {
                WaypointType.Base => "#4169E1",      // Royal Blue
                WaypointType.Resource => "#228B22",   // Forest Green
                WaypointType.Danger => "#DC143C",     // Crimson
                WaypointType.Vendor => "#FFD700",     // Gold
                WaypointType.Quest => "#FF8C00",      // Dark Orange
                WaypointType.Event => "#FF1493",      // Deep Pink
                WaypointType.Party => "#00CED1",      // Dark Turquoise
                _ => "#FFFFFF"                        // White
            };
        }

        // Message handlers
        private void OnMessageReceived(object sender, GameMessage message)
        {
            switch (message.Type)
            {
                case "waypoint_shared":
                    HandleWaypointShared(message);
                    break;
                case "waypoint_update":
                    HandleWaypointUpdate(message);
                    break;
                case "region_discovered":
                    HandleRegionDiscovered(message);
                    break;
            }
        }

        private void HandleWaypointShared(GameMessage message)
        {
            if (message.Data.TryGetValue("waypoint", out var waypointObj))
            {
                var waypoint = JsonSerializer.Deserialize<Waypoint>(waypointObj.ToString());

                if (waypoint.SharedWith.Contains(client.CurrentUsername))
                {
                    waypoints[waypoint.Id] = waypoint;

                    notificationManager?.CreateNotification(
                        NotificationType.Info,
                        "Waypoint Shared",
                        $"{waypoint.CreatedBy} shared waypoint: {waypoint.Name}",
                        new List<NotificationAction>
                        {
                            new NotificationAction
                            {
                                Id = "view",
                                Label = "View on Map",
                                ActionType = "view_waypoint",
                                ActionData = new Dictionary<string, object> { { "waypointId", waypoint.Id } }
                            }
                        }
                    );

                    WaypointAdded?.Invoke(this, waypoint);
                }
            }
        }

        private void HandleWaypointUpdate(GameMessage message)
        {
            if (message.Data.TryGetValue("waypoint", out var waypointObj))
            {
                var waypoint = JsonSerializer.Deserialize<Waypoint>(waypointObj.ToString());
                waypoints[waypoint.Id] = waypoint;

                WaypointUpdated?.Invoke(this, waypoint);
            }
        }

        private void HandleRegionDiscovered(GameMessage message)
        {
            if (message.Data.TryGetValue("regionId", out var regionIdObj) &&
                message.Data.TryGetValue("discoveredBy", out var discoveredByObj))
            {
                string regionId = regionIdObj.ToString();
                string discoveredBy = discoveredByObj.ToString();

                if (regions.TryGetValue(regionId, out var region))
                {
                    notificationManager?.CreateNotification(
                        NotificationType.Info,
                        "New Region Discovered",
                        $"{discoveredBy} discovered {region.Name}!",
                        priority: NotificationPriority.Low
                    );
                }
            }
        }
    }

    public class MapData
    {
        public List<Waypoint> Waypoints { get; set; } = new List<Waypoint>();
        public List<MapLayer> Layers { get; set; } = new List<MapLayer>();
        public List<MapRegion> Regions { get; set; } = new List<MapRegion>();
        public Dictionary<string, List<MapDiscovery>> Discoveries { get; set; } = new Dictionary<string, List<MapDiscovery>>();
    }
}