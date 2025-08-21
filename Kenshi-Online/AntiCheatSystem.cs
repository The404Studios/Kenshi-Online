using KenshiMultiplayer.Common;
using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Numerics;
using System.Security.Cryptography;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using KenshiMultiplayer.Auth;
using KenshiMultiplayer.Networking;
using KenshiMultiplayer.Common;
using KenshiMultiplayer.Utility;

namespace KenshiMultiplayer
{
    /// <summary>
    /// Comprehensive anti-cheat system for multiplayer
    /// </summary>
    public class AntiCheatSystem
    {
        private bool isEnabled;
        private bool isServer;

        // Detection modules
        private SpeedHackDetector speedHackDetector;
        private TeleportDetector teleportDetector;
        private StatManipulationDetector statDetector;
        private MemoryScanner memoryScanner;
        private ProcessMonitor processMonitor;
        private NetworkValidator networkValidator;
        private FileIntegrityChecker fileChecker;

        // Violation tracking
        private ConcurrentDictionary<string, PlayerViolations> violations;
        private ConcurrentDictionary<string, DateTime> bannedPlayers;

        // Configuration
        private AntiCheatConfig config;

        // Events
        public event Action<CheatDetection> OnCheatDetected;
        public event Action<string, ViolationType> OnViolation;
        public event Action<string, string> OnPlayerBanned;
        public event Action<string> OnPlayerKicked;

        // Statistics
        private AntiCheatStatistics stats;

        public AntiCheatSystem(bool serverMode)
        {
            isServer = serverMode;
            violations = new ConcurrentDictionary<string, PlayerViolations>();
            bannedPlayers = new ConcurrentDictionary<string, DateTime>();
            stats = new AntiCheatStatistics();

            InitializeDetectors();
            LoadConfiguration();
        }

        /// <summary>
        /// Initialize all detection modules
        /// </summary>
        private void InitializeDetectors()
        {
            speedHackDetector = new SpeedHackDetector();
            teleportDetector = new TeleportDetector();
            statDetector = new StatManipulationDetector();
            memoryScanner = new MemoryScanner();
            processMonitor = new ProcessMonitor();
            networkValidator = new NetworkValidator();
            fileChecker = new FileIntegrityChecker();
        }

        /// <summary>
        /// Load anti-cheat configuration
        /// </summary>
        private void LoadConfiguration()
        {
            config = new AntiCheatConfig
            {
                // Detection thresholds
                MaxSpeedVariance = 1.5f,
                MaxTeleportDistance = 100.0f,
                MaxStatChangeRate = 10.0f,

                // Violation limits
                MaxViolationsBeforeKick = 5,
                MaxViolationsBeforeBan = 10,
                ViolationDecayTime = 3600, // seconds

                // Scan intervals
                MemoryScanInterval = 30000, // ms
                ProcessScanInterval = 10000,
                FileScanInterval = 60000,

                // Actions
                AutoKick = true,
                AutoBan = true,
                BanDuration = 86400, // 24 hours

                // Whitelist
                WhitelistedProcesses = new List<string> { "discord", "steam", "obs" }
            };
        }

        /// <summary>
        /// Start anti-cheat monitoring
        /// </summary>
        public async Task Start()
        {
            isEnabled = true;

            // Start detection loops
            Task.Run(() => SpeedHackDetectionLoop());
            Task.Run(() => MemoryScanLoop());
            Task.Run(() => ProcessMonitorLoop());
            Task.Run(() => FileIntegrityLoop());
            Task.Run(() => ViolationDecayLoop());

            // Initial file integrity check
            await PerformFileIntegrityCheck();

            Logger.Log("Anti-cheat system started");
        }

        /// <summary>
        /// Validate player action
        /// </summary>
        public bool ValidateAction(string playerId, PlayerAction action)
        {
            if (!isEnabled) return true;

            var validationResult = true;

            switch (action.Type)
            {
                case ActionType.Movement:
                    validationResult = ValidateMovement(playerId, action);
                    break;

                case ActionType.Combat:
                    validationResult = ValidateCombat(playerId, action);
                    break;

                case ActionType.StatChange:
                    validationResult = ValidateStatChange(playerId, action);
                    break;

                case ActionType.ItemSpawn:
                    validationResult = ValidateItemSpawn(playerId, action);
                    break;
            }

            if (!validationResult)
            {
                RecordViolation(playerId, GetViolationType(action.Type), action.ToString());
            }

            return validationResult;
        }

        /// <summary>
        /// Validate movement
        /// </summary>
        private bool ValidateMovement(string playerId, PlayerAction action)
        {
            // Check for speed hacks
            if (!speedHackDetector.ValidateMovement(playerId, action.TargetPosition, action.Timestamp))
            {
                OnCheatDetected?.Invoke(new CheatDetection
                {
                    PlayerId = playerId,
                    Type = CheatType.SpeedHack,
                    Severity = Severity.High,
                    Details = "Movement speed exceeds maximum",
                    Timestamp = DateTime.UtcNow
                });
                return false;
            }

            // Check for teleportation
            if (!teleportDetector.ValidatePosition(playerId, action.TargetPosition))
            {
                OnCheatDetected?.Invoke(new CheatDetection
                {
                    PlayerId = playerId,
                    Type = CheatType.Teleport,
                    Severity = Severity.Critical,
                    Details = "Impossible position change detected",
                    Timestamp = DateTime.UtcNow
                });
                return false;
            }

            // Check for no-clip (position inside solid objects)
            if (IsPositionInsideGeometry(action.TargetPosition))
            {
                OnCheatDetected?.Invoke(new CheatDetection
                {
                    PlayerId = playerId,
                    Type = CheatType.NoClip,
                    Severity = Severity.Critical,
                    Details = "Player position inside solid geometry",
                    Timestamp = DateTime.UtcNow
                });
                return false;
            }

            return true;
        }

        /// <summary>
        /// Validate combat action
        /// </summary>
        private bool ValidateCombat(string playerId, PlayerAction action)
        {
            // Check for damage modification
            if (action.Damage > GetMaxPossibleDamage(action.WeaponType))
            {
                OnCheatDetected?.Invoke(new CheatDetection
                {
                    PlayerId = playerId,
                    Type = CheatType.DamageHack,
                    Severity = Severity.High,
                    Details = $"Damage {action.Damage} exceeds maximum for weapon type",
                    Timestamp = DateTime.UtcNow
                });
                return false;
            }

            // Check for attack speed hack
            if (!ValidateAttackSpeed(playerId, action.Timestamp))
            {
                OnCheatDetected?.Invoke(new CheatDetection
                {
                    PlayerId = playerId,
                    Type = CheatType.AttackSpeedHack,
                    Severity = Severity.Medium,
                    Details = "Attack speed exceeds maximum",
                    Timestamp = DateTime.UtcNow
                });
                return false;
            }

            // Check for range hack
            if (!ValidateAttackRange(playerId, action.TargetId, action.WeaponType))
            {
                OnCheatDetected?.Invoke(new CheatDetection
                {
                    PlayerId = playerId,
                    Type = CheatType.RangeHack,
                    Severity = Severity.Medium,
                    Details = "Attack range exceeds weapon maximum",
                    Timestamp = DateTime.UtcNow
                });
                return false;
            }

            return true;
        }

        /// <summary>
        /// Validate stat change
        /// </summary>
        private bool ValidateStatChange(string playerId, PlayerAction action)
        {
            return statDetector.ValidateStatChange(playerId, action.StatType, action.OldValue, action.NewValue);
        }

        /// <summary>
        /// Validate item spawn
        /// </summary>
        private bool ValidateItemSpawn(string playerId, PlayerAction action)
        {
            // Check if player has permission to spawn items
            if (!HasAdminPermission(playerId))
            {
                OnCheatDetected?.Invoke(new CheatDetection
                {
                    PlayerId = playerId,
                    Type = CheatType.ItemSpawn,
                    Severity = Severity.Critical,
                    Details = $"Unauthorized item spawn: {action.ItemType}",
                    Timestamp = DateTime.UtcNow
                });
                return false;
            }

            return true;
        }

        /// <summary>
        /// Speed hack detection loop
        /// </summary>
        private async Task SpeedHackDetectionLoop()
        {
            while (isEnabled)
            {
                try
                {
                    speedHackDetector.Update();
                    await Task.Delay(100);
                }
                catch (Exception ex)
                {
                    Logger.LogError("Speed hack detection error", ex);
                }
            }
        }

        /// <summary>
        /// Memory scan loop
        /// </summary>
        private async Task MemoryScanLoop()
        {
            while (isEnabled)
            {
                try
                {
                    await Task.Delay(config.MemoryScanInterval);

                    if (!isServer)
                    {
                        var scanResult = await memoryScanner.ScanForCheats();
                        if (!scanResult.IsClean)
                        {
                            OnCheatDetected?.Invoke(new CheatDetection
                            {
                                Type = CheatType.MemoryManipulation,
                                Severity = Severity.Critical,
                                Details = scanResult.Details,
                                Timestamp = DateTime.UtcNow
                            });
                        }
                    }
                }
                catch (Exception ex)
                {
                    Logger.LogError("Memory scan error", ex);
                }
            }
        }

        /// <summary>
        /// Process monitor loop
        /// </summary>
        private async Task ProcessMonitorLoop()
        {
            while (isEnabled)
            {
                try
                {
                    await Task.Delay(config.ProcessScanInterval);

                    if (!isServer)
                    {
                        var suspiciousProcesses = processMonitor.GetSuspiciousProcesses();
                        foreach (var process in suspiciousProcesses)
                        {
                            if (!config.WhitelistedProcesses.Any(w => process.ToLower().Contains(w)))
                            {
                                OnCheatDetected?.Invoke(new CheatDetection
                                {
                                    Type = CheatType.SuspiciousProcess,
                                    Severity = Severity.Medium,
                                    Details = $"Suspicious process detected: {process}",
                                    Timestamp = DateTime.UtcNow
                                });
                            }
                        }
                    }
                }
                catch (Exception ex)
                {
                    Logger.LogError("Process monitor error", ex);
                }
            }
        }

        /// <summary>
        /// File integrity check loop
        /// </summary>
        private async Task FileIntegrityLoop()
        {
            while (isEnabled)
            {
                try
                {
                    await Task.Delay(config.FileScanInterval);
                    await PerformFileIntegrityCheck();
                }
                catch (Exception ex)
                {
                    Logger.LogError("File integrity check error", ex);
                }
            }
        }

        /// <summary>
        /// Perform file integrity check
        /// </summary>
        private async Task PerformFileIntegrityCheck()
        {
            var result = await fileChecker.CheckGameFiles();
            if (!result.IsValid)
            {
                OnCheatDetected?.Invoke(new CheatDetection
                {
                    Type = CheatType.ModifiedFiles,
                    Severity = Severity.Critical,
                    Details = $"Modified game files detected: {string.Join(", ", result.ModifiedFiles)}",
                    Timestamp = DateTime.UtcNow
                });
            }
        }

        /// <summary>
        /// Violation decay loop
        /// </summary>
        private async Task ViolationDecayLoop()
        {
            while (isEnabled)
            {
                try
                {
                    await Task.Delay(60000); // Check every minute

                    foreach (var player in violations.Values)
                    {
                        player.DecayViolations(config.ViolationDecayTime);
                    }
                }
                catch (Exception ex)
                {
                    Logger.LogError("Violation decay error", ex);
                }
            }
        }

        /// <summary>
        /// Record violation
        /// </summary>
        private void RecordViolation(string playerId, ViolationType type, string details)
        {
            var playerViolations = violations.GetOrAdd(playerId, new PlayerViolations(playerId));
            playerViolations.AddViolation(type, details);

            OnViolation?.Invoke(playerId, type);

            // Check for auto-kick
            if (config.AutoKick && playerViolations.TotalViolations >= config.MaxViolationsBeforeKick)
            {
                KickPlayer(playerId, "Too many violations");
            }

            // Check for auto-ban
            if (config.AutoBan && playerViolations.TotalViolations >= config.MaxViolationsBeforeBan)
            {
                BanPlayer(playerId, config.BanDuration, "Excessive violations");
            }

            stats.TotalViolations++;
        }

        /// <summary>
        /// Kick player
        /// </summary>
        public void KickPlayer(string playerId, string reason)
        {
            OnPlayerKicked?.Invoke(playerId);
            Logger.Log($"Player {playerId} kicked: {reason}");
            stats.PlayersKicked++;
        }

        /// <summary>
        /// Ban player
        /// </summary>
        public void BanPlayer(string playerId, int duration, string reason)
        {
            var banExpiry = DateTime.UtcNow.AddSeconds(duration);
            bannedPlayers[playerId] = banExpiry;

            OnPlayerBanned?.Invoke(playerId, reason);
            Logger.Log($"Player {playerId} banned until {banExpiry}: {reason}");
            stats.PlayersBanned++;
        }

        /// <summary>
        /// Check if player is banned
        /// </summary>
        public bool IsPlayerBanned(string playerId)
        {
            if (bannedPlayers.TryGetValue(playerId, out var banExpiry))
            {
                if (DateTime.UtcNow < banExpiry)
                    return true;

                // Ban expired, remove it
                bannedPlayers.TryRemove(playerId, out _);
            }

            return false;
        }

        /// <summary>
        /// Validate network message
        /// </summary>
        public bool ValidateNetworkMessage(string playerId, NetworkMessage message)
        {
            return networkValidator.ValidateMessage(playerId, message);
        }

        // Helper methods
        private bool IsPositionInsideGeometry(Vector3 position)
        {
            // Check against world geometry
            // This would need actual collision detection
            return false;
        }

        private float GetMaxPossibleDamage(string weaponType)
        {
            // Return maximum damage for weapon type
            return 1000.0f;
        }

        private bool ValidateAttackSpeed(string playerId, double timestamp)
        {
            // Check attack speed against weapon stats
            return true;
        }

        private bool ValidateAttackRange(string playerId, string targetId, string weaponType)
        {
            // Check distance between attacker and target
            return true;
        }

        private bool HasAdminPermission(string playerId)
        {
            // Check if player has admin rights
            return false;
        }

        private ViolationType GetViolationType(ActionType actionType)
        {
            switch (actionType)
            {
                case ActionType.Movement: return ViolationType.Movement;
                case ActionType.Combat: return ViolationType.Combat;
                case ActionType.StatChange: return ViolationType.Stats;
                case ActionType.ItemSpawn: return ViolationType.Items;
                default: return ViolationType.Other;
            }
        }

        /// <summary>
        /// Get anti-cheat statistics
        /// </summary>
        public AntiCheatStatistics GetStatistics()
        {
            return stats;
        }
    }

    /// <summary>
    /// Speed hack detector
    /// </summary>
    public class SpeedHackDetector
    {
        private Dictionary<string, MovementHistory> playerMovement;
        private readonly float maxSpeed = 10.0f; // meters per second

        public SpeedHackDetector()
        {
            playerMovement = new Dictionary<string, MovementHistory>();
        }

        public bool ValidateMovement(string playerId, Vector3 position, double timestamp)
        {
            if (!playerMovement.TryGetValue(playerId, out var history))
            {
                history = new MovementHistory();
                playerMovement[playerId] = history;
            }

            if (history.LastPosition != null)
            {
                var distance = Vector3.Distance(history.LastPosition.Value, position);
                var timeDelta = timestamp - history.LastTimestamp;
                var speed = distance / timeDelta;

                if (speed > maxSpeed * 1.5f) // Allow some variance
                {
                    return false;
                }
            }

            history.LastPosition = position;
            history.LastTimestamp = timestamp;

            return true;
        }

        public void Update()
        {
            // Periodic update logic
        }

        private class MovementHistory
        {
            public Vector3? LastPosition { get; set; }
            public double LastTimestamp { get; set; }
        }
    }

    /// <summary>
    /// Teleport detector
    /// </summary>
    public class TeleportDetector
    {
        private Dictionary<string, Vector3> lastKnownPositions;
        private readonly float maxTeleportDistance = 100.0f;

        public TeleportDetector()
        {
            lastKnownPositions = new Dictionary<string, Vector3>();
        }

        public bool ValidatePosition(string playerId, Vector3 position)
        {
            if (lastKnownPositions.TryGetValue(playerId, out var lastPos))
            {
                var distance = Vector3.Distance(lastPos, position);
                if (distance > maxTeleportDistance)
                {
                    return false;
                }
            }

            lastKnownPositions[playerId] = position;
            return true;
        }
    }

    /// <summary>
    /// Stat manipulation detector
    /// </summary>
    public class StatManipulationDetector
    {
        private Dictionary<string, Dictionary<string, float>> playerStats;

        public StatManipulationDetector()
        {
            playerStats = new Dictionary<string, Dictionary<string, float>>();
        }

        public bool ValidateStatChange(string playerId, string statType, float oldValue, float newValue)
        {
            var maxChangeRate = GetMaxChangeRate(statType);
            var change = Math.Abs(newValue - oldValue);

            if (change > maxChangeRate)
            {
                return false;
            }

            // Store for future validation
            if (!playerStats.ContainsKey(playerId))
            {
                playerStats[playerId] = new Dictionary<string, float>();
            }
            playerStats[playerId][statType] = newValue;

            return true;
        }

        private float GetMaxChangeRate(string statType)
        {
            // Return maximum allowed change rate for stat type
            return 10.0f;
        }
    }

    /// <summary>
    /// Memory scanner for cheat detection
    /// </summary>
    public class MemoryScanner
    {
        private List<string> knownCheatSignatures;

        public MemoryScanner()
        {
            knownCheatSignatures = new List<string>
            {
                "CheatEngine",
                "speedhack",
                "trainer",
                "injector"
            };
        }

        public async Task<ScanResult> ScanForCheats()
        {
            // Scan running processes
            var processes = Process.GetProcesses();

            foreach (var process in processes)
            {
                try
                {
                    var processName = process.ProcessName.ToLower();

                    if (knownCheatSignatures.Any(sig => processName.Contains(sig)))
                    {
                        return new ScanResult
                        {
                            IsClean = false,
                            Details = $"Cheat software detected: {process.ProcessName}"
                        };
                    }
                }
                catch
                {
                    // Process access denied, skip
                }
            }

            return new ScanResult { IsClean = true };
        }

        public class ScanResult
        {
            public bool IsClean { get; set; }
            public string Details { get; set; }
        }
    }

    /// <summary>
    /// Process monitor
    /// </summary>
    public class ProcessMonitor
    {
        private HashSet<string> suspiciousProcessNames;

        public ProcessMonitor()
        {
            suspiciousProcessNames = new HashSet<string>
            {
                "cheatengine",
                "artmoney",
                "ollydbg",
                "x64dbg",
                "ida",
                "processhacker"
            };
        }

        public List<string> GetSuspiciousProcesses()
        {
            var suspicious = new List<string>();
            var processes = Process.GetProcesses();

            foreach (var process in processes)
            {
                try
                {
                    var name = process.ProcessName.ToLower();
                    if (suspiciousProcessNames.Any(s => name.Contains(s)))
                    {
                        suspicious.Add(process.ProcessName);
                    }
                }
                catch
                {
                    // Access denied
                }
            }

            return suspicious;
        }
    }

    /// <summary>
    /// Network validator
    /// </summary>
    public class NetworkValidator
    {
        private Dictionary<string, RateLimiter> rateLimiters;

        public NetworkValidator()
        {
            rateLimiters = new Dictionary<string, RateLimiter>();
        }

        public bool ValidateMessage(string playerId, NetworkMessage message)
        {
            // Check rate limiting
            if (!rateLimiters.TryGetValue(playerId, out var limiter))
            {
                limiter = new RateLimiter();
                rateLimiters[playerId] = limiter;
            }

            if (!limiter.AllowRequest())
            {
                return false;
            }

            // Validate message size
            if (message.Data?.ToString().Length > 65536)
            {
                return false;
            }

            return true;
        }

        private class RateLimiter
        {
            private Queue<DateTime> requests = new Queue<DateTime>();
            private readonly int maxRequests = 100;
            private readonly TimeSpan timeWindow = TimeSpan.FromSeconds(1);

            public bool AllowRequest()
            {
                var now = DateTime.UtcNow;

                // Remove old requests
                while (requests.Count > 0 && now - requests.Peek() > timeWindow)
                {
                    requests.Dequeue();
                }

                if (requests.Count >= maxRequests)
                {
                    return false;
                }

                requests.Enqueue(now);
                return true;
            }
        }
    }

    /// <summary>
    /// File integrity checker
    /// </summary>
    public class FileIntegrityChecker
    {
        private Dictionary<string, string> fileHashes;

        public FileIntegrityChecker()
        {
            fileHashes = new Dictionary<string, string>();
            // Load known good file hashes
        }

        public async Task<IntegrityResult> CheckGameFiles()
        {
            var modifiedFiles = new List<string>();

            // Check critical game files
            var gamePath = GetGamePath();
            var criticalFiles = new[]
            {
                "kenshi_x64.exe",
                "data.dat",
                "scripts.pak"
            };

            foreach (var file in criticalFiles)
            {
                var filePath = Path.Combine(gamePath, file);
                if (File.Exists(filePath))
                {
                    var hash = await CalculateFileHash(filePath);
                    if (fileHashes.ContainsKey(file) && fileHashes[file] != hash)
                    {
                        modifiedFiles.Add(file);
                    }
                }
            }

            return new IntegrityResult
            {
                IsValid = modifiedFiles.Count == 0,
                ModifiedFiles = modifiedFiles
            };
        }

        private async Task<string> CalculateFileHash(string filePath)
        {
            using (var sha256 = SHA256.Create())
            using (var stream = File.OpenRead(filePath))
            {
                var hash = await Task.Run(() => sha256.ComputeHash(stream));
                return BitConverter.ToString(hash).Replace("-", "");
            }
        }

        private string GetGamePath()
        {
            // Get Kenshi installation path
            return @"C:\Program Files\Steam\steamapps\common\Kenshi";
        }

        public class IntegrityResult
        {
            public bool IsValid { get; set; }
            public List<string> ModifiedFiles { get; set; }
        }
    }

    // Data structures
    public class PlayerViolations
    {
        public string PlayerId { get; set; }
        public int TotalViolations { get; private set; }
        public Dictionary<ViolationType, List<Violation>> Violations { get; set; }

        public PlayerViolations(string playerId)
        {
            PlayerId = playerId;
            Violations = new Dictionary<ViolationType, List<Violation>>();
        }

        public void AddViolation(ViolationType type, string details)
        {
            if (!Violations.ContainsKey(type))
            {
                Violations[type] = new List<Violation>();
            }

            Violations[type].Add(new Violation
            {
                Type = type,
                Details = details,
                Timestamp = DateTime.UtcNow
            });

            TotalViolations++;
        }

        public void DecayViolations(int decayTimeSeconds)
        {
            var cutoff = DateTime.UtcNow.AddSeconds(-decayTimeSeconds);

            foreach (var list in Violations.Values)
            {
                list.RemoveAll(v => v.Timestamp < cutoff);
            }

            TotalViolations = Violations.Values.Sum(l => l.Count);
        }
    }

    public class Violation
    {
        public ViolationType Type { get; set; }
        public string Details { get; set; }
        public DateTime Timestamp { get; set; }
    }

    public class CheatDetection
    {
        public string PlayerId { get; set; }
        public CheatType Type { get; set; }
        public Severity Severity { get; set; }
        public string Details { get; set; }
        public DateTime Timestamp { get; set; }
    }

    public class AntiCheatConfig
    {
        public float MaxSpeedVariance { get; set; }
        public float MaxTeleportDistance { get; set; }
        public float MaxStatChangeRate { get; set; }
        public int MaxViolationsBeforeKick { get; set; }
        public int MaxViolationsBeforeBan { get; set; }
        public int ViolationDecayTime { get; set; }
        public int MemoryScanInterval { get; set; }
        public int ProcessScanInterval { get; set; }
        public int FileScanInterval { get; set; }
        public bool AutoKick { get; set; }
        public bool AutoBan { get; set; }
        public int BanDuration { get; set; }
        public List<string> WhitelistedProcesses { get; set; }
    }

    public class AntiCheatStatistics
    {
        public int TotalViolations { get; set; }
        public int PlayersKicked { get; set; }
        public int PlayersBanned { get; set; }
        public Dictionary<CheatType, int> DetectionsByType { get; set; } = new Dictionary<CheatType, int>();
    }

    public enum CheatType
    {
        SpeedHack,
        Teleport,
        NoClip,
        DamageHack,
        AttackSpeedHack,
        RangeHack,
        ItemSpawn,
        StatManipulation,
        MemoryManipulation,
        ModifiedFiles,
        SuspiciousProcess,
        NetworkExploit
    }

    public enum ViolationType
    {
        Movement,
        Combat,
        Stats,
        Items,
        Network,
        Other
    }

    public enum Severity
    {
        Low,
        Medium,
        High,
        Critical
    }

    public enum ActionType
    {
        Movement,
        Combat,
        StatChange,
        ItemSpawn,
        Build,
        Trade,
        Interaction
    }
}