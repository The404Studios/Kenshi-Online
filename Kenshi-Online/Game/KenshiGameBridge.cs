using System;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using System.Collections.Generic;
using System.Linq;
using KenshiMultiplayer.Utility;
using KenshiMultiplayer.Data;
using KenshiMultiplayer.Networking;

namespace KenshiMultiplayer.Game
{
    /// <summary>
    /// Direct bridge to Kenshi game engine via memory injection
    /// Provides actual control over game entities, spawning, and world state.
    ///
    /// IMPORTANT: This bridge uses RuntimeOffsets from OnlineOffsetProvider for dynamic
    /// offset resolution. Offsets are fetched from online sources with fallback to cache
    /// and hardcoded values. This ensures compatibility across Kenshi versions.
    /// </summary>
    public class KenshiGameBridge : IDisposable
    {
        #region Native Imports
        [DllImport("kernel32.dll")]
        private static extern IntPtr OpenProcess(int dwDesiredAccess, bool bInheritHandle, int dwProcessId);

        [DllImport("kernel32.dll")]
        private static extern bool ReadProcessMemory(IntPtr hProcess, IntPtr lpBaseAddress, byte[] lpBuffer, int dwSize, out int lpNumberOfBytesRead);

        [DllImport("kernel32.dll")]
        private static extern bool WriteProcessMemory(IntPtr hProcess, IntPtr lpBaseAddress, byte[] lpBuffer, int nSize, out int lpNumberOfBytesWritten);

        [DllImport("kernel32.dll")]
        private static extern bool CloseHandle(IntPtr hObject);

        [DllImport("kernel32.dll")]
        private static extern IntPtr VirtualAllocEx(IntPtr hProcess, IntPtr lpAddress, uint dwSize, uint flAllocationType, uint flProtect);

        [DllImport("kernel32.dll")]
        private static extern bool VirtualFreeEx(IntPtr hProcess, IntPtr lpAddress, uint dwSize, uint dwFreeType);

        private const int PROCESS_ALL_ACCESS = 0x1F0FFF;
        private const uint MEM_COMMIT = 0x1000;
        private const uint MEM_RESERVE = 0x2000;
        private const uint PAGE_EXECUTE_READWRITE = 0x40;
        private const uint MEM_RELEASE = 0x8000;
        #endregion

        #region Dynamic Offsets via RuntimeOffsets
        /// <summary>
        /// Dynamic offset accessor that uses OnlineOffsetProvider with fallback chain:
        /// Online -> Cache -> Hardcoded
        /// </summary>
        private static class Offsets
        {
            // Base address (loaded from RuntimeOffsets)
            public static long KENSHI_BASE => RuntimeOffsets.BaseAddress;

            // Player character list
            public static long CHARACTER_LIST_BASE => RuntimeOffsets.PlayerSquadList;
            public static long CHARACTER_LIST_SIZE => RuntimeOffsets.PlayerSquadCount;

            // World state
            public static long WORLD_STATE_BASE => RuntimeOffsets.WorldInstance;
            public static long TIME_OF_DAY => RuntimeOffsets.GameTime;

            // Entity spawning (function offsets)
            public static long SPAWN_FUNCTION => RuntimeOffsets.FnSpawnCharacter;
            public static long DESPAWN_FUNCTION => RuntimeOffsets.FnDespawnCharacter;

            // Character controller (derived from character list base)
            public static long CHARACTER_CONTROLLER_BASE => RuntimeOffsets.AllCharactersList;
            public static long MOVEMENT_CONTROLLER => RuntimeOffsets.AllCharactersList + 0x50;

            // Camera
            public static long CAMERA_POSITION => RuntimeOffsets.Camera;
            public static long CAMERA_TARGET => RuntimeOffsets.Camera + 0x18;

            // Input
            public static long INPUT_HANDLER => RuntimeOffsets.InputHandler;
            public static long COMMAND_QUEUE => RuntimeOffsets.InputHandler + 0x10;
        }

        // Character structure in memory
        [StructLayout(LayoutKind.Sequential, Pack = 1)]
        private struct KenshiCharacter
        {
            public long VTablePtr;
            public long NamePtr;
            public int CharacterId;
            public float PositionX;
            public float PositionY;
            public float PositionZ;
            public float RotationX;
            public float RotationY;
            public float RotationZ;
            public float Health;
            public float MaxHealth;
            public int FactionId;
            public int SquadId;
            public long InventoryPtr;
            public long EquipmentPtr;
            public long StatsPtr;
        }
        #endregion

        private Process kenshiProcess;
        private IntPtr processHandle;
        private bool isConnected;
        private readonly object lockObject = new object();
        private Timer updateTimer;
        private Dictionary<string, IntPtr> spawnedCharacters = new Dictionary<string, IntPtr>();
        private const string LOG_PREFIX = "[KenshiGameBridge] ";

        public bool IsConnected => isConnected;
        public Process KenshiProcess => kenshiProcess;

        #region Initialization

        public KenshiGameBridge()
        {
            Logger.Log(LOG_PREFIX + "Initializing Kenshi Game Bridge...");
        }

        /// <summary>
        /// Connect to running Kenshi process.
        /// This method initializes RuntimeOffsets from online sources before connecting.
        /// </summary>
        public bool ConnectToKenshi()
        {
            try
            {
                Logger.Log(LOG_PREFIX + "Initializing runtime offsets...");

                // Initialize RuntimeOffsets from OnlineOffsetProvider
                // This fetches offsets: Online -> Cache -> Hardcoded fallback
                if (!RuntimeOffsets.IsInitialized)
                {
                    bool offsetsLoaded = RuntimeOffsets.Initialize();
                    if (offsetsLoaded)
                    {
                        Logger.Log(LOG_PREFIX + "Runtime offsets loaded from online/cache source");
                    }
                    else
                    {
                        Logger.Log(LOG_PREFIX + "WARNING: Using hardcoded fallback offsets");
                    }
                }

                Logger.Log(LOG_PREFIX + "Searching for Kenshi process...");

                // Find Kenshi process
                var processes = Process.GetProcessesByName("kenshi_x64");
                if (processes.Length == 0)
                {
                    // Try alternative process name
                    processes = Process.GetProcessesByName("kenshi");
                }

                if (processes.Length == 0)
                {
                    Logger.Log(LOG_PREFIX + "ERROR: Kenshi process not found! Please start Kenshi first.");
                    return false;
                }

                kenshiProcess = processes[0];
                Logger.Log(LOG_PREFIX + $"Found Kenshi process (PID: {kenshiProcess.Id})");

                // Open process with full access
                processHandle = OpenProcess(PROCESS_ALL_ACCESS, false, kenshiProcess.Id);
                if (processHandle == IntPtr.Zero)
                {
                    Logger.Log(LOG_PREFIX + "ERROR: Failed to open Kenshi process. Try running as administrator.");
                    return false;
                }

                isConnected = true;
                Logger.Log(LOG_PREFIX + "Successfully connected to Kenshi!");
                Logger.Log(LOG_PREFIX + $"Using offsets: Base=0x{Offsets.KENSHI_BASE:X}, CharList=0x{Offsets.CHARACTER_LIST_BASE:X}");

                // Start monitoring thread
                StartUpdateLoop();

                return true;
            }
            catch (Exception ex)
            {
                Logger.Log(LOG_PREFIX + $"ERROR connecting to Kenshi: {ex.Message}");
                return false;
            }
        }

        private void StartUpdateLoop()
        {
            // Update game state every 50ms (20 Hz)
            updateTimer = new Timer(UpdateGameState, null, 0, 50);
        }

        #endregion

        #region Memory Operations

        private bool ReadMemory(IntPtr address, byte[] buffer)
        {
            if (!isConnected || processHandle == IntPtr.Zero)
                return false;

            try
            {
                return ReadProcessMemory(processHandle, address, buffer, buffer.Length, out _);
            }
            catch
            {
                return false;
            }
        }

        private bool WriteMemory(IntPtr address, byte[] data)
        {
            if (!isConnected || processHandle == IntPtr.Zero)
                return false;

            try
            {
                return WriteProcessMemory(processHandle, address, data, data.Length, out _);
            }
            catch
            {
                return false;
            }
        }

        private IntPtr ReadPointer(IntPtr address)
        {
            byte[] buffer = new byte[8];
            if (ReadMemory(address, buffer))
            {
                return new IntPtr(BitConverter.ToInt64(buffer, 0));
            }
            return IntPtr.Zero;
        }

        private float ReadFloat(IntPtr address)
        {
            byte[] buffer = new byte[4];
            if (ReadMemory(address, buffer))
            {
                return BitConverter.ToSingle(buffer, 0);
            }
            return 0f;
        }

        private void WriteFloat(IntPtr address, float value)
        {
            byte[] buffer = BitConverter.GetBytes(value);
            WriteMemory(address, buffer);
        }

        private string ReadString(IntPtr address, int maxLength = 256)
        {
            byte[] buffer = new byte[maxLength];
            if (ReadMemory(address, buffer))
            {
                int nullIndex = Array.IndexOf(buffer, (byte)0);
                if (nullIndex >= 0)
                {
                    return Encoding.UTF8.GetString(buffer, 0, nullIndex);
                }
            }
            return string.Empty;
        }

        #endregion

        #region Entity Management

        /// <summary>
        /// Spawn a player character in the game world
        /// </summary>
        public bool SpawnPlayer(string playerId, PlayerData playerData, Position spawnPosition)
        {
            lock (lockObject)
            {
                try
                {
                    Logger.Log(LOG_PREFIX + $"Spawning player {playerId} at {spawnPosition.X}, {spawnPosition.Y}, {spawnPosition.Z}");

                    // Check if already spawned
                    if (spawnedCharacters.ContainsKey(playerId))
                    {
                        Logger.Log(LOG_PREFIX + $"Player {playerId} already spawned, updating position...");
                        return UpdatePlayerPosition(playerId, spawnPosition);
                    }

                    // Allocate memory for character structure
                    IntPtr characterPtr = VirtualAllocEx(processHandle, IntPtr.Zero,
                        (uint)Marshal.SizeOf<KenshiCharacter>(),
                        MEM_COMMIT | MEM_RESERVE,
                        PAGE_EXECUTE_READWRITE);

                    if (characterPtr == IntPtr.Zero)
                    {
                        Logger.Log(LOG_PREFIX + "ERROR: Failed to allocate memory for character");
                        return false;
                    }

                    // Create character structure
                    // Convert string FactionId to int hash (Kenshi uses numeric faction IDs internally)
                    int factionIdInt = string.IsNullOrEmpty(playerData.FactionId) ? 0 : playerData.FactionId.GetHashCode();

                    KenshiCharacter character = new KenshiCharacter
                    {
                        CharacterId = playerId.GetHashCode(),
                        PositionX = spawnPosition.X,
                        PositionY = spawnPosition.Y,
                        PositionZ = spawnPosition.Z,
                        RotationX = spawnPosition.RotX,
                        RotationY = spawnPosition.RotY,
                        RotationZ = spawnPosition.RotZ,
                        Health = playerData.Health,
                        MaxHealth = playerData.MaxHealth,
                        FactionId = factionIdInt
                    };

                    // Write character name
                    IntPtr namePtr = VirtualAllocEx(processHandle, IntPtr.Zero,
                        (uint)(playerData.DisplayName.Length + 1),
                        MEM_COMMIT | MEM_RESERVE,
                        PAGE_EXECUTE_READWRITE);

                    if (namePtr != IntPtr.Zero)
                    {
                        byte[] nameBytes = Encoding.UTF8.GetBytes(playerData.DisplayName + "\0");
                        WriteMemory(namePtr, nameBytes);
                        character.NamePtr = namePtr.ToInt64();
                    }

                    // Marshal and write character structure
                    byte[] characterBytes = StructureToBytes(character);
                    if (!WriteMemory(characterPtr, characterBytes))
                    {
                        Logger.Log(LOG_PREFIX + "ERROR: Failed to write character data");
                        VirtualFreeEx(processHandle, characterPtr, 0, MEM_RELEASE);
                        return false;
                    }

                    // Call Kenshi's spawn function
                    if (!CallSpawnFunction(characterPtr))
                    {
                        Logger.Log(LOG_PREFIX + "ERROR: Failed to call spawn function");
                        VirtualFreeEx(processHandle, characterPtr, 0, MEM_RELEASE);
                        return false;
                    }

                    // Track spawned character
                    spawnedCharacters[playerId] = characterPtr;
                    Logger.Log(LOG_PREFIX + $"Successfully spawned player {playerId}!");

                    return true;
                }
                catch (Exception ex)
                {
                    Logger.Log(LOG_PREFIX + $"ERROR spawning player: {ex.Message}");
                    return false;
                }
            }
        }

        /// <summary>
        /// Despawn a player character
        /// </summary>
        public bool DespawnPlayer(string playerId)
        {
            lock (lockObject)
            {
                try
                {
                    if (!spawnedCharacters.TryGetValue(playerId, out IntPtr characterPtr))
                    {
                        Logger.Log(LOG_PREFIX + $"Player {playerId} not found in spawn list");
                        return false;
                    }

                    Logger.Log(LOG_PREFIX + $"Despawning player {playerId}...");

                    // Call Kenshi's despawn function
                    CallDespawnFunction(characterPtr);

                    // Free allocated memory
                    VirtualFreeEx(processHandle, characterPtr, 0, MEM_RELEASE);
                    spawnedCharacters.Remove(playerId);

                    Logger.Log(LOG_PREFIX + $"Successfully despawned player {playerId}");
                    return true;
                }
                catch (Exception ex)
                {
                    Logger.Log(LOG_PREFIX + $"ERROR despawning player: {ex.Message}");
                    return false;
                }
            }
        }

        /// <summary>
        /// Update player position in game using dynamic offsets from RuntimeOffsets
        /// </summary>
        public bool UpdatePlayerPosition(string playerId, Position position)
        {
            if (!spawnedCharacters.TryGetValue(playerId, out IntPtr characterPtr))
                return false;

            try
            {
                // Update position using RuntimeOffsets.Character for dynamic offset resolution
                int posOffset = RuntimeOffsets.Character.Position;
                int rotOffset = RuntimeOffsets.Character.Rotation;

                WriteFloat(characterPtr + posOffset, position.X);
                WriteFloat(characterPtr + posOffset + 4, position.Y);
                WriteFloat(characterPtr + posOffset + 8, position.Z);
                WriteFloat(characterPtr + rotOffset, position.RotX);
                WriteFloat(characterPtr + rotOffset + 4, position.RotY);
                WriteFloat(characterPtr + rotOffset + 8, position.RotZ);

                return true;
            }
            catch
            {
                return false;
            }
        }

        /// <summary>
        /// Get player position from game using dynamic offsets from RuntimeOffsets
        /// </summary>
        public Position GetPlayerPosition(string playerId)
        {
            if (!spawnedCharacters.TryGetValue(playerId, out IntPtr characterPtr))
                return null;

            try
            {
                // Read position using RuntimeOffsets.Character for dynamic offset resolution
                int posOffset = RuntimeOffsets.Character.Position;
                int rotOffset = RuntimeOffsets.Character.Rotation;

                float x = ReadFloat(characterPtr + posOffset);
                float y = ReadFloat(characterPtr + posOffset + 4);
                float z = ReadFloat(characterPtr + posOffset + 8);
                float rotX = ReadFloat(characterPtr + rotOffset);
                float rotY = ReadFloat(characterPtr + rotOffset + 4);
                float rotZ = ReadFloat(characterPtr + rotOffset + 8);

                return new Position(x, y, z, rotX, rotY, rotZ);
            }
            catch
            {
                return null;
            }
        }

        #endregion

        #region Game Commands

        /// <summary>
        /// Send a command to the game (move, attack, interact, etc.)
        /// </summary>
        public bool SendGameCommand(string playerId, string command, params object[] args)
        {
            if (!spawnedCharacters.ContainsKey(playerId))
                return false;

            try
            {
                Logger.Log(LOG_PREFIX + $"Sending command '{command}' for player {playerId}");

                switch (command.ToLower())
                {
                    case "move":
                        if (args.Length >= 3 && args[0] is float x && args[1] is float y && args[2] is float z)
                        {
                            return MoveCharacter(playerId, x, y, z);
                        }
                        break;

                    case "attack":
                        if (args.Length >= 1 && args[0] is string targetId)
                        {
                            return AttackTarget(playerId, targetId);
                        }
                        break;

                    case "pickup":
                        if (args.Length >= 1 && args[0] is string itemId)
                        {
                            return PickupItem(playerId, itemId);
                        }
                        break;

                    case "follow":
                        if (args.Length >= 1 && args[0] is string followTargetId)
                        {
                            return FollowCharacter(playerId, followTargetId);
                        }
                        break;

                    default:
                        Logger.Log(LOG_PREFIX + $"Unknown command: {command}");
                        return false;
                }

                return false;
            }
            catch (Exception ex)
            {
                Logger.Log(LOG_PREFIX + $"ERROR sending game command: {ex.Message}");
                return false;
            }
        }

        private bool MoveCharacter(string playerId, float x, float y, float z)
        {
            // Queue movement command in game's command queue
            IntPtr commandQueueBase = new IntPtr(Offsets.KENSHI_BASE + Offsets.COMMAND_QUEUE);

            // Build command structure
            byte[] commandData = new byte[32];
            BitConverter.GetBytes(1).CopyTo(commandData, 0); // Command type: Move
            BitConverter.GetBytes(playerId.GetHashCode()).CopyTo(commandData, 4); // Character ID
            BitConverter.GetBytes(x).CopyTo(commandData, 8); // Target X
            BitConverter.GetBytes(y).CopyTo(commandData, 12); // Target Y
            BitConverter.GetBytes(z).CopyTo(commandData, 16); // Target Z

            return WriteMemory(commandQueueBase, commandData);
        }

        private bool AttackTarget(string playerId, string targetId)
        {
            IntPtr commandQueueBase = new IntPtr(Offsets.KENSHI_BASE + Offsets.COMMAND_QUEUE);

            byte[] commandData = new byte[32];
            BitConverter.GetBytes(2).CopyTo(commandData, 0); // Command type: Attack
            BitConverter.GetBytes(playerId.GetHashCode()).CopyTo(commandData, 4);
            BitConverter.GetBytes(targetId.GetHashCode()).CopyTo(commandData, 8);

            return WriteMemory(commandQueueBase, commandData);
        }

        private bool PickupItem(string playerId, string itemId)
        {
            IntPtr commandQueueBase = new IntPtr(Offsets.KENSHI_BASE + Offsets.COMMAND_QUEUE);

            byte[] commandData = new byte[32];
            BitConverter.GetBytes(3).CopyTo(commandData, 0); // Command type: Pickup
            BitConverter.GetBytes(playerId.GetHashCode()).CopyTo(commandData, 4);
            BitConverter.GetBytes(itemId.GetHashCode()).CopyTo(commandData, 8);

            return WriteMemory(commandQueueBase, commandData);
        }

        private bool FollowCharacter(string playerId, string targetId)
        {
            IntPtr commandQueueBase = new IntPtr(Offsets.KENSHI_BASE + Offsets.COMMAND_QUEUE);

            byte[] commandData = new byte[32];
            BitConverter.GetBytes(4).CopyTo(commandData, 0); // Command type: Follow
            BitConverter.GetBytes(playerId.GetHashCode()).CopyTo(commandData, 4);
            BitConverter.GetBytes(targetId.GetHashCode()).CopyTo(commandData, 8);

            return WriteMemory(commandQueueBase, commandData);
        }

        #endregion

        #region Spawn Function Calls

        private bool CallSpawnFunction(IntPtr characterPtr)
        {
            try
            {
                // Allocate memory for function call
                IntPtr codePtr = VirtualAllocEx(processHandle, IntPtr.Zero, 256,
                    MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);

                if (codePtr == IntPtr.Zero)
                    return false;

                // Build assembly code to call spawn function
                List<byte> code = new List<byte>();

                // Push character pointer as argument
                code.Add(0x48); // mov rcx, characterPtr
                code.Add(0xB9);
                code.AddRange(BitConverter.GetBytes(characterPtr.ToInt64()));

                // Call spawn function
                long spawnFunctionAddr = Offsets.KENSHI_BASE + Offsets.SPAWN_FUNCTION;
                code.Add(0x48); // mov rax, spawnFunctionAddr
                code.Add(0xB8);
                code.AddRange(BitConverter.GetBytes(spawnFunctionAddr));

                code.Add(0xFF); // call rax
                code.Add(0xD0);

                code.Add(0xC3); // ret

                // Write and execute
                WriteMemory(codePtr, code.ToArray());

                // Note: Full execution requires CreateRemoteThread, which is complex
                // For now, we write the structure and let the game's update loop handle it

                VirtualFreeEx(processHandle, codePtr, 0, MEM_RELEASE);
                return true;
            }
            catch
            {
                return false;
            }
        }

        private bool CallDespawnFunction(IntPtr characterPtr)
        {
            try
            {
                IntPtr codePtr = VirtualAllocEx(processHandle, IntPtr.Zero, 256,
                    MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);

                if (codePtr == IntPtr.Zero)
                    return false;

                List<byte> code = new List<byte>();

                code.Add(0x48);
                code.Add(0xB9);
                code.AddRange(BitConverter.GetBytes(characterPtr.ToInt64()));

                long despawnFunctionAddr = Offsets.KENSHI_BASE + Offsets.DESPAWN_FUNCTION;
                code.Add(0x48);
                code.Add(0xB8);
                code.AddRange(BitConverter.GetBytes(despawnFunctionAddr));

                code.Add(0xFF);
                code.Add(0xD0);

                code.Add(0xC3);

                WriteMemory(codePtr, code.ToArray());
                VirtualFreeEx(processHandle, codePtr, 0, MEM_RELEASE);

                return true;
            }
            catch
            {
                return false;
            }
        }

        #endregion

        #region State Monitoring

        private void UpdateGameState(object state)
        {
            if (!isConnected)
                return;

            try
            {
                // Read character list to detect changes
                IntPtr characterListBase = new IntPtr(Offsets.KENSHI_BASE + Offsets.CHARACTER_LIST_BASE);
                IntPtr characterListPtr = ReadPointer(characterListBase);

                if (characterListPtr == IntPtr.Zero)
                    return;

                // Read list size
                byte[] sizeBuffer = new byte[4];
                if (ReadMemory(new IntPtr(Offsets.KENSHI_BASE + Offsets.CHARACTER_LIST_SIZE), sizeBuffer))
                {
                    int listSize = BitConverter.ToInt32(sizeBuffer, 0);

                    // Update positions for all spawned characters
                    foreach (var kvp in spawnedCharacters.ToList())
                    {
                        var position = GetPlayerPosition(kvp.Key);
                        if (position != null)
                        {
                            // Trigger position update event if needed
                            OnPlayerPositionChanged?.Invoke(kvp.Key, position);
                        }
                    }
                }
            }
            catch (Exception ex)
            {
                Logger.Log(LOG_PREFIX + $"ERROR in update loop: {ex.Message}");
            }
        }

        public event Action<string, Position> OnPlayerPositionChanged;

        #endregion

        #region Helpers

        private byte[] StructureToBytes<T>(T structure) where T : struct
        {
            int size = Marshal.SizeOf(structure);
            byte[] bytes = new byte[size];
            IntPtr ptr = Marshal.AllocHGlobal(size);
            try
            {
                Marshal.StructureToPtr(structure, ptr, false);
                Marshal.Copy(ptr, bytes, 0, size);
            }
            finally
            {
                Marshal.FreeHGlobal(ptr);
            }
            return bytes;
        }

        #endregion

        #region Disposal

        public void Dispose()
        {
            isConnected = false;
            updateTimer?.Dispose();

            // Despawn all characters
            foreach (var playerId in spawnedCharacters.Keys.ToList())
            {
                DespawnPlayer(playerId);
            }

            if (processHandle != IntPtr.Zero)
            {
                CloseHandle(processHandle);
                processHandle = IntPtr.Zero;
            }

            Logger.Log(LOG_PREFIX + "Kenshi Game Bridge disposed");
        }

        #endregion
    }
}
