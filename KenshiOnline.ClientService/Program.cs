using System;
using System.Threading.Tasks;

namespace KenshiOnline.ClientService
{
    class Program
    {
        static async Task Main(string[] args)
        {
            // Parse arguments
            string serverAddress = "127.0.0.1";
            int serverPort = 7777;

            if (args.Length > 0)
                serverAddress = args[0];

            if (args.Length > 1 && int.TryParse(args[1], out var port))
                serverPort = port;

            // Create and start client service
            var clientService = new KenshiOnlineClientService(serverAddress, serverPort);

            // Handle Ctrl+C
            Console.CancelKeyPress += (sender, e) =>
            {
                e.Cancel = true;
                clientService.Stop();
                Environment.Exit(0);
            };

            try
            {
                await clientService.Start();
            }
            catch (Exception ex)
            {
                Console.WriteLine($"[FATAL] Client service error: {ex.Message}");
                Console.WriteLine(ex.StackTrace);
                Console.WriteLine("\nPress any key to exit...");
                Console.ReadKey();
            }
        }
    }
}
