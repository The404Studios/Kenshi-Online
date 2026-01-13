using System;
using System.Threading.Tasks;

namespace KenshiMultiplayer
{
    /// <summary>
    /// Main entry point for Kenshi Online.
    /// Can run as either Server or Client mode.
    /// </summary>
    public class Program
    {
        public static async Task Main(string[] args)
        {
            Console.Title = "Kenshi Online";

            // Check command line arguments
            if (args.Length > 0)
            {
                string mode = args[0].ToLower();

                if (mode == "-server" || mode == "--server" || mode == "server")
                {
                    // Run as server
                    await EnhancedProgram.Main(new string[] { "--server" });
                    return;
                }
                else if (mode == "-client" || mode == "--client" || mode == "client")
                {
                    // Run as client
                    await ClientProgram.Main(args);
                    return;
                }
            }

            // Interactive mode selection
            DisplayBanner();
            DisplayModeSelection();

            Console.Write("\nChoice: ");
            string choice = Console.ReadLine()?.Trim();

            switch (choice)
            {
                case "1":
                    Console.Clear();
                    await EnhancedProgram.Main(new string[] { "--server" });
                    break;

                case "2":
                    Console.Clear();
                    await ClientProgram.Main(args);
                    break;

                case "3":
                    DisplayHelp();
                    Console.WriteLine("\nPress any key to exit...");
                    Console.ReadKey();
                    break;

                default:
                    Console.WriteLine("Invalid choice.");
                    break;
            }
        }

        private static void DisplayBanner()
        {
            Console.ForegroundColor = ConsoleColor.Cyan;
            Console.WriteLine(@"
╔═══════════════════════════════════════════════════════╗
║                                                       ║
║               KENSHI ONLINE MULTIPLAYER               ║
║                                                       ║
║        Co-op multiplayer mod for Kenshi               ║
║                                                       ║
╚═══════════════════════════════════════════════════════╝
");
            Console.ResetColor();
        }

        private static void DisplayModeSelection()
        {
            Console.WriteLine("Select mode:\n");
            Console.WriteLine("  1. Start Server (Host a multiplayer session)");
            Console.WriteLine("  2. Start Client (Join a multiplayer session)");
            Console.WriteLine("  3. Help");
            Console.WriteLine();
            Console.WriteLine("Command line usage:");
            Console.WriteLine("  KenshiOnline.exe -server    Start as server");
            Console.WriteLine("  KenshiOnline.exe -client    Start as client");
        }

        private static void DisplayHelp()
        {
            Console.Clear();
            Console.ForegroundColor = ConsoleColor.Yellow;
            Console.WriteLine("=== KENSHI ONLINE HELP ===\n");
            Console.ResetColor();

            Console.WriteLine("GETTING STARTED:");
            Console.WriteLine("----------------");
            Console.WriteLine("1. One player hosts the server (choose option 1)");
            Console.WriteLine("2. Other players run the client (choose option 2)");
            Console.WriteLine("3. Start Kenshi and load/create a save");
            Console.WriteLine("4. In the client, connect to the server");
            Console.WriteLine("5. Login and spawn to start playing together!\n");

            Console.WriteLine("SERVER REQUIREMENTS:");
            Console.WriteLine("-------------------");
            Console.WriteLine("- Port 7777 (default) must be open/forwarded");
            Console.WriteLine("- Kenshi should be running on the host machine");
            Console.WriteLine("- All players need the same Kenshi version\n");

            Console.WriteLine("CLIENT REQUIREMENTS:");
            Console.WriteLine("-------------------");
            Console.WriteLine("- Run Kenshi and load a save first");
            Console.WriteLine("- Know the server IP address");
            Console.WriteLine("- Create an account on first connection\n");

            Console.WriteLine("FEATURES:");
            Console.WriteLine("---------");
            Console.WriteLine("- See other players in your game world");
            Console.WriteLine("- Synchronized positions and actions");
            Console.WriteLine("- Persistent world saves");
            Console.WriteLine("- 16 spawn locations across the Kenshi map");
            Console.WriteLine("- Friend spawning for group play\n");

            Console.WriteLine("SPAWN LOCATIONS:");
            Console.WriteLine("---------------");
            Console.WriteLine("Hub, Squin, Sho-Battai, Heng, Stack, Admag,");
            Console.WriteLine("BadTeeth, Bark, Stoat, WorldsEnd, FlatsLagoon,");
            Console.WriteLine("Shark, MudTown, Mongrel, Catun, Spring, Random\n");
        }
    }
}
