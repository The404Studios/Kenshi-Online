using System;
using System.Threading.Tasks;

namespace KenshiMultiplayer
{
    /// <summary>
    /// Kenshi Online - Multiplayer Mod
    ///
    /// Three modes:
    /// 1. Dedicated Server - Standalone server, no Kenshi needed
    /// 2. Client - Connect to server, runs alongside Kenshi
    /// 3. Host Mode - One player hosts, others connect (deprecated)
    /// </summary>
    public class Program
    {
        public static async Task Main(string[] args)
        {
            Console.Title = "Kenshi Online";

            // Command line arguments
            if (args.Length > 0)
            {
                string mode = args[0].ToLower();

                // DEDICATED SERVER (Recommended)
                if (mode == "--dedicated" || mode == "-d" || mode == "dedicated")
                {
                    int port = args.Length > 1 && int.TryParse(args[1], out int p) ? p : 7777;
                    DedicatedServerMode.RunDedicatedServer(port);
                    return;
                }

                // CLIENT (Connect to dedicated server)
                if (mode == "--connect" || mode == "-c" || mode == "connect")
                {
                    string ip = args.Length > 1 ? args[1] : "localhost";
                    int port = args.Length > 2 && int.TryParse(args[2], out int p) ? p : 7777;
                    DedicatedServerMode.RunDedicatedClient(ip, port);
                    return;
                }

                // HOST MODE (Host runs Kenshi, others control squads)
                if (mode == "--host" || mode == "-h" || mode == "host")
                {
                    int port = args.Length > 1 && int.TryParse(args[1], out int p) ? p : 7777;
                    HostCoop.RunHost(port);
                    return;
                }

                // JOIN HOST (Connect to host's game)
                if (mode == "--join" || mode == "-j" || mode == "join")
                {
                    string ip = args.Length > 1 ? args[1] : "localhost";
                    int port = args.Length > 2 && int.TryParse(args[2], out int p) ? p : 7777;
                    HostCoop.RunClient(ip, port);
                    return;
                }

                // Legacy modes
                if (mode == "--test-server" || mode == "-ts")
                {
                    int port = args.Length > 1 && int.TryParse(args[1], out int p) ? p : 7777;
                    QuickTest.RunServer(port);
                    return;
                }
                if (mode == "--test-client" || mode == "-tc")
                {
                    string ip = args.Length > 1 ? args[1] : "localhost";
                    int port = args.Length > 2 && int.TryParse(args[2], out int p) ? p : 7777;
                    QuickTest.RunClient(ip, port);
                    return;
                }
                if (mode == "-server" || mode == "--server")
                {
                    await EnhancedProgram.Main(new[] { "--server" });
                    return;
                }
                if (mode == "-client" || mode == "--client")
                {
                    await ClientProgram.Main(args);
                    return;
                }
            }

            // Interactive menu
            ShowMenu();
        }

        private static void ShowMenu()
        {
            while (true)
            {
                Console.Clear();
                Console.ForegroundColor = ConsoleColor.Cyan;
                Console.WriteLine(@"
╔═══════════════════════════════════════════════════════════════════╗
║                                                                   ║
║                    KENSHI ONLINE MULTIPLAYER                      ║
║                                                                   ║
╚═══════════════════════════════════════════════════════════════════╝");
                Console.ResetColor();
                Console.WriteLine();

                Console.ForegroundColor = ConsoleColor.Green;
                Console.WriteLine("  ═══ RECOMMENDED: DEDICATED SERVER MODE ═══");
                Console.ResetColor();
                Console.WriteLine();
                Console.WriteLine("  1. Start Dedicated Server");
                Console.WriteLine("     (Run this on a separate machine or one player's PC)");
                Console.WriteLine("     Server does NOT need Kenshi running.");
                Console.WriteLine();
                Console.WriteLine("  2. Connect to Server (Play with friends)");
                Console.WriteLine("     (Everyone runs this + their own Kenshi)");
                Console.WriteLine();

                Console.ForegroundColor = ConsoleColor.Yellow;
                Console.WriteLine("  ═══ ALTERNATIVE: HOST MODE ═══");
                Console.ResetColor();
                Console.WriteLine();
                Console.WriteLine("  3. Host Game (One player runs Kenshi for everyone)");
                Console.WriteLine("  4. Join Host (Control squads in host's game)");
                Console.WriteLine();

                Console.ForegroundColor = ConsoleColor.DarkGray;
                Console.WriteLine("  ═══ OTHER ═══");
                Console.ResetColor();
                Console.WriteLine("  5. Help");
                Console.WriteLine("  6. Exit");
                Console.WriteLine();

                Console.Write("Choice: ");
                var choice = Console.ReadLine()?.Trim();

                switch (choice)
                {
                    case "1":
                        Console.Clear();
                        Console.Write("Port (default 7777): ");
                        var portStr = Console.ReadLine()?.Trim();
                        int port = string.IsNullOrWhiteSpace(portStr) ? 7777 : int.Parse(portStr);
                        DedicatedServerMode.RunDedicatedServer(port);
                        break;

                    case "2":
                        Console.Clear();
                        Console.Write("Server IP: ");
                        var ip = Console.ReadLine()?.Trim();
                        if (string.IsNullOrWhiteSpace(ip)) ip = "localhost";
                        Console.Write("Port (default 7777): ");
                        portStr = Console.ReadLine()?.Trim();
                        port = string.IsNullOrWhiteSpace(portStr) ? 7777 : int.Parse(portStr);
                        DedicatedServerMode.RunDedicatedClient(ip, port);
                        break;

                    case "3":
                        Console.Clear();
                        Console.Write("Port (default 7777): ");
                        portStr = Console.ReadLine()?.Trim();
                        port = string.IsNullOrWhiteSpace(portStr) ? 7777 : int.Parse(portStr);
                        HostCoop.RunHost(port);
                        break;

                    case "4":
                        Console.Clear();
                        Console.Write("Host IP: ");
                        ip = Console.ReadLine()?.Trim();
                        if (string.IsNullOrWhiteSpace(ip)) ip = "localhost";
                        Console.Write("Port (default 7777): ");
                        portStr = Console.ReadLine()?.Trim();
                        port = string.IsNullOrWhiteSpace(portStr) ? 7777 : int.Parse(portStr);
                        HostCoop.RunClient(ip, port);
                        break;

                    case "5":
                        ShowHelp();
                        break;

                    case "6":
                    case "exit":
                    case "quit":
                        return;
                }
            }
        }

        private static void ShowHelp()
        {
            Console.Clear();
            Console.ForegroundColor = ConsoleColor.Cyan;
            Console.WriteLine("═══════════════════════════════════════════════════════════════════");
            Console.WriteLine("                        KENSHI ONLINE HELP");
            Console.WriteLine("═══════════════════════════════════════════════════════════════════");
            Console.ResetColor();
            Console.WriteLine();

            Console.ForegroundColor = ConsoleColor.Green;
            Console.WriteLine("DEDICATED SERVER MODE (Recommended)");
            Console.ResetColor();
            Console.WriteLine("────────────────────────────────────");
            Console.WriteLine("How it works:");
            Console.WriteLine("  1. One person runs the Dedicated Server (option 1)");
            Console.WriteLine("  2. Everyone else runs Connect (option 2) + their own Kenshi");
            Console.WriteLine("  3. Server tracks everyone's positions and broadcasts to all");
            Console.WriteLine("  4. Each player sees others move in their local Kenshi");
            Console.WriteLine();
            Console.WriteLine("Pros:");
            Console.WriteLine("  - Server can run 24/7 without Kenshi");
            Console.WriteLine("  - Everyone plays in their own Kenshi instance");
            Console.WriteLine("  - No single player is the bottleneck");
            Console.WriteLine();

            Console.ForegroundColor = ConsoleColor.Yellow;
            Console.WriteLine("HOST MODE (Alternative)");
            Console.ResetColor();
            Console.WriteLine("───────────────────────");
            Console.WriteLine("How it works:");
            Console.WriteLine("  1. Host runs Kenshi and the Host server");
            Console.WriteLine("  2. Friends connect and control squads in host's game");
            Console.WriteLine("  3. Only ONE Kenshi runs (the host's)");
            Console.WriteLine();
            Console.WriteLine("Pros:");
            Console.WriteLine("  - No sync issues (one game instance)");
            Console.WriteLine("  - Friends don't need Kenshi installed");
            Console.WriteLine("Cons:");
            Console.WriteLine("  - If host disconnects, game ends");
            Console.WriteLine("  - Host's PC must be powerful enough");
            Console.WriteLine();

            Console.ForegroundColor = ConsoleColor.White;
            Console.WriteLine("COMMAND LINE USAGE");
            Console.ResetColor();
            Console.WriteLine("──────────────────");
            Console.WriteLine("  KenshiOnline.exe --dedicated [port]     Start dedicated server");
            Console.WriteLine("  KenshiOnline.exe --connect [ip] [port]  Connect to server");
            Console.WriteLine("  KenshiOnline.exe --host [port]          Host mode");
            Console.WriteLine("  KenshiOnline.exe --join [ip] [port]     Join host");
            Console.WriteLine();

            Console.ForegroundColor = ConsoleColor.White;
            Console.WriteLine("REQUIREMENTS");
            Console.ResetColor();
            Console.WriteLine("────────────");
            Console.WriteLine("  - Kenshi 1.0.64 (64-bit)");
            Console.WriteLine("  - Windows 10/11");
            Console.WriteLine("  - Run as Administrator");
            Console.WriteLine("  - Port 7777 open (or custom port)");
            Console.WriteLine();

            Console.WriteLine("Press Enter to return to menu...");
            Console.ReadLine();
        }
    }
}
