using System;

namespace KenshiMultiplayer
{
    /// <summary>
    /// Kenshi Online - Multiplayer Mod
    ///
    /// Two modes:
    /// 1. Dedicated Server - Standalone server, no Kenshi needed
    /// 2. Client - Connect to server, runs alongside Kenshi
    /// </summary>
    public class Program
    {
        public static void Main(string[] args)
        {
            Console.Title = "Kenshi Online";

            // Command line arguments
            if (args.Length > 0)
            {
                string mode = args[0].ToLower();

                // DEDICATED SERVER
                if (mode == "--dedicated" || mode == "-d" || mode == "dedicated" || mode == "server")
                {
                    int port = args.Length > 1 && int.TryParse(args[1], out int p) ? p : 7777;
                    DedicatedServerMode.RunDedicatedServer(port);
                    return;
                }

                // CLIENT
                if (mode == "--connect" || mode == "-c" || mode == "connect" || mode == "client")
                {
                    string ip = args.Length > 1 ? args[1] : "localhost";
                    int port = args.Length > 2 && int.TryParse(args[2], out int p) ? p : 7777;
                    DedicatedServerMode.RunDedicatedClient(ip, port);
                    return;
                }

                // HOST MODE
                if (mode == "--host" || mode == "-h" || mode == "host")
                {
                    int port = args.Length > 1 && int.TryParse(args[1], out int p) ? p : 7777;
                    HostCoop.RunHost(port);
                    return;
                }

                // JOIN HOST
                if (mode == "--join" || mode == "-j" || mode == "join")
                {
                    string ip = args.Length > 1 ? args[1] : "localhost";
                    int port = args.Length > 2 && int.TryParse(args[2], out int p) ? p : 7777;
                    HostCoop.RunClient(ip, port);
                    return;
                }

                // Help
                if (mode == "--help" || mode == "-?" || mode == "help")
                {
                    ShowHelp();
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
║                    KENSHI ONLINE MULTIPLAYER                      ║
╚═══════════════════════════════════════════════════════════════════╝");
                Console.ResetColor();
                Console.WriteLine();

                Console.ForegroundColor = ConsoleColor.Green;
                Console.WriteLine("  ═══ DEDICATED SERVER MODE ═══");
                Console.ResetColor();
                Console.WriteLine();
                Console.WriteLine("  1. Start Dedicated Server");
                Console.WriteLine("     Server coordinates players. Does NOT need Kenshi.");
                Console.WriteLine();
                Console.WriteLine("  2. Connect to Server");
                Console.WriteLine("     Join a server. Run Kenshi first!");
                Console.WriteLine();

                Console.ForegroundColor = ConsoleColor.Yellow;
                Console.WriteLine("  ═══ HOST MODE (Alternative) ═══");
                Console.ResetColor();
                Console.WriteLine();
                Console.WriteLine("  3. Host Game");
                Console.WriteLine("     You run Kenshi, friends control squads remotely.");
                Console.WriteLine();
                Console.WriteLine("  4. Join Host");
                Console.WriteLine("     Control squads in host's game.");
                Console.WriteLine();

                Console.ForegroundColor = ConsoleColor.DarkGray;
                Console.WriteLine("  5. Help");
                Console.WriteLine("  6. Exit");
                Console.ResetColor();
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
            Console.WriteLine("DEDICATED SERVER MODE");
            Console.ResetColor();
            Console.WriteLine("─────────────────────");
            Console.WriteLine("1. One person runs 'Start Dedicated Server'");
            Console.WriteLine("2. Everyone else runs Kenshi, then 'Connect to Server'");
            Console.WriteLine("3. Pick a spawn location and play!");
            Console.WriteLine();

            Console.ForegroundColor = ConsoleColor.Yellow;
            Console.WriteLine("HOST MODE");
            Console.ResetColor();
            Console.WriteLine("─────────");
            Console.WriteLine("1. Host runs Kenshi + 'Host Game'");
            Console.WriteLine("2. Friends run 'Join Host' to control squads");
            Console.WriteLine("3. Only one Kenshi instance (the host's)");
            Console.WriteLine();

            Console.ForegroundColor = ConsoleColor.White;
            Console.WriteLine("COMMAND LINE");
            Console.ResetColor();
            Console.WriteLine("────────────");
            Console.WriteLine("  KenshiOnline.exe --dedicated [port]     Start server");
            Console.WriteLine("  KenshiOnline.exe --connect [ip] [port]  Connect to server");
            Console.WriteLine("  KenshiOnline.exe --host [port]          Host game");
            Console.WriteLine("  KenshiOnline.exe --join [ip] [port]     Join host");
            Console.WriteLine();

            Console.WriteLine("Press Enter to return...");
            Console.ReadLine();
        }
    }
}
