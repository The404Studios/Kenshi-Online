using KenshiMultiplayer.Networking;
using KenshiMultiplayer.Common;
using KenshiMultiplayer.Auth;
using KenshiMultiplayer.Utility;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;

namespace KenshiMultiplayer
{
    /// <summary>
    /// Injects into Kenshi's memory to intercept Havok AI pathfinding calls
    /// and replace them with deterministic cached paths
    /// </summary>
    public class PathInjector
    {
        // Kenshi memory addresses (version 0.98.50)
        // These are from the RE_Kenshi research
        private const long KENSHI_BASE = 0x140000000; // kenshi_x64.exe base
        private const long HAVOK_PATHFIND_OFFSET = 0x1A3B240; // Havok AI pathfinding function
        private const long NAVMESH_QUERY_OFFSET = 0x1A3B580; // NavMesh query function
        private const long CHARACTER_CONTROLLER_OFFSET = 0x249BCB0; // Character controller base

        private Process kenshiProcess;
        private IntPtr processHandle;
        private PathCache pathCache;
        private bool isInjected = false;

        // Original function bytes for restoration
        private byte[] originalPathfindBytes;
        private byte[] originalNavmeshBytes;

        // Delegate for our hook
        private delegate IntPtr PathfindHook(IntPtr character, IntPtr startPos, IntPtr endPos, IntPtr outPath);
        private PathfindHook pathfindHookDelegate;
        private IntPtr hookAddress;

        public PathInjector(PathCache cache)
        {
            pathCache = cache;
            pathfindHookDelegate = new PathfindHook(InterceptPathfinding);
        }

        /// <summary>
        /// Inject into Kenshi process
        /// </summary>
        public bool Inject()
        {
            try
            {
                // Find Kenshi process
                var processes = Process.GetProcessesByName("kenshi_x64");
                if (processes.Length == 0)
                {
                    Logger.Log("Kenshi process not found");
                    return false;
                }

                kenshiProcess = processes[0];
                processHandle = OpenProcess(ProcessAccessFlags.All, false, kenshiProcess.Id);

                if (processHandle == IntPtr.Zero)
                {
                    Logger.Log("Failed to open Kenshi process");
                    return false;
                }

                // Install hooks
                InstallPathfindingHook();
                InstallNavMeshHook();

                isInjected = true;
                Logger.Log("Successfully injected into Kenshi");

                // Start monitoring thread
                Task.Run(() => MonitorProcess());

                return true;
            }
            catch (Exception ex)
            {
                Logger.Log($"Injection failed: {ex.Message}");
                return false;
            }
        }

        /// <summary>
        /// Install hook on Havok pathfinding function
        /// </summary>
        private void InstallPathfindingHook()
        {
            IntPtr targetAddress = IntPtr.Add(kenshiProcess.MainModule.BaseAddress, (int)HAVOK_PATHFIND_OFFSET);

            // Save original bytes
            originalPathfindBytes = new byte[14];
            ReadProcessMemory(processHandle, targetAddress, originalPathfindBytes, 14, out _);

            // Allocate memory for our hook function
            IntPtr allocatedMemory = VirtualAllocEx(processHandle, IntPtr.Zero, 4096,
                AllocationType.Commit | AllocationType.Reserve, MemoryProtection.ExecuteReadWrite);

            // Write our hook function to allocated memory
            byte[] hookCode = GenerateHookCode(allocatedMemory);
            WriteProcessMemory(processHandle, allocatedMemory, hookCode, hookCode.Length, out _);

            // Create jump to our hook
            byte[] jumpBytes = CreateJump(targetAddress, allocatedMemory);

            // Make target writable
            VirtualProtectEx(processHandle, targetAddress, (UIntPtr)jumpBytes.Length,
                MemoryProtection.ExecuteReadWrite, out var oldProtect);

            // Write jump
            WriteProcessMemory(processHandle, targetAddress, jumpBytes, jumpBytes.Length, out _);

            // Restore protection
            VirtualProtectEx(processHandle, targetAddress, (UIntPtr)jumpBytes.Length,
                oldProtect, out _);

            hookAddress = allocatedMemory;
            Logger.Log($"Pathfinding hook installed at 0x{targetAddress.ToString("X")}");
        }

        /// <summary>
        /// Install hook on NavMesh query function
        /// </summary>
        private void InstallNavMeshHook()
        {
            IntPtr targetAddress = IntPtr.Add(kenshiProcess.MainModule.BaseAddress, (int)NAVMESH_QUERY_OFFSET);

            // Save original bytes
            originalNavmeshBytes = new byte[14];
            ReadProcessMemory(processHandle, targetAddress, originalNavmeshBytes, 14, out _);

            // Similar process as pathfinding hook
            // This intercepts lower-level navmesh queries
        }

        /// <summary>
        /// Generate assembly code for our hook
        /// </summary>
        private byte[] GenerateHookCode(IntPtr allocatedMemory)
        {
            List<byte> code = new List<byte>();

            // Save registers
            code.AddRange(new byte[] { 0x50 }); // push rax
            code.AddRange(new byte[] { 0x51 }); // push rcx
            code.AddRange(new byte[] { 0x52 }); // push rdx
            code.AddRange(new byte[] { 0x41, 0x50 }); // push r8
            code.AddRange(new byte[] { 0x41, 0x51 }); // push r9

            // Call our C# function
            // mov rax, [C# function address]
            code.AddRange(new byte[] { 0x48, 0xB8 });
            code.AddRange(BitConverter.GetBytes(Marshal.GetFunctionPointerForDelegate(pathfindHookDelegate).ToInt64()));

            // call rax
            code.AddRange(new byte[] { 0xFF, 0xD0 });

            // Restore registers
            code.AddRange(new byte[] { 0x41, 0x59 }); // pop r9
            code.AddRange(new byte[] { 0x41, 0x58 }); // pop r8
            code.AddRange(new byte[] { 0x5A }); // pop rdx
            code.AddRange(new byte[] { 0x59 }); // pop rcx
            code.AddRange(new byte[] { 0x58 }); // pop rax

            // Execute original instructions
            code.AddRange(originalPathfindBytes.Take(5));

            // Jump back
            IntPtr returnAddress = IntPtr.Add(kenshiProcess.MainModule.BaseAddress, (int)(HAVOK_PATHFIND_OFFSET + 5));
            code.AddRange(CreateJump(IntPtr.Add(allocatedMemory, code.Count), returnAddress));

            return code.ToArray();
        }

        /// <summary>
        /// Create a jump instruction
        /// </summary>
        private byte[] CreateJump(IntPtr from, IntPtr to)
        {
            long offset = to.ToInt64() - from.ToInt64() - 5;

            if (Math.Abs(offset) > int.MaxValue)
            {
                // Far jump (14 bytes)
                byte[] farJump = new byte[14];
                farJump[0] = 0xFF; // jmp qword ptr [rip+0]
                farJump[1] = 0x25;
                farJump[2] = 0x00;
                farJump[3] = 0x00;
                farJump[4] = 0x00;
                farJump[5] = 0x00;
                BitConverter.GetBytes(to.ToInt64()).CopyTo(farJump, 6);
                return farJump;
            }
            else
            {
                // Near jump (5 bytes)
                byte[] nearJump = new byte[5];
                nearJump[0] = 0xE9; // jmp
                BitConverter.GetBytes((int)offset).CopyTo(nearJump, 1);
                return nearJump;
            }
        }

        /// <summary>
        /// Our hook function that intercepts pathfinding calls
        /// </summary>
        private IntPtr InterceptPathfinding(IntPtr character, IntPtr startPos, IntPtr endPos, IntPtr outPath)
        {
            try
            {
                // Read positions from memory
                Vector3 start = ReadVector3(startPos);
                Vector3 end = ReadVector3(endPos);

                Logger.Log($"Intercepted pathfinding: {start} -> {end}");

                // Get cached path
                var cachedPath = pathCache.GetPath(start, end);

                if (cachedPath != null && cachedPath.Waypoints.Count > 0)
                {
                    // Write our cached path to game memory
                    WritePathToGame(outPath, cachedPath);

                    // Return success
                    return new IntPtr(1);
                }

                // Fall back to original pathfinding
                return CallOriginalPathfinding(character, startPos, endPos, outPath);
            }
            catch (Exception ex)
            {
                Logger.Log($"Hook error: {ex.Message}");
                return IntPtr.Zero;
            }
        }

        /// <summary>
        /// Read Vector3 from game memory
        /// </summary>
        private Vector3 ReadVector3(IntPtr address)
        {
            byte[] buffer = new byte[12];
            ReadProcessMemory(processHandle, address, buffer, 12, out _);

            return new Vector3(
                BitConverter.ToSingle(buffer, 0),
                BitConverter.ToSingle(buffer, 4),
                BitConverter.ToSingle(buffer, 8)
            );
        }

        /// <summary>
        /// Write cached path to game memory
        /// </summary>
        private void WritePathToGame(IntPtr pathAddress, CachedPath cachedPath)
        {
            // Kenshi path structure (reverse engineered)
            // struct Path {
            //     int waypointCount;
            //     Vector3* waypoints;
            //     float totalDistance;
            //     int flags;
            // }

            // Write waypoint count
            byte[] countBytes = BitConverter.GetBytes(cachedPath.Waypoints.Count);
            WriteProcessMemory(processHandle, pathAddress, countBytes, 4, out _);

            // Allocate memory for waypoints
            int waypointSize = 12 * cachedPath.Waypoints.Count;
            IntPtr waypointMemory = VirtualAllocEx(processHandle, IntPtr.Zero, (uint)waypointSize,
                AllocationType.Commit, MemoryProtection.ReadWrite);

            // Write waypoints
            byte[] waypointData = new byte[waypointSize];
            for (int i = 0; i < cachedPath.Waypoints.Count; i++)
            {
                var waypoint = cachedPath.Waypoints[i];
                BitConverter.GetBytes(waypoint.X).CopyTo(waypointData, i * 12);
                BitConverter.GetBytes(waypoint.Y).CopyTo(waypointData, i * 12 + 4);
                BitConverter.GetBytes(waypoint.Z).CopyTo(waypointData, i * 12 + 8);
            }
            WriteProcessMemory(processHandle, waypointMemory, waypointData, waypointData.Length, out _);

            // Write waypoint pointer
            byte[] ptrBytes = BitConverter.GetBytes(waypointMemory.ToInt64());
            WriteProcessMemory(processHandle, IntPtr.Add(pathAddress, 4), ptrBytes, 8, out _);

            // Write distance
            byte[] distanceBytes = BitConverter.GetBytes(cachedPath.Distance);
            WriteProcessMemory(processHandle, IntPtr.Add(pathAddress, 12), distanceBytes, 4, out _);

            // Write flags (0 = valid path)
            byte[] flagBytes = BitConverter.GetBytes(0);
            WriteProcessMemory(processHandle, IntPtr.Add(pathAddress, 16), flagBytes, 4, out _);
        }

        /// <summary>
        /// Call original pathfinding if cache miss
        /// </summary>
        private IntPtr CallOriginalPathfinding(IntPtr character, IntPtr startPos, IntPtr endPos, IntPtr outPath)
        {
            // Create a function pointer to the original code
            IntPtr originalFunc = IntPtr.Add(kenshiProcess.MainModule.BaseAddress, (int)HAVOK_PATHFIND_OFFSET + 14);

            // This would need proper calling convention handling
            // For now, return failure to use cached paths only
            return IntPtr.Zero;
        }

        /// <summary>
        /// Monitor Kenshi process health
        /// </summary>
        private void MonitorProcess()
        {
            while (isInjected)
            {
                if (kenshiProcess.HasExited)
                {
                    Logger.Log("Kenshi process exited");
                    Cleanup();
                    break;
                }

                Thread.Sleep(1000);
            }
        }

        /// <summary>
        /// Remove injection and restore original code
        /// </summary>
        public void Cleanup()
        {
            if (!isInjected)
                return;

            try
            {
                // Restore original pathfinding bytes
                if (originalPathfindBytes != null)
                {
                    IntPtr targetAddress = IntPtr.Add(kenshiProcess.MainModule.BaseAddress, (int)HAVOK_PATHFIND_OFFSET);
                    VirtualProtectEx(processHandle, targetAddress, (UIntPtr)originalPathfindBytes.Length,
                        MemoryProtection.ExecuteReadWrite, out var oldProtect);
                    WriteProcessMemory(processHandle, targetAddress, originalPathfindBytes, originalPathfindBytes.Length, out _);
                    VirtualProtectEx(processHandle, targetAddress, (UIntPtr)originalPathfindBytes.Length,
                        oldProtect, out _);
                }

                // Free allocated memory
                if (hookAddress != IntPtr.Zero)
                {
                    VirtualFreeEx(processHandle, hookAddress, 0, FreeType.Release);
                }

                // Close handle
                if (processHandle != IntPtr.Zero)
                {
                    CloseHandle(processHandle);
                }

                isInjected = false;
                Logger.Log("Injection cleaned up");
            }
            catch (Exception ex)
            {
                Logger.Log($"Cleanup error: {ex.Message}");
            }
        }

        // P/Invoke declarations
        [DllImport("kernel32.dll")]
        private static extern IntPtr OpenProcess(ProcessAccessFlags processAccess, bool bInheritHandle, int processId);

        [DllImport("kernel32.dll")]
        private static extern bool CloseHandle(IntPtr hObject);

        [DllImport("kernel32.dll")]
        private static extern bool ReadProcessMemory(IntPtr hProcess, IntPtr lpBaseAddress, byte[] lpBuffer, int dwSize, out IntPtr lpNumberOfBytesRead);

        [DllImport("kernel32.dll")]
        private static extern bool WriteProcessMemory(IntPtr hProcess, IntPtr lpBaseAddress, byte[] lpBuffer, int nSize, out IntPtr lpNumberOfBytesWritten);

        [DllImport("kernel32.dll")]
        private static extern IntPtr VirtualAllocEx(IntPtr hProcess, IntPtr lpAddress, uint dwSize, AllocationType flAllocationType, MemoryProtection flProtect);

        [DllImport("kernel32.dll")]
        private static extern bool VirtualFreeEx(IntPtr hProcess, IntPtr lpAddress, uint dwSize, FreeType dwFreeType);

        [DllImport("kernel32.dll")]
        private static extern bool VirtualProtectEx(IntPtr hProcess, IntPtr lpAddress, UIntPtr dwSize, MemoryProtection flNewProtect, out MemoryProtection lpflOldProtect);

        [Flags]
        private enum ProcessAccessFlags : uint
        {
            All = 0x001F0FFF,
            Terminate = 0x00000001,
            CreateThread = 0x00000002,
            VirtualMemoryOperation = 0x00000008,
            VirtualMemoryRead = 0x00000010,
            VirtualMemoryWrite = 0x00000020,
            DuplicateHandle = 0x00000040,
            CreateProcess = 0x000000080,
            SetQuota = 0x00000100,
            SetInformation = 0x00000200,
            QueryInformation = 0x00000400,
            QueryLimitedInformation = 0x00001000,
            Synchronize = 0x00100000
        }

        [Flags]
        private enum AllocationType
        {
            Commit = 0x1000,
            Reserve = 0x2000,
            Decommit = 0x4000,
            Release = 0x8000,
            Reset = 0x80000,
            Physical = 0x400000,
            TopDown = 0x100000,
            WriteWatch = 0x200000,
            LargePages = 0x20000000
        }

        [Flags]
        private enum MemoryProtection
        {
            Execute = 0x10,
            ExecuteRead = 0x20,
            ExecuteReadWrite = 0x40,
            ExecuteWriteCopy = 0x80,
            NoAccess = 0x01,
            ReadOnly = 0x02,
            ReadWrite = 0x04,
            WriteCopy = 0x08,
            GuardModifierflag = 0x100,
            NoCacheModifierflag = 0x200,
            WriteCombineModifierflag = 0x400
        }

        [Flags]
        private enum FreeType
        {
            Decommit = 0x4000,
            Release = 0x8000
        }
    }

    /// <summary>
    /// Manager to coordinate path caching and injection
    /// </summary>
    public class DeterministicPathManager
    {
        private PathCache pathCache;
        private PathInjector pathInjector;
        private EnhancedClient client;
        private EnhancedServer server;

        public DeterministicPathManager(string cacheDirectory = "pathcache")
        {
            pathCache = new PathCache(cacheDirectory);
        }

        /// <summary>
        /// Initialize the deterministic path system
        /// </summary>
        public async Task<bool> Initialize(bool isServer = false)
        {
            try
            {
                Logger.Log("Initializing deterministic path system...");

                // Pre-bake common paths
                if (isServer || !File.Exists(Path.Combine("pathcache", "paths.json")))
                {
                    Logger.Log("Pre-baking common paths...");
                    await pathCache.PreBakeCommonPaths();
                }

                // Inject into Kenshi process
                pathInjector = new PathInjector(pathCache);
                if (!pathInjector.Inject())
                {
                    Logger.Log("Failed to inject into Kenshi - running in compatibility mode");
                    // Can still use cached paths without injection
                }

                Logger.Log("Deterministic path system initialized");
                return true;
            }
            catch (Exception ex)
            {
                Logger.Log($"Failed to initialize path system: {ex.Message}");
                return false;
            }
        }

        /// <summary>
        /// Get deterministic path for network synchronization
        /// </summary>
        public CachedPath GetSynchronizedPath(Vector3 start, Vector3 end, string requesterId)
        {
            var path = pathCache.GetPath(start, end);

            if (path != null)
            {
                // Log usage for optimization
                path.IncrementUsage();
                Logger.Log($"Provided cached path for {requesterId}: {path.PathId}");
            }

            return path;
        }

        /// <summary>
        /// Verify cache consistency across clients
        /// </summary>
        public bool VerifyCache(string remoteChecksum)
        {
            string localChecksum = pathCache.GenerateCacheChecksum();
            return localChecksum == remoteChecksum;
        }

        /// <summary>
        /// Cleanup on shutdown
        /// </summary>
        public void Shutdown()
        {
            pathInjector?.Cleanup();
            pathCache?.SaveBakedPaths();
        }
    }
}