using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Text;
using System.Linq;
using KenshiMultiplayer.Utility;
using KenshiMultiplayer.Data;

namespace KenshiMultiplayer.Game
{
    /// <summary>
    /// Enhanced game bridge that reads actual Kenshi structures
    /// and provides full game state access
    /// </summary>
    public class EnhancedGameBridge : IDisposable
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

        [DllImport("kernel32.dll")]
        private static extern IntPtr CreateRemoteThread(IntPtr hProcess, IntPtr lpThreadAttributes, uint dwStackSize, IntPtr lpStartAddress, IntPtr lpParameter, uint dwCreationFlags, IntPtr lpThreadId);

        private const int PROCESS_ALL_ACCESS = 0x1F0FFF;
        private const uint MEM_COMMIT = 0x1000;
        private const uint MEM_RESERVE = 0x2000;
        private const uint PAGE_EXECUTE_READWRITE = 0x40;
        private const uint MEM_RELEASE = 0x8000;
        #endregion

        private Process kenshiProcess;
        private IntPtr processHandle;
        private long baseAddress;
        private bool isConnected;
        private Logger logger = new Logger("EnhancedGameBridge");

        // Caches
        private Dictionary<int, Character> characterCache = new Dictionary<int, Character>();
        private Dictionary<int, Faction> factionCache = new Dictionary<int, Faction>();
        private Dictionary<int, Squad> squadCache = new Dictionary<int, Squad>();
        private Dictionary<int, Building> buildingCache = new Dictionary<int, Building>();

        public bool IsConnected => isConnected;
        public long BaseAddress => baseAddress;

        #region Connection

        public bool Connect()
        {
            try
            {
                logger.Log("Connecting to Kenshi...");

                // Find Kenshi process
                var processes = Process.GetProcessesByName("kenshi_x64");
                if (processes.Length == 0)
                {
                    processes = Process.GetProcessesByName("kenshi");
                }

                if (processes.Length == 0)
                {
                    logger.Log("ERROR: Kenshi process not found!");
                    return false;
                }

                kenshiProcess = processes[0];
                logger.Log($"Found Kenshi process (PID: {kenshiProcess.Id})");

                // Open process
                processHandle = OpenProcess(PROCESS_ALL_ACCESS, false, kenshiProcess.Id);
                if (processHandle == IntPtr.Zero)
                {
                    logger.Log("ERROR: Failed to open process!");
                    return false;
                }

                // Calculate base address (ASLR)
                baseAddress = kenshiProcess.MainModule.BaseAddress.ToInt64();
                KenshiMemory.BaseAddress = baseAddress;
                logger.Log($"Base address: 0x{baseAddress:X}");

                isConnected = true;
                logger.Log("Successfully connected to Kenshi!");

                return true;
            }
            catch (Exception ex)
            {
                logger.Log($"ERROR connecting: {ex.Message}");
                return false;
            }
        }

        #endregion

        #region Memory Operations

        private byte[] ReadMemory(IntPtr address, int size)
        {
            byte[] buffer = new byte[size];
            if (ReadProcessMemory(processHandle, address, buffer, size, out _))
            {
                return buffer;
            }
            return null;
        }

        private T ReadStruct<T>(IntPtr address) where T : struct
        {
            int size = Marshal.SizeOf<T>();
            byte[] buffer = ReadMemory(address, size);
            if (buffer != null)
            {
                IntPtr ptr = Marshal.AllocHGlobal(size);
                try
                {
                    Marshal.Copy(buffer, 0, ptr, size);
                    return Marshal.PtrToStructure<T>(ptr);
                }
                finally
                {
                    Marshal.FreeHGlobal(ptr);
                }
            }
            return default;
        }

        private bool WriteStruct<T>(IntPtr address, T structure) where T : struct
        {
            int size = Marshal.SizeOf<T>();
            IntPtr ptr = Marshal.AllocHGlobal(size);
            try
            {
                Marshal.StructureToPtr(structure, ptr, false);
                byte[] buffer = new byte[size];
                Marshal.Copy(ptr, buffer, 0, size);
                return WriteProcessMemory(processHandle, address, buffer, size, out _);
            }
            finally
            {
                Marshal.FreeHGlobal(ptr);
            }
        }

        private IntPtr ReadPointer(IntPtr address)
        {
            byte[] buffer = ReadMemory(address, 8);
            if (buffer != null)
            {
                return new IntPtr(BitConverter.ToInt64(buffer, 0));
            }
            return IntPtr.Zero;
        }

        public string ReadString(IntPtr address, int maxLength = 256)
        {
            if (address == IntPtr.Zero) return "";

            byte[] buffer = ReadMemory(address, maxLength);
            if (buffer != null)
            {
                int nullIndex = Array.IndexOf(buffer, (byte)0);
                if (nullIndex >= 0)
                {
                    return Encoding.UTF8.GetString(buffer, 0, nullIndex);
                }
            }
            return "";
        }

        #endregion

        #region Character Management

        /// <summary>
        /// Get all player squad characters
        /// </summary>
        public List<Character> GetPlayerCharacters()
        {
            try
            {
                var characters = new List<Character>();

                IntPtr listAddr = new IntPtr(baseAddress + KenshiMemory.Characters.PlayerSquadList);
                IntPtr countAddr = new IntPtr(baseAddress + KenshiMemory.Characters.PlayerSquadCount);

                byte[] countBuffer = ReadMemory(countAddr, 4);
                if (countBuffer == null) return characters;

                int count = BitConverter.ToInt32(countBuffer, 0);
                logger.Log($"Found {count} player characters");

                IntPtr listPtr = ReadPointer(listAddr);
                if (listPtr == IntPtr.Zero) return characters;

                for (int i = 0; i < count; i++)
                {
                    IntPtr charPtr = ReadPointer(new IntPtr(listPtr.ToInt64() + (i * 8)));
                    if (charPtr != IntPtr.Zero)
                    {
                        Character character = ReadStruct<Character>(charPtr);
                        characterCache[character.CharacterID] = character;
                        characters.Add(character);
                    }
                }

                return characters;
            }
            catch (Exception ex)
            {
                logger.Log($"ERROR reading player characters: {ex.Message}");
                return new List<Character>();
            }
        }

        /// <summary>
        /// Get all characters in the world
        /// </summary>
        public List<Character> GetAllCharacters()
        {
            try
            {
                var characters = new List<Character>();

                IntPtr listAddr = new IntPtr(baseAddress + KenshiMemory.Characters.AllCharactersList);
                IntPtr countAddr = new IntPtr(baseAddress + KenshiMemory.Characters.AllCharactersCount);

                byte[] countBuffer = ReadMemory(countAddr, 4);
                if (countBuffer == null) return characters;

                int count = BitConverter.ToInt32(countBuffer, 0);

                IntPtr listPtr = ReadPointer(listAddr);
                if (listPtr == IntPtr.Zero) return characters;

                for (int i = 0; i < Math.Min(count, 1000); i++) // Limit to prevent crashes
                {
                    IntPtr charPtr = ReadPointer(new IntPtr(listPtr.ToInt64() + (i * 8)));
                    if (charPtr != IntPtr.Zero)
                    {
                        Character character = ReadStruct<Character>(charPtr);
                        characterCache[character.CharacterID] = character;
                        characters.Add(character);
                    }
                }

                return characters;
            }
            catch (Exception ex)
            {
                logger.Log($"ERROR reading all characters: {ex.Message}");
                return new List<Character>();
            }
        }

        /// <summary>
        /// Get character by ID
        /// </summary>
        public Character? GetCharacter(int characterId)
        {
            if (characterCache.TryGetValue(characterId, out var character))
            {
                return character;
            }

            // Search for character
            var allChars = GetAllCharacters();
            return allChars.FirstOrDefault(c => c.CharacterID == characterId);
        }

        /// <summary>
        /// Update character position
        /// </summary>
        public bool UpdateCharacterPosition(int characterId, float x, float y, float z)
        {
            try
            {
                var character = GetCharacter(characterId);
                if (!character.HasValue) return false;

                // Find character pointer
                var allChars = GetPlayerCharacters();
                IntPtr listAddr = new IntPtr(baseAddress + KenshiMemory.Characters.PlayerSquadList);
                IntPtr listPtr = ReadPointer(listAddr);

                for (int i = 0; i < allChars.Count; i++)
                {
                    IntPtr charPtr = ReadPointer(new IntPtr(listPtr.ToInt64() + (i * 8)));
                    Character c = ReadStruct<Character>(charPtr);

                    if (c.CharacterID == characterId)
                    {
                        // Update position in memory
                        int posXOffset = Marshal.OffsetOf<Character>("PosX").ToInt32();
                        WriteProcessMemory(processHandle, new IntPtr(charPtr.ToInt64() + posXOffset), BitConverter.GetBytes(x), 4, out _);
                        WriteProcessMemory(processHandle, new IntPtr(charPtr.ToInt64() + posXOffset + 4), BitConverter.GetBytes(y), 4, out _);
                        WriteProcessMemory(processHandle, new IntPtr(charPtr.ToInt64() + posXOffset + 8), BitConverter.GetBytes(z), 4, out _);

                        logger.Log($"Updated character {characterId} position to ({x}, {y}, {z})");
                        return true;
                    }
                }

                return false;
            }
            catch (Exception ex)
            {
                logger.Log($"ERROR updating character position: {ex.Message}");
                return false;
            }
        }

        /// <summary>
        /// Update character health
        /// </summary>
        public bool UpdateCharacterHealth(int characterId, float health)
        {
            try
            {
                var character = GetCharacter(characterId);
                if (!character.HasValue) return false;

                IntPtr listAddr = new IntPtr(baseAddress + KenshiMemory.Characters.PlayerSquadList);
                IntPtr listPtr = ReadPointer(listAddr);
                var allChars = GetPlayerCharacters();

                for (int i = 0; i < allChars.Count; i++)
                {
                    IntPtr charPtr = ReadPointer(new IntPtr(listPtr.ToInt64() + (i * 8)));
                    Character c = ReadStruct<Character>(charPtr);

                    if (c.CharacterID == characterId)
                    {
                        int healthOffset = Marshal.OffsetOf<Character>("Health").ToInt32();
                        WriteProcessMemory(processHandle, new IntPtr(charPtr.ToInt64() + healthOffset), BitConverter.GetBytes(health), 4, out _);
                        return true;
                    }
                }

                return false;
            }
            catch (Exception ex)
            {
                logger.Log($"ERROR updating health: {ex.Message}");
                return false;
            }
        }

        /// <summary>
        /// Get character stats
        /// </summary>
        public CharacterStats GetCharacterStats(int characterId)
        {
            try
            {
                var character = GetCharacter(characterId);
                if (!character.HasValue) return default;

                if (character.Value.StatsPtr != IntPtr.Zero)
                {
                    return ReadStruct<CharacterStats>(character.Value.StatsPtr);
                }

                return default;
            }
            catch (Exception ex)
            {
                logger.Log($"ERROR reading stats: {ex.Message}");
                return default;
            }
        }

        #endregion

        #region Faction Management

        /// <summary>
        /// Get all factions
        /// </summary>
        public List<Faction> GetAllFactions()
        {
            try
            {
                var factions = new List<Faction>();

                IntPtr listAddr = new IntPtr(baseAddress + KenshiMemory.Factions.FactionList);
                IntPtr countAddr = new IntPtr(baseAddress + KenshiMemory.Factions.FactionCount);

                byte[] countBuffer = ReadMemory(countAddr, 4);
                if (countBuffer == null) return factions;

                int count = BitConverter.ToInt32(countBuffer, 0);
                IntPtr listPtr = ReadPointer(listAddr);

                for (int i = 0; i < count; i++)
                {
                    IntPtr factionPtr = ReadPointer(new IntPtr(listPtr.ToInt64() + (i * 8)));
                    if (factionPtr != IntPtr.Zero)
                    {
                        Faction faction = ReadStruct<Faction>(factionPtr);
                        factionCache[faction.FactionID] = faction;
                        factions.Add(faction);
                    }
                }

                logger.Log($"Found {factions.Count} factions");
                return factions;
            }
            catch (Exception ex)
            {
                logger.Log($"ERROR reading factions: {ex.Message}");
                return new List<Faction>();
            }
        }

        /// <summary>
        /// Get faction by ID
        /// </summary>
        public Faction? GetFaction(int factionId)
        {
            if (factionCache.TryGetValue(factionId, out var faction))
            {
                return faction;
            }

            var allFactions = GetAllFactions();
            return allFactions.FirstOrDefault(f => f.FactionID == factionId);
        }

        /// <summary>
        /// Get faction relation
        /// </summary>
        public int GetFactionRelation(int factionId1, int factionId2)
        {
            try
            {
                IntPtr relationMatrixAddr = new IntPtr(baseAddress + KenshiMemory.Factions.RelationMatrix);
                IntPtr matrixPtr = ReadPointer(relationMatrixAddr);

                if (matrixPtr == IntPtr.Zero) return 0;

                // Relations stored as 2D array: [factionId1][factionId2]
                int index = (factionId1 * 256) + factionId2; // Assuming max 256 factions
                byte[] relationBuffer = ReadMemory(new IntPtr(matrixPtr.ToInt64() + (index * 4)), 4);

                if (relationBuffer != null)
                {
                    return BitConverter.ToInt32(relationBuffer, 0);
                }

                return 0;
            }
            catch (Exception ex)
            {
                logger.Log($"ERROR reading faction relation: {ex.Message}");
                return 0;
            }
        }

        /// <summary>
        /// Set faction relation
        /// </summary>
        public bool SetFactionRelation(int factionId1, int factionId2, int relation)
        {
            try
            {
                IntPtr relationMatrixAddr = new IntPtr(baseAddress + KenshiMemory.Factions.RelationMatrix);
                IntPtr matrixPtr = ReadPointer(relationMatrixAddr);

                if (matrixPtr == IntPtr.Zero) return false;

                int index = (factionId1 * 256) + factionId2;
                byte[] relationBytes = BitConverter.GetBytes(relation);

                bool success = WriteProcessMemory(processHandle,
                    new IntPtr(matrixPtr.ToInt64() + (index * 4)),
                    relationBytes, 4, out _);

                if (success)
                {
                    logger.Log($"Set faction relation: {factionId1} -> {factionId2} = {relation}");
                }

                return success;
            }
            catch (Exception ex)
            {
                logger.Log($"ERROR setting faction relation: {ex.Message}");
                return false;
            }
        }

        #endregion

        #region World State

        /// <summary>
        /// Get game time
        /// </summary>
        public (int day, float time) GetGameTime()
        {
            try
            {
                IntPtr dayAddr = new IntPtr(baseAddress + KenshiMemory.Game.GameDay);
                IntPtr timeAddr = new IntPtr(baseAddress + KenshiMemory.Game.GameTime);

                byte[] dayBuffer = ReadMemory(dayAddr, 4);
                byte[] timeBuffer = ReadMemory(timeAddr, 4);

                int day = dayBuffer != null ? BitConverter.ToInt32(dayBuffer, 0) : 0;
                float time = timeBuffer != null ? BitConverter.ToSingle(timeBuffer, 0) : 0f;

                return (day, time);
            }
            catch
            {
                return (0, 0f);
            }
        }

        /// <summary>
        /// Get all buildings
        /// </summary>
        public List<Building> GetAllBuildings()
        {
            try
            {
                var buildings = new List<Building>();

                IntPtr listAddr = new IntPtr(baseAddress + KenshiMemory.World.BuildingList);
                IntPtr countAddr = new IntPtr(baseAddress + KenshiMemory.World.BuildingCount);

                byte[] countBuffer = ReadMemory(countAddr, 4);
                if (countBuffer == null) return buildings;

                int count = BitConverter.ToInt32(countBuffer, 0);
                IntPtr listPtr = ReadPointer(listAddr);

                for (int i = 0; i < count; i++)
                {
                    IntPtr buildingPtr = ReadPointer(new IntPtr(listPtr.ToInt64() + (i * 8)));
                    if (buildingPtr != IntPtr.Zero)
                    {
                        Building building = ReadStruct<Building>(buildingPtr);
                        buildingCache[building.BuildingID] = building;
                        buildings.Add(building);
                    }
                }

                return buildings;
            }
            catch (Exception ex)
            {
                logger.Log($"ERROR reading buildings: {ex.Message}");
                return new List<Building>();
            }
        }

        /// <summary>
        /// Get weather state
        /// </summary>
        public WeatherSystem GetWeather()
        {
            try
            {
                IntPtr worldAddr = new IntPtr(baseAddress + KenshiMemory.Game.WorldInstance);
                IntPtr worldPtr = ReadPointer(worldAddr);

                if (worldPtr == IntPtr.Zero) return default;

                GameWorld world = ReadStruct<GameWorld>(worldPtr);

                if (world.WeatherSystemPtr != IntPtr.Zero)
                {
                    return ReadStruct<WeatherSystem>(world.WeatherSystemPtr);
                }

                return default;
            }
            catch (Exception ex)
            {
                logger.Log($"ERROR reading weather: {ex.Message}");
                return default;
            }
        }

        #endregion

        #region Inventory

        /// <summary>
        /// Get character inventory
        /// </summary>
        public List<GameItem> GetCharacterInventory(int characterId)
        {
            try
            {
                var items = new List<GameItem>();
                var character = GetCharacter(characterId);

                if (!character.HasValue || character.Value.InventoryPtr == IntPtr.Zero)
                    return items;

                for (int i = 0; i < character.Value.InventorySize; i++)
                {
                    IntPtr itemPtr = ReadPointer(new IntPtr(character.Value.InventoryPtr.ToInt64() + (i * 8)));
                    if (itemPtr != IntPtr.Zero)
                    {
                        GameItem item = ReadStruct<GameItem>(itemPtr);
                        items.Add(item);
                    }
                }

                return items;
            }
            catch (Exception ex)
            {
                logger.Log($"ERROR reading inventory: {ex.Message}");
                return new List<GameItem>();
            }
        }

        #endregion

        #region Commands

        /// <summary>
        /// Issue command to character
        /// </summary>
        public bool IssueCommand(int characterId, CommandType command, IntPtr target, float x, float y, float z)
        {
            try
            {
                // Allocate command structure
                IntPtr commandAddr = VirtualAllocEx(processHandle, IntPtr.Zero, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);

                if (commandAddr == IntPtr.Zero) return false;

                // Build command data
                byte[] commandData = new byte[64];
                BitConverter.GetBytes((int)command).CopyTo(commandData, 0);
                BitConverter.GetBytes(characterId).CopyTo(commandData, 4);
                BitConverter.GetBytes(target.ToInt64()).CopyTo(commandData, 8);
                BitConverter.GetBytes(x).CopyTo(commandData, 16);
                BitConverter.GetBytes(y).CopyTo(commandData, 20);
                BitConverter.GetBytes(z).CopyTo(commandData, 24);

                WriteProcessMemory(processHandle, commandAddr, commandData, commandData.Length, out _);

                // Call command function
                IntPtr funcAddr = new IntPtr(baseAddress + KenshiMemory.Functions.IssueCommand);
                CreateRemoteThread(processHandle, IntPtr.Zero, 0, funcAddr, commandAddr, 0, IntPtr.Zero);

                logger.Log($"Issued command {command} to character {characterId}");

                return true;
            }
            catch (Exception ex)
            {
                logger.Log($"ERROR issuing command: {ex.Message}");
                return false;
            }
        }

        #endregion

        #region Disposal

        public void Dispose()
        {
            isConnected = false;

            if (processHandle != IntPtr.Zero)
            {
                CloseHandle(processHandle);
                processHandle = IntPtr.Zero;
            }

            logger.Log("Enhanced Game Bridge disposed");
        }

        #endregion
    }
}
