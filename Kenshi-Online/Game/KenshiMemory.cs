using System;

namespace KenshiMultiplayer.Game
{
    /// <summary>
    /// Static memory offset constants for Kenshi game structures.
    /// These are hardcoded fallback values used when online offsets cannot be fetched.
    ///
    /// WARNING: These offsets are for Kenshi v1.0.64 (64-bit).
    /// Different game versions may have different offsets.
    /// Use RuntimeOffsets instead for dynamic offset resolution.
    /// </summary>
    public static class KenshiMemory
    {
        /// <summary>
        /// Base address of Kenshi executable (64-bit default)
        /// </summary>
        public static long BaseAddress { get; set; } = 0x140000000;

        /// <summary>
        /// Core game state offsets
        /// </summary>
        public static class Game
        {
            public const long WorldInstance = 0x24D8F40;
            public const long GameState = 0x24D8F48;
            public const long GameTime = 0x24D8F50;
            public const long GameDay = 0x24D8F58;
        }

        /// <summary>
        /// Character list and management offsets
        /// </summary>
        public static class Characters
        {
            public const long PlayerSquadList = 0x24C5A20;
            public const long PlayerSquadCount = 0x24C5A28;
            public const long AllCharactersList = 0x24C5B00;
            public const long AllCharactersCount = 0x24C5B08;
            public const long SelectedCharacter = 0x24C5A30;
        }

        /// <summary>
        /// Faction system offsets
        /// </summary>
        public static class Factions
        {
            public const long FactionList = 0x24D2100;
            public const long FactionCount = 0x24D2108;
            public const long PlayerFaction = 0x24D2110;
            public const long RelationMatrix = 0x24D2200;
        }

        /// <summary>
        /// World object offsets
        /// </summary>
        public static class World
        {
            public const long BuildingList = 0x24E1000;
            public const long BuildingCount = 0x24E1008;
            public const long WorldItemsList = 0x24E1100;
            public const long WorldItemsCount = 0x24E1108;
            public const long WeatherSystem = 0x24E7000;
        }

        /// <summary>
        /// Engine/rendering offsets
        /// </summary>
        public static class Engine
        {
            public const long PhysicsWorld = 0x24F0000;
            public const long Camera = 0x24E7C20;
            public const long CameraTarget = 0x24E7C38;
            public const long Renderer = 0x24F5000;
        }

        /// <summary>
        /// Input system offsets
        /// </summary>
        public static class Input
        {
            public const long InputHandler = 0x24F2D80;
            public const long CommandQueue = 0x24F2D90;
            public const long SelectedUnits = 0x24F2DA0;
            public const long UIState = 0x24F3000;
        }

        /// <summary>
        /// Function address offsets (relative to base)
        /// </summary>
        public static class Functions
        {
            public const long SpawnCharacter = 0x8B3C80;
            public const long DespawnCharacter = 0x8B4120;
            public const long AddToSquad = 0x8B4500;
            public const long RemoveFromSquad = 0x8B4600;
            public const long AddItemToInventory = 0x9C2100;
            public const long RemoveItemFromInventory = 0x9C2200;
            public const long SetCharacterState = 0x8C1000;
            public const long IssueCommand = 0x8D5000;
            public const long SetFactionRelation = 0x7A2500;
            public const long PathfindRequest = 0x7B1000;
            public const long CombatAttack = 0x8E2000;
        }

        /// <summary>
        /// Character structure field offsets (relative to character pointer)
        /// </summary>
        public static class CharacterOffsets
        {
            public const int VTable = 0x00;
            public const int Name = 0x08;
            public const int CharacterId = 0x10;
            public const int Position = 0x70;      // float X, Y, Z (12 bytes)
            public const int Rotation = 0x7C;      // float X, Y, Z (12 bytes)
            public const int Body = 0xB8;          // Body part pointer
            public const int Health = 0xC0;        // float
            public const int MaxHealth = 0xC4;     // float
            public const int Blood = 0xC8;         // float
            public const int Hunger = 0xD0;        // float
            public const int Inventory = 0xF0;     // pointer to inventory
            public const int Equipment = 0xF8;     // pointer to equipment
            public const int AI = 0x110;           // AI controller pointer
            public const int State = 0x118;        // Current state enum
            public const int AnimState = 0x140;    // Animation state
            public const int Faction = 0x158;      // Faction pointer
            public const int Squad = 0x168;        // Squad pointer
        }

        /// <summary>
        /// Squad structure field offsets
        /// </summary>
        public static class SquadOffsets
        {
            public const int Members = 0x20;
            public const int MemberCount = 0x28;
            public const int Leader = 0x30;
            public const int FactionId = 0x38;
            public const int Name = 0x40;
        }

        /// <summary>
        /// Item structure field offsets
        /// </summary>
        public static class ItemOffsets
        {
            public const int Name = 0x10;
            public const int Category = 0x18;
            public const int Value = 0x20;
            public const int Weight = 0x24;
            public const int StackCount = 0x28;
            public const int Quality = 0x2C;
        }

        /// <summary>
        /// Building structure field offsets
        /// </summary>
        public static class BuildingOffsets
        {
            public const int Position = 0x20;
            public const int Rotation = 0x2C;
            public const int BuildingType = 0x38;
            public const int Health = 0x40;
            public const int OwnerId = 0x48;
            public const int IsComplete = 0x50;
        }

        /// <summary>
        /// Get absolute address from base + offset
        /// </summary>
        public static long GetAbsolute(long offset)
        {
            return BaseAddress + offset;
        }

        /// <summary>
        /// Get absolute address as IntPtr
        /// </summary>
        public static IntPtr GetAbsolutePtr(long offset)
        {
            return new IntPtr(BaseAddress + offset);
        }
    }
}
