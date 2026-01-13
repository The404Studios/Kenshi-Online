using System;
using System.Collections.Generic;
using System.Collections.Concurrent;
using KenshiMultiplayer.Utility;
using KenshiMultiplayer.Data;

namespace KenshiMultiplayer.Networking.Authority
{
    /// <summary>
    /// Defines which system (server or client) has authority over different game aspects.
    /// Server authority prevents cheating by ensuring critical game state is validated server-side.
    /// </summary>
    public enum AuthorityOwner
    {
        Server,
        Client
    }

    /// <summary>
    /// Game systems that can have authority assigned
    /// </summary>
    public enum GameSystem
    {
        Position,
        Combat,
        Inventory,
        AI,
        Animation,
        Building,
        Trading,
        Quests,
        Faction,
        WorldEvents
    }

    /// <summary>
    /// Authority configuration for the multiplayer system.
    /// Centralizes all authority decisions to prevent inconsistent behavior.
    /// </summary>
    public static class AuthorityConfig
    {
        /// <summary>
        /// Authority mapping - defines who owns each system.
        /// Server authority is required for anything that affects game balance or can be exploited.
        /// </summary>
        private static readonly Dictionary<GameSystem, AuthorityOwner> AuthorityMap = new()
        {
            // Server-authoritative systems (anti-cheat critical)
            { GameSystem.Position, AuthorityOwner.Server },      // Prevents speed hacks, teleportation
            { GameSystem.Combat, AuthorityOwner.Server },        // Prevents damage hacks, invincibility
            { GameSystem.Inventory, AuthorityOwner.Server },     // Prevents item duplication, spawning
            { GameSystem.AI, AuthorityOwner.Server },            // NPC behavior must be consistent
            { GameSystem.Trading, AuthorityOwner.Server },       // Prevents trade exploits
            { GameSystem.Building, AuthorityOwner.Server },      // Prevents illegal placements
            { GameSystem.Quests, AuthorityOwner.Server },        // Prevents quest manipulation
            { GameSystem.Faction, AuthorityOwner.Server },       // Prevents faction exploits
            { GameSystem.WorldEvents, AuthorityOwner.Server },   // Server controls world state

            // Client-authoritative systems (cosmetic/input only)
            { GameSystem.Animation, AuthorityOwner.Client }      // Animation is cosmetic, client-predicted
        };

        /// <summary>
        /// Get the authority owner for a specific game system
        /// </summary>
        public static AuthorityOwner GetAuthority(GameSystem system)
        {
            return AuthorityMap.TryGetValue(system, out var owner) ? owner : AuthorityOwner.Server;
        }

        /// <summary>
        /// Check if server has authority over a system
        /// </summary>
        public static bool IsServerAuthoritative(GameSystem system)
        {
            return GetAuthority(system) == AuthorityOwner.Server;
        }

        /// <summary>
        /// Check if client has authority over a system
        /// </summary>
        public static bool IsClientAuthoritative(GameSystem system)
        {
            return GetAuthority(system) == AuthorityOwner.Client;
        }
    }

    /// <summary>
    /// Entity ownership tracking - determines which player/server owns an entity
    /// </summary>
    public class EntityOwnership
    {
        public string EntityId { get; set; }
        public string OwnerId { get; set; }  // PlayerId or "SERVER"
        public EntityType Type { get; set; }
        public long OwnershipAcquiredAt { get; set; }
        public bool IsServerOwned => OwnerId == "SERVER";
    }

    public enum EntityType
    {
        Player,
        NPC,
        Building,
        Item,
        Vehicle
    }

    /// <summary>
    /// Manages entity ownership and authority validation for the multiplayer system.
    /// Ensures only authorized clients can modify entities they own.
    /// </summary>
    public class AuthorityManager
    {
        private readonly ConcurrentDictionary<string, EntityOwnership> entityOwnership = new();
        private readonly ConcurrentDictionary<string, HashSet<string>> playerOwnedEntities = new();

        public const string SERVER_OWNER_ID = "SERVER";

        /// <summary>
        /// Register a new entity with ownership
        /// </summary>
        public void RegisterEntity(string entityId, string ownerId, EntityType type)
        {
            var ownership = new EntityOwnership
            {
                EntityId = entityId,
                OwnerId = ownerId,
                Type = type,
                OwnershipAcquiredAt = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds()
            };

            entityOwnership[entityId] = ownership;

            // Track player-owned entities
            if (ownerId != SERVER_OWNER_ID)
            {
                playerOwnedEntities.AddOrUpdate(
                    ownerId,
                    new HashSet<string> { entityId },
                    (_, existing) => { existing.Add(entityId); return existing; }
                );
            }
        }

        /// <summary>
        /// Register a player's character entity
        /// </summary>
        public void RegisterPlayerEntity(string playerId)
        {
            RegisterEntity(playerId, playerId, EntityType.Player);
        }

        /// <summary>
        /// Register an NPC as server-owned
        /// </summary>
        public void RegisterNPC(string npcId)
        {
            RegisterEntity(npcId, SERVER_OWNER_ID, EntityType.NPC);
        }

        /// <summary>
        /// Transfer ownership of an entity
        /// </summary>
        public bool TransferOwnership(string entityId, string newOwnerId, string requesterId)
        {
            if (!entityOwnership.TryGetValue(entityId, out var ownership))
                return false;

            // Only server or current owner can transfer
            if (requesterId != SERVER_OWNER_ID && requesterId != ownership.OwnerId)
                return false;

            // Remove from old owner's list
            if (ownership.OwnerId != SERVER_OWNER_ID &&
                playerOwnedEntities.TryGetValue(ownership.OwnerId, out var oldOwnerEntities))
            {
                oldOwnerEntities.Remove(entityId);
            }

            // Update ownership
            ownership.OwnerId = newOwnerId;
            ownership.OwnershipAcquiredAt = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();

            // Add to new owner's list
            if (newOwnerId != SERVER_OWNER_ID)
            {
                playerOwnedEntities.AddOrUpdate(
                    newOwnerId,
                    new HashSet<string> { entityId },
                    (_, existing) => { existing.Add(entityId); return existing; }
                );
            }

            return true;
        }

        /// <summary>
        /// Check if a player can modify an entity
        /// </summary>
        public bool CanModify(string playerId, string entityId, GameSystem system)
        {
            // Server can always modify
            if (playerId == SERVER_OWNER_ID)
                return true;

            // Check if system is server-authoritative
            if (AuthorityConfig.IsServerAuthoritative(system))
            {
                // For server-authoritative systems, client sends requests, server validates
                // Client can only REQUEST modifications, not directly apply them
                return true; // Request is allowed, but will be validated
            }

            // For client-authoritative systems, check ownership
            if (!entityOwnership.TryGetValue(entityId, out var ownership))
                return false;

            return ownership.OwnerId == playerId;
        }

        /// <summary>
        /// Validate a client action request for server-authoritative systems
        /// </summary>
        public AuthorityValidationResult ValidateAction(string playerId, string entityId, GameSystem system, object actionData)
        {
            var result = new AuthorityValidationResult { IsValid = false };

            // Check entity exists
            if (!entityOwnership.TryGetValue(entityId, out var ownership))
            {
                result.RejectionReason = "Entity not found";
                return result;
            }

            // For player entities, ensure player owns their character
            if (ownership.Type == EntityType.Player && ownership.OwnerId != playerId)
            {
                result.RejectionReason = "Cannot control another player's character";
                return result;
            }

            // System-specific validation
            result = ValidateSystemAction(playerId, entityId, system, actionData, ownership);

            return result;
        }

        private AuthorityValidationResult ValidateSystemAction(string playerId, string entityId,
            GameSystem system, object actionData, EntityOwnership ownership)
        {
            var result = new AuthorityValidationResult { IsValid = true };

            switch (system)
            {
                case GameSystem.Position:
                    result = ValidatePositionAction(playerId, entityId, actionData);
                    break;

                case GameSystem.Combat:
                    result = ValidateCombatAction(playerId, entityId, actionData);
                    break;

                case GameSystem.Inventory:
                    result = ValidateInventoryAction(playerId, entityId, actionData);
                    break;

                case GameSystem.Trading:
                    result = ValidateTradingAction(playerId, entityId, actionData);
                    break;

                case GameSystem.Building:
                    result = ValidateBuildingAction(playerId, entityId, actionData);
                    break;

                default:
                    // Default: allow if player owns entity
                    result.IsValid = ownership.OwnerId == playerId || ownership.OwnerId == SERVER_OWNER_ID;
                    break;
            }

            return result;
        }

        private AuthorityValidationResult ValidatePositionAction(string playerId, string entityId, object actionData)
        {
            var result = new AuthorityValidationResult { IsValid = true };

            if (actionData is PositionUpdateRequest posRequest)
            {
                // Validate movement speed (anti-speedhack)
                const float MAX_SPEED = 15.0f; // Maximum units per second
                const float MAX_TELEPORT_DISTANCE = 20.0f;

                float distance = posRequest.Distance;
                float timeDelta = posRequest.TimeDeltaSeconds;

                if (timeDelta > 0)
                {
                    float speed = distance / timeDelta;
                    if (speed > MAX_SPEED)
                    {
                        result.IsValid = false;
                        result.RejectionReason = $"Speed violation: {speed:F2} > {MAX_SPEED}";
                        return result;
                    }
                }

                // Check for teleportation
                if (distance > MAX_TELEPORT_DISTANCE)
                {
                    result.IsValid = false;
                    result.RejectionReason = $"Teleport violation: {distance:F2} > {MAX_TELEPORT_DISTANCE}";
                    return result;
                }
            }

            return result;
        }

        private AuthorityValidationResult ValidateCombatAction(string playerId, string entityId, object actionData)
        {
            var result = new AuthorityValidationResult { IsValid = true };

            if (actionData is CombatActionRequest combatRequest)
            {
                // Validate attack cooldowns
                // Validate damage values
                // Validate target is in range
                // These checks are performed by CombatSynchronizer
            }

            return result;
        }

        private AuthorityValidationResult ValidateInventoryAction(string playerId, string entityId, object actionData)
        {
            var result = new AuthorityValidationResult { IsValid = true };

            // Inventory changes must be validated by server
            // - Item pickup: Server validates item exists and is in range
            // - Item drop: Server validates player has item
            // - Item transfer: Server validates both parties agree

            return result;
        }

        private AuthorityValidationResult ValidateTradingAction(string playerId, string entityId, object actionData)
        {
            var result = new AuthorityValidationResult { IsValid = true };
            // Trading validation handled by trading system
            return result;
        }

        private AuthorityValidationResult ValidateBuildingAction(string playerId, string entityId, object actionData)
        {
            var result = new AuthorityValidationResult { IsValid = true };
            // Building placement validation
            return result;
        }

        /// <summary>
        /// Get all entities owned by a player
        /// </summary>
        public IEnumerable<string> GetPlayerEntities(string playerId)
        {
            if (playerOwnedEntities.TryGetValue(playerId, out var entities))
                return entities;
            return Array.Empty<string>();
        }

        /// <summary>
        /// Remove a player and their ownership records
        /// </summary>
        public void RemovePlayer(string playerId)
        {
            if (playerOwnedEntities.TryRemove(playerId, out var entities))
            {
                foreach (var entityId in entities)
                {
                    // Transfer to server or remove
                    entityOwnership.TryRemove(entityId, out _);
                }
            }
        }

        /// <summary>
        /// Get ownership info for an entity
        /// </summary>
        public EntityOwnership GetOwnership(string entityId)
        {
            entityOwnership.TryGetValue(entityId, out var ownership);
            return ownership;
        }
    }

    /// <summary>
    /// Result of authority validation
    /// </summary>
    public class AuthorityValidationResult
    {
        public bool IsValid { get; set; }
        public string RejectionReason { get; set; }
        public Dictionary<string, object> CorrectedData { get; set; }
    }

    /// <summary>
    /// Request types for validation
    /// </summary>
    public class PositionUpdateRequest
    {
        public float X { get; set; }
        public float Y { get; set; }
        public float Z { get; set; }
        public float Distance { get; set; }
        public float TimeDeltaSeconds { get; set; }
    }

    public class CombatActionRequest
    {
        public string AttackerId { get; set; }
        public string TargetId { get; set; }
        public string ActionType { get; set; }
        public string WeaponId { get; set; }
    }
}
