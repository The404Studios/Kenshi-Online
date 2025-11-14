using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Net;
using System.Net.NetworkInformation;
using System.Net.Sockets;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;
using KenshiOnline.Core.Synchronization;
using KenshiOnline.Core.Session;
using KenshiOnline.Core.Admin;
using KenshiOnline.Core.Chat;
using KenshiOnline.Core.Squad;
using KenshiOnline.Core.Social;
using KenshiOnline.Core.Trading;

namespace KenshiOnline.Launcher
{
    class Program
    {
        private static Config _config = new Config();
        private static readonly string ConfigPath = "kenshi_online.json";
        private static CancellationTokenSource? _serverCts;
        private static CancellationTokenSource? _clientCts;
        private static Task? _serverTask;
        private static Task? _clientTask;

        static async Task Main(string[] args)
        {
            Console.OutputEncoding = System.Text.Encoding.UTF8;

            LoadConfig();

            // Check for command line mode
            if (args.Length > 0)
            {
                await HandleCommandLine(args);
                return;
            }

            // Interactive mode
            await RunInteractiveMode();
        }

        static async Task RunInteractiveMode()
        {
            while (true)
            {
                Console.Clear();
                ShowBanner();
                ShowMainMenu();

                var key = Console.ReadKey(true);
                Console.Clear();

                switch (key.KeyChar)
                {
                    case '1':
                        await RunSoloMode();
                        break;
                    case '2':
                        await HostServer();
                        break;
                    case '3':
                        await JoinServer();
                        break;
                    case '4':
                        ShowSettings();
                        break;
                    case '5':
                        ShowServerHistory();
                        break;
                    case '6':
                        ShowQuickStart();
                        break;
                    case '7':
                        Console.WriteLine("Thank you for playing Kenshi Online!");
                        return;
                    default:
                        break;
                }
            }
        }

        static void ShowBanner()
        {
            Console.ForegroundColor = ConsoleColor.Cyan;
            Console.WriteLine("╔══════════════════════════════════════════════════════════════╗");
            Console.WriteLine("║                                                              ║");
            Console.WriteLine("║          KENSHI ONLINE - Unified Launcher v2.0               ║");
            Console.WriteLine("║                                                              ║");
            Console.WriteLine("╚══════════════════════════════════════════════════════════════╝");
            Console.ResetColor();
            Console.WriteLine();
        }

        static void ShowMainMenu()
        {
            Console.ForegroundColor = ConsoleColor.Yellow;
            Console.WriteLine("  PLAY MODES:");
            Console.ResetColor();
            Console.WriteLine("    [1] 🎮 Solo Mode         - Play alone (local server)");
            Console.WriteLine("    [2] 🌐 Host Server       - Host for friends");
            Console.WriteLine("    [3] 🤝 Join Server       - Join a friend");
            Console.WriteLine();
            Console.ForegroundColor = ConsoleColor.Yellow;
            Console.WriteLine("  OPTIONS:");
            Console.ResetColor();
            Console.WriteLine("    [4] ⚙️  Settings          - Configure launcher");
            Console.WriteLine("    [5] 📜 Server History    - Recent servers");
            Console.WriteLine("    [6] 📖 Quick Start       - How to play");
            Console.WriteLine("    [7] 🚪 Exit");
            Console.WriteLine();

            if (_serverTask != null || _clientTask != null)
            {
                Console.ForegroundColor = ConsoleColor.Green;
                if (_serverTask != null)
                    Console.WriteLine("  ⚡ Server: RUNNING");
                if (_clientTask != null)
                    Console.WriteLine("  ⚡ Client: RUNNING");
                Console.ResetColor();
                Console.WriteLine();
            }

            Console.Write("Select option: ");
        }

        static async Task RunSoloMode()
        {
            Console.WriteLine("╔══════════════════════════════════════════════════════════════╗");
            Console.WriteLine("║                    SOLO MODE                                 ║");
            Console.WriteLine("╚══════════════════════════════════════════════════════════════╝");
            Console.WriteLine();
            Console.WriteLine("Starting local server and client...");
            Console.WriteLine();

            // Start local server on localhost
            _serverCts = new CancellationTokenSource();
            _serverTask = Task.Run(() => RunServer("127.0.0.1", 7777, _serverCts.Token));

            await Task.Delay(2000); // Wait for server to start

            // Start client
            _clientCts = new CancellationTokenSource();
            _clientTask = Task.Run(() => RunClient("127.0.0.1", 7777, _clientCts.Token));

            Console.WriteLine();
            Console.ForegroundColor = ConsoleColor.Green;
            Console.WriteLine("✓ Solo mode active!");
            Console.ResetColor();
            Console.WriteLine();
            Console.WriteLine("You can now:");
            Console.WriteLine("  1. Start Kenshi");
            Console.WriteLine("  2. Inject Re_Kenshi_Plugin.dll into kenshi_x64.exe");
            Console.WriteLine("  3. Play!");
            Console.WriteLine();
            Console.WriteLine("The plugin will automatically connect to your local server.");
            Console.WriteLine();
            Console.WriteLine("Press any key to stop solo mode...");
            Console.ReadKey(true);

            // Cleanup
            _clientCts?.Cancel();
            _serverCts?.Cancel();
            await Task.WhenAll(_serverTask ?? Task.CompletedTask, _clientTask ?? Task.CompletedTask);
            _serverTask = null;
            _clientTask = null;
        }

        static async Task HostServer()
        {
            Console.WriteLine("╔══════════════════════════════════════════════════════════════╗");
            Console.WriteLine("║                    HOST SERVER                               ║");
            Console.WriteLine("╚══════════════════════════════════════════════════════════════╝");
            Console.WriteLine();

            // Auto-detect local IP
            var localIP = GetLocalIPAddress();
            Console.WriteLine($"Your local IP: {localIP}");
            Console.WriteLine();

            Console.Write($"Port (default {_config.DefaultPort}): ");
            var portInput = Console.ReadLine();
            var port = string.IsNullOrWhiteSpace(portInput) ? _config.DefaultPort : int.Parse(portInput);

            Console.Write($"Max players (default {_config.MaxPlayers}): ");
            var maxInput = Console.ReadLine();
            var maxPlayers = string.IsNullOrWhiteSpace(maxInput) ? _config.MaxPlayers : int.Parse(maxInput);

            Console.Write("Server name: ");
            var serverName = Console.ReadLine() ?? "Kenshi Online Server";

            Console.WriteLine();
            Console.WriteLine("Starting server...");
            Console.WriteLine();

            _serverCts = new CancellationTokenSource();
            _serverTask = Task.Run(() => RunServer("0.0.0.0", port, _serverCts.Token, serverName, maxPlayers));

            await Task.Delay(1000);

            Console.ForegroundColor = ConsoleColor.Green;
            Console.WriteLine("✓ Server is running!");
            Console.ResetColor();
            Console.WriteLine();
            Console.WriteLine("═══════════════════════════════════════════════════════════════");
            Console.WriteLine("  SHARE THIS WITH FRIENDS:");
            Console.WriteLine("═══════════════════════════════════════════════════════════════");
            Console.ForegroundColor = ConsoleColor.Yellow;
            Console.WriteLine($"  Server Address: {localIP}:{port}");
            Console.ResetColor();
            Console.WriteLine();
            Console.WriteLine("  Connection string:");
            Console.ForegroundColor = ConsoleColor.Cyan;
            Console.WriteLine($"  kenshi://{localIP}:{port}/{serverName.Replace(" ", "%20")}");
            Console.ResetColor();
            Console.WriteLine("═══════════════════════════════════════════════════════════════");
            Console.WriteLine();
            Console.WriteLine("ℹ️  Make sure to forward port {0} in your router!", port);
            Console.WriteLine("ℹ️  Get your public IP from: https://whatismyip.com");
            Console.WriteLine();
            Console.WriteLine("Server will run in the background.");
            Console.WriteLine("Press any key to continue...");
            Console.ReadKey(true);
        }

        static async Task JoinServer()
        {
            Console.WriteLine("╔══════════════════════════════════════════════════════════════╗");
            Console.WriteLine("║                    JOIN SERVER                               ║");
            Console.WriteLine("╚══════════════════════════════════════════════════════════════╝");
            Console.WriteLine();

            if (_config.ServerHistory.Any())
            {
                Console.WriteLine("Recent servers:");
                for (int i = 0; i < Math.Min(5, _config.ServerHistory.Count); i++)
                {
                    Console.WriteLine($"  [{i + 1}] {_config.ServerHistory[i]}");
                }
                Console.WriteLine();
            }

            Console.Write("Server IP:Port (or paste connection string): ");
            var input = Console.ReadLine() ?? "";

            string ip;
            int port;

            // Check if it's a connection string
            if (input.StartsWith("kenshi://"))
            {
                var uri = new Uri(input);
                ip = uri.Host;
                port = uri.Port > 0 ? uri.Port : _config.DefaultPort;
                Console.WriteLine($"Connecting to: {uri.Host}:{port}");
            }
            else if (int.TryParse(input, out int historyIndex) && historyIndex > 0 && historyIndex <= _config.ServerHistory.Count)
            {
                var server = _config.ServerHistory[historyIndex - 1];
                var parts = server.Split(':');
                ip = parts[0];
                port = int.Parse(parts[1]);
            }
            else
            {
                var parts = input.Split(':');
                ip = parts[0];
                port = parts.Length > 1 ? int.Parse(parts[1]) : _config.DefaultPort;
            }

            Console.WriteLine();
            Console.WriteLine($"Connecting to {ip}:{port}...");
            Console.WriteLine();

            _clientCts = new CancellationTokenSource();
            _clientTask = Task.Run(() => RunClient(ip, port, _clientCts.Token));

            await Task.Delay(1000);

            // Add to history
            var serverAddress = $"{ip}:{port}";
            _config.ServerHistory.Remove(serverAddress);
            _config.ServerHistory.Insert(0, serverAddress);
            if (_config.ServerHistory.Count > 10)
                _config.ServerHistory.RemoveAt(_config.ServerHistory.Count - 1);
            SaveConfig();

            Console.ForegroundColor = ConsoleColor.Green;
            Console.WriteLine("✓ Connected to server!");
            Console.ResetColor();
            Console.WriteLine();
            Console.WriteLine("You can now:");
            Console.WriteLine("  1. Start Kenshi");
            Console.WriteLine("  2. Inject Re_Kenshi_Plugin.dll into kenshi_x64.exe");
            Console.WriteLine("  3. Play!");
            Console.WriteLine();
            Console.WriteLine("Client will run in the background.");
            Console.WriteLine("Press any key to continue...");
            Console.ReadKey(true);
        }

        static void ShowSettings()
        {
            while (true)
            {
                Console.WriteLine("╔══════════════════════════════════════════════════════════════╗");
                Console.WriteLine("║                      SETTINGS                                ║");
                Console.WriteLine("╚══════════════════════════════════════════════════════════════╝");
                Console.WriteLine();
                Console.WriteLine($"  [1] Default Port: {_config.DefaultPort}");
                Console.WriteLine($"  [2] Max Players: {_config.MaxPlayers}");
                Console.WriteLine($"  [3] Player Name: {_config.PlayerName}");
                Console.WriteLine($"  [4] Auto-connect on startup: {_config.AutoConnect}");
                Console.WriteLine($"  [5] Show FPS overlay: {_config.ShowFPS}");
                Console.WriteLine();
                Console.WriteLine("  [S] Save settings");
                Console.WriteLine("  [B] Back to main menu");
                Console.WriteLine();
                Console.Write("Select option: ");

                var key = Console.ReadKey(true);
                Console.Clear();

                switch (key.KeyChar)
                {
                    case '1':
                        Console.Write("New default port: ");
                        if (int.TryParse(Console.ReadLine(), out int port))
                            _config.DefaultPort = port;
                        break;
                    case '2':
                        Console.Write("Max players (1-32): ");
                        if (int.TryParse(Console.ReadLine(), out int max))
                            _config.MaxPlayers = Math.Clamp(max, 1, 32);
                        break;
                    case '3':
                        Console.Write("Your player name: ");
                        _config.PlayerName = Console.ReadLine() ?? "Player";
                        break;
                    case '4':
                        _config.AutoConnect = !_config.AutoConnect;
                        break;
                    case '5':
                        _config.ShowFPS = !_config.ShowFPS;
                        break;
                    case 's':
                    case 'S':
                        SaveConfig();
                        Console.WriteLine("Settings saved!");
                        Thread.Sleep(1000);
                        break;
                    case 'b':
                    case 'B':
                        return;
                }
            }
        }

        static void ShowServerHistory()
        {
            Console.WriteLine("╔══════════════════════════════════════════════════════════════╗");
            Console.WriteLine("║                   SERVER HISTORY                             ║");
            Console.WriteLine("╚══════════════════════════════════════════════════════════════╝");
            Console.WriteLine();

            if (!_config.ServerHistory.Any())
            {
                Console.WriteLine("  No server history yet.");
            }
            else
            {
                Console.WriteLine("  Recent servers:");
                Console.WriteLine();
                for (int i = 0; i < _config.ServerHistory.Count; i++)
                {
                    Console.WriteLine($"    {i + 1}. {_config.ServerHistory[i]}");
                }
            }

            Console.WriteLine();
            Console.WriteLine("Press any key to return...");
            Console.ReadKey(true);
        }

        static void ShowQuickStart()
        {
            Console.WriteLine("╔══════════════════════════════════════════════════════════════╗");
            Console.WriteLine("║                   QUICK START GUIDE                          ║");
            Console.WriteLine("╚══════════════════════════════════════════════════════════════╝");
            Console.WriteLine();
            Console.WriteLine("  HOW TO PLAY:");
            Console.WriteLine();
            Console.WriteLine("  Option 1: SOLO MODE (easiest)");
            Console.WriteLine("    1. Select 'Solo Mode' from main menu");
            Console.WriteLine("    2. Start Kenshi game");
            Console.WriteLine("    3. Inject Re_Kenshi_Plugin.dll into kenshi_x64.exe");
            Console.WriteLine("    4. Play!");
            Console.WriteLine();
            Console.WriteLine("  Option 2: PLAY WITH FRIENDS");
            Console.WriteLine("    Host:");
            Console.WriteLine("      1. Select 'Host Server'");
            Console.WriteLine("      2. Forward the port in your router");
            Console.WriteLine("      3. Share connection string with friends");
            Console.WriteLine("      4. Start Kenshi and inject plugin");
            Console.WriteLine();
            Console.WriteLine("    Join:");
            Console.WriteLine("      1. Select 'Join Server'");
            Console.WriteLine("      2. Paste connection string from friend");
            Console.WriteLine("      3. Start Kenshi and inject plugin");
            Console.WriteLine();
            Console.WriteLine("  RECOMMENDED DLL INJECTOR:");
            Console.WriteLine("    - Process Hacker (https://processhacker.sourceforge.io/)");
            Console.WriteLine("    - Extreme Injector (https://github.com/master131/ExtremeInjector)");
            Console.WriteLine();
            Console.WriteLine("  PLUGIN LOCATION:");
            Console.WriteLine("    bin/Release/Plugin/Re_Kenshi_Plugin.dll");
            Console.WriteLine();
            Console.WriteLine("Press any key to return...");
            Console.ReadKey(true);
        }

        static async Task RunServer(string bindAddress, int port, CancellationToken ct, string serverName = "Kenshi Online Server", int maxPlayers = 32)
        {
            try
            {
                var entityManager = new EntityManager();
                var sessionManager = new SessionManager(maxPlayers);
                var combatSync = new CombatSync(entityManager);
                var inventorySync = new InventorySync(entityManager);
                var worldStateManager = new WorldStateManager();
                var chatSystem = new ChatSystem();
                var squadSystem = new SquadSystem();
                var friendSystem = new FriendSystem();
                var tradingSystem = new TradingSystem();
                var adminCommands = new AdminCommands(sessionManager, entityManager, worldStateManager);

                var listener = new TcpListener(IPAddress.Parse(bindAddress), port);
                listener.Start();

                Console.WriteLine($"[SERVER] Listening on {bindAddress}:{port}");
                Console.WriteLine($"[SERVER] Name: {serverName}");
                Console.WriteLine($"[SERVER] Max Players: {maxPlayers}");

                var lastUpdate = DateTime.UtcNow;
                const float updateRate = 20f; // 20 Hz

                while (!ct.IsCancellationRequested)
                {
                    // Accept new connections
                    if (listener.Pending())
                    {
                        var client = await listener.AcceptTcpClientAsync();
                        Console.WriteLine($"[SERVER] Client connected from {client.Client.RemoteEndPoint}");
                        _ = Task.Run(() => HandleClient(client, sessionManager, ct));
                    }

                    // Update game state
                    var now = DateTime.UtcNow;
                    var deltaTime = (float)(now - lastUpdate).TotalSeconds;

                    if (deltaTime >= 1.0f / updateRate)
                    {
                        entityManager.Update(deltaTime);
                        worldStateManager.Update(deltaTime);
                        combatSync.Update(deltaTime);
                        inventorySync.Update(deltaTime);
                        sessionManager.Update();

                        lastUpdate = now;
                    }

                    await Task.Delay(10, ct);
                }

                listener.Stop();
                Console.WriteLine("[SERVER] Stopped");
            }
            catch (Exception ex)
            {
                Console.WriteLine($"[SERVER] Error: {ex.Message}");
            }
        }

        static async Task HandleClient(TcpClient client, SessionManager sessionManager, CancellationToken ct)
        {
            try
            {
                using var stream = client.GetStream();
                var buffer = new byte[4096];

                while (!ct.IsCancellationRequested && client.Connected)
                {
                    var bytesRead = await stream.ReadAsync(buffer, 0, buffer.Length, ct);
                    if (bytesRead == 0) break;

                    // Process messages
                    // (Message handling logic would go here)
                }
            }
            catch (Exception ex)
            {
                Console.WriteLine($"[SERVER] Client error: {ex.Message}");
            }
            finally
            {
                client.Close();
            }
        }

        static async Task RunClient(string serverIP, int port, CancellationToken ct)
        {
            try
            {
                // Connect to game server
                var tcpClient = new TcpClient();
                await tcpClient.ConnectAsync(serverIP, port);

                Console.WriteLine($"[CLIENT] Connected to {serverIP}:{port}");

                // Start IPC server for plugin
                _ = Task.Run(() => RunIPCServer(tcpClient, ct), ct);

                var stream = tcpClient.GetStream();
                var buffer = new byte[4096];

                while (!ct.IsCancellationRequested && tcpClient.Connected)
                {
                    if (stream.DataAvailable)
                    {
                        var bytesRead = await stream.ReadAsync(buffer, 0, buffer.Length, ct);
                        if (bytesRead == 0) break;

                        // Forward to plugin via IPC
                    }

                    await Task.Delay(10, ct);
                }

                Console.WriteLine("[CLIENT] Disconnected");
            }
            catch (Exception ex)
            {
                Console.WriteLine($"[CLIENT] Error: {ex.Message}");
            }
        }

        static async Task RunIPCServer(TcpClient gameServerConnection, CancellationToken ct)
        {
            try
            {
                // Named pipe IPC server for plugin communication
                Console.WriteLine("[IPC] Waiting for game plugin to connect...");

                // IPC logic would go here

                while (!ct.IsCancellationRequested)
                {
                    await Task.Delay(100, ct);
                }
            }
            catch (Exception ex)
            {
                Console.WriteLine($"[IPC] Error: {ex.Message}");
            }
        }

        static string GetLocalIPAddress()
        {
            try
            {
                var host = Dns.GetHostEntry(Dns.GetHostName());
                foreach (var ip in host.AddressList)
                {
                    if (ip.AddressFamily == AddressFamily.InterNetwork && !IPAddress.IsLoopback(ip))
                    {
                        return ip.ToString();
                    }
                }
            }
            catch { }

            return "127.0.0.1";
        }

        static void LoadConfig()
        {
            try
            {
                if (File.Exists(ConfigPath))
                {
                    var json = File.ReadAllText(ConfigPath);
                    _config = JsonSerializer.Deserialize<Config>(json) ?? new Config();
                }
            }
            catch
            {
                _config = new Config();
            }
        }

        static void SaveConfig()
        {
            try
            {
                var json = JsonSerializer.Serialize(_config, new JsonSerializerOptions { WriteIndented = true });
                File.WriteAllText(ConfigPath, json);
            }
            catch (Exception ex)
            {
                Console.WriteLine($"Failed to save config: {ex.Message}");
            }
        }

        static async Task HandleCommandLine(string[] args)
        {
            var command = args[0].ToLower();

            switch (command)
            {
                case "solo":
                case "-solo":
                case "--solo":
                    _serverCts = new CancellationTokenSource();
                    _clientCts = new CancellationTokenSource();
                    await Task.WhenAll(
                        RunServer("127.0.0.1", 7777, _serverCts.Token),
                        Task.Delay(2000).ContinueWith(_ => RunClient("127.0.0.1", 7777, _clientCts.Token))
                    );
                    break;

                case "host":
                case "-host":
                case "--host":
                    var port = args.Length > 1 ? int.Parse(args[1]) : 7777;
                    _serverCts = new CancellationTokenSource();
                    await RunServer("0.0.0.0", port, _serverCts.Token);
                    break;

                case "join":
                case "-join":
                case "--join":
                    if (args.Length < 2)
                    {
                        Console.WriteLine("Usage: KenshiOnline join <ip:port>");
                        return;
                    }
                    var parts = args[1].Split(':');
                    var ip = parts[0];
                    var joinPort = parts.Length > 1 ? int.Parse(parts[1]) : 7777;
                    _clientCts = new CancellationTokenSource();
                    await RunClient(ip, joinPort, _clientCts.Token);
                    break;

                default:
                    Console.WriteLine("Unknown command. Use: solo, host, or join");
                    break;
            }
        }
    }

    class Config
    {
        public int DefaultPort { get; set; } = 7777;
        public int MaxPlayers { get; set; } = 32;
        public string PlayerName { get; set; } = "Player";
        public bool AutoConnect { get; set; } = false;
        public bool ShowFPS { get; set; } = true;
        public List<string> ServerHistory { get; set; } = new List<string>();
    }
}
