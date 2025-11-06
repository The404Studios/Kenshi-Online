using System;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using Microsoft.Win32;
using KenshiMultiplayer.Game;

namespace KenshiMultiplayer.Managers
{
    /// <summary>
    /// Manages the server-hosted Kenshi game instance
    /// Launches Kenshi, injects mod, loads world, and manages game state
    /// </summary>
    public class GameHostManager : IDisposable
    {
        private Process? _kenshiProcess;
        private KenshiGameBridge? _gameBridge;
        private ModInjector? _modInjector;
        private SaveGameLoader? _saveLoader;

        private string _kenshiInstallPath;
        private string _kenshiExecutable;
        private string _modDllPath;
        private string _serverWorldSavePath;

        private bool _isHosting;
        private readonly object _hostLock = new object();

        // Configuration
        public string ServerWorldName { get; set; } = "KenshiOnlineWorld";
        public bool UseExistingSave { get; set; } = false;
        public string? ExistingSaveName { get; set; }
        public bool MinimizeWindow { get; set; } = true;
        public bool AutoSave { get; set; } = true;
        public int AutoSaveIntervalMinutes { get; set; } = 10;

        // Events
        public event Action? OnGameStarted;
        public event Action? OnGameStopped;
        public event Action<string>? OnPlayerJoined;
        public event Action<string>? OnPlayerLeft;

        public GameHostManager(string? kenshiPath = null)
        {
            _kenshiInstallPath = kenshiPath ?? FindKenshiInstallPath();
            _kenshiExecutable = Path.Combine(_kenshiInstallPath, "kenshi_x64.exe");
            _modDllPath = Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "KenshiOnlineMod.dll");

            _saveLoader = new SaveGameLoader(_kenshiInstallPath);

            ValidateFiles();
        }

        /// <summary>
        /// Start hosting the game server
        /// </summary>
        public async Task<bool> StartHosting()
        {
            lock (_hostLock)
            {
                if (_isHosting)
                {
                    Console.WriteLine("Game server is already hosting");
                    return false;
                }
                _isHosting = true;
            }

            try
            {
                Console.WriteLine("====================================");
                Console.WriteLine("  Starting Kenshi Game Server");
                Console.WriteLine("====================================");

                // Step 1: Prepare the save game
                Console.WriteLine("Preparing server world...");
                bool worldReady = await PrepareServerWorld();
                if (!worldReady)
                {
                    Console.WriteLine("Failed to prepare server world");
                    return false;
                }

                // Step 2: Launch Kenshi
                Console.WriteLine($"Launching Kenshi from: {_kenshiExecutable}");
                bool launched = LaunchKenshi();
                if (!launched)
                {
                    Console.WriteLine("Failed to launch Kenshi");
                    return false;
                }

                // Step 3: Wait for Kenshi to initialize
                Console.WriteLine("Waiting for Kenshi to initialize...");
                await Task.Delay(5000);

                // Step 4: Inject the mod
                Console.WriteLine("Injecting multiplayer mod...");
                bool injected = InjectMod();
                if (!injected)
                {
                    Console.WriteLine("Failed to inject mod");
                    StopHosting();
                    return false;
                }

                // Step 5: Wait for mod to initialize
                await Task.Delay(2000);

                // Step 6: Initialize game bridge
                Console.WriteLine("Establishing game bridge connection...");
                _gameBridge = new KenshiGameBridge(_kenshiProcess!);

                // Step 7: Load the world
                Console.WriteLine($"Loading world: {ServerWorldName}");
                bool worldLoaded = await LoadServerWorld();
                if (!worldLoaded)
                {
                    Console.WriteLine("Warning: Failed to auto-load world, manual load may be required");
                }

                // Step 8: Start auto-save timer
                if (AutoSave)
                {
                    StartAutoSaveTimer();
                }

                Console.WriteLine("====================================");
                Console.WriteLine("  Game Server Started Successfully!");
                Console.WriteLine($"  World: {ServerWorldName}");
                Console.WriteLine($"  Process ID: {_kenshiProcess?.Id}");
                Console.WriteLine("  Players can now connect!");
                Console.WriteLine("====================================");

                OnGameStarted?.Invoke();

                // Monitor the game process
                _ = Task.Run(() => MonitorGameProcess());

                return true;
            }
            catch (Exception ex)
            {
                Console.WriteLine($"Error starting game server: {ex.Message}");
                _isHosting = false;
                return false;
            }
        }

        /// <summary>
        /// Stop hosting the game server
        /// </summary>
        public void StopHosting()
        {
            lock (_hostLock)
            {
                if (!_isHosting)
                {
                    return;
                }
                _isHosting = false;
            }

            Console.WriteLine("Stopping game server...");

            try
            {
                // Save the world first
                if (_gameBridge != null && _kenshiProcess != null && !_kenshiProcess.HasExited)
                {
                    Console.WriteLine("Saving world...");
                    SaveWorld();
                    Thread.Sleep(2000); // Give it time to save
                }

                // Close Kenshi gracefully
                if (_kenshiProcess != null && !_kenshiProcess.HasExited)
                {
                    Console.WriteLine("Closing Kenshi...");
                    _kenshiProcess.CloseMainWindow();

                    // Wait up to 10 seconds for graceful shutdown
                    if (!_kenshiProcess.WaitForExit(10000))
                    {
                        Console.WriteLine("Force killing Kenshi process...");
                        _kenshiProcess.Kill();
                    }
                }

                _kenshiProcess?.Dispose();
                _kenshiProcess = null;
                _gameBridge?.Dispose();
                _gameBridge = null;

                Console.WriteLine("Game server stopped");
                OnGameStopped?.Invoke();
            }
            catch (Exception ex)
            {
                Console.WriteLine($"Error stopping game server: {ex.Message}");
            }
        }

        /// <summary>
        /// Prepare the server world save
        /// </summary>
        private async Task<bool> PrepareServerWorld()
        {
            try
            {
                string savesPath = Path.Combine(
                    Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
                    "Kenshi", "save"
                );

                _serverWorldSavePath = Path.Combine(savesPath, ServerWorldName);

                if (UseExistingSave && !string.IsNullOrEmpty(ExistingSaveName))
                {
                    // Copy existing save
                    string existingSavePath = Path.Combine(savesPath, ExistingSaveName);

                    if (!Directory.Exists(existingSavePath))
                    {
                        Console.WriteLine($"Existing save not found: {ExistingSaveName}");
                        return false;
                    }

                    // Copy to server world name
                    if (Directory.Exists(_serverWorldSavePath))
                    {
                        Directory.Delete(_serverWorldSavePath, true);
                    }

                    CopyDirectory(existingSavePath, _serverWorldSavePath);
                    Console.WriteLine($"Copied save '{ExistingSaveName}' to '{ServerWorldName}'");
                }
                else if (!Directory.Exists(_serverWorldSavePath))
                {
                    // Create new world
                    Console.WriteLine("Creating new server world...");

                    // Create basic save structure
                    Directory.CreateDirectory(_serverWorldSavePath);
                    Directory.CreateDirectory(Path.Combine(_serverWorldSavePath, "gamedata"));
                    Directory.CreateDirectory(Path.Combine(_serverWorldSavePath, "platoon"));
                    Directory.CreateDirectory(Path.Combine(_serverWorldSavePath, "zone"));

                    // Create metadata
                    string metadataPath = Path.Combine(_serverWorldSavePath, "multiplayer_server.json");
                    var metadata = new
                    {
                        ServerWorld = true,
                        Created = DateTime.Now.ToString("o"),
                        WorldName = ServerWorldName
                    };

                    await File.WriteAllTextAsync(metadataPath,
                        Newtonsoft.Json.JsonConvert.SerializeObject(metadata, Newtonsoft.Json.Formatting.Indented));

                    Console.WriteLine("New server world created");
                }
                else
                {
                    Console.WriteLine($"Using existing server world: {ServerWorldName}");
                }

                return true;
            }
            catch (Exception ex)
            {
                Console.WriteLine($"Error preparing server world: {ex.Message}");
                return false;
            }
        }

        /// <summary>
        /// Launch Kenshi process
        /// </summary>
        private bool LaunchKenshi()
        {
            try
            {
                var startInfo = new ProcessStartInfo
                {
                    FileName = _kenshiExecutable,
                    WorkingDirectory = _kenshiInstallPath,
                    UseShellExecute = true,
                    WindowStyle = MinimizeWindow ? ProcessWindowStyle.Minimized : ProcessWindowStyle.Normal
                };

                _kenshiProcess = Process.Start(startInfo);

                if (_kenshiProcess == null)
                {
                    Console.WriteLine("Failed to start Kenshi process");
                    return false;
                }

                Console.WriteLine($"Kenshi started (PID: {_kenshiProcess.Id})");
                return true;
            }
            catch (Exception ex)
            {
                Console.WriteLine($"Error launching Kenshi: {ex.Message}");
                return false;
            }
        }

        /// <summary>
        /// Inject the mod into Kenshi
        /// </summary>
        private bool InjectMod()
        {
            try
            {
                if (_kenshiProcess == null || _kenshiProcess.HasExited)
                {
                    Console.WriteLine("Kenshi process not running");
                    return false;
                }

                _modInjector = new ModInjector();
                bool success = _modInjector.InjectMod(_kenshiProcess.Id, _modDllPath);

                if (success)
                {
                    Console.WriteLine("Mod injected successfully");
                }
                else
                {
                    Console.WriteLine("Mod injection failed");
                }

                return success;
            }
            catch (Exception ex)
            {
                Console.WriteLine($"Error injecting mod: {ex.Message}");
                return false;
            }
        }

        /// <summary>
        /// Load the server world (attempts to automate, may require manual intervention)
        /// </summary>
        private async Task<bool> LoadServerWorld()
        {
            try
            {
                // Create a signal file that the mod can detect
                string signalPath = Path.Combine(
                    Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
                    "Kenshi", "save", "load_server_world.txt"
                );

                await File.WriteAllTextAsync(signalPath, ServerWorldName);

                Console.WriteLine($"Server world load signal created: {ServerWorldName}");
                Console.WriteLine("NOTE: You may need to manually load the save from the Kenshi menu");

                return true;
            }
            catch (Exception ex)
            {
                Console.WriteLine($"Error signaling world load: {ex.Message}");
                return false;
            }
        }

        /// <summary>
        /// Save the current world
        /// </summary>
        public void SaveWorld()
        {
            try
            {
                if (_gameBridge == null)
                {
                    Console.WriteLine("Game bridge not initialized");
                    return;
                }

                // Create save signal
                string signalPath = Path.Combine(
                    Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
                    "Kenshi", "save", "save_world_signal.txt"
                );

                File.WriteAllText(signalPath, DateTime.Now.ToString("o"));

                Console.WriteLine("World save requested");
            }
            catch (Exception ex)
            {
                Console.WriteLine($"Error saving world: {ex.Message}");
            }
        }

        /// <summary>
        /// Start auto-save timer
        /// </summary>
        private void StartAutoSaveTimer()
        {
            _ = Task.Run(async () =>
            {
                while (_isHosting)
                {
                    await Task.Delay(TimeSpan.FromMinutes(AutoSaveIntervalMinutes));

                    if (_isHosting)
                    {
                        Console.WriteLine($"Auto-saving world... (interval: {AutoSaveIntervalMinutes} minutes)");
                        SaveWorld();
                    }
                }
            });
        }

        /// <summary>
        /// Monitor the game process
        /// </summary>
        private async Task MonitorGameProcess()
        {
            while (_isHosting)
            {
                await Task.Delay(1000);

                if (_kenshiProcess != null && _kenshiProcess.HasExited)
                {
                    Console.WriteLine("Kenshi process has exited unexpectedly");
                    _isHosting = false;
                    OnGameStopped?.Invoke();
                    break;
                }
            }
        }

        /// <summary>
        /// Spawn a player into the world
        /// </summary>
        public bool SpawnPlayer(string playerId, string characterName, float x, float y, float z)
        {
            try
            {
                if (_gameBridge == null)
                {
                    Console.WriteLine("Game bridge not initialized");
                    return false;
                }

                Console.WriteLine($"Spawning player: {characterName} at ({x}, {y}, {z})");

                // Use game bridge to spawn character
                // This will call into Kenshi's spawn system

                OnPlayerJoined?.Invoke(playerId);

                return true;
            }
            catch (Exception ex)
            {
                Console.WriteLine($"Error spawning player: {ex.Message}");
                return false;
            }
        }

        /// <summary>
        /// Find Kenshi installation path
        /// </summary>
        private string FindKenshiInstallPath()
        {
            // Check Steam registry
            try
            {
                using var key = Registry.LocalMachine.OpenSubKey(@"SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Steam App 233860");
                if (key != null)
                {
                    var installLocation = key.GetValue("InstallLocation") as string;
                    if (!string.IsNullOrEmpty(installLocation) && Directory.Exists(installLocation))
                    {
                        Console.WriteLine($"Found Kenshi via Steam registry: {installLocation}");
                        return installLocation;
                    }
                }
            }
            catch { }

            // Check common Steam library paths
            string[] steamPaths = new[]
            {
                @"C:\Program Files (x86)\Steam\steamapps\common\Kenshi",
                @"C:\Program Files\Steam\steamapps\common\Kenshi",
                @"D:\SteamLibrary\steamapps\common\Kenshi",
                @"E:\SteamLibrary\steamapps\common\Kenshi"
            };

            foreach (var path in steamPaths)
            {
                if (Directory.Exists(path) && File.Exists(Path.Combine(path, "kenshi_x64.exe")))
                {
                    Console.WriteLine($"Found Kenshi at: {path}");
                    return path;
                }
            }

            // Default fallback
            return @"C:\Program Files (x86)\Steam\steamapps\common\Kenshi";
        }

        /// <summary>
        /// Validate required files exist
        /// </summary>
        private void ValidateFiles()
        {
            if (!File.Exists(_kenshiExecutable))
            {
                Console.WriteLine($"WARNING: Kenshi executable not found: {_kenshiExecutable}");
                Console.WriteLine("Please ensure Kenshi is installed and the path is correct");
            }

            if (!File.Exists(_modDllPath))
            {
                Console.WriteLine($"WARNING: Mod DLL not found: {_modDllPath}");
                Console.WriteLine("Please ensure KenshiOnlineMod.dll is in the server directory");
            }
        }

        /// <summary>
        /// Copy directory recursively
        /// </summary>
        private void CopyDirectory(string sourceDir, string destDir)
        {
            Directory.CreateDirectory(destDir);

            foreach (string file in Directory.GetFiles(sourceDir))
            {
                string fileName = Path.GetFileName(file);
                string destFile = Path.Combine(destDir, fileName);
                File.Copy(file, destFile, true);
            }

            foreach (string dir in Directory.GetDirectories(sourceDir))
            {
                string dirName = Path.GetFileName(dir);
                string destSubDir = Path.Combine(destDir, dirName);
                CopyDirectory(dir, destSubDir);
            }
        }

        /// <summary>
        /// Get game statistics
        /// </summary>
        public (bool IsHosting, int? ProcessId, string WorldName, int PlayerCount) GetStatus()
        {
            return (
                _isHosting,
                _kenshiProcess?.HasExited == false ? _kenshiProcess.Id : null,
                ServerWorldName,
                0 // TODO: Track connected players
            );
        }

        public void Dispose()
        {
            StopHosting();
        }
    }
}
