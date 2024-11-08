using System;

namespace KenshiMultiplayer
{
    class Program
    {
        static void Main(string[] args)
        {
            Console.WriteLine("Type 'server' to start as server or 'client' to start as client:");
            string input = Console.ReadLine()?.Trim().ToLower();

            if (input == "server")
            {
                var server = new Server();
                server.Start();
            }
            else if (input == "client")
            {
                var client = new Client();
                client.Connect();
            }
            else
            {
                Console.WriteLine("Invalid input. Please restart and type 'server' or 'client'.");
            }
        }
    }
}
