using System;
using System.IO;
using KenshiMultiplayer.Networking;
using KenshiMultiplayer.Managers;
using KenshiMultiplayer.Data;

namespace KenshiMultiplayer.Utility
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
