using System;
using System.Collections.Generic;
using System.Collections.Concurrent;
using KenshiMultiplayer.Utility;

namespace KenshiMultiplayer.Networking.Authority
{
    /// <summary>
    /// Trust Boundary System
    ///
    /// PRINCIPLE: Injection + networking without trust rules = cheat engine with a socket
    ///
    /// This system provides:
    /// - Position delta clamps
    /// - Rate limits on actions
    /// - Authority checks on inventory & combat
    /// - Abuse detection
    ///
    /// We don't need anti-cheat yet. We DO need anti-nonsense.
    /// </summary>
    public class TrustBoundary
    {
        private const string LOG_PREFIX = "[TrustBoundary] ";

        #region Configuration Constants

        // Position constraints (per tick at 20Hz = 50ms)
        public const float MAX_POSITION_DELTA_PER_TICK = 3.0f;    // 3 meters per tick max
        public const float MAX_POSITION_DELTA_PER_SECOND = 15.0f; // 15 m/s absolute max
        public const float TELEPORT_THRESHOLD = 50.0f;            // Anything > 50m is teleport

        // Combat constraints
        public const float MAX_ATTACK_RANGE = 5.0f;               // 5 meter max melee range
        public const float MAX_RANGED_ATTACK_RANGE = 100.0f;      // 100 meter max ranged
        public const int MIN_ATTACK_COOLDOWN_MS = 500;            // 500ms min between attacks
        public const int MAX_ATTACKS_PER_SECOND = 3;              // Rate limit attacks

        // Inventory constraints
        public const int MAX_INVENTORY_CHANGES_PER_SECOND = 10;   // Rate limit inventory
        public const int MAX_ITEM_STACK = 999;                    // Max stack size
        public const float MAX_PICKUP_RANGE = 5.0f;               // Must be within 5m to pickup

        // General rate limits
        public const int MAX_MESSAGES_PER_SECOND = 60;            // Overall message rate limit
        public const int MAX_CHAT_MESSAGES_PER_MINUTE = 30;       // Chat spam prevention

        // Violation thresholds
        public const int VIOLATION_WARNING_THRESHOLD = 3;         // Warnings before action
        public const int VIOLATION_KICK_THRESHOLD = 10;           // Violations before kick
        public const int VIOLATION_BAN_THRESHOLD = 25;            // Violations before ban

        #endregion

        // Client rate tracking
        private readonly ConcurrentDictionary<string, ClientRateLimiter> clientRateLimiters = new();

        // Violation tracking
        private readonly ConcurrentDictionary<string, ViolationRecord> violationRecords = new();

        // Events
        public event Action<string, string, int> OnViolationDetected;  // playerId, type, count
        public event Action<string, string> OnPlayerShouldBeKicked;    // playerId, reason
        public event Action<string, string> OnPlayerShouldBeBanned;    // playerId, reason

        /// <summary>
        /// Register a client for tracking
        /// </summary>
        public void RegisterClient(string playerId)
        {
            clientRateLimiters[playerId] = new ClientRateLimiter();
            violationRecords[playerId] = new ViolationRecord { PlayerId = playerId };
            Logger.Log(LOG_PREFIX + $"Client {playerId} registered for trust boundary tracking");
        }

        /// <summary>
        /// Unregister a client
        /// </summary>
        public void UnregisterClient(string playerId)
        {
            clientRateLimiters.TryRemove(playerId, out _);
            // Keep violation records for potential reconnect
        }

        #region Position Validation

        /// <summary>
        /// Validate a position update
        /// </summary>
        public TrustValidationResult ValidatePositionUpdate(
            string playerId,
            float newX, float newY, float newZ,
            float oldX, float oldY, float oldZ,
            float deltaTimeSeconds)
        {
            var result = new TrustValidationResult { IsValid = true };

            // Calculate delta
            float dx = newX - oldX;
            float dy = newY - oldY;
            float dz = newZ - oldZ;
            float distance = MathF.Sqrt(dx * dx + dy * dy + dz * dz);

            // Check for teleport (absolute rejection)
            if (distance > TELEPORT_THRESHOLD)
            {
                result.IsValid = false;
                result.ViolationType = ViolationType.Teleport;
                result.Reason = $"Teleport detected: {distance:F1}m > {TELEPORT_THRESHOLD}m";
                RecordViolation(playerId, ViolationType.Teleport);
                return result;
            }

            // Calculate speed
            float speed = deltaTimeSeconds > 0 ? distance / deltaTimeSeconds : distance;

            // Check speed hack (per second)
            if (speed > MAX_POSITION_DELTA_PER_SECOND)
            {
                result.IsValid = false;
                result.ViolationType = ViolationType.SpeedHack;
                result.Reason = $"Speed violation: {speed:F1}m/s > {MAX_POSITION_DELTA_PER_SECOND}m/s";
                RecordViolation(playerId, ViolationType.SpeedHack);
                return result;
            }

            // Check per-tick delta (for 20Hz tick rate)
            float expectedMaxDelta = MAX_POSITION_DELTA_PER_TICK * (deltaTimeSeconds / 0.05f);
            if (distance > expectedMaxDelta * 1.5f) // 50% tolerance for network jitter
            {
                result.IsValid = false;
                result.ViolationType = ViolationType.MovementAnomaly;
                result.Reason = $"Movement anomaly: {distance:F1}m > {expectedMaxDelta:F1}m expected";
                RecordViolation(playerId, ViolationType.MovementAnomaly);
                return result;
            }

            // Clamp position if slightly over (soft correction)
            if (distance > expectedMaxDelta)
            {
                result.WasClamped = true;
                float clampRatio = expectedMaxDelta / distance;
                result.ClampedX = oldX + dx * clampRatio;
                result.ClampedY = oldY + dy * clampRatio;
                result.ClampedZ = oldZ + dz * clampRatio;
            }

            return result;
        }

        #endregion

        #region Combat Validation

        /// <summary>
        /// Validate a combat action
        /// </summary>
        public TrustValidationResult ValidateCombatAction(
            string attackerId,
            string targetId,
            float attackerX, float attackerY, float attackerZ,
            float targetX, float targetY, float targetZ,
            bool isRanged = false)
        {
            var result = new TrustValidationResult { IsValid = true };

            // Check rate limit
            if (!CheckRateLimit(attackerId, RateLimitType.Attack))
            {
                result.IsValid = false;
                result.ViolationType = ViolationType.RateLimit;
                result.Reason = "Attack rate limit exceeded";
                RecordViolation(attackerId, ViolationType.RateLimit);
                return result;
            }

            // Check cooldown
            if (!CheckCooldown(attackerId, CooldownType.Attack, MIN_ATTACK_COOLDOWN_MS))
            {
                result.IsValid = false;
                result.ViolationType = ViolationType.CooldownViolation;
                result.Reason = "Attack cooldown not elapsed";
                // Don't record as violation - might be network lag
                return result;
            }

            // Calculate distance to target
            float dx = targetX - attackerX;
            float dy = targetY - attackerY;
            float dz = targetZ - attackerZ;
            float distance = MathF.Sqrt(dx * dx + dy * dy + dz * dz);

            // Check range
            float maxRange = isRanged ? MAX_RANGED_ATTACK_RANGE : MAX_ATTACK_RANGE;
            if (distance > maxRange)
            {
                result.IsValid = false;
                result.ViolationType = ViolationType.RangeViolation;
                result.Reason = $"Target out of range: {distance:F1}m > {maxRange}m";
                RecordViolation(attackerId, ViolationType.RangeViolation);
                return result;
            }

            // Can't attack self
            if (attackerId == targetId)
            {
                result.IsValid = false;
                result.ViolationType = ViolationType.InvalidTarget;
                result.Reason = "Cannot attack self";
                return result;
            }

            // Update cooldown
            SetCooldown(attackerId, CooldownType.Attack);

            return result;
        }

        #endregion

        #region Inventory Validation

        /// <summary>
        /// Validate an inventory action
        /// </summary>
        public TrustValidationResult ValidateInventoryAction(
            string playerId,
            string itemId,
            int quantity,
            string actionType,
            float playerX = 0, float playerY = 0, float playerZ = 0,
            float itemX = 0, float itemY = 0, float itemZ = 0)
        {
            var result = new TrustValidationResult { IsValid = true };

            // Check rate limit
            if (!CheckRateLimit(playerId, RateLimitType.Inventory))
            {
                result.IsValid = false;
                result.ViolationType = ViolationType.RateLimit;
                result.Reason = "Inventory action rate limit exceeded";
                RecordViolation(playerId, ViolationType.RateLimit);
                return result;
            }

            // Validate quantity
            if (quantity <= 0 || quantity > MAX_ITEM_STACK)
            {
                result.IsValid = false;
                result.ViolationType = ViolationType.InvalidQuantity;
                result.Reason = $"Invalid quantity: {quantity}";
                RecordViolation(playerId, ViolationType.InvalidQuantity);
                return result;
            }

            // For pickup actions, check range
            if (actionType.ToLower() == "pickup")
            {
                float dx = itemX - playerX;
                float dy = itemY - playerY;
                float dz = itemZ - playerZ;
                float distance = MathF.Sqrt(dx * dx + dy * dy + dz * dz);

                if (distance > MAX_PICKUP_RANGE)
                {
                    result.IsValid = false;
                    result.ViolationType = ViolationType.RangeViolation;
                    result.Reason = $"Item out of pickup range: {distance:F1}m > {MAX_PICKUP_RANGE}m";
                    RecordViolation(playerId, ViolationType.RangeViolation);
                    return result;
                }
            }

            // Validate item ID format
            if (string.IsNullOrWhiteSpace(itemId))
            {
                result.IsValid = false;
                result.ViolationType = ViolationType.InvalidItem;
                result.Reason = "Invalid item ID";
                return result;
            }

            return result;
        }

        #endregion

        #region Chat Validation

        /// <summary>
        /// Validate a chat message
        /// </summary>
        public TrustValidationResult ValidateChatMessage(string playerId, string message)
        {
            var result = new TrustValidationResult { IsValid = true };

            // Check rate limit
            if (!CheckRateLimit(playerId, RateLimitType.Chat))
            {
                result.IsValid = false;
                result.ViolationType = ViolationType.RateLimit;
                result.Reason = "Chat rate limit exceeded (spam)";
                RecordViolation(playerId, ViolationType.ChatSpam);
                return result;
            }

            // Check message length
            if (string.IsNullOrWhiteSpace(message))
            {
                result.IsValid = false;
                result.Reason = "Empty message";
                return result;
            }

            if (message.Length > 500)
            {
                result.IsValid = false;
                result.ViolationType = ViolationType.InvalidInput;
                result.Reason = "Message too long";
                return result;
            }

            return result;
        }

        #endregion

        #region General Message Validation

        /// <summary>
        /// Check overall message rate limit
        /// </summary>
        public bool CheckGeneralRateLimit(string playerId)
        {
            return CheckRateLimit(playerId, RateLimitType.General);
        }

        #endregion

        #region Rate Limiting

        private bool CheckRateLimit(string playerId, RateLimitType type)
        {
            if (!clientRateLimiters.TryGetValue(playerId, out var limiter))
                return true;

            return limiter.CheckAndIncrement(type);
        }

        private bool CheckCooldown(string playerId, CooldownType type, int cooldownMs)
        {
            if (!clientRateLimiters.TryGetValue(playerId, out var limiter))
                return true;

            return limiter.CheckCooldown(type, cooldownMs);
        }

        private void SetCooldown(string playerId, CooldownType type)
        {
            if (clientRateLimiters.TryGetValue(playerId, out var limiter))
            {
                limiter.SetCooldown(type);
            }
        }

        #endregion

        #region Violation Tracking

        private void RecordViolation(string playerId, ViolationType type)
        {
            if (!violationRecords.TryGetValue(playerId, out var record))
            {
                record = new ViolationRecord { PlayerId = playerId };
                violationRecords[playerId] = record;
            }

            record.TotalViolations++;
            record.ViolationCounts[type] = record.ViolationCounts.GetValueOrDefault(type) + 1;
            record.LastViolation = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();

            OnViolationDetected?.Invoke(playerId, type.ToString(), record.TotalViolations);

            // Check thresholds
            if (record.TotalViolations >= VIOLATION_BAN_THRESHOLD)
            {
                OnPlayerShouldBeBanned?.Invoke(playerId, $"Exceeded violation threshold: {record.TotalViolations}");
            }
            else if (record.TotalViolations >= VIOLATION_KICK_THRESHOLD)
            {
                OnPlayerShouldBeKicked?.Invoke(playerId, $"Exceeded violation threshold: {record.TotalViolations}");
            }

            Logger.Log(LOG_PREFIX + $"Violation: {playerId} - {type} (total: {record.TotalViolations})");
        }

        /// <summary>
        /// Get violation record for a player
        /// </summary>
        public ViolationRecord GetViolationRecord(string playerId)
        {
            violationRecords.TryGetValue(playerId, out var record);
            return record;
        }

        /// <summary>
        /// Clear violation record (e.g., after warning)
        /// </summary>
        public void ClearViolations(string playerId)
        {
            if (violationRecords.TryGetValue(playerId, out var record))
            {
                record.TotalViolations = 0;
                record.ViolationCounts.Clear();
            }
        }

        #endregion
    }

    #region Supporting Types

    public enum ViolationType
    {
        SpeedHack,
        Teleport,
        MovementAnomaly,
        RangeViolation,
        RateLimit,
        CooldownViolation,
        InvalidQuantity,
        InvalidItem,
        InvalidTarget,
        InvalidInput,
        ChatSpam
    }

    public enum RateLimitType
    {
        General,
        Attack,
        Inventory,
        Chat
    }

    public enum CooldownType
    {
        Attack,
        Ability,
        Interact
    }

    public class TrustValidationResult
    {
        public bool IsValid { get; set; }
        public ViolationType? ViolationType { get; set; }
        public string Reason { get; set; }
        public bool WasClamped { get; set; }
        public float ClampedX { get; set; }
        public float ClampedY { get; set; }
        public float ClampedZ { get; set; }
    }

    public class ViolationRecord
    {
        public string PlayerId { get; set; }
        public int TotalViolations { get; set; }
        public Dictionary<ViolationType, int> ViolationCounts { get; set; } = new();
        public long LastViolation { get; set; }
    }

    /// <summary>
    /// Rate limiter for a single client
    /// </summary>
    internal class ClientRateLimiter
    {
        private readonly Dictionary<RateLimitType, RateLimitBucket> buckets = new();
        private readonly Dictionary<CooldownType, long> cooldowns = new();
        private readonly object lockObj = new object();

        public ClientRateLimiter()
        {
            // Initialize buckets
            buckets[RateLimitType.General] = new RateLimitBucket(TrustBoundary.MAX_MESSAGES_PER_SECOND, 1000);
            buckets[RateLimitType.Attack] = new RateLimitBucket(TrustBoundary.MAX_ATTACKS_PER_SECOND, 1000);
            buckets[RateLimitType.Inventory] = new RateLimitBucket(TrustBoundary.MAX_INVENTORY_CHANGES_PER_SECOND, 1000);
            buckets[RateLimitType.Chat] = new RateLimitBucket(TrustBoundary.MAX_CHAT_MESSAGES_PER_MINUTE, 60000);
        }

        public bool CheckAndIncrement(RateLimitType type)
        {
            lock (lockObj)
            {
                if (buckets.TryGetValue(type, out var bucket))
                {
                    return bucket.TryConsume();
                }
                return true;
            }
        }

        public bool CheckCooldown(CooldownType type, int cooldownMs)
        {
            lock (lockObj)
            {
                if (cooldowns.TryGetValue(type, out var lastTime))
                {
                    long now = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();
                    return (now - lastTime) >= cooldownMs;
                }
                return true;
            }
        }

        public void SetCooldown(CooldownType type)
        {
            lock (lockObj)
            {
                cooldowns[type] = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();
            }
        }
    }

    /// <summary>
    /// Token bucket rate limiter
    /// </summary>
    internal class RateLimitBucket
    {
        private readonly int maxTokens;
        private readonly int refillIntervalMs;
        private int tokens;
        private long lastRefill;

        public RateLimitBucket(int maxTokens, int refillIntervalMs)
        {
            this.maxTokens = maxTokens;
            this.refillIntervalMs = refillIntervalMs;
            this.tokens = maxTokens;
            this.lastRefill = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();
        }

        public bool TryConsume()
        {
            Refill();

            if (tokens > 0)
            {
                tokens--;
                return true;
            }
            return false;
        }

        private void Refill()
        {
            long now = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();
            long elapsed = now - lastRefill;

            if (elapsed >= refillIntervalMs)
            {
                tokens = maxTokens;
                lastRefill = now;
            }
        }
    }

    #endregion
}
