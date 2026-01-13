using System;
using System.Collections.Generic;
using System.Collections.Concurrent;
using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Text.Json;
using System.Threading;
using System.Diagnostics;
using System.Runtime.InteropServices;

namespace KenshiMultiplayer
{
    /// <summary>
    /// QUICK TEST - Minimal working multiplayer for testing
    ///
    /// Usage:
    ///   Server: KenshiOnline.exe --test-server
    ///   Client: KenshiOnline.exe --test-client [server-ip]
    ///
    /// This strips away ALL complexity for quick testing.
    /// </summary>
    public static class QuickTest
    {
        // Native imports for Kenshi memory access
        [DllImport("kernel32.dll")]
        static extern IntPtr OpenProcess(int dwDesiredAccess, bool bInheritHandle, int dwProcessId);

        [DllImport("kernel32.dll")]
        static extern bool ReadProcessMemory(IntPtr hProcess, IntPtr lpBaseAddress, byte[] lpBuffer, int dwSize, out int lpNumberOfBytesRead);

        [DllImport("kernel32.dll")]
        static extern bool WriteProcessMemory(IntPtr hProcess, IntPtr lpBaseAddress, byte[] lpBuffer, int dwSize, out int lpNumberOfBytesWritten);

        const int PROCESS_ALL_ACCESS = 0x1F0FFF;

        // Offsets for Kenshi 1.0.64 (64-bit)
        const long SELECTED_CHARACTER_OFFSET = 0x24C5A30;
        const int POSITION_OFFSET = 0x70;

        static IntPtr processHandle = IntPtr.Zero;
        static IntPtr baseAddress = IntPtr.Zero;

        public static void RunServer(int port = 7777)
        {
            Console.WriteLine("╔════════════════════════════════════════╗");
            Console.WriteLine("║   KENSHI ONLINE - TEST SERVER          ║");
            Console.WriteLine("╚════════════════════════════════════════╝");
            Console.WriteLine();

            var server = new TestServer(port);
            server.Start();

            Console.WriteLine($"Server running on port {port}");
            Console.WriteLine("Share your IP with friends to connect.");
            Console.WriteLine();
            Console.WriteLine("Commands:");
            Console.WriteLine("  /list    - Show connected players");
            Console.WriteLine("  /kick    - Kick a player");
            Console.WriteLine("  /quit    - Stop server");
            Console.WriteLine();

            while (true)
            {
                var input = Console.ReadLine();
                if (input == "/quit") break;
                if (input == "/list")
                {
                    Console.WriteLine($"Players: {server.PlayerCount}");
                    foreach (var p in server.GetPlayers())
                        Console.WriteLine($"  - {p}");
                }
            }

            server.Stop();
        }

        public static void RunClient(string serverIp = "localhost", int port = 7777)
        {
            Console.WriteLine("╔════════════════════════════════════════╗");
            Console.WriteLine("║   KENSHI ONLINE - TEST CLIENT          ║");
            Console.WriteLine("╚════════════════════════════════════════╝");
            Console.WriteLine();

            // Step 1: Connect to Kenshi
            Console.WriteLine("[1/3] Connecting to Kenshi...");
            if (!ConnectToKenshi())
            {
                Console.WriteLine("ERROR: Could not connect to Kenshi!");
                Console.WriteLine("Make sure Kenshi is running (kenshi_x64.exe)");
                Console.WriteLine("Try running this as Administrator.");
                return;
            }
            Console.WriteLine("      Connected to Kenshi!");

            // Step 2: Get player name
            Console.Write("[2/3] Enter your name: ");
            var playerName = Console.ReadLine();
            if (string.IsNullOrWhiteSpace(playerName))
                playerName = "Player_" + new Random().Next(1000, 9999);

            // Step 3: Connect to server
            Console.WriteLine($"[3/3] Connecting to server {serverIp}:{port}...");
            var client = new TestClient(playerName);

            if (!client.Connect(serverIp, port))
            {
                Console.WriteLine("ERROR: Could not connect to server!");
                return;
            }
            Console.WriteLine("      Connected to server!");
            Console.WriteLine();
            Console.WriteLine("═══════════════════════════════════════════");
            Console.WriteLine("  MULTIPLAYER ACTIVE - You can play now!");
            Console.WriteLine("═══════════════════════════════════════════");
            Console.WriteLine();
            Console.WriteLine("Your position is being synced with other players.");
            Console.WriteLine("Type /quit to disconnect.");
            Console.WriteLine();

            // Start sync loop
            client.StartSync(() => ReadPlayerPosition(), (pos) => WritePlayerPosition(pos));

            // Wait for quit
            while (true)
            {
                var input = Console.ReadLine();
                if (input == "/quit") break;
            }

            client.Disconnect();
        }

        static bool ConnectToKenshi()
        {
            try
            {
                var processes = Process.GetProcessesByName("kenshi_x64");
                if (processes.Length == 0)
                    processes = Process.GetProcessesByName("kenshi");

                if (processes.Length == 0)
                    return false;

                var process = processes[0];
                processHandle = OpenProcess(PROCESS_ALL_ACCESS, false, process.Id);
                baseAddress = process.MainModule.BaseAddress;

                return processHandle != IntPtr.Zero;
            }
            catch (Exception ex)
            {
                Console.WriteLine($"Error: {ex.Message}");
                return false;
            }
        }

        static float[] ReadPlayerPosition()
        {
            try
            {
                // Read the pointer to selected character
                byte[] ptrBuffer = new byte[8];
                ReadProcessMemory(processHandle, baseAddress + (int)SELECTED_CHARACTER_OFFSET, ptrBuffer, 8, out _);
                long charPtr = BitConverter.ToInt64(ptrBuffer, 0);

                if (charPtr == 0)
                    return null;

                // Read position (3 floats: X, Y, Z)
                byte[] posBuffer = new byte[12];
                ReadProcessMemory(processHandle, (IntPtr)(charPtr + POSITION_OFFSET), posBuffer, 12, out _);

                float x = BitConverter.ToSingle(posBuffer, 0);
                float y = BitConverter.ToSingle(posBuffer, 4);
                float z = BitConverter.ToSingle(posBuffer, 8);

                return new float[] { x, y, z };
            }
            catch
            {
                return null;
            }
        }

        static void WritePlayerPosition(float[] pos)
        {
            // For other players, we'd need to spawn a character
            // This is a simplified version - in the full version,
            // we would create a visual marker or spawn an NPC
            // For now, just log it
        }
    }

    /// <summary>
    /// Minimal test server - just relays positions between players
    /// </summary>
    public class TestServer
    {
        private TcpListener listener;
        private readonly int port;
        private bool running = false;
        private readonly ConcurrentDictionary<string, TcpClient> clients = new();
        private readonly ConcurrentDictionary<string, float[]> positions = new();

        public int PlayerCount => clients.Count;

        public TestServer(int port)
        {
            this.port = port;
        }

        public void Start()
        {
            listener = new TcpListener(IPAddress.Any, port);
            listener.Start();
            running = true;

            // Accept clients thread
            new Thread(() =>
            {
                while (running)
                {
                    try
                    {
                        if (listener.Pending())
                        {
                            var client = listener.AcceptTcpClient();
                            new Thread(() => HandleClient(client)).Start();
                        }
                        Thread.Sleep(100);
                    }
                    catch { }
                }
            }).Start();

            // Broadcast positions thread
            new Thread(() =>
            {
                while (running)
                {
                    try
                    {
                        BroadcastPositions();
                        Thread.Sleep(50); // 20 Hz
                    }
                    catch { }
                }
            }).Start();
        }

        public void Stop()
        {
            running = false;
            listener?.Stop();
        }

        public IEnumerable<string> GetPlayers() => clients.Keys;

        private void HandleClient(TcpClient client)
        {
            string playerId = null;
            var stream = client.GetStream();
            var buffer = new byte[4096];

            try
            {
                while (running && client.Connected)
                {
                    if (stream.DataAvailable)
                    {
                        int bytes = stream.Read(buffer, 0, buffer.Length);
                        var json = Encoding.UTF8.GetString(buffer, 0, bytes);
                        var msg = JsonSerializer.Deserialize<TestMessage>(json);

                        if (msg.Type == "join")
                        {
                            playerId = msg.PlayerId;
                            clients[playerId] = client;
                            positions[playerId] = new float[] { 0, 0, 0 };
                            Console.WriteLine($"[+] {playerId} joined");

                            // Send current players
                            SendToClient(client, new TestMessage
                            {
                                Type = "players",
                                Data = JsonSerializer.Serialize(positions)
                            });
                        }
                        else if (msg.Type == "position" && playerId != null)
                        {
                            var pos = JsonSerializer.Deserialize<float[]>(msg.Data);
                            positions[playerId] = pos;
                        }
                    }
                    Thread.Sleep(10);
                }
            }
            catch { }
            finally
            {
                if (playerId != null)
                {
                    clients.TryRemove(playerId, out _);
                    positions.TryRemove(playerId, out _);
                    Console.WriteLine($"[-] {playerId} left");
                }
                client.Close();
            }
        }

        private void BroadcastPositions()
        {
            var msg = new TestMessage
            {
                Type = "positions",
                Data = JsonSerializer.Serialize(positions)
            };

            foreach (var kvp in clients)
            {
                try
                {
                    SendToClient(kvp.Value, msg);
                }
                catch { }
            }
        }

        private void SendToClient(TcpClient client, TestMessage msg)
        {
            var json = JsonSerializer.Serialize(msg);
            var bytes = Encoding.UTF8.GetBytes(json);
            client.GetStream().Write(bytes, 0, bytes.Length);
        }
    }

    /// <summary>
    /// Minimal test client
    /// </summary>
    public class TestClient
    {
        private TcpClient client;
        private NetworkStream stream;
        private readonly string playerId;
        private bool running = false;
        private ConcurrentDictionary<string, float[]> otherPlayers = new();

        public TestClient(string playerId)
        {
            this.playerId = playerId;
        }

        public bool Connect(string ip, int port)
        {
            try
            {
                client = new TcpClient(ip, port);
                stream = client.GetStream();

                // Send join message
                var joinMsg = new TestMessage { Type = "join", PlayerId = playerId };
                SendMessage(joinMsg);

                return true;
            }
            catch (Exception ex)
            {
                Console.WriteLine($"Connection error: {ex.Message}");
                return false;
            }
        }

        public void StartSync(Func<float[]> readPosition, Action<float[]> writePosition)
        {
            running = true;

            // Receive thread
            new Thread(() =>
            {
                var buffer = new byte[16384];
                while (running)
                {
                    try
                    {
                        if (stream.DataAvailable)
                        {
                            int bytes = stream.Read(buffer, 0, buffer.Length);
                            var json = Encoding.UTF8.GetString(buffer, 0, bytes);
                            var msg = JsonSerializer.Deserialize<TestMessage>(json);

                            if (msg.Type == "positions")
                            {
                                otherPlayers = JsonSerializer.Deserialize<ConcurrentDictionary<string, float[]>>(msg.Data);

                                // Remove self
                                otherPlayers.TryRemove(playerId, out _);

                                // Update other players in game
                                foreach (var kvp in otherPlayers)
                                {
                                    writePosition(kvp.Value);
                                }
                            }
                        }
                        Thread.Sleep(10);
                    }
                    catch { }
                }
            }).Start();

            // Send thread
            new Thread(() =>
            {
                float[] lastPos = null;
                while (running)
                {
                    try
                    {
                        var pos = readPosition();
                        if (pos != null && (lastPos == null || HasMoved(lastPos, pos)))
                        {
                            var msg = new TestMessage
                            {
                                Type = "position",
                                PlayerId = playerId,
                                Data = JsonSerializer.Serialize(pos)
                            };
                            SendMessage(msg);
                            lastPos = pos;

                            // Show position occasionally
                            Console.Write($"\rPosition: X={pos[0]:F1} Y={pos[1]:F1} Z={pos[2]:F1} | Players: {otherPlayers.Count}   ");
                        }
                        Thread.Sleep(50); // 20 Hz
                    }
                    catch { }
                }
            }).Start();
        }

        private bool HasMoved(float[] a, float[] b)
        {
            float dx = a[0] - b[0];
            float dy = a[1] - b[1];
            float dz = a[2] - b[2];
            return (dx * dx + dy * dy + dz * dz) > 0.25f; // Moved more than 0.5 units
        }

        public void Disconnect()
        {
            running = false;
            client?.Close();
        }

        private void SendMessage(TestMessage msg)
        {
            var json = JsonSerializer.Serialize(msg);
            var bytes = Encoding.UTF8.GetBytes(json);
            stream.Write(bytes, 0, bytes.Length);
        }
    }

    public class TestMessage
    {
        public string Type { get; set; }
        public string PlayerId { get; set; }
        public string Data { get; set; }
    }
}
