namespace KenshiMultiplayer
{
    public static class MessageType
    {
        // Basic types from original implementation
        public const string Position = "position";
        public const string Inventory = "inventory";
        public const string Combat = "combat";
        public const string Health = "health";
        public const string Chat = "chat";
        public const string Reconnect = "reconnect";
        public const string AdminKick = "adminkick";

        // Enhanced message types

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

        // Advanced inventory & trading
        public const string InventoryDetailed = "inventory_detailed";
        public const string Equipment = "equipment";
        public const string TradeOffer = "trade_offer";
        public const string TradeAccept = "trade_accept";
        public const string TradeDecline = "trade_decline";

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

        // Ping/heartbeat for connection validation
        public const string Ping = "ping";
        public const string Pong = "pong";
    }
}