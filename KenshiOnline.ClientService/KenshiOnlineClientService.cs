using System;
using System.Collections.Generic;
using System.IO;
using System.IO.Pipes;
using System.Net.Sockets;
using System.Text;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;

namespace KenshiOnline.ClientService
{
    /// <summary>
    /// Client Service - Bridges C++ plugin to C# server
    /// Named Pipe (C++) <-> TCP Socket (Server)
    /// </summary>
    public class KenshiOnlineClientService
    {
        private readonly string _serverAddress;
        private readonly int _serverPort;
        private readonly string _pipeName;

        private NamedPipeServerStream? _pipeServer;
        private TcpClient? _tcpClient;
        private NetworkStream? _tcpStream;

        private CancellationTokenSource? _cts;
        private bool _isRunning;

        // Connection states
        private bool _pluginConnected;
        private bool _serverConnected;

        public KenshiOnlineClientService(string serverAddress = "127.0.0.1", int serverPort = 7777, string pipeName = "KenshiOnline_IPC")
        {
            _serverAddress = serverAddress;
            _serverPort = serverPort;
            _pipeName = pipeName;
        }

        public async Task Start()
        {
            _cts = new CancellationTokenSource();
            _isRunning = true;

            PrintBanner();

            // Start both connection tasks
            var pipeTask = Task.Run(() => RunIPCServerAsync(_cts.Token));
            var tcpTask = Task.Run(() => ConnectToServerAsync(_cts.Token));

            // Wait for both to complete (or until cancelled)
            await Task.WhenAll(pipeTask, tcpTask);
        }

        public void Stop()
        {
            Console.WriteLine("\nShutting down client service...");

            _isRunning = false;
            _cts?.Cancel();

            _pipeServer?.Dispose();
            _tcpStream?.Dispose();
            _tcpClient?.Close();

            Console.WriteLine("Client service stopped.");
        }

        private void PrintBanner()
        {
            Console.Clear();
            Console.WriteLine("╔════════════════════════════════════════════════════════╗");
            Console.WriteLine("║     Kenshi Online Client Service v2.0                 ║");
            Console.WriteLine("║     Plugin <-> Server Bridge                           ║");
            Console.WriteLine("╚════════════════════════════════════════════════════════╝");
            Console.WriteLine();
            Console.WriteLine($"Server: {_serverAddress}:{_serverPort}");
            Console.WriteLine($"IPC Pipe: {_pipeName}");
            Console.WriteLine();
            Console.WriteLine("Waiting for connections...\n");
        }

        #region IPC Server (C++ Plugin Connection)

        private async Task RunIPCServerAsync(CancellationToken ct)
        {
            while (!ct.IsCancellationRequested && _isRunning)
            {
                try
                {
                    Console.WriteLine("[IPC] Creating named pipe server...");

                    _pipeServer = new NamedPipeServerStream(
                        _pipeName,
                        PipeDirection.InOut,
                        1,
                        PipeTransmissionMode.Message,
                        PipeOptions.Asynchronous
                    );

                    Console.WriteLine("[IPC] Waiting for plugin to connect...");

                    await _pipeServer.WaitForConnectionAsync(ct);

                    _pluginConnected = true;
                    Console.WriteLine("[IPC] Plugin connected!");
                    UpdateStatus();

                    // Handle plugin messages
                    await HandlePluginMessagesAsync(ct);
                }
                catch (OperationCanceledException)
                {
                    break;
                }
                catch (Exception ex)
                {
                    Console.WriteLine($"[IPC ERROR] {ex.Message}");
                    await Task.Delay(1000, ct);
                }
                finally
                {
                    _pluginConnected = false;
                    _pipeServer?.Dispose();
                    _pipeServer = null;
                    UpdateStatus();
                }
            }
        }

        private async Task HandlePluginMessagesAsync(CancellationToken ct)
        {
            var buffer = new byte[8192];

            try
            {
                while (!ct.IsCancellationRequested && _pipeServer != null && _pipeServer.IsConnected)
                {
                    int bytesRead = await _pipeServer.ReadAsync(buffer, 0, buffer.Length, ct);
                    if (bytesRead == 0)
                        break;

                    var json = Encoding.UTF8.GetString(buffer, 0, bytesRead);
                    var lines = json.Split('\n', StringSplitOptions.RemoveEmptyEntries);

                    foreach (var line in lines)
                    {
                        // Forward to server
                        await ForwardToServer(line);
                    }
                }
            }
            catch (IOException)
            {
                Console.WriteLine("[IPC] Plugin disconnected");
            }
            catch (OperationCanceledException)
            {
                // Normal shutdown
            }
            catch (Exception ex)
            {
                Console.WriteLine($"[IPC ERROR] {ex.Message}");
            }
        }

        private async Task SendToPlugin(string json)
        {
            if (_pipeServer == null || !_pipeServer.IsConnected)
                return;

            try
            {
                var message = json + "\n";
                var bytes = Encoding.UTF8.GetBytes(message);
                await _pipeServer.WriteAsync(bytes, 0, bytes.Length);
                await _pipeServer.FlushAsync();
            }
            catch (Exception ex)
            {
                Console.WriteLine($"[IPC ERROR] Failed to send to plugin: {ex.Message}");
            }
        }

        #endregion

        #region TCP Client (Server Connection)

        private async Task ConnectToServerAsync(CancellationToken ct)
        {
            while (!ct.IsCancellationRequested && _isRunning)
            {
                try
                {
                    Console.WriteLine($"[TCP] Connecting to server {_serverAddress}:{_serverPort}...");

                    _tcpClient = new TcpClient();
                    await _tcpClient.ConnectAsync(_serverAddress, _serverPort);
                    _tcpStream = _tcpClient.GetStream();

                    _serverConnected = true;
                    Console.WriteLine("[TCP] Connected to server!");
                    UpdateStatus();

                    // Handle server messages
                    await HandleServerMessagesAsync(ct);
                }
                catch (SocketException ex)
                {
                    Console.WriteLine($"[TCP ERROR] Connection failed: {ex.Message}");
                    Console.WriteLine("[TCP] Retrying in 5 seconds...");
                    await Task.Delay(5000, ct);
                }
                catch (OperationCanceledException)
                {
                    break;
                }
                catch (Exception ex)
                {
                    Console.WriteLine($"[TCP ERROR] {ex.Message}");
                    await Task.Delay(5000, ct);
                }
                finally
                {
                    _serverConnected = false;
                    _tcpStream?.Dispose();
                    _tcpClient?.Close();
                    _tcpStream = null;
                    _tcpClient = null;
                    UpdateStatus();
                }
            }
        }

        private async Task HandleServerMessagesAsync(CancellationToken ct)
        {
            var buffer = new byte[8192];

            try
            {
                while (!ct.IsCancellationRequested && _tcpStream != null)
                {
                    int bytesRead = await _tcpStream.ReadAsync(buffer, 0, buffer.Length, ct);
                    if (bytesRead == 0)
                        break;

                    var json = Encoding.UTF8.GetString(buffer, 0, bytesRead);
                    var lines = json.Split('\n', StringSplitOptions.RemoveEmptyEntries);

                    foreach (var line in lines)
                    {
                        // Forward to plugin
                        await ForwardToPlugin(line);
                    }
                }
            }
            catch (IOException)
            {
                Console.WriteLine("[TCP] Server disconnected");
            }
            catch (OperationCanceledException)
            {
                // Normal shutdown
            }
            catch (Exception ex)
            {
                Console.WriteLine($"[TCP ERROR] {ex.Message}");
            }
        }

        private async Task SendToServer(string json)
        {
            if (_tcpStream == null)
                return;

            try
            {
                var message = json + "\n";
                var bytes = Encoding.UTF8.GetBytes(message);
                await _tcpStream.WriteAsync(bytes, 0, bytes.Length);
                await _tcpStream.FlushAsync();
            }
            catch (Exception ex)
            {
                Console.WriteLine($"[TCP ERROR] Failed to send to server: {ex.Message}");
            }
        }

        #endregion

        #region Message Forwarding

        private async Task ForwardToServer(string json)
        {
            if (!_serverConnected)
            {
                Console.WriteLine("[WARN] Cannot forward to server - not connected");
                return;
            }

            try
            {
                // Parse and log
                using var doc = JsonDocument.Parse(json);
                var root = doc.RootElement;
                var type = root.GetProperty("Type").GetString();

                Console.WriteLine($"[PLUGIN -> SERVER] {type}");

                await SendToServer(json);
            }
            catch (Exception ex)
            {
                Console.WriteLine($"[ERROR] Forward to server failed: {ex.Message}");
            }
        }

        private async Task ForwardToPlugin(string json)
        {
            if (!_pluginConnected)
            {
                Console.WriteLine("[WARN] Cannot forward to plugin - not connected");
                return;
            }

            try
            {
                // Parse and log
                using var doc = JsonDocument.Parse(json);
                var root = doc.RootElement;
                var type = root.GetProperty("Type").GetString();

                Console.WriteLine($"[SERVER -> PLUGIN] {type}");

                await SendToPlugin(json);
            }
            catch (Exception ex)
            {
                Console.WriteLine($"[ERROR] Forward to plugin failed: {ex.Message}");
            }
        }

        #endregion

        #region Status Display

        private void UpdateStatus()
        {
            Console.WriteLine();
            Console.WriteLine("═══ Connection Status ═══");
            Console.WriteLine($"Plugin:  {(_pluginConnected ? "✓ Connected" : "✗ Disconnected")}");
            Console.WriteLine($"Server:  {(_serverConnected ? "✓ Connected" : "✗ Disconnected")}");
            Console.WriteLine($"Bridge:  {(_pluginConnected && _serverConnected ? "✓ Active" : "✗ Inactive")}");
            Console.WriteLine("══════════════════════════\n");
        }

        #endregion
    }
}
