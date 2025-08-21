using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Numerics;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using KenshiMultiplayer.Auth;
using KenshiMultiplayer.Networking;
using KenshiMultiplayer.Utility;

namespace KenshiMultiplayer.Common
{
    /// <summary>
    /// Interface for reading and writing Kenshi game memory
    /// Handles all low-level memory operations
    /// </summary>
    public class MemoryInterface : IDisposable
    {
        private Process gameProcess;
        private IntPtr processHandle;
        private IntPtr baseAddress;

        // Hooked functions
        private Dictionary<IntPtr, HookedFunction> hookedFunctions;
        private Dictionary<string, IntPtr> functionAddresses;

        // Memory patterns for finding dynamic addresses
        private readonly Dictionary<string, byte[]> memoryPatterns = new Dictionary<string, byte[]>
        {
            // Patterns for Kenshi v0.98.50
            ["PlayerController"] = new byte[] { 0x48, 0x8B, 0x05, 0xFF, 0xFF, 0xFF, 0xFF, 0x48, 0x85, 0xC0 },
            ["SquadManager"] = new byte[] { 0x48, 0x89, 0x5C, 0x24, 0xFF, 0x48, 0x89, 0x74, 0x24 },
            ["WorldState"] = new byte[] { 0x48, 0x8D, 0x0D, 0xFF, 0xFF, 0xFF, 0xFF, 0xE8 },
            ["PathfindingSystem"] = new byte[] { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9 },
            ["CombatSystem"] = new byte[] { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC },
            ["InventoryManager"] = new byte[] { 0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x08, 0x48, 0x89, 0x68 }
        };

        // Win32 API imports
        [DllImport("kernel32.dll")]
        static extern IntPtr OpenProcess(ProcessAccessFlags processAccess, bool bInheritHandle, int processId);

        [DllImport("kernel32.dll")]
        static extern bool ReadProcessMemory(IntPtr hProcess, IntPtr lpBaseAddress, byte[] lpBuffer, int dwSize, out int lpNumberOfBytesRead);

        [DllImport("kernel32.dll")]
        static extern bool WriteProcessMemory(IntPtr hProcess, IntPtr lpBaseAddress, byte[] lpBuffer, int dwSize, out int lpNumberOfBytesWritten);

        [DllImport("kernel32.dll")]
        static extern bool VirtualProtectEx(IntPtr hProcess, IntPtr lpAddress, IntPtr dwSize, uint flNewProtect, out uint lpflOldProtect);

        [DllImport("kernel32.dll")]
        static extern IntPtr VirtualAllocEx(IntPtr hProcess, IntPtr lpAddress, IntPtr dwSize, uint flAllocationType, uint flProtect);

        [DllImport("kernel32.dll")]
        static extern bool VirtualFreeEx(IntPtr hProcess, IntPtr lpAddress, IntPtr dwSize, uint dwFreeType);

        [DllImport("kernel32.dll")]
        static extern bool CloseHandle(IntPtr hObject);

        [DllImport("kernel32.dll")]
        static extern IntPtr CreateRemoteThread(IntPtr hProcess, IntPtr lpThreadAttributes, uint dwStackSize, IntPtr lpStartAddress, IntPtr lpParameter, uint dwCreationFlags, out IntPtr lpThreadId);

        [Flags]
        enum ProcessAccessFlags : uint
        {
            All = 0x001F0FFF,
            CreateThread = 0x0002,
            QueryInformation = 0x0400,
            VMOperation = 0x0008,
            VMRead = 0x0010,
            VMWrite = 0x0020
        }

        // Memory protection constants
        const uint PAGE_EXECUTE_READWRITE = 0x40;
        const uint PAGE_READWRITE = 0x04;
        const uint MEM_COMMIT = 0x1000;
        const uint MEM_RESERVE = 0x2000;
        const uint MEM_RELEASE = 0x8000;

        public MemoryInterface(Process process)
        {
            gameProcess = process;
            hookedFunctions = new Dictionary<IntPtr, HookedFunction>();
            functionAddresses = new Dictionary<string, IntPtr>();
        }

        /// <summary>
        /// Initialize memory interface
        /// </summary>
        public bool Initialize()
        {
            try
            {
                // Open process with required permissions
                processHandle = OpenProcess(ProcessAccessFlags.All, false, gameProcess.Id);
                if (processHandle == IntPtr.Zero)
                {
                    Logger.Log("Failed to open process");
                    return false;
                }

                // Get base address
                baseAddress = gameProcess.MainModule.BaseAddress;
                Logger.Log($"Game base address: 0x{baseAddress.ToInt64():X}");

                // Find important addresses using pattern scanning
                FindDynamicAddresses();

                return true;
            }
            catch (Exception ex)
            {
                Logger.Log($"Failed to initialize memory interface: {ex.Message}");
                return false;
            }
        }

        /// <summary>
        /// Find dynamic addresses using pattern scanning
        /// </summary>
        private void FindDynamicAddresses()
        {
            foreach (var pattern in memoryPatterns)
            {
                var address = PatternScan(pattern.Value);
                if (address != IntPtr.Zero)
                {
                    functionAddresses[pattern.Key] = address;
                    Logger.Log($"Found {pattern.Key} at 0x{address.ToInt64():X}");
                }
            }
        }

        /// <summary>
        /// Pattern scan for finding addresses
        /// </summary>
        private IntPtr PatternScan(byte[] pattern)
        {
            try
            {
                var moduleSize = gameProcess.MainModule.ModuleMemorySize;
                var buffer = new byte[moduleSize];
                int bytesRead;

                if (!ReadProcessMemory(processHandle, baseAddress, buffer, moduleSize, out bytesRead))
                    return IntPtr.Zero;

                for (int i = 0; i < buffer.Length - pattern.Length; i++)
                {
                    bool found = true;
                    for (int j = 0; j < pattern.Length; j++)
                    {
                        if (pattern[j] != 0xFF && buffer[i + j] != pattern[j])
                        {
                            found = false;
                            break;
                        }
                    }

                    if (found)
                        return baseAddress + i;
                }

                return IntPtr.Zero;
            }
            catch
            {
                return IntPtr.Zero;
            }
        }

        /// <summary>
        /// Read memory
        /// </summary>
        public T ReadStruct<T>(IntPtr address) where T : struct
        {
            var size = Marshal.SizeOf<T>();
            var buffer = new byte[size];
            int bytesRead;

            if (ReadProcessMemory(processHandle, address, buffer, size, out bytesRead))
            {
                var handle = GCHandle.Alloc(buffer, GCHandleType.Pinned);
                try
                {
                    return Marshal.PtrToStructure<T>(handle.AddrOfPinnedObject());
                }
                finally
                {
                    handle.Free();
                }
            }

            return default(T);
        }

        /// <summary>
        /// Write memory
        /// </summary>
        public bool WriteStruct<T>(IntPtr address, T value) where T : struct
        {
            var size = Marshal.SizeOf<T>();
            var buffer = new byte[size];

            var handle = GCHandle.Alloc(value, GCHandleType.Pinned);
            try
            {
                Marshal.Copy(handle.AddrOfPinnedObject(), buffer, 0, size);
            }
            finally
            {
                handle.Free();
            }

            int bytesWritten;
            return WriteProcessMemory(processHandle, address, buffer, size, out bytesWritten);
        }

        /// <summary>
        /// Read pointer
        /// </summary>
        public IntPtr ReadPointer(IntPtr address)
        {
            var buffer = new byte[8]; // 64-bit pointer
            int bytesRead;

            if (ReadProcessMemory(processHandle, address, buffer, 8, out bytesRead))
            {
                return new IntPtr(BitConverter.ToInt64(buffer, 0));
            }

            return IntPtr.Zero;
        }

        /// <summary>
        /// Read pointer chain
        /// </summary>
        public IntPtr ReadPointerChain(IntPtr baseAddress, params int[] offsets)
        {
            var currentAddress = baseAddress;

            for (int i = 0; i < offsets.Length; i++)
            {
                currentAddress = ReadPointer(currentAddress);
                if (currentAddress == IntPtr.Zero)
                    return IntPtr.Zero;

                if (i < offsets.Length - 1)
                    currentAddress += offsets[i];
            }

            return currentAddress + offsets[offsets.Length - 1];
        }

        /// <summary>
        /// Read string
        /// </summary>
        public string ReadString(IntPtr address, int maxLength = 256)
        {
            var buffer = new byte[maxLength];
            int bytesRead;

            if (ReadProcessMemory(processHandle, address, buffer, maxLength, out bytesRead))
            {
                var nullIndex = Array.IndexOf(buffer, (byte)0);
                if (nullIndex >= 0)
                {
                    return Encoding.UTF8.GetString(buffer, 0, nullIndex);
                }
            }

            return string.Empty;
        }

        /// <summary>
        /// Read float
        /// </summary>
        public float ReadFloat(IntPtr address)
        {
            var buffer = new byte[4];
            int bytesRead;

            if (ReadProcessMemory(processHandle, address, buffer, 4, out bytesRead))
            {
                return BitConverter.ToSingle(buffer, 0);
            }

            return 0f;
        }

        /// <summary>
        /// Read double
        /// </summary>
        public double ReadDouble(IntPtr address)
        {
            var buffer = new byte[8];
            int bytesRead;

            if (ReadProcessMemory(processHandle, address, buffer, 8, out bytesRead))
            {
                return BitConverter.ToDouble(buffer, 0);
            }

            return 0.0;
        }

        /// <summary>
        /// Read int
        /// </summary>
        public int ReadInt(IntPtr address)
        {
            var buffer = new byte[4];
            int bytesRead;

            if (ReadProcessMemory(processHandle, address, buffer, 4, out bytesRead))
            {
                return BitConverter.ToInt32(buffer, 0);
            }

            return 0;
        }

        /// <summary>
        /// Write bytes
        /// </summary>
        public bool WriteBytes(IntPtr address, byte[] bytes)
        {
            int bytesWritten;
            return WriteProcessMemory(processHandle, address, bytes, bytes.Length, out bytesWritten);
        }

        /// <summary>
        /// Hook a function
        /// </summary>
        public bool HookFunction(IntPtr functionAddress, Action<IntPtr> callback)
        {
            try
            {
                // Store original bytes
                var originalBytes = new byte[14];
                int bytesRead;
                if (!ReadProcessMemory(processHandle, functionAddress, originalBytes, 14, out bytesRead))
                    return false;

                // Allocate memory for our hook
                var hookMemory = VirtualAllocEx(processHandle, IntPtr.Zero, new IntPtr(1024),
                    MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);

                if (hookMemory == IntPtr.Zero)
                    return false;

                // Create hook structure
                var hook = new HookedFunction
                {
                    OriginalAddress = functionAddress,
                    HookAddress = hookMemory,
                    OriginalBytes = originalBytes,
                    Callback = callback
                };

                // Write jump to our hook
                var jumpBytes = CreateJump(functionAddress, hookMemory);

                // Change memory protection
                uint oldProtect;
                if (!VirtualProtectEx(processHandle, functionAddress, new IntPtr(14), PAGE_EXECUTE_READWRITE, out oldProtect))
                    return false;

                // Write jump
                if (!WriteBytes(functionAddress, jumpBytes))
                    return false;

                // Restore protection
                VirtualProtectEx(processHandle, functionAddress, new IntPtr(14), oldProtect, out _);

                hookedFunctions[functionAddress] = hook;

                // Start hook handler thread
                StartHookHandler(hook);

                return true;
            }
            catch (Exception ex)
            {
                Logger.Log($"Failed to hook function: {ex.Message}");
                return false;
            }
        }

        /// <summary>
        /// Unhook a function
        /// </summary>
        public bool UnhookFunction(IntPtr functionAddress)
        {
            if (!hookedFunctions.TryGetValue(functionAddress, out var hook))
                return false;

            try
            {
                // Restore original bytes
                uint oldProtect;
                VirtualProtectEx(processHandle, functionAddress, new IntPtr(14), PAGE_EXECUTE_READWRITE, out oldProtect);
                WriteBytes(functionAddress, hook.OriginalBytes);
                VirtualProtectEx(processHandle, functionAddress, new IntPtr(14), oldProtect, out _);

                // Free allocated memory
                VirtualFreeEx(processHandle, hook.HookAddress, IntPtr.Zero, MEM_RELEASE);

                hookedFunctions.Remove(functionAddress);
                return true;
            }
            catch
            {
                return false;
            }
        }

        /// <summary>
        /// Create jump instruction
        /// </summary>
        private byte[] CreateJump(IntPtr from, IntPtr to)
        {
            var bytes = new byte[14];

            // JMP [RIP+0]
            bytes[0] = 0xFF;
            bytes[1] = 0x25;
            bytes[2] = 0x00;
            bytes[3] = 0x00;
            bytes[4] = 0x00;
            bytes[5] = 0x00;

            // Address
            var addressBytes = BitConverter.GetBytes(to.ToInt64());
            Array.Copy(addressBytes, 0, bytes, 6, 8);

            return bytes;
        }

        /// <summary>
        /// Start hook handler thread
        /// </summary>
        private void StartHookHandler(HookedFunction hook)
        {
            var thread = new Thread(() =>
            {
                while (hookedFunctions.ContainsKey(hook.OriginalAddress))
                {
                    // Check if hook was triggered
                    // This is simplified - real implementation would use shared memory or other IPC
                    Thread.Sleep(1);
                }
            });

            thread.IsBackground = true;
            thread.Start();
        }

        /// <summary>
        /// Get character data from memory
        /// </summary>
        public KenshiCharacter GetCharacterData(IntPtr characterAddress)
        {
            try
            {
                var character = new KenshiCharacter();

                // Read basic info
                character.ID = ReadInt(characterAddress + 0x10);
                character.Name = ReadString(ReadPointer(characterAddress + 0x18));

                // Read position
                character.PosX = ReadFloat(characterAddress + 0x30);
                character.PosY = ReadFloat(characterAddress + 0x34);
                character.PosZ = ReadFloat(characterAddress + 0x38);
                character.Rotation = ReadFloat(characterAddress + 0x3C);

                // Read health for each limb
                var healthBase = characterAddress + 0x100;
                character.LimbHealth["head"] = ReadInt(healthBase);
                character.LimbHealth["chest"] = ReadInt(healthBase + 0x4);
                character.LimbHealth["stomach"] = ReadInt(healthBase + 0x8);
                character.LimbHealth["left_arm"] = ReadInt(healthBase + 0xC);
                character.LimbHealth["right_arm"] = ReadInt(healthBase + 0x10);
                character.LimbHealth["left_leg"] = ReadInt(healthBase + 0x14);
                character.LimbHealth["right_leg"] = ReadInt(healthBase + 0x18);

                // Read skills
                var skillsBase = characterAddress + 0x200;
                character.Skills["athletics"] = ReadFloat(skillsBase);
                character.Skills["strength"] = ReadFloat(skillsBase + 0x4);
                character.Skills["toughness"] = ReadFloat(skillsBase + 0x8);
                character.Skills["dexterity"] = ReadFloat(skillsBase + 0xC);
                character.Skills["melee_attack"] = ReadFloat(skillsBase + 0x10);
                character.Skills["melee_defence"] = ReadFloat(skillsBase + 0x14);
                character.Skills["katanas"] = ReadFloat(skillsBase + 0x18);
                character.Skills["sabres"] = ReadFloat(skillsBase + 0x1C);

                // Read status
                character.IsUnconscious = ReadInt(characterAddress + 0x300) == 1;
                character.IsDead = ReadInt(characterAddress + 0x304) == 1;
                character.Hunger = ReadFloat(characterAddress + 0x308);

                return character;
            }
            catch (Exception ex)
            {
                Logger.Log($"Failed to read character data: {ex.Message}");
                return null;
            }
        }

        /// <summary>
        /// Get all squad members
        /// </summary>
        public List<KenshiCharacter> GetSquadMembers()
        {
            var members = new List<KenshiCharacter>();

            try
            {
                if (!functionAddresses.TryGetValue("SquadManager", out var squadManagerAddr))
                    return members;

                var squadPtr = ReadPointer(squadManagerAddr);
                if (squadPtr == IntPtr.Zero)
                    return members;

                // Read squad member count
                var memberCount = ReadInt(squadPtr + 0x20);

                // Read squad member array
                var memberArrayPtr = ReadPointer(squadPtr + 0x28);

                for (int i = 0; i < memberCount && i < 256; i++) // Max 256 to prevent infinite loops
                {
                    var characterPtr = ReadPointer(memberArrayPtr + (i * 8));
                    if (characterPtr != IntPtr.Zero)
                    {
                        var character = GetCharacterData(characterPtr);
                        if (character != null)
                            members.Add(character);
                    }
                }
            }
            catch (Exception ex)
            {
                Logger.Log($"Failed to get squad members: {ex.Message}");
            }

            return members;
        }

        /// <summary>
        /// Inject DLL into game process
        /// </summary>
        public bool InjectDll(string dllPath)
        {
            try
            {
                // Get LoadLibraryA address
                var kernel32 = Process.GetCurrentProcess().Modules
                    .Cast<ProcessModule>()
                    .FirstOrDefault(m => m.ModuleName.ToLower() == "kernel32.dll");

                if (kernel32 == null)
                    return false;

                var loadLibraryAddr = GetProcAddress(kernel32.BaseAddress, "LoadLibraryA");
                if (loadLibraryAddr == IntPtr.Zero)
                    return false;

                // Allocate memory for DLL path
                var pathBytes = Encoding.ASCII.GetBytes(dllPath + "\0");
                var pathAddr = VirtualAllocEx(processHandle, IntPtr.Zero, new IntPtr(pathBytes.Length),
                    MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);

                if (pathAddr == IntPtr.Zero)
                    return false;

                // Write DLL path
                int bytesWritten;
                if (!WriteProcessMemory(processHandle, pathAddr, pathBytes, pathBytes.Length, out bytesWritten))
                {
                    VirtualFreeEx(processHandle, pathAddr, IntPtr.Zero, MEM_RELEASE);
                    return false;
                }

                // Create remote thread to load DLL
                IntPtr threadId;
                var thread = CreateRemoteThread(processHandle, IntPtr.Zero, 0, loadLibraryAddr, pathAddr, 0, out threadId);

                if (thread == IntPtr.Zero)
                {
                    VirtualFreeEx(processHandle, pathAddr, IntPtr.Zero, MEM_RELEASE);
                    return false;
                }

                // Wait for thread to complete
                WaitForSingleObject(thread, 5000);

                // Cleanup
                CloseHandle(thread);
                VirtualFreeEx(processHandle, pathAddr, IntPtr.Zero, MEM_RELEASE);

                Logger.Log($"Successfully injected {dllPath}");
                return true;
            }
            catch (Exception ex)
            {
                Logger.Log($"DLL injection failed: {ex.Message}");
                return false;
            }
        }

        [DllImport("kernel32.dll")]
        static extern IntPtr GetProcAddress(IntPtr hModule, string procName);

        [DllImport("kernel32.dll")]
        static extern uint WaitForSingleObject(IntPtr hHandle, uint dwMilliseconds);

        /// <summary>
        /// Dispose resources
        /// </summary>
        public void Dispose()
        {
            // Unhook all functions
            foreach (var address in hookedFunctions.Keys.ToList())
            {
                UnhookFunction(address);
            }

            // Close process handle
            if (processHandle != IntPtr.Zero)
            {
                CloseHandle(processHandle);
            }
        }

        /// <summary>
        /// Hooked function structure
        /// </summary>
        private class HookedFunction
        {
            public IntPtr OriginalAddress { get; set; }
            public IntPtr HookAddress { get; set; }
            public byte[] OriginalBytes { get; set; }
            public Action<IntPtr> Callback { get; set; }
        }
    }

    // Game memory structures
    [StructLayout(LayoutKind.Sequential)]
    public struct CharacterMoveData
    {
        public int CharacterId;
        public float X;
        public float Y;
        public float Z;
        public float Speed;
        public int Action;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct CombatActionData
    {
        public int AttackerId;
        public int TargetId;
        public int AttackType;
        public float Damage;
        public int TargetLimb;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct TimeData
    {
        public double GameTime;
        public float TimeMultiplier;
        public int IsPaused;
    }
}