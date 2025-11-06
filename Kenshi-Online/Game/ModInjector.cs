using System;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using KenshiMultiplayer.Utility;

namespace KenshiMultiplayer.Game
{
    /// <summary>
    /// DLL injector to load the Kenshi Online mod into Kenshi process
    /// </summary>
    public class ModInjector
    {
        #region Native Imports

        [DllImport("kernel32.dll")]
        private static extern IntPtr OpenProcess(int dwDesiredAccess, bool bInheritHandle, int dwProcessId);

        [DllImport("kernel32.dll", CharSet = CharSet.Ansi, ExactSpelling = true, SetLastError = true)]
        private static extern IntPtr GetProcAddress(IntPtr hModule, string procName);

        [DllImport("kernel32.dll", CharSet = CharSet.Auto)]
        private static extern IntPtr GetModuleHandle(string lpModuleName);

        [DllImport("kernel32.dll", SetLastError = true, ExactSpelling = true)]
        private static extern IntPtr VirtualAllocEx(IntPtr hProcess, IntPtr lpAddress, uint dwSize, uint flAllocationType, uint flProtect);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool WriteProcessMemory(IntPtr hProcess, IntPtr lpBaseAddress, byte[] lpBuffer, uint nSize, out UIntPtr lpNumberOfBytesWritten);

        [DllImport("kernel32.dll")]
        private static extern IntPtr CreateRemoteThread(IntPtr hProcess, IntPtr lpThreadAttributes, uint dwStackSize, IntPtr lpStartAddress, IntPtr lpParameter, uint dwCreationFlags, IntPtr lpThreadId);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool CloseHandle(IntPtr hObject);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern uint WaitForSingleObject(IntPtr hHandle, uint dwMilliseconds);

        private const int PROCESS_CREATE_THREAD = 0x0002;
        private const int PROCESS_QUERY_INFORMATION = 0x0400;
        private const int PROCESS_VM_OPERATION = 0x0008;
        private const int PROCESS_VM_WRITE = 0x0020;
        private const int PROCESS_VM_READ = 0x0010;

        private const uint MEM_COMMIT = 0x00001000;
        private const uint MEM_RESERVE = 0x00002000;
        private const uint PAGE_READWRITE = 4;

        #endregion

        private Logger logger = new Logger("ModInjector");

        /// <summary>
        /// Inject the Kenshi Online mod DLL into Kenshi process
        /// </summary>
        public bool InjectMod(string dllPath)
        {
            try
            {
                logger.Log("Starting mod injection...");

                // Validate DLL path
                if (!File.Exists(dllPath))
                {
                    logger.Log($"ERROR: DLL not found at {dllPath}");
                    return false;
                }

                logger.Log($"DLL path: {dllPath}");

                // Find Kenshi process
                Process[] processes = Process.GetProcessesByName("kenshi_x64");
                if (processes.Length == 0)
                {
                    processes = Process.GetProcessesByName("kenshi");
                }

                if (processes.Length == 0)
                {
                    logger.Log("ERROR: Kenshi process not found!");
                    return false;
                }

                Process targetProcess = processes[0];
                logger.Log($"Found Kenshi process (PID: {targetProcess.Id})");

                // Open process
                IntPtr processHandle = OpenProcess(
                    PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
                    PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ,
                    false, targetProcess.Id);

                if (processHandle == IntPtr.Zero)
                {
                    logger.Log("ERROR: Failed to open process! Run as administrator.");
                    return false;
                }

                logger.Log("Process opened successfully");

                // Get LoadLibraryA address
                IntPtr loadLibraryAddr = GetProcAddress(GetModuleHandle("kernel32.dll"), "LoadLibraryA");
                if (loadLibraryAddr == IntPtr.Zero)
                {
                    logger.Log("ERROR: Failed to get LoadLibraryA address!");
                    CloseHandle(processHandle);
                    return false;
                }

                logger.Log($"LoadLibraryA address: 0x{loadLibraryAddr.ToInt64():X}");

                // Allocate memory for DLL path in target process
                IntPtr allocMemAddress = VirtualAllocEx(
                    processHandle,
                    IntPtr.Zero,
                    (uint)((dllPath.Length + 1) * Marshal.SizeOf(typeof(char))),
                    MEM_COMMIT | MEM_RESERVE,
                    PAGE_READWRITE);

                if (allocMemAddress == IntPtr.Zero)
                {
                    logger.Log("ERROR: Failed to allocate memory in target process!");
                    CloseHandle(processHandle);
                    return false;
                }

                logger.Log($"Allocated memory at: 0x{allocMemAddress.ToInt64():X}");

                // Write DLL path to allocated memory
                byte[] dllPathBytes = Encoding.ASCII.GetBytes(dllPath);
                bool writeSuccess = WriteProcessMemory(
                    processHandle,
                    allocMemAddress,
                    dllPathBytes,
                    (uint)dllPathBytes.Length,
                    out _);

                if (!writeSuccess)
                {
                    logger.Log("ERROR: Failed to write DLL path to process memory!");
                    CloseHandle(processHandle);
                    return false;
                }

                logger.Log("DLL path written to process memory");

                // Create remote thread to call LoadLibraryA
                IntPtr threadHandle = CreateRemoteThread(
                    processHandle,
                    IntPtr.Zero,
                    0,
                    loadLibraryAddr,
                    allocMemAddress,
                    0,
                    IntPtr.Zero);

                if (threadHandle == IntPtr.Zero)
                {
                    logger.Log("ERROR: Failed to create remote thread!");
                    CloseHandle(processHandle);
                    return false;
                }

                logger.Log("Remote thread created successfully");

                // Wait for thread to complete
                WaitForSingleObject(threadHandle, 5000);
                logger.Log("Thread execution completed");

                // Cleanup
                CloseHandle(threadHandle);
                CloseHandle(processHandle);

                logger.Log("Mod injection successful!");
                return true;
            }
            catch (Exception ex)
            {
                logger.Log($"ERROR during injection: {ex.Message}");
                return false;
            }
        }

        /// <summary>
        /// Check if mod is already loaded
        /// </summary>
        public bool IsModLoaded()
        {
            try
            {
                Process[] processes = Process.GetProcessesByName("kenshi_x64");
                if (processes.Length == 0)
                {
                    processes = Process.GetProcessesByName("kenshi");
                }

                if (processes.Length == 0)
                    return false;

                Process targetProcess = processes[0];

                // Check if KenshiOnlineMod.dll is loaded
                foreach (ProcessModule module in targetProcess.Modules)
                {
                    if (module.ModuleName.Contains("KenshiOnlineMod"))
                    {
                        return true;
                    }
                }

                return false;
            }
            catch
            {
                return false;
            }
        }

        /// <summary>
        /// Build the mod DLL if needed
        /// </summary>
        public bool BuildMod(string modSourcePath, string outputPath)
        {
            try
            {
                logger.Log("Building Kenshi Online mod...");

                // Check if CMake is available
                ProcessStartInfo cmakeInfo = new ProcessStartInfo
                {
                    FileName = "cmake",
                    Arguments = $"-B \"{outputPath}\" -S \"{modSourcePath}\"",
                    UseShellExecute = false,
                    RedirectStandardOutput = true,
                    RedirectStandardError = true,
                    CreateNoWindow = true
                };

                Process cmakeProcess = Process.Start(cmakeInfo);
                if (cmakeProcess == null)
                {
                    logger.Log("ERROR: Failed to start CMake. Make sure CMake is installed.");
                    return false;
                }

                cmakeProcess.WaitForExit();

                if (cmakeProcess.ExitCode != 0)
                {
                    logger.Log("ERROR: CMake configuration failed!");
                    return false;
                }

                logger.Log("CMake configuration successful");

                // Build with CMake
                ProcessStartInfo buildInfo = new ProcessStartInfo
                {
                    FileName = "cmake",
                    Arguments = $"--build \"{outputPath}\" --config Release",
                    UseShellExecute = false,
                    RedirectStandardOutput = true,
                    RedirectStandardError = true,
                    CreateNoWindow = true
                };

                Process buildProcess = Process.Start(buildInfo);
                if (buildProcess == null)
                {
                    logger.Log("ERROR: Failed to start build process.");
                    return false;
                }

                buildProcess.WaitForExit();

                if (buildProcess.ExitCode != 0)
                {
                    logger.Log("ERROR: Build failed!");
                    return false;
                }

                logger.Log("Mod build successful!");
                return true;
            }
            catch (Exception ex)
            {
                logger.Log($"ERROR building mod: {ex.Message}");
                return false;
            }
        }
    }
}
