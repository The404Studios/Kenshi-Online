using System;
using System.Runtime.InteropServices;
using System.Text;

namespace KenshiMultiplayer.Game
{
    /// <summary>
    /// C# wrapper for the embedded Server API in KenshiOnlineMod.dll
    /// Provides P/Invoke bindings to call native C++ functions
    /// </summary>
    public static class ServerAPIWrapper
    {
        private const string DLL_NAME = "KenshiOnlineMod.dll";

        // ===== Initialization =====

        [DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl)]
        [return: MarshalAs(UnmanagedType.I1)]
        public static extern bool ServerAPI_Initialize();

        [DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl)]
        public static extern void ServerAPI_Shutdown();

        [DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl)]
        [return: MarshalAs(UnmanagedType.I1)]
        public static extern bool ServerAPI_IsInitialized();

        // ===== Player Management =====

        [DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        [return: MarshalAs(UnmanagedType.I1)]
        public static extern bool ServerAPI_SpawnPlayer(
            string playerId,
            string characterName,
            float x, float y, float z,
            int factionID);

        [DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        [return: MarshalAs(UnmanagedType.I1)]
        public static extern bool ServerAPI_DespawnPlayer(string playerId);

        [DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        [return: MarshalAs(UnmanagedType.I1)]
        public static extern bool ServerAPI_SetPlayerPosition(
            string playerId,
            float x, float y, float z);

        [DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        [return: MarshalAs(UnmanagedType.I1)]
        public static extern bool ServerAPI_GetPlayerPosition(
            string playerId,
            out float x, out float y, out float z);

        [DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        [return: MarshalAs(UnmanagedType.I1)]
        public static extern bool ServerAPI_SetPlayerHealth(string playerId, float health);

        [DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        public static extern float ServerAPI_GetPlayerHealth(string playerId);

        // ===== Animation Control =====

        [DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        [return: MarshalAs(UnmanagedType.I1)]
        public static extern bool ServerAPI_PlayAnimation(
            string playerId,
            string animName,
            [MarshalAs(UnmanagedType.I1)] bool loop);

        [DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        [return: MarshalAs(UnmanagedType.I1)]
        public static extern bool ServerAPI_StopAnimation(string playerId, string animName);

        [DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        [return: MarshalAs(UnmanagedType.I1)]
        public static extern bool ServerAPI_BlendToAnimation(
            string playerId,
            string animName,
            float blendTime);

        [DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        [return: MarshalAs(UnmanagedType.I1)]
        public static extern bool ServerAPI_SetAnimationSpeed(
            string playerId,
            string animName,
            float speed);

        // ===== Character State =====

        [DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        [return: MarshalAs(UnmanagedType.I1)]
        public static extern bool ServerAPI_SetCharacterState(string playerId, int stateType);

        [DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        public static extern int ServerAPI_GetCharacterState(string playerId);

        [DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        [return: MarshalAs(UnmanagedType.I1)]
        public static extern bool ServerAPI_SetCombatMode(
            string playerId,
            [MarshalAs(UnmanagedType.I1)] bool inCombat);

        [DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        [return: MarshalAs(UnmanagedType.I1)]
        public static extern bool ServerAPI_IsInCombat(string playerId);

        // ===== Faction Management =====

        [DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        [return: MarshalAs(UnmanagedType.I1)]
        public static extern bool ServerAPI_SetPlayerFaction(string playerId, int factionID);

        [DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        public static extern int ServerAPI_GetPlayerFaction(string playerId);

        [DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl)]
        [return: MarshalAs(UnmanagedType.I1)]
        public static extern bool ServerAPI_SetFactionRelation(
            int factionID1,
            int factionID2,
            int relationValue);

        [DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl)]
        public static extern int ServerAPI_GetFactionRelation(int factionID1, int factionID2);

        [DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl)]
        public static extern int ServerAPI_GetFactionCount();

        [DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl)]
        [return: MarshalAs(UnmanagedType.I1)]
        public static extern bool ServerAPI_GetFactionInfo(
            int index,
            out int factionID,
            StringBuilder nameBuffer,
            int bufferSize);

        // ===== Squad Management =====

        [DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        [return: MarshalAs(UnmanagedType.I1)]
        public static extern bool ServerAPI_CreateSquad(string squadName, string leaderId);

        [DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl)]
        [return: MarshalAs(UnmanagedType.I1)]
        public static extern bool ServerAPI_DisbandSquad(int squadID);

        [DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        [return: MarshalAs(UnmanagedType.I1)]
        public static extern bool ServerAPI_AddToSquad(int squadID, string playerId);

        [DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        [return: MarshalAs(UnmanagedType.I1)]
        public static extern bool ServerAPI_RemoveFromSquad(int squadID, string playerId);

        [DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl)]
        [return: MarshalAs(UnmanagedType.I1)]
        public static extern bool ServerAPI_IssueSquadCommand(
            int squadID,
            int commandType,
            float x, float y, float z);

        [DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl)]
        public static extern int ServerAPI_GetSquadCount();

        // ===== Building Management =====

        [DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl)]
        [return: MarshalAs(UnmanagedType.I1)]
        public static extern bool ServerAPI_PlaceBuilding(
            int buildingType,
            float x, float y, float z,
            float rotX, float rotY, float rotZ,
            int ownerFactionID);

        [DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl)]
        [return: MarshalAs(UnmanagedType.I1)]
        public static extern bool ServerAPI_RemoveBuilding(int buildingID);

        [DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl)]
        [return: MarshalAs(UnmanagedType.I1)]
        public static extern bool ServerAPI_SetBuildingHealth(int buildingID, float health);

        [DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl)]
        public static extern float ServerAPI_GetBuildingHealth(int buildingID);

        [DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl)]
        public static extern int ServerAPI_GetBuildingCount();

        // ===== Network =====

        [DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        [return: MarshalAs(UnmanagedType.I1)]
        public static extern bool ServerAPI_BroadcastMessage(string message);

        [DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        [return: MarshalAs(UnmanagedType.I1)]
        public static extern bool ServerAPI_SendMessageToPlayer(string playerId, string message);

        [DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl)]
        public static extern int ServerAPI_GetConnectedPlayerCount();

        // ===== Callbacks =====

        [UnmanagedFunctionPointer(CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        public delegate void OnPlayerJoinCallback(string playerId);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        public delegate void OnPlayerLeaveCallback(string playerId);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        public delegate void OnPlayerActionCallback(string playerId, int actionType, string data);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        public delegate void OnCombatEventCallback(string attacker, string target, int damageType, float damage);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        public delegate void OnChatMessageCallback(string playerId, string message);

        [DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl)]
        public static extern void ServerAPI_SetOnPlayerJoinCallback(OnPlayerJoinCallback callback);

        [DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl)]
        public static extern void ServerAPI_SetOnPlayerLeaveCallback(OnPlayerLeaveCallback callback);

        [DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl)]
        public static extern void ServerAPI_SetOnPlayerActionCallback(OnPlayerActionCallback callback);

        [DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl)]
        public static extern void ServerAPI_SetOnCombatEventCallback(OnCombatEventCallback callback);

        [DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl)]
        public static extern void ServerAPI_SetOnChatMessageCallback(OnChatMessageCallback callback);

        // ===== Update =====

        [DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl)]
        public static extern void ServerAPI_Update(float deltaTime);

        // ===== Statistics =====

        [DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl)]
        public static extern float ServerAPI_GetServerTime();

        [DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl)]
        public static extern int ServerAPI_GetFrameRate();

        [DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl)]
        public static extern int ServerAPI_GetTotalEntities();
    }
}
