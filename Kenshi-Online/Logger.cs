using System;
using System.IO;

namespace KenshiMultiplayer
{
    public static class Logger
    {
        private static readonly string logFilePath = "server_log.txt";

        public static void Log(string message)
        {
            File.AppendAllText(logFilePath, $"{DateTime.Now}: {message}\n");
        }
    }
}
