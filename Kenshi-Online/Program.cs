using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading;

namespace KenshiMultiplayer
{
    class EnhancedProgram
    {
        static void Main(string[] args)
        {
            DisplayHeader();
            
            Console.WriteLine("Kenshi Multiplayer Server/Client");
            Console.WriteLine("---------------------------------");
            Console.WriteLine("1. Start Server");
            Console.WriteLine("2. Start Client");
            Console.Write("Select option: ");
            
            string input = Console.ReadLine()?.Trim();
            
            if (input == "1")
            {
                StartServer();
            }
            else if (input == "2")
            {
                StartClient();
            }
            else
            {
                Console.WriteLine("Invalid option.");
            }
        }
        
        static void DisplayHeader()
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
        
        static void StartServer()
        {
            Console.Write("Enter Kenshi installation path (e.g. C:\\Steam\\steamapps\\common\\Kenshi): ");
            string kenshiPath = Console.ReadLine()?.Trim();
            
            if (string.IsNullOrEmpty(kenshiPath) || !Directory.Exists(kenshiPath))
            {
                Console.ForegroundColor = ConsoleColor.Red;
                Console.WriteLine("Invalid Kenshi path. Path does not exist.");
                Console.ResetColor();
                return;
            }
            
            Console.Write("Server port [5555]: ");
            string portInput = Console.ReadLine()?.Trim();
            int port = string.IsNullOrEmpty(portInput) ? 5555 : int.Parse(portInput);
            
            try
            {
                Console.ForegroundColor = ConsoleColor.Green;
                Console.WriteLine("Starting Kenshi Multiplayer server...");
                Console.WriteLine($"Using Kenshi installation at: {kenshiPath}");
                Console.WriteLine($"Server will listen on port: {port}");
                Console.ResetColor();
                
                var server = new EnhancedServer(kenshiPath);
                server.Start(port);
            }
            catch (Exception ex)
            {
                Console.ForegroundColor = ConsoleColor.Red;
                Console.WriteLine($"Failed to start server: {ex.Message}");
                Console.ResetColor();
            }
        }
        
        static void StartClient()
        {
            Console.Write("Enter cache directory path [./cache]: ");
            string cachePath = Console.ReadLine()?.Trim();
            
            if (string.IsNullOrEmpty(cachePath))
            {
                cachePath = "./cache";
            }
            
            Directory.CreateDirectory(cachePath);
            
            Console.Write("Server address [localhost]: ");
            string serverAddress = Console.ReadLine()?.Trim();
            if (string.IsNullOrEmpty(serverAddress))
            {
                serverAddress = "localhost";
            }
            
            Console.Write("Server port [5555]: ");
            string portInput = Console.ReadLine()?.Trim();
            int port = string.IsNullOrEmpty(portInput) ? 5555 : int.Parse(portInput);
            
            // Initialize client
            var client = new EnhancedClient(cachePath);
            
            // Register message handler
            client.MessageReceived += (sender, msg) => {
                if (msg.Type == MessageType.Chat || msg.Type == MessageType.SystemMessage)
                {
                    string chatMessage = msg.Data.ContainsKey("message") ? msg.Data["message"].ToString() : "";
                    Console.WriteLine($"[{msg.PlayerId}]: {chatMessage}");
                }
            };
            
            while (true)
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
                
                if (input == "1")
                {
                    // Login
                    Console.Write("Username: ");
                    string username = Console.ReadLine();
                    
                    Console.Write("Password: ");
                    string password = ReadPassword();
                    
                    Console.WriteLine("\nLogging in...");
                    
                    if (client.Login(serverAddress, port, username, password))
                    {
                        Console.ForegroundColor = ConsoleColor.Green;
                        Console.WriteLine("Login successful!");
                        Console.ResetColor();
                        ClientMainMenu(client);
                        break;
                    }
                    else
                    {
                        Console.ForegroundColor = ConsoleColor.Red;
                        Console.WriteLine("Login failed.");
                        Console.ResetColor();
                        Thread.Sleep(2000);
                    }
                }
                else if (input == "2")
                {
                    // Register
                    Console.Write("Username: ");
                    string username = Console.ReadLine();
                    
                    Console.Write("Password: ");
                    string password = ReadPassword();
                    
                    Console.Write("\nEmail: ");
                    string email = Console.ReadLine();
                    
                    Console.WriteLine("Registering...");
                    
                    if (client.Register(serverAddress, port, username, password, email))
                    {
                        Console.ForegroundColor = ConsoleColor.Green;
                        Console.WriteLine("Registration successful! You can now login.");
                        Console.ResetColor();
                        Thread.Sleep(2000);
                    }
                    else
                    {
                        Console.ForegroundColor = ConsoleColor.Red;
                        Console.WriteLine("Registration failed.");
                        Console.ResetColor();
                        Thread.Sleep(2000);
                    }
                }
                else if (input == "3")
                {
                    break;
                }
            }
        }
        
        static void ClientMainMenu(EnhancedClient client)
        {
            while (true)
            {
                Console.Clear();
                DisplayHeader();
                Console.WriteLine("Kenshi Multiplayer Client - Connected");
                Console.WriteLine("---------------------------------");
                Console.WriteLine("1. Download Game Files");
                Console.WriteLine("2. Chat");
                Console.WriteLine("3. Disconnect");
                Console.Write("Select option: ");
                
                string input = Console.ReadLine()?.Trim();
                
                if (input == "1")
                {
                    DownloadFilesMenu(client);
                }
                else if (input == "2")
                {
                    ChatMenu(client);
                }
                else if (input == "3")
                {
                    client.Disconnect();
                    break;
                }
            }
        }
        
        static void DownloadFilesMenu(EnhancedClient client)
        {
            try
            {
                Console.Clear();
                Console.WriteLine("Downloading file list...");
                
                var files = client.RequestFileList();
                
                Console.Clear();
                Console.WriteLine("Game Files");
                Console.WriteLine("---------------------------------");
                
                // Group files by directory
                var directories = files
                    .Select(f => Path.GetDirectoryName(f.RelativePath).Replace('\\', '/'))
                    .Distinct()
                    .OrderBy(d => d)
                    .ToList();
                
                int index = 1;
                foreach (var dir in directories)
                {
                    Console.WriteLine($"{index}. {dir}/");
                    index++;
                }
                
                Console.WriteLine($"{index}. Back to main menu");
                Console.Write("Select directory to browse or download: ");
                
                string input = Console.ReadLine()?.Trim();
                if (int.TryParse(input, out int selection) && selection >= 1 && selection <= directories.Count)
                {
                    string selectedDir = directories[selection - 1];
                    BrowseDirectory(client, selectedDir);
                }
            }
            catch (Exception ex)
            {
                Console.ForegroundColor = ConsoleColor.Red;
                Console.WriteLine($"Error: {ex.Message}");
                Console.ResetColor();
                Console.WriteLine("Press any key to continue...");
                Console.ReadKey();
            }
        }
        
        static void BrowseDirectory(EnhancedClient client, string directory)
        {
            try
            {
                Console.Clear();
                Console.WriteLine($"Browsing: {directory}");
                Console.WriteLine("---------------------------------");
                
                var files = client.RequestFileList(directory)
                    .OrderBy(f => Path.GetFileName(f.RelativePath))
                    .ToList();
                
                int index = 1;
                foreach (var file in files)
                {
                    string fileName = Path.GetFileName(file.RelativePath);
                    Console.WriteLine($"{index}. {fileName} ({GetFileSizeString(file.Size)})");
                    index++;
                }
                
                Console.WriteLine($"{index}. Download all files");
                Console.WriteLine($"{index + 1}. Back");
                Console.Write("Select file to download or action: ");
                
                string input = Console.ReadLine()?.Trim();
                if (int.TryParse(input, out int selection))
                {
                    if (selection >= 1 && selection <= files.Count)
                    {
                        // Download specific file
                        var fileToDownload = files[selection - 1];
                        DownloadFile(client, fileToDownload.RelativePath);
                    }
                    else if (selection == files.Count + 1)
                    {
                        // Download all files
                        DownloadMultipleFiles(client, files);
                    }
                }
            }
            catch (Exception ex)
            {
                Console.ForegroundColor = ConsoleColor.Red;
                Console.WriteLine($"Error: {ex.Message}");
                Console.ResetColor();
                Console.WriteLine("Press any key to continue...");
                Console.ReadKey();
            }
        }
        
        static void DownloadFile(EnhancedClient client, string relativePath)
        {
            try
            {
                Console.Clear();
                Console.WriteLine($"Downloading: {relativePath}");
                Console.WriteLine("Please wait...");
                
                byte[] fileData = client.RequestGameFile(relativePath);
                
                Console.ForegroundColor = ConsoleColor.Green;
                Console.WriteLine($"Downloaded {GetFileSizeString(fileData.Length)} successfully!");
                Console.ResetColor();
                
                Console.WriteLine("Press any key to continue...");
                Console.ReadKey();
            }
            catch (Exception ex)
            {
                Console.ForegroundColor = ConsoleColor.Red;
                Console.WriteLine($"Download error: {ex.Message}");
                Console.ResetColor();
                Console.WriteLine("Press any key to continue...");
                Console.ReadKey();
            }
        }
        
        static void DownloadMultipleFiles(EnhancedClient client, List<GameFileInfo> files)
        {
            Console.Clear();
            Console.WriteLine($"Downloading {files.Count} files");
            Console.WriteLine("Progress: 0%");
            
            int totalFiles = files.Count;
            int filesDownloaded = 0;
            
            foreach (var file in files)
            {
                try
                {
                    int percent = (int)((double)filesDownloaded / totalFiles * 100);
                    Console.SetCursorPosition(10, 1);
                    Console.Write($"{percent}%");
                    
                    Console.SetCursorPosition(0, 2);
                    Console.Write($"Current: {file.RelativePath.PadRight(40)}");
                    
                    byte[] fileData = client.RequestGameFile(file.RelativePath);
                    filesDownloaded++;
                }
                catch (Exception ex)
                {
                    Console.SetCursorPosition(0, 3);
                    Console.ForegroundColor = ConsoleColor.Red;
                    Console.WriteLine($"Error: {ex.Message}".PadRight(50));
                    Console.ResetColor();
                }
            }
            
            Console.SetCursorPosition(10, 1);
            Console.Write("100%");
            
            Console.SetCursorPosition(0, 4);
            Console.ForegroundColor = ConsoleColor.Green;
            Console.WriteLine($"Downloaded {filesDownloaded} of {totalFiles} files");
            Console.ResetColor();
            
            Console.WriteLine("Press any key to continue...");
            Console.ReadKey();
        }
        
        static void ChatMenu(EnhancedClient client)
        {
            Console.Clear();
            Console.WriteLine("Chat");
            Console.WriteLine("---------------------------------");
            Console.WriteLine("Type your messages (type /exit to return to menu)");
            Console.WriteLine("---------------------------------");
            
            while (true)
            {
                string message = Console.ReadLine()?.Trim();
                
                if (message == "/exit")
                {
                    break;
                }
                
                if (!string.IsNullOrEmpty(message))
                {
                    // Create a chat message
                    var chatMessage = new GameMessage
                    {
                        Type = MessageType.Chat,
                        Data = new Dictionary<string, object> 
                        { 
                            { "message", message }
                        }
                    };
                    
                    try
                    {
                        client.SendMessageToServer(chatMessage);
                    }
                    catch (Exception ex)
                    {
                        Console.ForegroundColor = ConsoleColor.Red;
                        Console.WriteLine($"Error sending message: {ex.Message}");
                        Console.ResetColor();
                    }
                }
            }
        }
        
        // Helper method to read password without showing characters
        static string ReadPassword()
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
        
        // Helper method to format file size
        static string GetFileSizeString(long byteCount)
        {
            string[] suf = { "B", "KB", "MB", "GB" };
            if (byteCount == 0)
                return "0" + suf[0];
                
            long bytes = Math.Abs(byteCount);
            int place = Convert.ToInt32(Math.Floor(Math.Log(bytes, 1024)));
            double num = Math.Round(bytes / Math.Pow(1024, place), 1);
            return $"{(Math.Sign(byteCount) * num).ToString("0.##")} {suf[place]}";
        }
    }
}