using System;
using System.Collections.Generic;
using System.Collections.Concurrent;
using System.Linq;
using System.Security.Cryptography;
using System.Text;
using System.Threading.Tasks;
using System.Diagnostics;
using System.IO;
using KenshiMultiplayer.Networking.Player;
using KenshiMultiplayer.Networking;

namespace KenshiMultiplayer
{
    /// <summary>
    /// Comprehensive anti-cheat system for Kenshi multiplayer
    /// </summary>
    public class AntiCheatSystem
    {
        // Violation tracking
        private readonly ConcurrentDictionary<string, PlayerViolations> playerViolations = new ConcurrentDictionary<string, PlayerViolations>();
        private readonly ConcurrentDictionary<string, List<ViolationRecord>> violationHistory = new ConcurrentDictionary<string, List<ViolationRecord>>();

        // Detection modules
        private readonly SpeedHackDetector speedHackDetector = new SpeedHackDetector();
        private readonly TeleportDetector teleportDetector = new TeleportDetector();
        private readonly StatValidator statValidator = new StatValidator();
        private readonly InventoryValidator inventoryValidator = new InventoryValidator();
        private readonly CombatValidator combatValidator = new CombatValidator();
        private readonly MemoryIntegrityChecker memoryChecker = new MemoryIntegrityChecker();
        private readonly PacketValidator packetValidator = new PacketValidator();

        // Configuration
        private readonly AntiCheatConfig config;
        private readonly int maxViolationPoints = 100;
        private readonly TimeSpan violationDecayPeriod = TimeSpan.FromMinutes(30);

        // Callbacks
        public event EventHandler<CheatDetectedEventArgs> CheatDetected;
        public event EventHandler<string> PlayerBanned;

        public AntiCheatSystem(AntiCheatConfig configuration = null)
        {
            config = configuration ?? new AntiCheatConfig();
            InitializeDetectors();
        }

        /// <summary>
        /// Initialize all detection modules
        /// </summary>
        private void InitializeDetectors()
        {
            speedHackDetector.Initialize(config.SpeedHackSettings);
            teleportDetector.Initialize(config.TeleportSettings);
            statValidator.Initialize(config.StatValidationSettings);
            inventoryValidator.Initialize(config.InventorySettings);
            combatValidator.Initialize(config.CombatSettings);
            memoryChecker.Initialize(config.MemoryCheckSettings);
            packetValidator.Initialize(config.PacketSettings);

            Logger.Log("Anti-cheat system initialized");
        }

        /// <summary>
        /// Validate player movement
        /// </summary>
        public ValidationResult ValidateMovement(string playerId, MovementData movement)
        {
            var result = new ValidationResult { IsValid = true };

            // Speed hack detection
            var speedResult = speedHackDetector.Check(playerId, movement);
            if (!speedResult.IsValid)
            {
                RecordViolation(playerId, ViolationType.SpeedHack, speedResult.Severity, speedResult.Details);
                result.IsValid = false;
                result.Reason = "Abnormal movement speed detected";
            }

            // Teleport detection
            var teleportResult = teleportDetector.Check(playerId, movement);
            if (!teleportResult.IsValid)
            {
                RecordViolation(playerId, ViolationType.Teleport, teleportResult.Severity, teleportResult.Details);
                result.IsValid = false;
                result.Reason = "Teleportation detected";
            }

            // No-clip detection (moving through walls)
            if (IsNoClipping(movement))
            {
                RecordViolation(playerId, ViolationType.NoClip, ViolationSeverity.Critical, "Moving through solid objects");
                result.IsValid = false;
                result.Reason = "No-clip detected";
            }

            return result;
        }

        /// <summary>
        /// Validate player stats
        /// </summary>
        public ValidationResult ValidateStats(string playerId, PlayerStats stats)
        {
            var result = statValidator.Validate(playerId, stats);

            if (!result.IsValid)
            {
                RecordViolation(playerId, ViolationType.StatManipulation, result.Severity, result.Details);
            }

            return result;
        }

        /// <summary>
        /// Validate inventory changes
        /// </summary>
        public ValidationResult ValidateInventory(string playerId, InventoryChange change)
        {
            var result = inventoryValidator.Validate(playerId, change);

            if (!result.IsValid)
            {
                RecordViolation(playerId, ViolationType.ItemDuplication, result.Severity, result.Details);
            }

            return result;
        }

        /// <summary>
        /// Validate combat action
        /// </summary>
        public ValidationResult ValidateCombat(string playerId, CombatAction action)
        {
            var result = combatValidator.Validate(playerId, action);

            if (!result.IsValid)
            {
                RecordViolation(playerId, ViolationType.CombatHack, result.Severity, result.Details);
            }

            return result;
        }

        /// <summary>
        /// Validate network packet
        /// </summary>
        public ValidationResult ValidatePacket(string playerId, NetworkPacket packet)
        {
            var result = packetValidator.Validate(playerId, packet);

            if (!result.IsValid)
            {
                RecordViolation(playerId, ViolationType.PacketManipulation, result.Severity, result.Details);
            }

            return result;
        }

        /// <summary>
        /// Perform memory integrity check on client
        /// </summary>
        public async Task<bool> PerformMemoryCheck(string playerId)
        {
            var checksum = await memoryChecker.RequestChecksum(playerId);

            if (!memoryChecker.ValidateChecksum(checksum))
            {
                RecordViolation(playerId, ViolationType.MemoryManipulation, ViolationSeverity.Critical, "Memory integrity check failed");
                return false;
            }

            return true;
        }

        /// <summary>
        /// Record a violation
        /// </summary>
        private void RecordViolation(string playerId, ViolationType type, ViolationSeverity severity, string details)
        {
            // Get or create player violations
            var violations = playerViolations.GetOrAdd(playerId, new PlayerViolations { PlayerId = playerId });

            // Calculate points based on severity
            int points = CalculateViolationPoints(type, severity);
            violations.TotalPoints += points;
            violations.LastViolation = DateTime.UtcNow;

            // Record in history
            var record = new ViolationRecord
            {
                Type = type,
                Severity = severity,
                Details = details,
                Points = points,
                Timestamp = DateTime.UtcNow
            };

            var history = violationHistory.GetOrAdd(playerId, new List<ViolationRecord>());
            history.Add(record);

            // Log the violation
            Logger.Log($"VIOLATION: Player {playerId} - {type} ({severity}) - {details}");

            // Raise event
            CheatDetected?.Invoke(this, new CheatDetectedEventArgs
            {
                PlayerId = playerId,
                ViolationType = type,
                Severity = severity,
                Details = details
            });

            // Check for automatic ban
            if (violations.TotalPoints >= maxViolationPoints)
            {
                BanPlayer(playerId, "Excessive violations");
            }
            else if (severity == ViolationSeverity.Critical)
            {
                BanPlayer(playerId, $"Critical violation: {type}");
            }
        }

        /// <summary>
        /// Calculate violation points
        /// </summary>
        private int CalculateViolationPoints(ViolationType type, ViolationSeverity severity)
        {
            int basePoints = type switch
            {
                ViolationType.SpeedHack => 20,
                ViolationType.Teleport => 30,
                ViolationType.NoClip => 50,
                ViolationType.StatManipulation => 40,
                ViolationType.ItemDuplication => 35,
                ViolationType.CombatHack => 25,
                ViolationType.MemoryManipulation => 100,
                ViolationType.PacketManipulation => 15,
                _ => 10
            };

            return severity switch
            {
                ViolationSeverity.Minor => basePoints / 2,
                ViolationSeverity.Moderate => basePoints,
                ViolationSeverity.Major => basePoints * 2,
                ViolationSeverity.Critical => basePoints * 5,
                _ => basePoints
            };
        }

        /// <summary>
        /// Ban a player
        /// </summary>
        private void BanPlayer(string playerId, string reason)
        {
            Logger.Log($"BANNING PLAYER: {playerId} - Reason: {reason}");

            // Create ban record
            var banRecord = new BanRecord
            {
                PlayerId = playerId,
                Reason = reason,
                Timestamp = DateTime.UtcNow,
                Violations = violationHistory.GetOrAdd(playerId, new List<ViolationRecord>())
            };

            // Save ban record
            SaveBanRecord(banRecord);

            // Raise event
            PlayerBanned?.Invoke(this, playerId);
        }

        /// <summary>
        /// Check for no-clip
        /// </summary>
        private bool IsNoClipping(MovementData movement)
        {
            // Check if path goes through solid objects
            // This would need terrain/building collision data

            // Simplified check - height changes without proper pathing
            if (movement.EndPosition.Z - movement.StartPosition.Z > 2.0f)
            {
                var horizontalDistance = Math.Sqrt(
                    Math.Pow(movement.EndPosition.X - movement.StartPosition.X, 2) +
                    Math.Pow(movement.EndPosition.Y - movement.StartPosition.Y, 2)
                );

                // Climbing too steep without path
                if (horizontalDistance < 1.0f)
                    return true;
            }

            return false;
        }

        /// <summary>
        /// Decay violation points over time
        /// </summary>
        public void ProcessViolationDecay()
        {
            var now = DateTime.UtcNow;

            foreach (var kvp in playerViolations)
            {
                var violations = kvp.Value;

                if (now - violations.LastViolation > violationDecayPeriod)
                {
                    // Decay points
                    violations.TotalPoints = Math.Max(0, violations.TotalPoints - 10);

                    // Clean up if no violations
                    if (violations.TotalPoints == 0)
                    {
                        playerViolations.TryRemove(kvp.Key, out _);
                    }
                }
            }
        }

        /// <summary>
        /// Get player violation status
        /// </summary>
        public PlayerViolations GetPlayerViolations(string playerId)
        {
            return playerViolations.GetOrAdd(playerId, new PlayerViolations { PlayerId = playerId });
        }

        /// <summary>
        /// Save ban record to file
        /// </summary>
        private void SaveBanRecord(BanRecord record)
        {
            try
            {
                string banFile = $"bans/{record.PlayerId}_{record.Timestamp:yyyyMMddHHmmss}.json";
                Directory.CreateDirectory("bans");

                var json = System.Text.Json.JsonSerializer.Serialize(record, new System.Text.Json.JsonSerializerOptions
                {
                    WriteIndented = true
                });

                File.WriteAllText(banFile, json);
            }
            catch (Exception ex)
            {
                Logger.Log($"Error saving ban record: {ex.Message}");
            }
        }
    }

    /// <summary>
    /// Speed hack detector
    /// </summary>
    public class SpeedHackDetector
    {
        private readonly ConcurrentDictionary<string, MovementHistory> playerMovement = new ConcurrentDictionary<string, MovementHistory>();
        private SpeedHackSettings settings;

        public void Initialize(SpeedHackSettings config)
        {
            settings = config ?? new SpeedHackSettings();
        }

        public ValidationResult Check(string playerId, MovementData movement)
        {
            var history = playerMovement.GetOrAdd(playerId, new MovementHistory());

            // Calculate speed
            var distance = Vector3.Distance(movement.StartPosition, movement.EndPosition);
            var time = (movement.Timestamp - history.LastTimestamp) / 1000.0f;

            if (time <= 0)
                return new ValidationResult { IsValid = true };

            var speed = distance / time;

            // Check against max speed (Kenshi's max running speed is ~8 m/s)
            float maxSpeed = settings.MaxRunSpeed;

            // Add tolerance for lag
            maxSpeed *= settings.LagTolerance;

            if (speed > maxSpeed)
            {
                // Check if consistent speed hacking
                history.SpeedViolations++;

                if (history.SpeedViolations >= settings.ViolationThreshold)
                {
                    return new ValidationResult
                    {
                        IsValid = false,
                        Severity = speed > maxSpeed * 2 ? ViolationSeverity.Critical : ViolationSeverity.Major,
                        Details = $"Speed: {speed:F2} m/s (Max: {settings.MaxRunSpeed:F2} m/s)"
                    };
                }
            }
            else
            {
                // Decay violations
                history.SpeedViolations = Math.Max(0, history.SpeedViolations - 1);
            }

            // Update history
            history.LastPosition = movement.EndPosition;
            history.LastTimestamp = movement.Timestamp;

            return new ValidationResult { IsValid = true };
        }
    }

    /// <summary>
    /// Teleport detector
    /// </summary>
    public class TeleportDetector
    {
        private readonly ConcurrentDictionary<string, Vector3> lastPositions = new ConcurrentDictionary<string, Vector3>();
        private TeleportSettings settings;

        public void Initialize(TeleportSettings config)
        {
            settings = config ?? new TeleportSettings();
        }

        public ValidationResult Check(string playerId, MovementData movement)
        {
            if (!lastPositions.TryGetValue(playerId, out var lastPos))
            {
                lastPositions[playerId] = movement.EndPosition;
                return new ValidationResult { IsValid = true };
            }

            var distance = Vector3.Distance(lastPos, movement.StartPosition);

            // Check for position mismatch (teleport)
            if (distance > settings.MaxPositionMismatch)
            {
                return new ValidationResult
                {
                    IsValid = false,
                    Severity = distance > settings.MaxPositionMismatch * 10 ? ViolationSeverity.Critical : ViolationSeverity.Major,
                    Details = $"Position mismatch: {distance:F2}m"
                };
            }

            lastPositions[playerId] = movement.EndPosition;
            return new ValidationResult { IsValid = true };
        }
    }

    /// <summary>
    /// Stat validator
    /// </summary>
    public class StatValidator
    {
        private readonly ConcurrentDictionary<string, PlayerStats> lastStats = new ConcurrentDictionary<string, PlayerStats>();
        private StatValidationSettings settings;

        public void Initialize(StatValidationSettings config)
        {
            settings = config ?? new StatValidationSettings();
        }

        public ValidationResult Validate(string playerId, PlayerStats stats)
        {
            // Check stat limits
            if (stats.Strength > settings.MaxStat || stats.Dexterity > settings.MaxStat ||
                stats.Toughness > settings.MaxStat || stats.Perception > settings.MaxStat)
            {
                return new ValidationResult
                {
                    IsValid = false,
                    Severity = ViolationSeverity.Critical,
                    Details = "Stats exceed maximum allowed values"
                };
            }

            // Check for instant stat increases
            if (lastStats.TryGetValue(playerId, out var last))
            {
                var strengthGain = stats.Strength - last.Strength;
                var dexGain = stats.Dexterity - last.Dexterity;

                if (strengthGain > settings.MaxStatGainPerMinute || dexGain > settings.MaxStatGainPerMinute)
                {
                    return new ValidationResult
                    {
                        IsValid = false,
                        Severity = ViolationSeverity.Major,
                        Details = $"Abnormal stat gain detected"
                    };
                }
            }

            lastStats[playerId] = stats;
            return new ValidationResult { IsValid = true };
        }
    }

    /// <summary>
    /// Inventory validator
    /// </summary>
    public class InventoryValidator
    {
        private readonly ConcurrentDictionary<string, Dictionary<string, int>> playerInventories = new ConcurrentDictionary<string, Dictionary<string, int>>();
        private InventorySettings settings;

        public void Initialize(InventorySettings config)
        {
            settings = config ?? new InventorySettings();
        }

        public ValidationResult Validate(string playerId, InventoryChange change)
        {
            var inventory = playerInventories.GetOrAdd(playerId, new Dictionary<string, int>());

            // Check for item duplication
            if (change.Type == ChangeType.Add)
            {
                // Check if adding more than possible
                if (change.Quantity > settings.MaxStackSize)
                {
                    return new ValidationResult
                    {
                        IsValid = false,
                        Severity = ViolationSeverity.Major,
                        Details = $"Adding {change.Quantity} items exceeds max stack size"
                    };
                }

                // Check for rare item duplication
                if (IsRareItem(change.ItemId) && change.Quantity > 1)
                {
                    return new ValidationResult
                    {
                        IsValid = false,
                        Severity = ViolationSeverity.Critical,
                        Details = $"Duplicating rare item: {change.ItemId}"
                    };
                }
            }

            // Update tracked inventory
            if (!inventory.ContainsKey(change.ItemId))
                inventory[change.ItemId] = 0;

            inventory[change.ItemId] += change.Type == ChangeType.Add ? change.Quantity : -change.Quantity;

            if (inventory[change.ItemId] < 0)
            {
                return new ValidationResult
                {
                    IsValid = false,
                    Severity = ViolationSeverity.Major,
                    Details = "Negative inventory detected"
                };
            }

            return new ValidationResult { IsValid = true };
        }

        private bool IsRareItem(string itemId)
        {
            // Check against list of rare/unique items
            var rareItems = new[] { "meitou", "edge_weapon", "masterwork" };
            return rareItems.Any(rare => itemId.ToLower().Contains(rare));
        }
    }

    /// <summary>
    /// Combat validator
    /// </summary>
    public class CombatValidator
    {
        private readonly ConcurrentDictionary<string, CombatStats> combatStats = new ConcurrentDictionary<string, CombatStats>();
        private CombatSettings settings;

        public void Initialize(CombatSettings config)
        {
            settings = config ?? new CombatSettings();
        }

        public ValidationResult Validate(string playerId, CombatAction action)
        {
            var stats = combatStats.GetOrAdd(playerId, new CombatStats());

            // Check attack speed
            var timeSinceLastAttack = (action.Timestamp - stats.LastAttackTime) / 1000.0f;

            if (timeSinceLastAttack < settings.MinAttackInterval)
            {
                stats.AttackSpeedViolations++;

                if (stats.AttackSpeedViolations >= settings.ViolationThreshold)
                {
                    return new ValidationResult
                    {
                        IsValid = false,
                        Severity = ViolationSeverity.Major,
                        Details = $"Attack speed too fast: {timeSinceLastAttack:F2}s"
                    };
                }
            }
            else
            {
                stats.AttackSpeedViolations = Math.Max(0, stats.AttackSpeedViolations - 1);
            }

            // Check damage
            if (action.Damage > settings.MaxDamage)
            {
                return new ValidationResult
                {
                    IsValid = false,
                    Severity = ViolationSeverity.Critical,
                    Details = $"Damage exceeds maximum: {action.Damage}"
                };
            }

            // Check range
            if (action.Range > settings.MaxAttackRange)
            {
                return new ValidationResult
                {
                    IsValid = false,
                    Severity = ViolationSeverity.Major,
                    Details = $"Attack range too far: {action.Range:F2}m"
                };
            }

            stats.LastAttackTime = action.Timestamp;
            return new ValidationResult { IsValid = true };
        }
    }

    /// <summary>
    /// Memory integrity checker
    /// </summary>
    public class MemoryIntegrityChecker
    {
        private readonly Dictionary<string, string> expectedChecksums = new Dictionary<string, string>();
        private MemoryCheckSettings settings;

        public void Initialize(MemoryCheckSettings config)
        {
            settings = config ?? new MemoryCheckSettings();
            GenerateExpectedChecksums();
        }

        private void GenerateExpectedChecksums()
        {
            // Generate checksums for critical memory regions
            expectedChecksums["player_stats"] = "a1b2c3d4e5f6";
            expectedChecksums["game_speed"] = "f6e5d4c3b2a1";
            // Add more...
        }

        public async Task<string> RequestChecksum(string playerId)
        {
            // Request memory checksum from client
            await Task.Delay(100); // Simulate network request
            return "a1b2c3d4e5f6"; // Placeholder
        }

        public bool ValidateChecksum(string checksum)
        {
            return expectedChecksums.Values.Contains(checksum);
        }
    }

    /// <summary>
    /// Packet validator
    /// </summary>
    public class PacketValidator
    {
        private readonly ConcurrentDictionary<string, PacketStats> packetStats = new ConcurrentDictionary<string, PacketStats>();
        private PacketSettings settings;

        public void Initialize(PacketSettings config)
        {
            settings = config ?? new PacketSettings();
        }

        public ValidationResult Validate(string playerId, NetworkPacket packet)
        {
            var stats = packetStats.GetOrAdd(playerId, new PacketStats());

            // Check packet rate
            stats.PacketsPerSecond++;

            if (stats.PacketsPerSecond > settings.MaxPacketsPerSecond)
            {
                return new ValidationResult
                {
                    IsValid = false,
                    Severity = ViolationSeverity.Moderate,
                    Details = $"Packet flood detected: {stats.PacketsPerSecond} pps"
                };
            }

            // Check packet size
            if (packet.Data.Length > settings.MaxPacketSize)
            {
                return new ValidationResult
                {
                    IsValid = false,
                    Severity = ViolationSeverity.Major,
                    Details = $"Packet too large: {packet.Data.Length} bytes"
                };
            }

            return new ValidationResult { IsValid = true };
        }
    }

    // Supporting classes

    public class ValidationResult
    {
        public bool IsValid { get; set; }
        public ViolationSeverity Severity { get; set; }
        public string Details { get; set; }
        public string Reason { get; set; }
    }

    public enum ViolationType
    {
        SpeedHack,
        Teleport,
        NoClip,
        StatManipulation,
        ItemDuplication,
        CombatHack,
        MemoryManipulation,
        PacketManipulation
    }

    public enum ViolationSeverity
    {
        Minor,
        Moderate,
        Major,
        Critical
    }

    public class PlayerViolations
    {
        public string PlayerId { get; set; }
        public int TotalPoints { get; set; }
        public DateTime LastViolation { get; set; }
        public Dictionary<ViolationType, int> ViolationCounts { get; set; } = new Dictionary<ViolationType, int>();
    }

    public class ViolationRecord
    {
        public ViolationType Type { get; set; }
        public ViolationSeverity Severity { get; set; }
        public string Details { get; set; }
        public int Points { get; set; }
        public DateTime Timestamp { get; set; }
    }

    public class BanRecord
    {
        public string PlayerId { get; set; }
        public string Reason { get; set; }
        public DateTime Timestamp { get; set; }
        public List<ViolationRecord> Violations { get; set; }
    }

    public class CheatDetectedEventArgs : EventArgs
    {
        public string PlayerId { get; set; }
        public ViolationType ViolationType { get; set; }
        public ViolationSeverity Severity { get; set; }
        public string Details { get; set; }
    }

    public class MovementData
    {
        public Vector3 StartPosition { get; set; }
        public Vector3 EndPosition { get; set; }
        public long Timestamp { get; set; }
    }

    public class MovementHistory
    {
        public Vector3 LastPosition { get; set; }
        public long LastTimestamp { get; set; }
        public int SpeedViolations { get; set; }
    }

    public class PlayerStats
    {
        public float Strength { get; set; }
        public float Dexterity { get; set; }
        public float Toughness { get; set; }
        public float Perception { get; set; }
    }

    public class InventoryChange
    {
        public string ItemId { get; set; }
        public int Quantity { get; set; }
        public ChangeType Type { get; set; }
    }

    public enum ChangeType
    {
        Add,
        Remove
    }

    public class CombatStats
    {
        public long LastAttackTime { get; set; }
        public int AttackSpeedViolations { get; set; }
    }

    public class PacketStats
    {
        public int PacketsPerSecond { get; set; }
        public DateTime LastReset { get; set; } = DateTime.UtcNow;
    }

    // Configuration classes

    public class AntiCheatConfig
    {
        public SpeedHackSettings SpeedHackSettings { get; set; } = new SpeedHackSettings();
        public TeleportSettings TeleportSettings { get; set; } = new TeleportSettings();
        public StatValidationSettings StatValidationSettings { get; set; } = new StatValidationSettings();
        public InventorySettings InventorySettings { get; set; } = new InventorySettings();
        public CombatSettings CombatSettings { get; set; } = new CombatSettings();
        public MemoryCheckSettings MemoryCheckSettings { get; set; } = new MemoryCheckSettings();
        public PacketSettings PacketSettings { get; set; } = new PacketSettings();
    }

    public class SpeedHackSettings
    {
        public float MaxRunSpeed { get; set; } = 8.0f; // Kenshi max run speed
        public float LagTolerance { get; set; } = 1.2f; // 20% tolerance for lag
        public int ViolationThreshold { get; set; } = 3;
    }

    public class TeleportSettings
    {
        public float MaxPositionMismatch { get; set; } = 5.0f; // meters
    }

    public class StatValidationSettings
    {
        public float MaxStat { get; set; } = 100.0f;
        public float MaxStatGainPerMinute { get; set; } = 1.0f;
    }

    public class InventorySettings
    {
        public int MaxStackSize { get; set; } = 999;
    }

    public class CombatSettings
    {
        public float MinAttackInterval { get; set; } = 0.5f; // seconds
        public int MaxDamage { get; set; } = 200;
        public float MaxAttackRange { get; set; } = 5.0f; // meters
        public int ViolationThreshold { get; set; } = 3;
    }

    public class MemoryCheckSettings
    {
        public int CheckInterval { get; set; } = 60000; // milliseconds
    }

    public class PacketSettings
    {
        public int MaxPacketsPerSecond { get; set; } = 60;
        public int MaxPacketSize { get; set; } = 8192; // bytes
    }
}