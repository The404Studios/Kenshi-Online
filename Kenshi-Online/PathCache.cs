using System;
using System.Collections.Generic;
using System.Collections.Concurrent;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Threading.Tasks;
using KenshiMultiplayer.Common;
using KenshiMultiplayer.Auth;
using KenshiMultiplayer.Networking;
using KenshiMultiplayer.Utility;

namespace KenshiMultiplayer
{
    /// <summary>
    /// Deterministic path caching system to replace Kenshi's non-deterministic Havok AI pathfinding
    /// </summary>
    public class PathCache
    {
        // Path storage structures
        private readonly ConcurrentDictionary<ulong, CachedPath> pathDatabase = new ConcurrentDictionary<ulong, CachedPath>();
        private readonly ConcurrentDictionary<int, PathSector> sectorCache = new ConcurrentDictionary<int, PathSector>();
        private readonly ConcurrentDictionary<string, NavMeshNode> navMeshNodes = new ConcurrentDictionary<string, NavMeshNode>();

        // Frequently used paths for optimization
        private readonly LRUCache<ulong, CachedPath> hotPathCache = new LRUCache<ulong, CachedPath>(1000);

        private readonly string cacheDirectory;
        private readonly object syncLock = new object();

        // Kenshi world constants (from research)
        private const int WORLD_SIZE = 870000; // Kenshi world units
        private const int SECTOR_SIZE = 10000;  // Divide world into sectors
        private const int MAX_PATH_POINTS = 256; // Max waypoints per path

        public PathCache(string dataDirectory = "pathcache")
        {
            cacheDirectory = dataDirectory;
            Directory.CreateDirectory(cacheDirectory);

            LoadBakedPaths();
            InitializeSectors();
        }

        /// <summary>
        /// Get a deterministic path between two points
        /// </summary>
        public CachedPath GetPath(Vector3 start, Vector3 end, bool allowGeneration = true)
        {
            // Generate path hash
            ulong pathHash = GeneratePathHash(start, end);

            // Check hot cache first
            if (hotPathCache.TryGetValue(pathHash, out var hotPath))
            {
                Logger.Log($"Path found in hot cache: {pathHash}");
                return hotPath;
            }

            // Check main database
            if (pathDatabase.TryGetValue(pathHash, out var cachedPath))
            {
                hotPathCache.Add(pathHash, cachedPath);
                return cachedPath;
            }

            // Try to find approximate path
            var approximatePath = FindApproximatePath(start, end);
            if (approximatePath != null)
            {
                return approximatePath;
            }

            // Generate new path if allowed
            if (allowGeneration)
            {
                return GenerateAndCachePath(start, end);
            }

            // Return simple direct path as fallback
            return CreateDirectPath(start, end);
        }

        /// <summary>
        /// Pre-bake common paths based on Kenshi's major locations
        /// </summary>
        public async Task PreBakeCommonPaths()
        {
            // Major cities and locations in Kenshi
            var majorLocations = new List<(string name, Vector3 pos)>
            {
                ("The Hub", new Vector3(-4680, 56700, 0)),
                ("Squin", new Vector3(24300, 45900, 0)),
                ("Stack", new Vector3(-52200, 37800, 0)),
                ("Admag", new Vector3(43200, 21600, 0)),
                ("Shark", new Vector3(-81000, -27000, 0)),
                ("Catun", new Vector3(8100, -48600, 0)),
                ("Black Scratch", new Vector3(-21600, -75600, 0)),
                ("Mongrel", new Vector3(-64800, 91800, 0)),
                ("World's End", new Vector3(97200, 118800, 0)),
                ("Blister Hill", new Vector3(-32400, 21600, 0)),
                ("Bad Teeth", new Vector3(70200, -16200, 0)),
                ("Bark", new Vector3(16200, 75600, 0)),
                ("Sho-Battai", new Vector3(-48600, -16200, 0)),
                ("Stoat", new Vector3(-27000, 102600, 0)),
                ("Spring", new Vector3(102600, 48600, 0))
            };

            int totalPaths = 0;

            // Generate paths between all major locations
            for (int i = 0; i < majorLocations.Count; i++)
            {
                for (int j = i + 1; j < majorLocations.Count; j++)
                {
                    var start = majorLocations[i];
                    var end = majorLocations[j];

                    Logger.Log($"Pre-baking path: {start.name} -> {end.name}");

                    // Generate bidirectional paths
                    await Task.Run(() =>
                    {
                        GenerateAndCachePath(start.pos, end.pos, $"{start.name}_to_{end.name}");
                        GenerateAndCachePath(end.pos, start.pos, $"{end.name}_to_{start.name}");
                    });

                    totalPaths += 2;
                }
            }

            Logger.Log($"Pre-baked {totalPaths} common paths");
            SaveBakedPaths();
        }

        /// <summary>
        /// Generate and cache a new path
        /// </summary>
        private CachedPath GenerateAndCachePath(Vector3 start, Vector3 end, string name = null)
        {
            var waypoints = CalculatePath(start, end);

            var cachedPath = new CachedPath
            {
                PathId = GeneratePathHash(start, end),
                Name = name ?? $"Path_{start}_{end}",
                Start = start,
                End = end,
                Waypoints = waypoints,
                Distance = CalculatePathDistance(waypoints),
                CreatedAt = DateTime.UtcNow,
                UseCount = 0
            };

            // Store in database
            pathDatabase[cachedPath.PathId] = cachedPath;

            // Broadcast to other clients
            BroadcastNewPath(cachedPath);

            return cachedPath;
        }

        /// <summary>
        /// Calculate path using A* algorithm (deterministic replacement for Havok)
        /// </summary>
        private List<Vector3> CalculatePath(Vector3 start, Vector3 end)
        {
            var path = new List<Vector3>();

            // Get sectors for start and end
            int startSector = GetSectorId(start);
            int endSector = GetSectorId(end);

            if (startSector == endSector)
            {
                // Simple path within same sector
                return CalculateLocalPath(start, end);
            }

            // Complex path across sectors
            var sectorPath = FindSectorPath(startSector, endSector);

            if (sectorPath.Count == 0)
            {
                // No sector path found, use direct path
                return CreateDirectPathPoints(start, end);
            }

            // Build path through sectors
            Vector3 currentPos = start;

            foreach (int sectorId in sectorPath)
            {
                if (sectorCache.TryGetValue(sectorId, out var sector))
                {
                    var sectorExit = sector.GetNearestExit(currentPos, end);
                    var localPath = CalculateLocalPath(currentPos, sectorExit);
                    path.AddRange(localPath);
                    currentPos = sectorExit;
                }
            }

            // Add final segment
            path.AddRange(CalculateLocalPath(currentPos, end));

            // Optimize path
            return OptimizePath(path);
        }

        /// <summary>
        /// Calculate local path within a sector using simplified A*
        /// </summary>
        private List<Vector3> CalculateLocalPath(Vector3 start, Vector3 end)
        {
            var openSet = new SortedSet<PathNode>(new PathNodeComparer());
            var closedSet = new HashSet<Vector3>();
            var cameFrom = new Dictionary<Vector3, Vector3>();

            var startNode = new PathNode
            {
                Position = start,
                G = 0,
                H = Vector3.Distance(start, end),
                F = Vector3.Distance(start, end)
            };

            openSet.Add(startNode);

            while (openSet.Count > 0)
            {
                var current = openSet.Min;
                openSet.Remove(current);

                if (Vector3.Distance(current.Position, end) < 100) // Close enough
                {
                    // Reconstruct path
                    return ReconstructPath(cameFrom, current.Position, start);
                }

                closedSet.Add(current.Position);

                // Get neighbors (simplified grid)
                var neighbors = GetNeighbors(current.Position);

                foreach (var neighbor in neighbors)
                {
                    if (closedSet.Contains(neighbor))
                        continue;

                    float tentativeG = current.G + Vector3.Distance(current.Position, neighbor);

                    var neighborNode = new PathNode
                    {
                        Position = neighbor,
                        G = tentativeG,
                        H = Vector3.Distance(neighbor, end),
                        F = tentativeG + Vector3.Distance(neighbor, end)
                    };

                    var existing = openSet.FirstOrDefault(n => n.Position.Equals(neighbor));

                    if (existing != null && tentativeG >= existing.G)
                        continue;

                    cameFrom[neighbor] = current.Position;

                    if (existing != null)
                        openSet.Remove(existing);

                    openSet.Add(neighborNode);
                }
            }

            // No path found, return direct path
            return CreateDirectPathPoints(start, end);
        }

        /// <summary>
        /// Get neighboring positions for pathfinding
        /// </summary>
        private List<Vector3> GetNeighbors(Vector3 position)
        {
            var neighbors = new List<Vector3>();
            float stepSize = 500; // Kenshi units

            // 8-directional movement
            for (int dx = -1; dx <= 1; dx++)
            {
                for (int dy = -1; dy <= 1; dy++)
                {
                    if (dx == 0 && dy == 0)
                        continue;

                    var neighbor = new Vector3(
                        position.X + dx * stepSize,
                        position.Y + dy * stepSize,
                        position.Z // Maintain Z for now
                    );

                    // Check if position is valid (would need terrain data)
                    if (IsValidPosition(neighbor))
                    {
                        neighbors.Add(neighbor);
                    }
                }
            }

            return neighbors;
        }

        /// <summary>
        /// Check if position is walkable (simplified - would need actual terrain data)
        /// </summary>
        private bool IsValidPosition(Vector3 position)
        {
            // Bounds check
            if (Math.Abs(position.X) > WORLD_SIZE / 2 || Math.Abs(position.Y) > WORLD_SIZE / 2)
                return false;

            // In real implementation, check:
            // - Terrain height
            // - Water bodies
            // - Impassable terrain
            // - Buildings

            return true;
        }

        /// <summary>
        /// Find approximate path using nearby cached paths
        /// </summary>
        private CachedPath FindApproximatePath(Vector3 start, Vector3 end)
        {
            float searchRadius = 1000; // Kenshi units
            CachedPath bestPath = null;
            float bestScore = float.MaxValue;

            foreach (var path in pathDatabase.Values)
            {
                float startDist = Vector3.Distance(start, path.Start);
                float endDist = Vector3.Distance(end, path.End);

                if (startDist < searchRadius && endDist < searchRadius)
                {
                    float score = startDist + endDist;
                    if (score < bestScore)
                    {
                        bestScore = score;
                        bestPath = path;
                    }
                }
            }

            if (bestPath != null)
            {
                // Create modified path
                var modifiedPath = new CachedPath
                {
                    PathId = GeneratePathHash(start, end),
                    Name = $"Approximate_{bestPath.Name}",
                    Start = start,
                    End = end,
                    Waypoints = new List<Vector3>()
                };

                // Add connection from start to path
                modifiedPath.Waypoints.Add(start);
                modifiedPath.Waypoints.AddRange(bestPath.Waypoints);
                modifiedPath.Waypoints.Add(end);

                return modifiedPath;
            }

            return null;
        }

        /// <summary>
        /// Initialize world sectors for hierarchical pathfinding
        /// </summary>
        private void InitializeSectors()
        {
            int sectorsPerAxis = WORLD_SIZE / SECTOR_SIZE;

            for (int x = 0; x < sectorsPerAxis; x++)
            {
                for (int y = 0; y < sectorsPerAxis; y++)
                {
                    int sectorId = y * sectorsPerAxis + x;

                    var sector = new PathSector
                    {
                        Id = sectorId,
                        X = x,
                        Y = y,
                        CenterX = (x - sectorsPerAxis / 2) * SECTOR_SIZE,
                        CenterY = (y - sectorsPerAxis / 2) * SECTOR_SIZE
                    };

                    // Calculate connections to adjacent sectors
                    if (x > 0) sector.Connections.Add(sectorId - 1);
                    if (x < sectorsPerAxis - 1) sector.Connections.Add(sectorId + 1);
                    if (y > 0) sector.Connections.Add(sectorId - sectorsPerAxis);
                    if (y < sectorsPerAxis - 1) sector.Connections.Add(sectorId + sectorsPerAxis);

                    sectorCache[sectorId] = sector;
                }
            }

            Logger.Log($"Initialized {sectorCache.Count} path sectors");
        }

        /// <summary>
        /// Find path through sectors
        /// </summary>
        private List<int> FindSectorPath(int startSector, int endSector)
        {
            var path = new List<int>();
            var openSet = new Queue<int>();
            var visited = new HashSet<int>();
            var parent = new Dictionary<int, int>();

            openSet.Enqueue(startSector);
            visited.Add(startSector);

            while (openSet.Count > 0)
            {
                int current = openSet.Dequeue();

                if (current == endSector)
                {
                    // Reconstruct path
                    int node = endSector;
                    while (parent.ContainsKey(node))
                    {
                        path.Insert(0, node);
                        node = parent[node];
                    }
                    return path;
                }

                if (sectorCache.TryGetValue(current, out var sector))
                {
                    foreach (int connection in sector.Connections)
                    {
                        if (!visited.Contains(connection))
                        {
                            visited.Add(connection);
                            parent[connection] = current;
                            openSet.Enqueue(connection);
                        }
                    }
                }
            }

            return path; // Empty if no path found
        }

        /// <summary>
        /// Optimize path by removing unnecessary waypoints
        /// </summary>
        private List<Vector3> OptimizePath(List<Vector3> path)
        {
            if (path.Count <= 2)
                return path;

            var optimized = new List<Vector3> { path[0] };

            for (int i = 1; i < path.Count - 1; i++)
            {
                Vector3 prev = optimized[optimized.Count - 1];
                Vector3 curr = path[i];
                Vector3 next = path[i + 1];

                // Check if we can skip current point
                if (!IsDirectPathClear(prev, next))
                {
                    optimized.Add(curr);
                }
            }

            optimized.Add(path[path.Count - 1]);

            return optimized;
        }

        /// <summary>
        /// Check if direct path between two points is clear
        /// </summary>
        private bool IsDirectPathClear(Vector3 start, Vector3 end)
        {
            // Simplified check - in reality would need terrain collision
            float distance = Vector3.Distance(start, end);

            // If too far, probably has obstacles
            if (distance > 5000)
                return false;

            // Ray cast would go here
            return true;
        }

        /// <summary>
        /// Create simple direct path
        /// </summary>
        private CachedPath CreateDirectPath(Vector3 start, Vector3 end)
        {
            return new CachedPath
            {
                PathId = GeneratePathHash(start, end),
                Name = "Direct_Path",
                Start = start,
                End = end,
                Waypoints = CreateDirectPathPoints(start, end),
                Distance = Vector3.Distance(start, end),
                CreatedAt = DateTime.UtcNow,
                UseCount = 0
            };
        }

        private List<Vector3> CreateDirectPathPoints(Vector3 start, Vector3 end)
        {
            var waypoints = new List<Vector3> { start };

            float distance = Vector3.Distance(start, end);
            int segments = Math.Min((int)(distance / 1000) + 1, 10);

            for (int i = 1; i < segments; i++)
            {
                float t = (float)i / segments;
                waypoints.Add(Vector3.Lerp(start, end, t));
            }

            waypoints.Add(end);
            return waypoints;
        }

        private List<Vector3> ReconstructPath(Dictionary<Vector3, Vector3> cameFrom, Vector3 current, Vector3 start)
        {
            var path = new List<Vector3> { current };

            while (cameFrom.ContainsKey(current) && !current.Equals(start))
            {
                current = cameFrom[current];
                path.Insert(0, current);
            }

            return path;
        }

        private ulong GeneratePathHash(Vector3 start, Vector3 end)
        {
            unchecked
            {
                ulong hash = 17;
                hash = hash * 31 + (ulong)start.GetHashCode();
                hash = hash * 31 + (ulong)end.GetHashCode();
                return hash;
            }
        }

        private int GetSectorId(Vector3 position)
        {
            int sectorsPerAxis = WORLD_SIZE / SECTOR_SIZE;
            int x = (int)((position.X + WORLD_SIZE / 2) / SECTOR_SIZE);
            int y = (int)((position.Y + WORLD_SIZE / 2) / SECTOR_SIZE);

            x = Math.Max(0, Math.Min(sectorsPerAxis - 1, x));
            y = Math.Max(0, Math.Min(sectorsPerAxis - 1, y));

            return y * sectorsPerAxis + x;
        }

        private float CalculatePathDistance(List<Vector3> waypoints)
        {
            float distance = 0;
            for (int i = 1; i < waypoints.Count; i++)
            {
                distance += Vector3.Distance(waypoints[i - 1], waypoints[i]);
            }
            return distance;
        }

        /// <summary>
        /// Save baked paths to disk
        /// </summary>
        public void SaveBakedPaths()
        {
            try
            {
                var data = new PathCacheData
                {
                    Paths = pathDatabase.Values.ToList(),
                    Sectors = sectorCache.Values.ToList()
                };

                string json = JsonSerializer.Serialize(data, new JsonSerializerOptions { WriteIndented = true });
                string filePath = Path.Combine(cacheDirectory, "paths.json");
                File.WriteAllText(filePath, json);

                // Save binary version for faster loading
                SaveBinaryCache();

                Logger.Log($"Saved {pathDatabase.Count} paths to cache");
            }
            catch (Exception ex)
            {
                Logger.Log($"Error saving path cache: {ex.Message}");
            }
        }

        /// <summary>
        /// Load baked paths from disk
        /// </summary>
        private void LoadBakedPaths()
        {
            try
            {
                // Try binary first for speed
                if (LoadBinaryCache())
                    return;

                string filePath = Path.Combine(cacheDirectory, "paths.json");
                if (File.Exists(filePath))
                {
                    string json = File.ReadAllText(filePath);
                    var data = JsonSerializer.Deserialize<PathCacheData>(json);

                    if (data != null && data.Paths != null)
                    {
                        foreach (var path in data.Paths)
                        {
                            pathDatabase[path.PathId] = path;
                        }
                    }

                    if (data != null && data.Sectors != null)
                    {
                        foreach (var sector in data.Sectors)
                        {
                            sectorCache[sector.Id] = sector;
                        }
                    }

                    Logger.Log($"Loaded {pathDatabase.Count} paths from cache");
                }
            }
            catch (Exception ex)
            {
                Logger.Log($"Error loading path cache: {ex.Message}");
            }
        }

        /// <summary>
        /// Save binary cache for faster loading
        /// </summary>
        private void SaveBinaryCache()
        {
            string binaryPath = Path.Combine(cacheDirectory, "paths.bin");

            using (var fs = new FileStream(binaryPath, FileMode.Create))
            using (var writer = new BinaryWriter(fs))
            {
                // Write version
                writer.Write(1);

                // Write path count
                writer.Write(pathDatabase.Count);

                foreach (var path in pathDatabase.Values)
                {
                    writer.Write(path.PathId);
                    writer.Write(path.Name ?? "");
                    WriteVector3(writer, path.Start);
                    WriteVector3(writer, path.End);
                    writer.Write(path.Distance);
                    writer.Write(path.UseCount);

                    // Write waypoints
                    writer.Write(path.Waypoints.Count);
                    foreach (var waypoint in path.Waypoints)
                    {
                        WriteVector3(writer, waypoint);
                    }
                }
            }
        }

        private bool LoadBinaryCache()
        {
            string binaryPath = Path.Combine(cacheDirectory, "paths.bin");

            if (!File.Exists(binaryPath))
                return false;

            try
            {
                using (var fs = new FileStream(binaryPath, FileMode.Open))
                using (var reader = new BinaryReader(fs))
                {
                    int version = reader.ReadInt32();
                    if (version != 1)
                        return false;

                    int pathCount = reader.ReadInt32();

                    for (int i = 0; i < pathCount; i++)
                    {
                        var path = new CachedPath
                        {
                            PathId = reader.ReadUInt64(),
                            Name = reader.ReadString(),
                            Start = ReadVector3(reader),
                            End = ReadVector3(reader),
                            Distance = reader.ReadSingle(),
                            UseCount = reader.ReadInt32()
                        };

                        int waypointCount = reader.ReadInt32();
                        path.Waypoints = new List<Vector3>(waypointCount);

                        for (int j = 0; j < waypointCount; j++)
                        {
                            path.Waypoints.Add(ReadVector3(reader));
                        }

                        pathDatabase[path.PathId] = path;
                    }

                    Logger.Log($"Loaded {pathCount} paths from binary cache");
                    return true;
                }
            }
            catch (Exception ex)
            {
                Logger.Log($"Error loading binary cache: {ex.Message}");
                return false;
            }
        }

        private void WriteVector3(BinaryWriter writer, Vector3 vector)
        {
            writer.Write(vector.X);
            writer.Write(vector.Y);
            writer.Write(vector.Z);
        }

        private Vector3 ReadVector3(BinaryReader reader)
        {
            return new Vector3(
                reader.ReadSingle(),
                reader.ReadSingle(),
                reader.ReadSingle()
            );
        }

        /// <summary>
        /// Broadcast new path to all connected clients
        /// </summary>
        private void BroadcastNewPath(CachedPath path)
        {
            // This would integrate with your networking layer
            var message = new GameMessage
            {
                Type = "path_update",
                Data = new Dictionary<string, object>
                {
                    { "path", JsonSerializer.Serialize(path) }
                }
            };

            // Broadcast through your server
            // server.BroadcastMessage(message);
        }

        /// <summary>
        /// Verify cache integrity across clients
        /// </summary>
        public string GenerateCacheChecksum()
        {
            var sortedHashes = pathDatabase.Keys.OrderBy(k => k).ToList();

            using (var sha256 = SHA256.Create())
            {
                var bytes = new List<byte>();

                foreach (var hash in sortedHashes)
                {
                    bytes.AddRange(BitConverter.GetBytes(hash));
                }

                var hashBytes = sha256.ComputeHash(bytes.ToArray());
                return Convert.ToBase64String(hashBytes);
            }
        }

        /// <summary>
        /// Synchronize cache with other clients
        /// </summary>
        public void SynchronizePaths(List<CachedPath> remotePaths)
        {
            foreach (var path in remotePaths)
            {
                if (!pathDatabase.ContainsKey(path.PathId))
                {
                    pathDatabase[path.PathId] = path;
                    Logger.Log($"Added remote path: {path.PathId}");
                }
            }

            SaveBakedPaths();
        }
    }

    /// <summary>
    /// Represents a cached deterministic path
    /// </summary>
    public class CachedPath
    {
        public ulong PathId { get; set; }
        public string Name { get; set; }
        public Vector3 Start { get; set; }
        public Vector3 End { get; set; }
        public List<Vector3> Waypoints { get; set; } = new List<Vector3>();
        public float Distance { get; set; }
        public DateTime CreatedAt { get; set; }
        public int UseCount { get; set; }

        public void IncrementUsage()
        {
            UseCount++;
        }
    }

    /// <summary>
    /// World sector for hierarchical pathfinding
    /// </summary>
    public class PathSector
    {
        public int Id { get; set; }
        public int X { get; set; }
        public int Y { get; set; }
        public float CenterX { get; set; }
        public float CenterY { get; set; }
        public List<int> Connections { get; set; } = new List<int>();
        public List<CachedPath> InternalPaths { get; set; } = new List<CachedPath>();

        public Vector3 GetNearestExit(Vector3 from, Vector3 to)
        {
            // Find the edge point closest to destination
            float bestDistance = float.MaxValue;
            Vector3 bestExit = from;

            // Check all four edges
            float sectorSize = 10000;
            float halfSize = sectorSize / 2;

            Vector3[] edgePoints = new Vector3[]
            {
                new Vector3(CenterX - halfSize, from.Y, from.Z), // West
                new Vector3(CenterX + halfSize, from.Y, from.Z), // East
                new Vector3(from.X, CenterY - halfSize, from.Z), // South
                new Vector3(from.X, CenterY + halfSize, from.Z)  // North
            };

            foreach (var point in edgePoints)
            {
                float distance = Vector3.Distance(point, to);
                if (distance < bestDistance)
                {
                    bestDistance = distance;
                    bestExit = point;
                }
            }

            return bestExit;
        }
    }

    /// <summary>
    /// Navigation mesh node for detailed pathfinding
    /// </summary>
    public class NavMeshNode
    {
        public string Id { get; set; }
        public Vector3 Position { get; set; }
        public List<string> Connections { get; set; } = new List<string>();
        public bool IsWalkable { get; set; } = true;
        public float Cost { get; set; } = 1.0f;
    }

    /// <summary>
    /// Path node for A* algorithm
    /// </summary>
    public class PathNode
    {
        public Vector3 Position { get; set; }
        public float G { get; set; } // Cost from start
        public float H { get; set; } // Heuristic to end
        public float F { get; set; } // Total cost
    }

    public class PathNodeComparer : IComparer<PathNode>
    {
        public int Compare(PathNode x, PathNode y)
        {
            int result = x.F.CompareTo(y.F);
            if (result == 0)
            {
                result = x.Position.GetHashCode().CompareTo(y.Position.GetHashCode());
            }
            return result;
        }
    }

    /// <summary>
    /// Simple 3D vector for positions
    /// </summary>
    public struct Vector3
    {
        public float X { get; set; }
        public float Y { get; set; }
        public float Z { get; set; }

        public Vector3(float x, float y, float z)
        {
            X = x;
            Y = y;
            Z = z;
        }

        public static float Distance(Vector3 a, Vector3 b)
        {
            float dx = a.X - b.X;
            float dy = a.Y - b.Y;
            float dz = a.Z - b.Z;
            return (float)Math.Sqrt(dx * dx + dy * dy + dz * dz);
        }

        public static Vector3 Lerp(Vector3 a, Vector3 b, float t)
        {
            return new Vector3(
                a.X + (b.X - a.X) * t,
                a.Y + (b.Y - a.Y) * t,
                a.Z + (b.Z - a.Z) * t
            );
        }

        public override bool Equals(object obj)
        {
            if (obj is Vector3 other)
            {
                return Math.Abs(X - other.X) < 0.01f &&
                       Math.Abs(Y - other.Y) < 0.01f &&
                       Math.Abs(Z - other.Z) < 0.01f;
            }
            return false;
        }

        public override int GetHashCode()
        {
            return HashCode.Combine(X, Y, Z);
        }

        public override string ToString()
        {
            return $"({X:F1}, {Y:F1}, {Z:F1})";
        }

        internal static int Normalize(object value, Vector3 position)
        {
            throw new NotImplementedException();
        }

        public static implicit operator Vector3(int v)
        {
            throw new NotImplementedException();
        }
    }

    /// <summary>
    /// Serialization helper
    /// </summary>
    public class PathCacheData
    {
        public List<CachedPath> Paths { get; set; } = new List<CachedPath>();
        public List<PathSector> Sectors { get; set; } = new List<PathSector>();
    }

    /// <summary>
    /// Simple LRU cache for hot paths
    /// </summary>
    public class LRUCache<TKey, TValue>
    {
        private readonly int capacity;
        private readonly Dictionary<TKey, LinkedListNode<KeyValuePair<TKey, TValue>>> cache;
        private readonly LinkedList<KeyValuePair<TKey, TValue>> lruList;

        public LRUCache(int capacity)
        {
            this.capacity = capacity;
            cache = new Dictionary<TKey, LinkedListNode<KeyValuePair<TKey, TValue>>>(capacity);
            lruList = new LinkedList<KeyValuePair<TKey, TValue>>();
        }

        public bool TryGetValue(TKey key, out TValue value)
        {
            if (cache.TryGetValue(key, out var node))
            {
                value = node.Value.Value;
                lruList.Remove(node);
                lruList.AddFirst(node);
                return true;
            }

            value = default(TValue);
            return false;
        }

        public void Add(TKey key, TValue value)
        {
            if (cache.TryGetValue(key, out var node))
            {
                lruList.Remove(node);
                lruList.AddFirst(node);
                node.Value = new KeyValuePair<TKey, TValue>(key, value);
            }
            else
            {
                if (cache.Count >= capacity)
                {
                    var lastNode = lruList.Last;
                    cache.Remove(lastNode.Value.Key);
                    lruList.RemoveLast();
                }

                var newNode = lruList.AddFirst(new KeyValuePair<TKey, TValue>(key, value));
                cache[key] = newNode;
            }
        }
    }
}