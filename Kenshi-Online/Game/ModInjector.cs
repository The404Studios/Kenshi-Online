using System;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using KenshiMultiplayer.Utility;

namespace KenshiMultiplayer.Game
{
    /// <summary>
    /// Enhanced DLL injector for Kenshi Online mod
    /// Supports multiple injection methods for maximum compatibility
    /// </summary>
    public class ModInjector
    {
        #region Native Imports

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern IntPtr OpenProcess(uint dwDesiredAccess, bool bInheritHandle, int dwProcessId);

        [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Ansi)]
        private static extern IntPtr GetProcAddress(IntPtr hModule, string procName);

        [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
        private static extern IntPtr GetModuleHandleW(string lpModuleName);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern IntPtr VirtualAllocEx(
            IntPtr hProcess, IntPtr lpAddress, uint dwSize, uint flAllocationType, uint flProtect);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool VirtualFreeEx(IntPtr hProcess, IntPtr lpAddress, uint dwSize, uint dwFreeType);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool WriteProcessMemory(
            IntPtr hProcess, IntPtr lpBaseAddress, byte[] lpBuffer, uint nSize, out UIntPtr lpNumberOfBytesWritten);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern IntPtr CreateRemoteThread(
            IntPtr hProcess, IntPtr lpThreadAttributes, uint dwStackSize,
            IntPtr lpStartAddress, IntPtr lpParameter, uint dwCreationFlags, out uint lpThreadId);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool CloseHandle(IntPtr hObject);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern uint WaitForSingleObject(IntPtr hHandle, uint dwMilliseconds);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool GetExitCodeThread(IntPtr hThread, out uint lpExitCode);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool IsWow64Process(IntPtr hProcess, out bool wow64Process);

        // Process access rights
        private const uint PROCESS_ALL_ACCESS = 0x001F0FFF;
        private const uint PROCESS_CREATE_THREAD = 0x0002;
        private const uint PROCESS_QUERY_INFORMATION = 0x0400;
        private const uint PROCESS_VM_OPERATION = 0x0008;
        private const uint PROCESS_VM_WRITE = 0x0020;
        private const uint PROCESS_VM_READ = 0x0010;

        // Memory allocation
        private const uint MEM_COMMIT = 0x00001000;
        private const uint MEM_RESERVE = 0x00002000;
        private const uint MEM_RELEASE = 0x00008000;
        private const uint PAGE_READWRITE = 0x04;

        // Wait constants
        private const uint INFINITE = 0xFFFFFFFF;
        private const uint WAIT_OBJECT_0 = 0x00000000;
        private const uint WAIT_TIMEOUT = 0x00000102;

        #endregion

        private const string LOG_PREFIX = "[ModInjector] ";

        public event Action<string> OnLogMessage;
        public event Action<InjectionResult> OnInjectionComplete;

        public enum InjectionResult
        {
            Success,
            ProcessNotFound,
            DllNotFound,
            AccessDenied,
            AllocationFailed,
            WriteFailed,
            ThreadCreationFailed,
            LoadLibraryFailed,
            AlreadyInjected,
            ArchitectureMismatch,
            Unknown
        }

        private void Log(string message)
        {
            string fullMessage = LOG_PREFIX + message;
            Logger.Log(fullMessage);
            OnLogMessage?.Invoke(fullMessage);
        }

        /// <summary>
        /// Inject the Kenshi Online mod DLL into running Kenshi process
        /// </summary>
        public InjectionResult InjectMod(string dllPath)
        {
            IntPtr processHandle = IntPtr.Zero;
            IntPtr allocatedMem = IntPtr.Zero;
            IntPtr threadHandle = IntPtr.Zero;

            try
            {
                Log("Starting mod injection...");

                // Validate DLL path
                if (!File.Exists(dllPath))
                {
                    Log($"ERROR: DLL not found at {dllPath}");
                    return InjectionResult.DllNotFound;
                }

                dllPath = Path.GetFullPath(dllPath);
                Log($"DLL path: {dllPath}");

                // Find Kenshi process
                Process targetProcess = FindKenshiProcess();
                if (targetProcess == null)
                {
                    Log("ERROR: Kenshi process not found!");
                    return InjectionResult.ProcessNotFound;
                }

                Log($"Found Kenshi process (PID: {targetProcess.Id}, Name: {targetProcess.ProcessName})");

                // Check if already injected
                if (IsModLoaded(targetProcess))
                {
                    Log("Mod is already loaded in process");
                    return InjectionResult.AlreadyInjected;
                }

                // Check architecture match
                if (!CheckArchitectureMatch(targetProcess, dllPath))
                {
                    Log("ERROR: Architecture mismatch between process and DLL");
                    return InjectionResult.ArchitectureMismatch;
                }

                // Open process with required access
                processHandle = OpenProcess(
                    PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
                    PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ,
                    false, targetProcess.Id);

                if (processHandle == IntPtr.Zero)
                {
                    int error = Marshal.GetLastWin32Error();
                    Log($"ERROR: Failed to open process! Error: {error}. Run as administrator.");
                    return InjectionResult.AccessDenied;
                }

                Log("Process opened successfully");

                // Get LoadLibraryW address
                IntPtr kernel32 = GetModuleHandleW("kernel32.dll");
                IntPtr loadLibraryAddr = GetProcAddress(kernel32, "LoadLibraryW");

                if (loadLibraryAddr == IntPtr.Zero)
                {
                    Log("ERROR: Failed to get LoadLibraryW address!");
                    return InjectionResult.Unknown;
                }

                Log($"LoadLibraryW address: 0x{loadLibraryAddr.ToInt64():X}");

                // Convert path to Unicode and allocate memory
                byte[] dllPathBytes = Encoding.Unicode.GetBytes(dllPath + "\0");

                allocatedMem = VirtualAllocEx(
                    processHandle,
                    IntPtr.Zero,
                    (uint)dllPathBytes.Length,
                    MEM_COMMIT | MEM_RESERVE,
                    PAGE_READWRITE);

                if (allocatedMem == IntPtr.Zero)
                {
                    int error = Marshal.GetLastWin32Error();
                    Log($"ERROR: Failed to allocate memory! Error: {error}");
                    return InjectionResult.AllocationFailed;
                }

                Log($"Allocated memory at: 0x{allocatedMem.ToInt64():X}");

                // Write DLL path
                bool writeSuccess = WriteProcessMemory(
                    processHandle,
                    allocatedMem,
                    dllPathBytes,
                    (uint)dllPathBytes.Length,
                    out _);

                if (!writeSuccess)
                {
                    int error = Marshal.GetLastWin32Error();
                    Log($"ERROR: Failed to write DLL path! Error: {error}");
                    return InjectionResult.WriteFailed;
                }

                Log("DLL path written to process memory");

                // Create remote thread
                threadHandle = CreateRemoteThread(
                    processHandle,
                    IntPtr.Zero,
                    0,
                    loadLibraryAddr,
                    allocatedMem,
                    0,
                    out _);

                if (threadHandle == IntPtr.Zero)
                {
                    int error = Marshal.GetLastWin32Error();
                    Log($"ERROR: Failed to create remote thread! Error: {error}");
                    return InjectionResult.ThreadCreationFailed;
                }

                Log("Remote thread created successfully");

                // Wait for thread to complete
                uint waitResult = WaitForSingleObject(threadHandle, 10000);

                if (waitResult == WAIT_TIMEOUT)
                {
                    Log("WARNING: Thread wait timed out");
                }
                else if (waitResult != WAIT_OBJECT_0)
                {
                    Log($"WARNING: Unexpected wait result: {waitResult}");
                }

                // Check if LoadLibrary succeeded
                if (GetExitCodeThread(threadHandle, out uint exitCode))
                {
                    if (exitCode == 0)
                    {
                        Log("ERROR: LoadLibrary returned NULL - DLL failed to load");
                        return InjectionResult.LoadLibraryFailed;
                    }
                    Log($"DLL loaded at module address: 0x{exitCode:X}");
                }

                Log("Mod injection successful!");
                OnInjectionComplete?.Invoke(InjectionResult.Success);
                return InjectionResult.Success;
            }
            catch (Exception ex)
            {
                Log($"ERROR during injection: {ex.Message}");
                return InjectionResult.Unknown;
            }
            finally
            {
                // Cleanup
                if (threadHandle != IntPtr.Zero)
                    CloseHandle(threadHandle);

                if (allocatedMem != IntPtr.Zero && processHandle != IntPtr.Zero)
                    VirtualFreeEx(processHandle, allocatedMem, 0, MEM_RELEASE);

                if (processHandle != IntPtr.Zero)
                    CloseHandle(processHandle);
            }
        }

        /// <summary>
        /// Inject into a specific process handle (used by GameLauncher)
        /// </summary>
        public InjectionResult InjectIntoProcess(IntPtr processHandle, string dllPath)
        {
            IntPtr allocatedMem = IntPtr.Zero;
            IntPtr threadHandle = IntPtr.Zero;

            try
            {
                if (!File.Exists(dllPath))
                {
                    Log($"ERROR: DLL not found at {dllPath}");
                    return InjectionResult.DllNotFound;
                }

                dllPath = Path.GetFullPath(dllPath);

                // Get LoadLibraryW address
                IntPtr kernel32 = GetModuleHandleW("kernel32.dll");
                IntPtr loadLibraryAddr = GetProcAddress(kernel32, "LoadLibraryW");

                if (loadLibraryAddr == IntPtr.Zero)
                {
                    return InjectionResult.Unknown;
                }

                // Allocate and write
                byte[] dllPathBytes = Encoding.Unicode.GetBytes(dllPath + "\0");

                allocatedMem = VirtualAllocEx(
                    processHandle, IntPtr.Zero, (uint)dllPathBytes.Length,
                    MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);

                if (allocatedMem == IntPtr.Zero)
                    return InjectionResult.AllocationFailed;

                if (!WriteProcessMemory(processHandle, allocatedMem, dllPathBytes, (uint)dllPathBytes.Length, out _))
                    return InjectionResult.WriteFailed;

                // Create thread
                threadHandle = CreateRemoteThread(processHandle, IntPtr.Zero, 0, loadLibraryAddr, allocatedMem, 0, out _);

                if (threadHandle == IntPtr.Zero)
                    return InjectionResult.ThreadCreationFailed;

                WaitForSingleObject(threadHandle, 10000);

                if (GetExitCodeThread(threadHandle, out uint exitCode) && exitCode == 0)
                    return InjectionResult.LoadLibraryFailed;

                return InjectionResult.Success;
            }
            finally
            {
                if (threadHandle != IntPtr.Zero)
                    CloseHandle(threadHandle);

                if (allocatedMem != IntPtr.Zero)
                    VirtualFreeEx(processHandle, allocatedMem, 0, MEM_RELEASE);
            }
        }

        /// <summary>
        /// Find running Kenshi process
        /// </summary>
        public static Process FindKenshiProcess()
        {
            // Try 64-bit first
            Process[] processes = Process.GetProcessesByName("kenshi_x64");
            if (processes.Length > 0)
                return processes[0];

            // Fallback to 32-bit
            processes = Process.GetProcessesByName("kenshi");
            if (processes.Length > 0)
                return processes[0];

            return null;
        }

        /// <summary>
        /// Check if mod is already loaded
        /// </summary>
        public bool IsModLoaded()
        {
            var process = FindKenshiProcess();
            return process != null && IsModLoaded(process);
        }

        /// <summary>
        /// Check if mod is loaded in specific process
        /// </summary>
        public static bool IsModLoaded(Process process)
        {
            try
            {
                foreach (ProcessModule module in process.Modules)
                {
                    if (module.ModuleName.Contains("KenshiOnlineMod", StringComparison.OrdinalIgnoreCase))
                    {
                        return true;
                    }
                }
            }
            catch (Exception ex)
            {
                // Module enumeration can fail for various reasons (access denied, process exited, etc.)
                System.Diagnostics.Debug.WriteLine($"[ModInjector] Failed to enumerate modules: {ex.Message}");
            }

            return false;
        }

        /// <summary>
        /// Check if process and DLL architectures match
        /// </summary>
        private bool CheckArchitectureMatch(Process process, string dllPath)
        {
            try
            {
                // Check if target is 32-bit on 64-bit Windows
                bool isWow64 = false;
                IsWow64Process(process.Handle, out isWow64);

                bool processIs64Bit = !isWow64 && Environment.Is64BitOperatingSystem;

                // Check DLL architecture by reading PE header
                using (var stream = new FileStream(dllPath, FileMode.Open, FileAccess.Read))
                using (var reader = new BinaryReader(stream))
                {
                    // Read DOS header
                    stream.Seek(0x3C, SeekOrigin.Begin);
                    int peOffset = reader.ReadInt32();

                    // Read PE signature
                    stream.Seek(peOffset, SeekOrigin.Begin);
                    uint peSignature = reader.ReadUInt32();

                    if (peSignature != 0x4550) // "PE\0\0"
                        return true; // Can't determine, assume ok

                    // Read machine type
                    ushort machine = reader.ReadUInt16();

                    bool dllIs64Bit = machine == 0x8664; // AMD64

                    if (processIs64Bit != dllIs64Bit)
                    {
                        Log($"Architecture mismatch: Process={processIs64Bit}, DLL={dllIs64Bit}");
                        return false;
                    }
                }

                return true;
            }
            catch
            {
                return true; // Assume ok if we can't check
            }
        }

        /// <summary>
        /// Build the mod DLL using CMake
        /// </summary>
        public bool BuildMod(string modSourcePath, string outputPath)
        {
            try
            {
                Log("Building Kenshi Online mod...");

                Directory.CreateDirectory(outputPath);

                // Configure with CMake
                var cmakeConfig = new ProcessStartInfo
                {
                    FileName = "cmake",
                    Arguments = $"-B \"{outputPath}\" -S \"{modSourcePath}\" -DCMAKE_BUILD_TYPE=Release",
                    UseShellExecute = false,
                    RedirectStandardOutput = true,
                    RedirectStandardError = true,
                    CreateNoWindow = true
                };

                using (var process = Process.Start(cmakeConfig))
                {
                    process.WaitForExit();
                    if (process.ExitCode != 0)
                    {
                        string error = process.StandardError.ReadToEnd();
                        Log($"CMake configuration failed: {error}");
                        return false;
                    }
                }

                Log("CMake configuration successful");

                // Build
                var cmakeBuild = new ProcessStartInfo
                {
                    FileName = "cmake",
                    Arguments = $"--build \"{outputPath}\" --config Release",
                    UseShellExecute = false,
                    RedirectStandardOutput = true,
                    RedirectStandardError = true,
                    CreateNoWindow = true
                };

                using (var process = Process.Start(cmakeBuild))
                {
                    process.WaitForExit();
                    if (process.ExitCode != 0)
                    {
                        string error = process.StandardError.ReadToEnd();
                        Log($"Build failed: {error}");
                        return false;
                    }
                }

                Log("Mod build successful!");
                return true;
            }
            catch (Exception ex)
            {
                Log($"ERROR building mod: {ex.Message}");
                return false;
            }
        }

        /// <summary>
        /// Unload the mod from Kenshi process
        /// </summary>
        public bool UnloadMod()
        {
            try
            {
                var process = FindKenshiProcess();
                if (process == null)
                {
                    Log("Kenshi process not found");
                    return false;
                }

                // Find our module
                ProcessModule targetModule = null;
                foreach (ProcessModule module in process.Modules)
                {
                    if (module.ModuleName.Contains("KenshiOnlineMod", StringComparison.OrdinalIgnoreCase))
                    {
                        targetModule = module;
                        break;
                    }
                }

                if (targetModule == null)
                {
                    Log("Mod module not found");
                    return false;
                }

                IntPtr processHandle = OpenProcess(
                    PROCESS_CREATE_THREAD | PROCESS_VM_OPERATION | PROCESS_QUERY_INFORMATION,
                    false, process.Id);

                if (processHandle == IntPtr.Zero)
                {
                    Log("Failed to open process for unload");
                    return false;
                }

                try
                {
                    IntPtr kernel32 = GetModuleHandleW("kernel32.dll");
                    IntPtr freeLibraryAddr = GetProcAddress(kernel32, "FreeLibrary");

                    IntPtr threadHandle = CreateRemoteThread(
                        processHandle, IntPtr.Zero, 0,
                        freeLibraryAddr, targetModule.BaseAddress, 0, out _);

                    if (threadHandle != IntPtr.Zero)
                    {
                        WaitForSingleObject(threadHandle, 5000);
                        CloseHandle(threadHandle);
                        Log("Mod unloaded successfully");
                        return true;
                    }
                }
                finally
                {
                    CloseHandle(processHandle);
                }

                return false;
            }
            catch (Exception ex)
            {
                Log($"ERROR unloading mod: {ex.Message}");
                return false;
            }
        }
    }
}
