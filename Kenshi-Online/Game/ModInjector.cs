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

        private const string LOG_PREFIX = "[ModInjector] ";

        /// <summary>
        /// Inject the Kenshi Online mod DLL into Kenshi process
        /// </summary>
        public bool InjectMod(string dllPath)
        {
            try
            {
                Logger.Log(LOG_PREFIX + "Starting mod injection...");

                // Validate DLL path
                if (!File.Exists(dllPath))
                {
                    Logger.Log(LOG_PREFIX + $"ERROR: DLL not found at {dllPath}");
                    return false;
                }

                Logger.Log(LOG_PREFIX + $"DLL path: {dllPath}");

                // Find Kenshi process
                Process[] processes = Process.GetProcessesByName("kenshi_x64");
                if (processes.Length == 0)
                {
                    processes = Process.GetProcessesByName("kenshi");
                }

                if (processes.Length == 0)
                {
                    Logger.Log(LOG_PREFIX + "ERROR: Kenshi process not found!");
                    return false;
                }

                Process targetProcess = processes[0];
                Logger.Log(LOG_PREFIX + $"Found Kenshi process (PID: {targetProcess.Id})");

                // Open process
                IntPtr processHandle = OpenProcess(
                    PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
                    PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ,
                    false, targetProcess.Id);

                if (processHandle == IntPtr.Zero)
                {
                    Logger.Log(LOG_PREFIX + "ERROR: Failed to open process! Run as administrator.");
                    return false;
                }

                Logger.Log(LOG_PREFIX + "Process opened successfully");

                // Get LoadLibraryA address
                IntPtr loadLibraryAddr = GetProcAddress(GetModuleHandle("kernel32.dll"), "LoadLibraryA");
                if (loadLibraryAddr == IntPtr.Zero)
                {
                    Logger.Log(LOG_PREFIX + "ERROR: Failed to get LoadLibraryA address!");
                    CloseHandle(processHandle);
                    return false;
                }

                Logger.Log(LOG_PREFIX + $"LoadLibraryA address: 0x{loadLibraryAddr.ToInt64():X}");

                // Allocate memory for DLL path in target process
                IntPtr allocMemAddress = VirtualAllocEx(
                    processHandle,
                    IntPtr.Zero,
                    (uint)((dllPath.Length + 1) * Marshal.SizeOf(typeof(char))),
                    MEM_COMMIT | MEM_RESERVE,
                    PAGE_READWRITE);

                if (allocMemAddress == IntPtr.Zero)
                {
                    Logger.Log(LOG_PREFIX + "ERROR: Failed to allocate memory in target process!");
                    CloseHandle(processHandle);
                    return false;
                }

                Logger.Log(LOG_PREFIX + $"Allocated memory at: 0x{allocMemAddress.ToInt64():X}");

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
                    Logger.Log(LOG_PREFIX + "ERROR: Failed to write DLL path to process memory!");
                    CloseHandle(processHandle);
                    return false;
                }

                Logger.Log(LOG_PREFIX + "DLL path written to process memory");

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
                    Logger.Log(LOG_PREFIX + "ERROR: Failed to create remote thread!");
                    CloseHandle(processHandle);
                    return false;
                }

                Logger.Log(LOG_PREFIX + "Remote thread created successfully");

                // Wait for thread to complete
                WaitForSingleObject(threadHandle, 5000);
                Logger.Log(LOG_PREFIX + "Thread execution completed");

                // Cleanup
                CloseHandle(threadHandle);
                CloseHandle(processHandle);

                Logger.Log(LOG_PREFIX + "Mod injection successful!");
                return true;
            }
            catch (Exception ex)
            {
                Logger.Log(LOG_PREFIX + $"ERROR during injection: {ex.Message}");
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
                Logger.Log(LOG_PREFIX + "Building Kenshi Online mod...");

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
                    Logger.Log(LOG_PREFIX + "ERROR: Failed to start CMake. Make sure CMake is installed.");
                    return false;
                }

                cmakeProcess.WaitForExit();

                if (cmakeProcess.ExitCode != 0)
                {
                    Logger.Log(LOG_PREFIX + "ERROR: CMake configuration failed!");
                    return false;
                }

                Logger.Log(LOG_PREFIX + "CMake configuration successful");

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
                    Logger.Log(LOG_PREFIX + "ERROR: Failed to start build process.");
                    return false;
                }

                buildProcess.WaitForExit();

                if (buildProcess.ExitCode != 0)
                {
                    Logger.Log(LOG_PREFIX + "ERROR: Build failed!");
                    return false;
                }

                Logger.Log(LOG_PREFIX + "Mod build successful!");
                return true;
            }
            catch (Exception ex)
            {
                Logger.Log(LOG_PREFIX + $"ERROR building mod: {ex.Message}");
                return false;
            }
        }
    }
}
