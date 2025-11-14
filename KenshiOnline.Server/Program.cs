using System;

namespace KenshiOnline.Server
{
    class Program
    {
        static void Main(string[] args)
        {
            // Parse port from command line
            int port = 7777;
            if (args.Length > 0 && int.TryParse(args[0], out var parsedPort))
            {
                port = parsedPort;
            }

            // Create and start server
            var server = new KenshiOnlineServer(port);

            // Handle Ctrl+C
            Console.CancelKeyPress += (sender, e) =>
            {
                e.Cancel = true;
                server.Stop();
                Environment.Exit(0);
            };

            try
            {
                server.Start();

                // Keep running
                while (true)
                {
                    System.Threading.Thread.Sleep(1000);
                }
            }
            catch (Exception ex)
            {
                Console.WriteLine($"[FATAL] Server error: {ex.Message}");
                Console.WriteLine(ex.StackTrace);
                Console.WriteLine("\nPress any key to exit...");
                Console.ReadKey();
            }
        }
    }
}
