using System;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using KenshiMultiplayer.Utility;

namespace KenshiMultiplayer.Game
{
    /// <summary>
    /// Game launcher that starts Kenshi and automatically injects the multiplayer mod
    /// Uses suspended process creation for reliable early injection
    /// </summary>
    public class GameLauncher
    {
        #region Native Imports

        [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
        private static extern bool CreateProcessW(
            string lpApplicationName,
            string lpCommandLine,
            IntPtr lpProcessAttributes,
            IntPtr lpThreadAttributes,
            bool bInheritHandles,
            uint dwCreationFlags,
            IntPtr lpEnvironment,
            string lpCurrentDirectory,
            ref STARTUPINFO lpStartupInfo,
            out PROCESS_INFORMATION lpProcessInformation);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern uint ResumeThread(IntPtr hThread);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool CloseHandle(IntPtr hObject);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern IntPtr OpenProcess(uint dwDesiredAccess, bool bInheritHandle, uint dwProcessId);

        [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Ansi)]
        private static extern IntPtr GetProcAddress(IntPtr hModule, string procName);

        [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
        private static extern IntPtr GetModuleHandleW(string lpModuleName);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern IntPtr VirtualAllocEx(
            IntPtr hProcess,
            IntPtr lpAddress,
            uint dwSize,
            uint flAllocationType,
            uint flProtect);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool VirtualFreeEx(
            IntPtr hProcess,
            IntPtr lpAddress,
            uint dwSize,
            uint dwFreeType);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool WriteProcessMemory(
            IntPtr hProcess,
            IntPtr lpBaseAddress,
            byte[] lpBuffer,
            uint nSize,
            out UIntPtr lpNumberOfBytesWritten);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern IntPtr CreateRemoteThread(
            IntPtr hProcess,
            IntPtr lpThreadAttributes,
            uint dwStackSize,
            IntPtr lpStartAddress,
            IntPtr lpParameter,
            uint dwCreationFlags,
            out uint lpThreadId);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern uint WaitForSingleObject(IntPtr hHandle, uint dwMilliseconds);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool GetExitCodeThread(IntPtr hThread, out uint lpExitCode);

        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
        private struct STARTUPINFO
        {
            public int cb;
            public string lpReserved;
            public string lpDesktop;
            public string lpTitle;
            public int dwX;
            public int dwY;
            public int dwXSize;
            public int dwYSize;
            public int dwXCountChars;
            public int dwYCountChars;
            public int dwFillAttribute;
            public int dwFlags;
            public short wShowWindow;
            public short cbReserved2;
            public IntPtr lpReserved2;
            public IntPtr hStdInput;
            public IntPtr hStdOutput;
            public IntPtr hStdError;
        }

        [StructLayout(LayoutKind.Sequential)]
        private struct PROCESS_INFORMATION
        {
            public IntPtr hProcess;
            public IntPtr hThread;
            public uint dwProcessId;
            public uint dwThreadId;
        }

        // Process creation flags
        private const uint CREATE_SUSPENDED = 0x00000004;
        private const uint CREATE_NEW_CONSOLE = 0x00000010;
        private const uint NORMAL_PRIORITY_CLASS = 0x00000020;

        // Process access rights
        private const uint PROCESS_ALL_ACCESS = 0x001F0FFF;
        private const uint PROCESS_CREATE_THREAD = 0x0002;
        private const uint PROCESS_VM_OPERATION = 0x0008;
        private const uint PROCESS_VM_WRITE = 0x0020;
        private const uint PROCESS_VM_READ = 0x0010;
        private const uint PROCESS_QUERY_INFORMATION = 0x0400;

        // Memory allocation
        private const uint MEM_COMMIT = 0x00001000;
        private const uint MEM_RESERVE = 0x00002000;
        private const uint MEM_RELEASE = 0x00008000;
        private const uint PAGE_READWRITE = 0x04;
        private const uint PAGE_EXECUTE_READWRITE = 0x40;

        // Wait
        private const uint INFINITE = 0xFFFFFFFF;
        private const uint WAIT_OBJECT_0 = 0x00000000;

        #endregion

        private const string LOG_PREFIX = "[GameLauncher] ";

        public event Action<string> OnLogMessage;
        public event Action<LaunchState> OnStateChanged;
        public event Action<Process> OnGameLaunched;
        public event Action<Exception> OnError;

        public Process GameProcess { get; private set; }
        public LaunchState CurrentState { get; private set; } = LaunchState.Idle;
        public bool IsGameRunning => GameProcess != null && !GameProcess.HasExited;

        private string _kenshiPath;
        private string _dllPath;
        private PROCESS_INFORMATION _processInfo;

        public enum LaunchState
        {
            Idle,
            PreparingLaunch,
            CreatingProcess,
            InjectingMod,
            ResumingProcess,
            WaitingForGame,
            Running,
            Error,
            Stopped
        }

        public GameLauncher()
        {
        }

        private void Log(string message)
        {
            string fullMessage = LOG_PREFIX + message;
            Logger.Log(fullMessage);
            OnLogMessage?.Invoke(fullMessage);
        }

        private void SetState(LaunchState state)
        {
            CurrentState = state;
            OnStateChanged?.Invoke(state);
            Log($"State: {state}");
        }

        /// <summary>
        /// Launch Kenshi with automatic mod injection
        /// </summary>
        public async Task<bool> LaunchGameAsync(string kenshiExePath, string modDllPath, string[] gameArgs = null)
        {
            try
            {
                SetState(LaunchState.PreparingLaunch);

                _kenshiPath = kenshiExePath;
                _dllPath = modDllPath;

                // Validate paths
                if (!File.Exists(_kenshiPath))
                {
                    throw new FileNotFoundException($"Kenshi executable not found: {_kenshiPath}");
                }

                if (!File.Exists(_dllPath))
                {
                    throw new FileNotFoundException($"Mod DLL not found: {_dllPath}");
                }

                Log($"Kenshi path: {_kenshiPath}");
                Log($"Mod DLL path: {_dllPath}");

                // Build command line
                string commandLine = $"\"{_kenshiPath}\"";
                if (gameArgs != null && gameArgs.Length > 0)
                {
                    commandLine += " " + string.Join(" ", gameArgs);
                }

                string workingDir = Path.GetDirectoryName(_kenshiPath);

                // Create process in suspended state
                SetState(LaunchState.CreatingProcess);
                if (!CreateSuspendedProcess(commandLine, workingDir))
                {
                    throw new Win32Exception(Marshal.GetLastWin32Error(), "Failed to create Kenshi process");
                }

                Log($"Process created (PID: {_processInfo.dwProcessId})");

                // Wait a moment for process initialization
                await Task.Delay(100);

                // Inject the mod DLL
                SetState(LaunchState.InjectingMod);
                if (!InjectDll(_processInfo.hProcess, _dllPath))
                {
                    // Kill the process if injection fails
                    TerminateProcess();
                    throw new Exception("Failed to inject mod DLL");
                }

                Log("Mod DLL injected successfully");

                // Wait for DLL to initialize
                await Task.Delay(500);

                // Resume the main thread
                SetState(LaunchState.ResumingProcess);
                if (ResumeThread(_processInfo.hThread) == unchecked((uint)-1))
                {
                    throw new Win32Exception(Marshal.GetLastWin32Error(), "Failed to resume process");
                }

                Log("Process resumed");

                // Get Process object
                GameProcess = Process.GetProcessById((int)_processInfo.dwProcessId);

                // Clean up handles
                CloseHandle(_processInfo.hThread);
                CloseHandle(_processInfo.hProcess);

                // Wait for game window
                SetState(LaunchState.WaitingForGame);
                await WaitForGameWindowAsync();

                SetState(LaunchState.Running);
                OnGameLaunched?.Invoke(GameProcess);

                Log("Game launched successfully!");
                return true;
            }
            catch (Exception ex)
            {
                Log($"ERROR: {ex.Message}");
                SetState(LaunchState.Error);
                OnError?.Invoke(ex);
                return false;
            }
        }

        /// <summary>
        /// Launch Kenshi synchronously
        /// </summary>
        public bool LaunchGame(string kenshiExePath, string modDllPath, string[] gameArgs = null)
        {
            return LaunchGameAsync(kenshiExePath, modDllPath, gameArgs).GetAwaiter().GetResult();
        }

        private bool CreateSuspendedProcess(string commandLine, string workingDirectory)
        {
            STARTUPINFO startupInfo = new STARTUPINFO();
            startupInfo.cb = Marshal.SizeOf(startupInfo);

            uint creationFlags = CREATE_SUSPENDED | NORMAL_PRIORITY_CLASS;

            bool result = CreateProcessW(
                null,
                commandLine,
                IntPtr.Zero,
                IntPtr.Zero,
                false,
                creationFlags,
                IntPtr.Zero,
                workingDirectory,
                ref startupInfo,
                out _processInfo);

            return result;
        }

        private bool InjectDll(IntPtr processHandle, string dllPath)
        {
            IntPtr allocatedMem = IntPtr.Zero;
            IntPtr threadHandle = IntPtr.Zero;

            try
            {
                // Get LoadLibraryW address (use Unicode version for better compatibility)
                IntPtr kernel32 = GetModuleHandleW("kernel32.dll");
                if (kernel32 == IntPtr.Zero)
                {
                    Log("ERROR: Could not get kernel32.dll handle");
                    return false;
                }

                IntPtr loadLibraryAddr = GetProcAddress(kernel32, "LoadLibraryW");
                if (loadLibraryAddr == IntPtr.Zero)
                {
                    Log("ERROR: Could not get LoadLibraryW address");
                    return false;
                }

                // Convert DLL path to Unicode bytes
                byte[] dllPathBytes = Encoding.Unicode.GetBytes(dllPath + "\0");

                // Allocate memory in target process
                allocatedMem = VirtualAllocEx(
                    processHandle,
                    IntPtr.Zero,
                    (uint)dllPathBytes.Length,
                    MEM_COMMIT | MEM_RESERVE,
                    PAGE_READWRITE);

                if (allocatedMem == IntPtr.Zero)
                {
                    Log($"ERROR: VirtualAllocEx failed - {Marshal.GetLastWin32Error()}");
                    return false;
                }

                // Write DLL path to allocated memory
                if (!WriteProcessMemory(processHandle, allocatedMem, dllPathBytes, (uint)dllPathBytes.Length, out _))
                {
                    Log($"ERROR: WriteProcessMemory failed - {Marshal.GetLastWin32Error()}");
                    return false;
                }

                // Create remote thread to call LoadLibraryW
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
                    Log($"ERROR: CreateRemoteThread failed - {Marshal.GetLastWin32Error()}");
                    return false;
                }

                // Wait for thread to complete
                uint waitResult = WaitForSingleObject(threadHandle, 10000);
                if (waitResult != WAIT_OBJECT_0)
                {
                    Log($"ERROR: Thread wait failed - {waitResult}");
                    return false;
                }

                // Check thread exit code (should be non-zero if LoadLibrary succeeded)
                if (GetExitCodeThread(threadHandle, out uint exitCode))
                {
                    if (exitCode == 0)
                    {
                        Log("WARNING: LoadLibrary may have failed (exit code 0)");
                    }
                    else
                    {
                        Log($"DLL loaded at address: 0x{exitCode:X}");
                    }
                }

                return true;
            }
            finally
            {
                // Cleanup
                if (threadHandle != IntPtr.Zero)
                    CloseHandle(threadHandle);

                if (allocatedMem != IntPtr.Zero)
                    VirtualFreeEx(processHandle, allocatedMem, 0, MEM_RELEASE);
            }
        }

        private async Task WaitForGameWindowAsync(int timeoutMs = 60000)
        {
            var stopwatch = Stopwatch.StartNew();

            while (stopwatch.ElapsedMilliseconds < timeoutMs)
            {
                if (GameProcess == null || GameProcess.HasExited)
                {
                    throw new Exception("Game process terminated unexpectedly");
                }

                try
                {
                    GameProcess.Refresh();
                    if (GameProcess.MainWindowHandle != IntPtr.Zero)
                    {
                        Log("Game window detected");
                        return;
                    }
                }
                catch (InvalidOperationException)
                {
                    // Process may have exited during refresh - continue waiting
                }

                await Task.Delay(500);
            }

            Log("WARNING: Timeout waiting for game window");
        }

        /// <summary>
        /// Attach to an already running Kenshi process and inject
        /// </summary>
        public bool AttachAndInject(string modDllPath)
        {
            try
            {
                SetState(LaunchState.PreparingLaunch);

                if (!File.Exists(modDllPath))
                {
                    throw new FileNotFoundException($"Mod DLL not found: {modDllPath}");
                }

                // Find Kenshi process
                Process[] processes = Process.GetProcessesByName("kenshi_x64");
                if (processes.Length == 0)
                {
                    processes = Process.GetProcessesByName("kenshi");
                }

                if (processes.Length == 0)
                {
                    throw new Exception("Kenshi process not found. Please start the game first.");
                }

                GameProcess = processes[0];
                Log($"Found Kenshi process (PID: {GameProcess.Id})");

                // Check if already injected
                foreach (ProcessModule module in GameProcess.Modules)
                {
                    if (module.ModuleName.Contains("KenshiOnlineMod"))
                    {
                        Log("Mod is already injected");
                        SetState(LaunchState.Running);
                        return true;
                    }
                }

                // Open process for injection
                SetState(LaunchState.InjectingMod);
                IntPtr processHandle = OpenProcess(
                    PROCESS_CREATE_THREAD | PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ | PROCESS_QUERY_INFORMATION,
                    false,
                    (uint)GameProcess.Id);

                if (processHandle == IntPtr.Zero)
                {
                    throw new Win32Exception(Marshal.GetLastWin32Error(), "Failed to open process. Run as administrator.");
                }

                try
                {
                    if (!InjectDll(processHandle, modDllPath))
                    {
                        throw new Exception("Failed to inject mod DLL");
                    }
                }
                finally
                {
                    CloseHandle(processHandle);
                }

                SetState(LaunchState.Running);
                Log("Mod injected successfully!");
                OnGameLaunched?.Invoke(GameProcess);
                return true;
            }
            catch (Exception ex)
            {
                Log($"ERROR: {ex.Message}");
                SetState(LaunchState.Error);
                OnError?.Invoke(ex);
                return false;
            }
        }

        /// <summary>
        /// Stop the game
        /// </summary>
        public void StopGame()
        {
            try
            {
                if (GameProcess != null && !GameProcess.HasExited)
                {
                    Log("Stopping game...");
                    GameProcess.Kill();
                    GameProcess.WaitForExit(5000);
                }
            }
            catch (Exception ex)
            {
                Log($"Error stopping game: {ex.Message}");
            }
            finally
            {
                GameProcess = null;
                SetState(LaunchState.Stopped);
            }
        }

        private void TerminateProcess()
        {
            try
            {
                if (_processInfo.hProcess != IntPtr.Zero)
                {
                    var process = Process.GetProcessById((int)_processInfo.dwProcessId);
                    process?.Kill();
                }
            }
            catch (ArgumentException)
            {
                // Process already exited - nothing to do
            }
            catch (InvalidOperationException)
            {
                // Process already exited - nothing to do
            }
        }

        /// <summary>
        /// Check if Kenshi is currently running
        /// </summary>
        public static bool IsKenshiRunning()
        {
            var processes = Process.GetProcessesByName("kenshi_x64");
            if (processes.Length == 0)
                processes = Process.GetProcessesByName("kenshi");
            return processes.Length > 0;
        }

        /// <summary>
        /// Find Kenshi executable path
        /// </summary>
        public static string FindKenshiExecutable()
        {
            string kenshiDir = Managers.GameModManager.FindKenshiInstallation();
            if (string.IsNullOrEmpty(kenshiDir))
                return null;

            // Check for 64-bit first
            string exe64 = Path.Combine(kenshiDir, "kenshi_x64.exe");
            if (File.Exists(exe64))
                return exe64;

            // Fallback to 32-bit
            string exe32 = Path.Combine(kenshiDir, "kenshi.exe");
            if (File.Exists(exe32))
                return exe32;

            return null;
        }

        /// <summary>
        /// Get default mod DLL path
        /// </summary>
        public static string GetDefaultModDllPath()
        {
            // Check multiple locations
            string[] possiblePaths = new[]
            {
                Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "KenshiOnlineMod.dll"),
                Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "mods", "KenshiOnlineMod.dll"),
                Path.Combine(Directory.GetCurrentDirectory(), "KenshiOnlineMod.dll"),
                Path.Combine(Directory.GetCurrentDirectory(), "build", "Release", "KenshiOnlineMod.dll"),
                Path.Combine(Directory.GetCurrentDirectory(), "..", "KenshiOnlineMod", "build", "Release", "KenshiOnlineMod.dll"),
            };

            foreach (var path in possiblePaths)
            {
                if (File.Exists(path))
                    return Path.GetFullPath(path);
            }

            return possiblePaths[0]; // Return default path even if doesn't exist
        }
    }
}
