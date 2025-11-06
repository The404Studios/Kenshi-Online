using KenshiMultiplayer.Networking;
using KenshiMultiplayer.Managers;
using KenshiMultiplayer.Data;

namespace KenshiMultiplayer.Utility
{
    public static class MessageType
    {
        // Basic types from original implementation
        public const string Position = "position";
        public const string Inventory = "inventory";
        public const string Combat = "combat";
        public const string Health = "health";
        public const string Chat = "chat";
        public const string ChatMessage = "chat"; // Alias for Chat
        public const string Reconnect = "reconnect";
        public const string AdminKick = "adminkick";

        // Authentication & session management
        public const string Login = "login";
        public const string Logout = "logout";
        public const string Register = "register";
        public const string Authentication = "auth";

        // Data synchronization
        public const string WorldState = "worldstate";
        public const string PlayerState = "playerstate";
        public const string EntityUpdate = "entity";
        public const string Batch = "batch";
        public const string Delta = "delta";
        public const string Acknowledgment = "ack";

        // Faction system
        public const string FactionCreate = "faction_create";
        public const string FactionJoin = "faction_join";
        public const string FactionLeave = "faction_leave";
        public const string FactionInvite = "faction_invite";
        public const string FactionRelation = "faction_relation";

        // Friends system
        public const string FriendRequest = "friend_request";
        public const string FriendAccept = "friend_accept";
        public const string FriendDecline = "friend_decline";
        public const string FriendRemove = "friend_remove";
        public const string FriendBlock = "friend_block";
        public const string FriendStatus = "friend_status";

        // Marketplace system
        public const string MarketplaceCreate = "marketplace_create";
        public const string MarketplaceCancel = "marketplace_cancel";
        public const string MarketplacePurchase = "marketplace_purchase";
        public const string MarketplaceUpdate = "marketplace_update";
        public const string MarketplaceQuery = "marketplace_query";

        // Trading system
        public const string TradeRequest = "trade_request";
        public const string TradeAccept = "trade_accept";
        public const string TradeDecline = "trade_decline";
        public const string TradeUpdate = "trade_update";
        public const string TradeCancel = "trade_cancel";
        public const string TradeComplete = "trade_complete";

        // Advanced inventory & trading
        public const string InventoryDetailed = "inventory_detailed";
        public const string Equipment = "equipment";
        public const string TradeOffer = "trade_offer";

        // Advanced combat
        public const string CombatAction = "combat_action";
        public const string Damage = "damage";
        public const string StatusEffect = "status_effect";

        // Quest system
        public const string QuestOffer = "quest_offer";
        public const string QuestAccept = "quest_accept";
        public const string QuestDecline = "quest_decline";
        public const string QuestUpdate = "quest_update";
        public const string QuestComplete = "quest_complete";

        // Base building
        public const string BuildingPlace = "building_place";
        public const string BuildingRemove = "building_remove";
        public const string BuildingUpdate = "building_update";
        public const string BuildingPermission = "building_permission";

        // World events
        public const string WorldEvent = "world_event";
        public const string EventJoin = "event_join";
        public const string EventUpdate = "event_update";

        // System messages
        public const string Error = "error";
        public const string Warning = "warning";
        public const string Notification = "notification";
        public const string SystemMessage = "system";

        // WebSocket communication
        public const string WebSocketConnect = "ws_connect";
        public const string WebSocketDisconnect = "ws_disconnect";
        public const string WebSocketUpdate = "ws_update";

        // Ping/heartbeat for connection validation
        public const string Ping = "ping";
        public const string Pong = "pong";

        // Player spawning
        public const string SpawnRequest = "spawn_request";
        public const string PlayerSpawned = "player_spawned";
        public const string PlayerDespawned = "player_despawned";
        public const string PlayerRespawn = "player_respawn";

        // Group/Friend spawning
        public const string GroupSpawnRequest = "group_spawn_request";
        public const string GroupSpawnCreated = "group_spawn_created";
        public const string GroupSpawnReady = "group_spawn_ready";
        public const string GroupSpawnCompleted = "group_spawn_completed";
        public const string GroupSpawnCancelled = "group_spawn_cancelled";

        // Player join/leave
        public const string PlayerJoined = "player_joined";
        public const string PlayerLeft = "player_left";
        public const string PlayerStateUpdate = "player_state_update";

        // Game commands
        public const string MoveCommand = "move_command";
        public const string AttackCommand = "attack_command";
        public const string FollowCommand = "follow_command";
        public const string PickupCommand = "pickup_command";
        public const string InteractCommand = "interact_command";

        // Squad commands
        public const string SquadCreate = "squad_create";
        public const string SquadDisband = "squad_disband";
        public const string SquadJoin = "squad_join";
        public const string SquadLeave = "squad_leave";
        public const string SquadCommand = "squad_command";
    }
}