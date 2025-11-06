using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using System.Text;
using System.Threading.Tasks;
using System.Runtime.InteropServices;
using System.Numerics;
using KenshiMultiplayer.Networking;
using KenshiMultiplayer.Managers;
using KenshiMultiplayer.Data;

namespace KenshiMultiplayer.Utility
{
    /// <summary>
    /// Deterministic pathfinding cache to replace Havok AI's non-deterministic pathfinding
    /// </summary>
    public class PathCache
    {
        // Kenshi world is roughly 870km²
        private const int WORLD_SIZE = 870000; // meters
        private const int SECTOR_SIZE = 10000; // 10km sectors
        private const int MAX_PATH_POINTS = 256;
        private const int CACHE_SIZE = 1000;
        
        private readonly string cacheDirectory;
        private readonly ConcurrentDictionary<string, CachedPath> pathCache;
        private readonly LRUCache<string, CachedPath> memoryCache;
        private readonly object cacheLock = new object();
        
        // Major locations in Kenshi for pre-baking
        private readonly Dictionary<string, Vector3> majorLocations = new Dictionary<string, Vector3>
        {
            // The Great Desert
            { "Sho-Battai", new Vector3(-14850, 0, 25650) },
            { "Heft", new Vector3(-8550, 0, 18450) },
            { "Heng", new Vector3(2250, 0, 31950) },
            
            // The Hub and surroundings
            { "The Hub", new Vector3(-3150, 0, -4350) },
            { "Squin", new Vector3(-11250, 0, -8550) },
            { "Stack", new Vector3(8550, 0, -12150) },
            
            // Holy Nation territory
            { "Blister Hill", new Vector3(18450, 0, 4350) },
            { "Stack", new Vector3(8550, 0, -12150) },
            { "Bad Teeth", new Vector3(24750, 0, -2850) },
            
            // United Cities
            { "Stoat", new Vector3(-32850, 0, 14850) },
            { "Bark", new Vector3(-28650, 0, 8550) },
            { "Sho-Battai", new Vector3(-14850, 0, 25650) },
            
            // Shek Kingdom
            { "Admag", new Vector3(4350, 0, -18450) },
            { "Squin", new Vector3(-11250, 0, -8550) },
            
            // Tech Hunters
            { "World's End", new Vector3(42750, 0, 18450) },
            { "Flats Lagoon", new Vector3(31950, 0, -8550) },
            { "Black Desert City", new Vector3(-42750, 0, -18450) },
            
            // Swamp
            { "Shark", new Vector3(-24450, 0, -28650) },
            { "Mud Town", new Vector3(-18450, 0, -32850) },
            
            // Other notable
            { "Mongrel", new Vector3(14850, 0, 32850) },
            { "Catun", new Vector3(-8550, 0, -42750) },
            { "Spring", new Vector3(28650, 0, 12150) }
        };

        public PathCache(string gameDirectory)
        {
            cacheDirectory = Path.Combine(gameDirectory, "pathcache");
            Directory.CreateDirectory(cacheDirectory);
            
            pathCache = new ConcurrentDictionary<string, CachedPath>();
            memoryCache = new LRUCache<string, CachedPath>(CACHE_SIZE);
            
            LoadCache();
        }

        /// <summary>
        /// Get or calculate a path between two points
        /// </summary>
        public async Task<CachedPath> GetPath(Vector3 start, Vector3 end, bool allowGeneration = true)
        {
            string pathKey = GeneratePathKey(start, end);
            
            // Check memory cache first
            if (memoryCache.TryGet(pathKey, out CachedPath cachedPath))
            {
                return cachedPath;
            }
            
            // Check disk cache
            if (pathCache.TryGetValue(pathKey, out cachedPath))
            {
                memoryCache.Add(pathKey, cachedPath);
                return cachedPath;
            }
            
            // Generate new path if allowed
            if (allowGeneration)
            {
                cachedPath = await GeneratePath(start, end);
                if (cachedPath != null)
                {
                    await CachePath(pathKey, cachedPath);
                }
                return cachedPath;
            }
            
            return null;
        }

        /// <summary>
        /// Pre-bake common paths between major locations
        /// </summary>
        public async Task PreBakeCommonPaths()
        {
            Logger.Log("Pre-baking common paths between major locations...");
            
            var locations = majorLocations.ToList();
            int totalPaths = 0;
            
            for (int i = 0; i < locations.Count; i++)
            {
                for (int j = i + 1; j < locations.Count; j++)
                {
                    var start = locations[i].Value;
                    var end = locations[j].Value;
                    
                    // Generate bidirectional paths
                    await GetPath(start, end, true);
                    await GetPath(end, start, true);
                    
                    totalPaths += 2;
                    
                    if (totalPaths % 10 == 0)
                    {
                        Logger.Log($"Pre-baked {totalPaths} paths...");
                    }
                }
            }
            
            Logger.Log($"Pre-baking complete. Generated {totalPaths} paths.");
            await SaveCache();
        }

        /// <summary>
        /// Generate a deterministic path using sector-based A* pathfinding
        /// </summary>
        private async Task<CachedPath> GeneratePath(Vector3 start, Vector3 end)
        {
            return await Task.Run(() =>
            {
                var path = new CachedPath
                {
                    PathId = GeneratePathKey(start, end),
                    Start = start,
                    End = end,
                    Points = new List<Vector3>(),
                    Distance = 0,
                    GeneratedAt = DateTime.UtcNow
                };

                // Sector-based pathfinding for long distances
                var sectors = GetSectorPath(start, end);

                // Generate waypoints through sectors
                Vector3 current = start;
                foreach (var sector in sectors)
                {
                    var waypoint = GetSectorWaypoint(current, end, sector);
                    path.Points.Add(waypoint);
                    path.Distance += Vector3.Distance(current, waypoint);
                    current = waypoint;
                }

                // Add final point
                path.Points.Add(end);
                path.Distance += Vector3.Distance(current, end);

                // Optimize path
                path = OptimizePath(path);

                // Calculate checksum for synchronization
                path.Checksum = CalculatePathChecksum(path);

                return path;
            });
        }

        /// <summary>
        /// Get sector-based path for hierarchical pathfinding
        /// </summary>
        private List<Vector2> GetSectorPath(Vector3 start, Vector3 end)
        {
            var sectors = new List<Vector2>();
            
            // Convert to sector coordinates
            int startX = (int)(start.X / SECTOR_SIZE);
            int startY = (int)(start.Z / SECTOR_SIZE);
            int endX = (int)(end.X / SECTOR_SIZE);
            int endY = (int)(end.Z / SECTOR_SIZE);
            
            // Simple line-based sector traversal (can be improved with A*)
            int dx = Math.Abs(endX - startX);
            int dy = Math.Abs(endY - startY);
            int sx = startX < endX ? 1 : -1;
            int sy = startY < endY ? 1 : -1;
            int err = dx - dy;
            
            int currentX = startX;
            int currentY = startY;
            
            while (currentX != endX || currentY != endY)
            {
                sectors.Add(new Vector2(currentX * SECTOR_SIZE, currentY * SECTOR_SIZE));
                
                int e2 = 2 * err;
                if (e2 > -dy)
                {
                    err -= dy;
                    currentX += sx;
                }
                if (e2 < dx)
                {
                    err += dx;
                    currentY += sy;
                }
            }
            
            return sectors;
        }

        /// <summary>
        /// Get waypoint within a sector toward destination
        /// </summary>
        private Vector3 GetSectorWaypoint(Vector3 current, Vector3 destination, Vector2 sector)
        {
            // Calculate sector center
            float centerX = sector.X + SECTOR_SIZE / 2;
            float centerZ = sector.Y + SECTOR_SIZE / 2;
            
            // Create waypoint biased toward destination
            Vector3 sectorCenter = new Vector3(centerX, 0, centerZ);
            Vector3 direction = Vector3.Normalize(destination - current);
            
            // Add some deterministic variation based on sector coordinates
            float variation = (float)((sector.X + sector.Y) % 1000) / 1000f * 100;
            
            Vector3 waypoint = sectorCenter + direction * variation;
            
            // Clamp to sector bounds
            waypoint.X = Math.Clamp(waypoint.X, sector.X, sector.X + SECTOR_SIZE);
            waypoint.Z = Math.Clamp(waypoint.Z, sector.Y, sector.Y + SECTOR_SIZE);
            
            return waypoint;
        }

        /// <summary>
        /// Optimize path by removing unnecessary waypoints
        /// </summary>
        private CachedPath OptimizePath(CachedPath path)
        {
            if (path.Points.Count <= 2)
                return path;
            
            var optimized = new List<Vector3> { path.Points[0] };
            
            for (int i = 1; i < path.Points.Count - 1; i++)
            {
                Vector3 prev = optimized[optimized.Count - 1];
                Vector3 current = path.Points[i];
                Vector3 next = path.Points[i + 1];
                
                // Check if current point is necessary (angle threshold)
                Vector3 dir1 = Vector3.Normalize(current - prev);
                Vector3 dir2 = Vector3.Normalize(next - current);
                float angle = (float)Math.Acos(Vector3.Dot(dir1, dir2));
                
                if (angle > 0.1f) // ~5.7 degrees
                {
                    optimized.Add(current);
                }
            }
            
            optimized.Add(path.Points[path.Points.Count - 1]);
            
            // Limit to MAX_PATH_POINTS
            if (optimized.Count > MAX_PATH_POINTS)
            {
                // Resample path
                optimized = ResamplePath(optimized, MAX_PATH_POINTS);
            }
            
            path.Points = optimized;
            return path;
        }

        /// <summary>
        /// Resample path to specific number of points
        /// </summary>
        private List<Vector3> ResamplePath(List<Vector3> points, int targetCount)
        {
            if (points.Count <= targetCount)
                return points;
            
            var resampled = new List<Vector3>();
            float step = (float)(points.Count - 1) / (targetCount - 1);
            
            for (int i = 0; i < targetCount; i++)
            {
                float index = i * step;
                int lowIndex = (int)index;
                int highIndex = Math.Min(lowIndex + 1, points.Count - 1);
                float t = index - lowIndex;
                
                Vector3 interpolated = Vector3.Lerp(points[lowIndex], points[highIndex], t);
                resampled.Add(interpolated);
            }
            
            return resampled;
        }

        /// <summary>
        /// Generate unique key for path
        /// </summary>
        private string GeneratePathKey(Vector3 start, Vector3 end)
        {
            // Round to nearest meter for consistency
            int sx = (int)Math.Round(start.X);
            int sz = (int)Math.Round(start.Z);
            int ex = (int)Math.Round(end.X);
            int ez = (int)Math.Round(end.Z);
            
            return $"{sx},{sz}_{ex},{ez}";
        }

        /// <summary>
        /// Calculate checksum for path synchronization
        /// </summary>
        private string CalculatePathChecksum(CachedPath path)
        {
            using (var sha256 = SHA256.Create())
            {
                var data = new StringBuilder();
                foreach (var point in path.Points)
                {
                    data.Append($"{point.X:F2},{point.Y:F2},{point.Z:F2};");
                }
                
                byte[] bytes = Encoding.UTF8.GetBytes(data.ToString());
                byte[] hash = sha256.ComputeHash(bytes);
                return Convert.ToBase64String(hash);
            }
        }

        /// <summary>
        /// Cache path to memory and disk
        /// </summary>
        private async Task CachePath(string key, CachedPath path)
        {
            pathCache[key] = path;
            memoryCache.Add(key, path);
            
            // Save to disk asynchronously
            await Task.Run(() =>
            {
                string filePath = Path.Combine(cacheDirectory, $"{key.GetHashCode():X8}.path");
                using (var stream = new FileStream(filePath, FileMode.Create))
                using (var writer = new BinaryWriter(stream))
                {
                    path.Serialize(writer);
                }
            });
        }

        /// <summary>
        /// Load cache from disk
        /// </summary>
        private void LoadCache()
        {
            if (!Directory.Exists(cacheDirectory))
                return;
            
            var files = Directory.GetFiles(cacheDirectory, "*.path");
            foreach (var file in files)
            {
                try
                {
                    using (var stream = new FileStream(file, FileMode.Open))
                    using (var reader = new BinaryReader(stream))
                    {
                        var path = CachedPath.Deserialize(reader);
                        string key = GeneratePathKey(path.Start, path.End);
                        pathCache[key] = path;
                    }
                }
                catch (Exception ex)
                {
                    Logger.Log($"Failed to load path cache file {file}: {ex.Message}");
                }
            }
            
            Logger.Log($"Loaded {pathCache.Count} cached paths");
        }

        /// <summary>
        /// Save entire cache to disk
        /// </summary>
        public async Task SaveCache()
        {
            await Task.Run(() =>
            {
                foreach (var kvp in pathCache)
                {
                    string filePath = Path.Combine(cacheDirectory, $"{kvp.Key.GetHashCode():X8}.path");
                    using (var stream = new FileStream(filePath, FileMode.Create))
                    using (var writer = new BinaryWriter(stream))
                    {
                        kvp.Value.Serialize(writer);
                    }
                }
            });
            
            Logger.Log($"Saved {pathCache.Count} paths to cache");
        }

        /// <summary>
        /// Validate cache checksums for synchronization
        /// </summary>
        public bool ValidateCache(Dictionary<string, string> checksums)
        {
            foreach (var kvp in checksums)
            {
                if (!pathCache.TryGetValue(kvp.Key, out var path) || path.Checksum != kvp.Value)
                {
                    return false;
                }
            }
            return true;
        }

        /// <summary>
        /// Synchronize paths from server/client
        /// </summary>
        public void SynchronizePaths(List<CachedPath> paths)
        {
            foreach (var path in paths)
            {
                if (path == null || string.IsNullOrEmpty(path.PathId))
                    continue;

                // Add to both disk cache and memory cache
                pathCache[path.PathId] = path;
                memoryCache.Add(path.PathId, path);
            }

            Logger.Log($"Synchronized {paths.Count} paths to cache");
        }

        /// <summary>
        /// Get cache statistics
        /// </summary>
        public CacheStatistics GetStatistics()
        {
            return new CacheStatistics
            {
                TotalPaths = pathCache.Count,
                MemoryCachePaths = memoryCache.Count,
                CacheSizeMB = CalculateCacheSize() / (1024.0 * 1024.0),
                OldestPath = pathCache.Values.Min(p => p.GeneratedAt),
                NewestPath = pathCache.Values.Max(p => p.GeneratedAt)
            };
        }

        private long CalculateCacheSize()
        {
            long size = 0;
            foreach (var path in pathCache.Values)
            {
                size += path.Points.Count * 12; // 3 floats per point
                size += 100; // Overhead
            }
            return size;
        }
    }

    /// <summary>
    /// Cached path data structure
    /// </summary>
    public class CachedPath
    {
        public string PathId { get; set; }
        public Vector3 Start { get; set; }
        public Vector3 End { get; set; }
        public List<Vector3> Points { get; set; }

        // Alias for compatibility with code expecting Waypoints
        public List<Vector3> Waypoints
        {
            get => Points;
            set => Points = value;
        }

        public float Distance { get; set; }
        public string Checksum { get; set; }
        public DateTime GeneratedAt { get; set; }

        public void Serialize(BinaryWriter writer)
        {
            // Write PathId
            writer.Write(PathId ?? "");

            // Write start and end points
            writer.Write(Start.X);
            writer.Write(Start.Y);
            writer.Write(Start.Z);
            writer.Write(End.X);
            writer.Write(End.Y);
            writer.Write(End.Z);

            // Write path points
            writer.Write(Points.Count);
            foreach (var point in Points)
            {
                writer.Write(point.X);
                writer.Write(point.Y);
                writer.Write(point.Z);
            }

            // Write metadata
            writer.Write(Distance);
            writer.Write(Checksum ?? "");
            writer.Write(GeneratedAt.Ticks);
        }

        public static CachedPath Deserialize(BinaryReader reader)
        {
            var path = new CachedPath
            {
                PathId = reader.ReadString(),
                Start = new Vector3(reader.ReadSingle(), reader.ReadSingle(), reader.ReadSingle()),
                End = new Vector3(reader.ReadSingle(), reader.ReadSingle(), reader.ReadSingle()),
                Points = new List<Vector3>()
            };
            
            int pointCount = reader.ReadInt32();
            for (int i = 0; i < pointCount; i++)
            {
                path.Points.Add(new Vector3(
                    reader.ReadSingle(),
                    reader.ReadSingle(),
                    reader.ReadSingle()
                ));
            }
            
            path.Distance = reader.ReadSingle();
            path.Checksum = reader.ReadString();
            path.GeneratedAt = new DateTime(reader.ReadInt64());
            
            return path;
        }
    }

    /// <summary>
    /// Simple LRU cache implementation
    /// </summary>
    public class LRUCache<TKey, TValue>
    {
        private readonly int capacity;
        private readonly Dictionary<TKey, LinkedListNode<CacheItem>> cache;
        private readonly LinkedList<CacheItem> lruList;

        public int Count => cache.Count;

        public LRUCache(int capacity)
        {
            this.capacity = capacity;
            cache = new Dictionary<TKey, LinkedListNode<CacheItem>>(capacity);
            lruList = new LinkedList<CacheItem>();
        }

        public bool TryGet(TKey key, out TValue value)
        {
            if (cache.TryGetValue(key, out var node))
            {
                value = node.Value.Value;
                lruList.Remove(node);
                lruList.AddFirst(node);
                return true;
            }
            
            value = default;
            return false;
        }

        public void Add(TKey key, TValue value)
        {
            if (cache.TryGetValue(key, out var node))
            {
                node.Value.Value = value;
                lruList.Remove(node);
                lruList.AddFirst(node);
            }
            else
            {
                if (cache.Count >= capacity)
                {
                    var last = lruList.Last;
                    cache.Remove(last.Value.Key);
                    lruList.RemoveLast();
                }
                
                var cacheItem = new CacheItem { Key = key, Value = value };
                var newNode = lruList.AddFirst(cacheItem);
                cache[key] = newNode;
            }
        }

        private class CacheItem
        {
            public TKey Key { get; set; }
            public TValue Value { get; set; }
        }
    }

    public class CacheStatistics
    {
        public int TotalPaths { get; set; }
        public int MemoryCachePaths { get; set; }
        public double CacheSizeMB { get; set; }
        public DateTime OldestPath { get; set; }
        public DateTime NewestPath { get; set; }
    }
}