using System;
using System.Threading.Tasks;

namespace KenshiMultiplayer
{
    class Program
    {
        private static KenshiOnlinePlugin plugin;
        
        static async Task Main(string[] args)
        {
            Console.Title = "Kenshi Online";
            
            DisplayHeader();
            Console.WriteLine("Kenshi Multiplayer Client");
            Console.WriteLine("---------------------------------");
            
            // Initialize the plugin
            plugin = new KenshiOnlinePlugin();
            if (!plugin.Initialize())
            {
                Console.WriteLine("Failed to initialize plugin. Press any key to exit.");
                Console.ReadKey();
                return;
            }
            
            // Main menu loop
            bool exit = false;
            while (!exit)
            {
                Console.Clear();
                DisplayHeader();
                Console.WriteLine("Kenshi Multiplayer Client");
                Console.WriteLine("---------------------------------");
                Console.WriteLine("1. Login");
                Console.WriteLine("2. Register");
                Console.WriteLine("3. Exit");
                Console.Write("Select option: ");
                
                string input = Console.ReadLine()?.Trim();
                
                switch (input)
                {
                    case "1":
                        await LoginMenu();
                        break;
                    
                    case "2":
                        await RegisterMenu();
                        break;
                    
                    case "3":
                        exit = true;
                        break;
                    
                    default:
                        Console.WriteLine("Invalid option. Press any key to continue.");
                        Console.ReadKey();
                        break;
                }
            }
            
            Console.WriteLine("Exiting Kenshi Multiplayer Client. Press any key to close.");
            Console.ReadKey();
        }
        
        private static void DisplayHeader()
        {
            Console.ForegroundColor = ConsoleColor.Cyan;
            Console.WriteLine(@"
  _  __                _     _   ____        _ _            
 | |/ /   _ _ __  ___| |__ (_) / ___| _ __ | (_)_ __   ___ 
 | ' / | | | '_ \/ __| '_ \| | \___ \| '_ \| | | '_ \ / _ \
 | . \ |_| | | | \__ \ | | | |  ___) | | | | | | | | |  __/
 |_|\_\__,_|_| |_|___/_| |_|_| |____/|_| |_|_|_|_| |_|\___|
                                                           
");
            Console.ResetColor();
        }
        
        private static async Task LoginMenu()
        {
            Console.Clear();
            DisplayHeader();
            Console.WriteLine("Login to Kenshi Online");
            Console.WriteLine("---------------------------------");
            
            Console.Write("Username: ");
            string username = Console.ReadLine();
            
            Console.Write("Password: ");
            string password = ReadPassword();
            
            Console.WriteLine("\nConnecting to server...");
            
            bool success = await plugin.Connect(username, password);
            
            if (success)
            {
                Console.ForegroundColor = ConsoleColor.Green;
                Console.WriteLine("Login successful!");
                Console.ResetColor();
                
                // Enter the main game interface
                await MainGameInterface();
            }
            else
            {
                Console.ForegroundColor = ConsoleColor.Red;
                Console.WriteLine("Login failed. Press any key to return to main menu.");
                Console.ResetColor();
                Console.ReadKey();
            }
        }
        
        private static async Task RegisterMenu()
        {
            Console.Clear();
            DisplayHeader();
            Console.WriteLine("Register a New Account");
            Console.WriteLine("---------------------------------");
            
            Console.Write("Username: ");
            string username = Console.ReadLine();
            
            Console.Write("Password: ");
            string password = ReadPassword();
            
            Console.Write("\nEmail: ");
            string email = Console.ReadLine();
            
            Console.WriteLine("Registering account...");
            
            bool success = await plugin.Register(username, password, email);
            
            if (success)
            {
                Console.ForegroundColor = ConsoleColor.Green;
                Console.WriteLine("Registration successful! You can now login. Press any key to continue.");
                Console.ResetColor();
            }
            else
            {
                Console.ForegroundColor = ConsoleColor.Red;
                Console.WriteLine("Registration failed. Press any key to continue.");
                Console.ResetColor();
            }
            
            Console.ReadKey();
        }
        
        private static async Task MainGameInterface()
        {
            bool exit = false;
            while (!exit)
            {
                Console.Clear();
                DisplayHeader();
                Console.WriteLine("Kenshi Online - Connected");
                Console.WriteLine("---------------------------------");
                Console.WriteLine("1. Send Chat Message");
                Console.WriteLine("2. Player List");
                Console.WriteLine("3. Help");
                Console.WriteLine("4. Disconnect");
                Console.Write("Select option: ");
                
                string input = Console.ReadLine()?.Trim();
                
                switch (input)
                {
                    case "1":
                        ChatInterface();
                        break;
                    
                    case "2":
                        PlayerListInterface();
                        break;
                    
                    case "3":
                        HelpInterface();
                        break;
                    
                    case "4":
                        exit = true;
                        plugin.Disconnect();
                        break;
                    
                    default:
                        Console.WriteLine("Invalid option. Press any key to continue.");
                        Console.ReadKey();
                        break;
                }
            }
        }
        
        private static void ChatInterface()
        {
            Console.Clear();
            DisplayHeader();
            Console.WriteLine("Chat Interface");
            Console.WriteLine("---------------------------------");
            Console.WriteLine("Type your message (or 'exit' to return to menu):");
            
            while (true)
            {
                Console.Write("> ");
                string message = Console.ReadLine();
                
                if (string.IsNullOrEmpty(message))
                    continue;
                
                if (message.ToLower() == "exit")
                    break;
                
                // Send the chat message
                plugin.SendChatMessage(message);
            }
        }
        
        private static void PlayerListInterface()
        {
            Console.Clear();
            DisplayHeader();
            Console.WriteLine("Player List");
            Console.WriteLine("---------------------------------");
            Console.WriteLine("This feature is not yet implemented.");
            Console.WriteLine("\nPress any key to return to menu.");
            Console.ReadKey();
        }
        
        private static void HelpInterface()
        {
            Console.Clear();
            DisplayHeader();
            Console.WriteLine("Help & Information");
            Console.WriteLine("---------------------------------");
            Console.WriteLine("Kenshi Online allows you to play Kenshi with other players.");
            Console.WriteLine("\nUsage:");
            Console.WriteLine("1. Make sure Kenshi is running before connecting");
            Console.WriteLine("2. Use the chat interface to communicate with other players");
            Console.WriteLine("3. Other players will appear in your game world automatically");
            Console.WriteLine("\nControls:");
            Console.WriteLine("- Play the game normally with Kenshi's controls");
            Console.WriteLine("- All actions are automatically synchronized with other players");
            Console.WriteLine("\nPress any key to return to menu.");
            Console.ReadKey();
        }
        
        private static string ReadPassword()
        {
            string password = "";
            ConsoleKeyInfo key;
            
            do
            {
                key = Console.ReadKey(true);
                
                if (key.Key != ConsoleKey.Enter && key.Key != ConsoleKey.Backspace)
                {
                    password += key.KeyChar;
                    Console.Write("*");
                }
                else if (key.Key == ConsoleKey.Backspace && password.Length > 0)
                {
                    password = password.Substring(0, password.Length - 1);
                    Console.Write("\b \b");
                }
            }
            while (key.Key != ConsoleKey.Enter);
            
            return password;
        }
    }
}