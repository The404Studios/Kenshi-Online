using Binarysharp.MemoryManagement.Native;
using System;
using System.Collections.Generic;
using System.Drawing;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading.Tasks;
using System.Windows.Forms;
using KenshiMultiplayer.Auth;
using KenshiMultiplayer.Common;
using KenshiMultiplayer.Networking;
using KenshiMultiplayer.Utility;

namespace KenshiMultiplayer
{
    /// <summary>
    /// Manages the multiplayer UI overlay and integration with Kenshi's UI
    /// </summary>
    public class UIManager
    {
        // DirectX overlay for in-game UI
        private DirectXOverlay overlay;
        private bool isOverlayEnabled = true;

        // UI Elements
        private MultiplayerMenu mainMenu;
        private ServerBrowser serverBrowser;
        private ChatWindow chatWindow;
        private PlayerList playerList;
        private PerformanceDisplay perfDisplay;
        private NotificationSystem notifications;

        // UI State
        private bool isMenuVisible = false;
        private bool isChatVisible = true;
        private bool isPlayerListVisible = true;
        private bool isPerfDisplayVisible = false;

        // Keybindings
        private Dictionary<Keys, Action> keybindings;
        private IntPtr keyboardHook;

        // Window handles
        private IntPtr gameWindowHandle;
        private Rectangle gameWindowRect;

        // Events
        public event Action<string> OnChatMessage;
        public event Action<string, int> OnServerSelected;
        public event Action OnHostGame;
        public event Action OnDisconnect;

        // Win32 imports for window manipulation
        [DllImport("user32.dll")]
        static extern IntPtr FindWindow(string lpClassName, string lpWindowName);

        [DllImport("user32.dll")]
        static extern bool GetWindowRect(IntPtr hWnd, out RECT lpRect);

        [DllImport("user32.dll")]
        static extern IntPtr SetWindowsHookEx(int idHook, LowLevelKeyboardProc lpfn, IntPtr hMod, uint dwThreadId);

        [DllImport("user32.dll")]
        static extern bool UnhookWindowsHookEx(IntPtr hhk);

        [DllImport("user32.dll")]
        static extern IntPtr CallNextHookEx(IntPtr hhk, int nCode, IntPtr wParam, IntPtr lParam);

        [DllImport("kernel32.dll")]
        static extern IntPtr GetModuleHandle(string lpModuleName);

        [StructLayout(LayoutKind.Sequential)]
        struct RECT
        {
            public int Left;
            public int Top;
            public int Right;
            public int Bottom;
        }

        delegate IntPtr LowLevelKeyboardProc(int nCode, IntPtr wParam, IntPtr lParam);

        private const int WH_KEYBOARD_LL = 13;
        private const int WM_KEYDOWN = 0x0100;

        public UIManager()
        {
            keybindings = new Dictionary<Keys, Action>();
            InitializeKeybindings();
        }

        /// <summary>
        /// Initialize the UI system
        /// </summary>
        public async Task<bool> Initialize()
        {
            try
            {
                // Find Kenshi window
                gameWindowHandle = FindWindow(null, "Kenshi");
                if (gameWindowHandle == IntPtr.Zero)
                {
                    Logger.Log("Kenshi window not found");
                    return false;
                }

                // Get window dimensions
                UpdateWindowRect();

                // Initialize DirectX overlay
                overlay = new DirectXOverlay(gameWindowHandle);
                if (!overlay.Initialize())
                {
                    Logger.Log("Failed to initialize DirectX overlay");
                    return false;
                }

                // Initialize UI components
                InitializeUIComponents();

                // Setup keyboard hook
                SetupKeyboardHook();

                // Start render loop
                Task.Run(() => RenderLoop());

                Logger.Log("UI Manager initialized successfully");
                return true;
            }
            catch (Exception ex)
            {
                Logger.Log($"Failed to initialize UI Manager: {ex.Message}");
                return false;
            }
        }

        /// <summary>
        /// Initialize UI components
        /// </summary>
        private void InitializeUIComponents()
        {
            // Main menu
            mainMenu = new MultiplayerMenu
            {
                Position = new Point(gameWindowRect.Width / 2 - 200, gameWindowRect.Height / 2 - 300),
                Size = new Size(400, 600)
            };
            mainMenu.OnHostClicked += () => OnHostGame?.Invoke();
            mainMenu.OnBrowseClicked += ShowServerBrowser;
            mainMenu.OnDirectConnectClicked += ShowDirectConnect;
            mainMenu.OnSettingsClicked += ShowSettings;

            // Server browser
            serverBrowser = new ServerBrowser
            {
                Position = new Point(gameWindowRect.Width / 2 - 400, gameWindowRect.Height / 2 - 300),
                Size = new Size(800, 600)
            };
            serverBrowser.OnServerSelected += (server) => OnServerSelected?.Invoke(server.Address, server.Port);
            serverBrowser.OnRefreshClicked += RefreshServerList;

            // Chat window
            chatWindow = new ChatWindow
            {
                Position = new Point(10, gameWindowRect.Height - 310),
                Size = new Size(400, 300)
            };
            chatWindow.OnMessageSent += (msg) => OnChatMessage?.Invoke(msg);

            // Player list
            playerList = new PlayerList
            {
                Position = new Point(gameWindowRect.Width - 210, 100),
                Size = new Size(200, 400)
            };

            // Performance display
            perfDisplay = new PerformanceDisplay
            {
                Position = new Point(10, 10),
                Size = new Size(200, 150)
            };

            // Notification system
            notifications = new NotificationSystem
            {
                Position = new Point(gameWindowRect.Width / 2 - 200, 50),
                Size = new Size(400, 100)
            };
        }

        /// <summary>
        /// Initialize keybindings
        /// </summary>
        private void InitializeKeybindings()
        {
            keybindings[Keys.F1] = ToggleMenu;
            keybindings[Keys.Enter] = FocusChat;
            keybindings[Keys.Tab] = TogglePlayerList;
            keybindings[Keys.F3] = TogglePerformanceDisplay;
            keybindings[Keys.Escape] = CloseCurrentWindow;
        }

        /// <summary>
        /// Setup keyboard hook
        /// </summary>
        private void SetupKeyboardHook()
        {
            LowLevelKeyboardProc proc = HookCallback;
            keyboardHook = SetWindowsHookEx(WH_KEYBOARD_LL, proc, GetModuleHandle(null), 0);
        }

        /// <summary>
        /// Keyboard hook callback
        /// </summary>
        private IntPtr HookCallback(int nCode, IntPtr wParam, IntPtr lParam)
        {
            if (nCode >= 0 && wParam == (IntPtr)WM_KEYDOWN)
            {
                int vkCode = Marshal.ReadInt32(lParam);
                var key = (Keys)vkCode;

                if (keybindings.TryGetValue(key, out var action))
                {
                    action.Invoke();
                }
            }

            return CallNextHookEx(keyboardHook, nCode, wParam, lParam);
        }

        /// <summary>
        /// Render loop
        /// </summary>
        private async Task RenderLoop()
        {
            while (isOverlayEnabled)
            {
                try
                {
                    overlay.BeginFrame();

                    // Render UI components
                    if (isMenuVisible)
                        RenderMenu();

                    if (isChatVisible)
                        RenderChat();

                    if (isPlayerListVisible)
                        RenderPlayerList();

                    if (isPerfDisplayVisible)
                        RenderPerformanceDisplay();

                    RenderNotifications();

                    overlay.EndFrame();

                    await Task.Delay(16); // ~60 FPS
                }
                catch (Exception ex)
                {
                    Logger.Log($"Render error: {ex.Message}");
                }
            }
        }

        /// <summary>
        /// Render main menu
        /// </summary>
        private void RenderMenu()
        {
            if (mainMenu.IsVisible)
            {
                overlay.DrawPanel(mainMenu.Position, mainMenu.Size, new Color(20, 20, 20, 230));

                // Title
                overlay.DrawText("KENSHI MULTIPLAYER",
                    new Point(mainMenu.Position.X + 100, mainMenu.Position.Y + 30),
                    new Font("Arial", 24, FontStyle.Bold),
                    Color.White);

                // Menu buttons
                var buttonY = mainMenu.Position.Y + 100;

                RenderButton("HOST GAME", new Rectangle(mainMenu.Position.X + 50, buttonY, 300, 50),
                    mainMenu.HostHovered, () => mainMenu.OnHostClicked?.Invoke());

                buttonY += 70;
                RenderButton("BROWSE SERVERS", new Rectangle(mainMenu.Position.X + 50, buttonY, 300, 50),
                    mainMenu.BrowseHovered, () => mainMenu.OnBrowseClicked?.Invoke());

                buttonY += 70;
                RenderButton("DIRECT CONNECT", new Rectangle(mainMenu.Position.X + 50, buttonY, 300, 50),
                    mainMenu.DirectHovered, () => mainMenu.OnDirectConnectClicked?.Invoke());

                buttonY += 70;
                RenderButton("SETTINGS", new Rectangle(mainMenu.Position.X + 50, buttonY, 300, 50),
                    mainMenu.SettingsHovered, () => mainMenu.OnSettingsClicked?.Invoke());

                buttonY += 70;
                RenderButton("DISCONNECT", new Rectangle(mainMenu.Position.X + 50, buttonY, 300, 50),
                    mainMenu.DisconnectHovered, () => OnDisconnect?.Invoke());

                // Version info
                overlay.DrawText("Version 1.0.0",
                    new Point(mainMenu.Position.X + 150, mainMenu.Position.Y + 550),
                    new Font("Arial", 10),
                    Color.Gray);
            }
        }

        /// <summary>
        /// Render chat window
        /// </summary>
        private void RenderChat()
        {
            // Background
            overlay.DrawPanel(chatWindow.Position, chatWindow.Size, new Color(10, 10, 10, 200));

            // Chat messages
            var messageY = chatWindow.Position.Y + 10;
            foreach (var message in chatWindow.Messages.TakeLast(15))
            {
                var color = GetChatColor(message.Type);
                overlay.DrawText($"[{message.Timestamp:HH:mm}] {message.Sender}: {message.Text}",
                    new Point(chatWindow.Position.X + 10, messageY),
                    new Font("Arial", 10),
                    color);
                messageY += 18;
            }

            // Input field
            var inputY = chatWindow.Position.Y + chatWindow.Size.Height - 30;
            overlay.DrawPanel(new Point(chatWindow.Position.X + 5, inputY),
                new Size(chatWindow.Size.Width - 10, 25),
                new Color(30, 30, 30, 255));

            if (chatWindow.IsInputActive)
            {
                overlay.DrawText(chatWindow.CurrentInput + "_",
                    new Point(chatWindow.Position.X + 10, inputY + 5),
                    new Font("Arial", 10),
                    Color.White);
            }
        }

        /// <summary>
        /// Render player list
        /// </summary>
        private void RenderPlayerList()
        {
            // Background
            overlay.DrawPanel(playerList.Position, playerList.Size, new Color(10, 10, 10, 180));

            // Title
            overlay.DrawText($"PLAYERS ({playerList.Players.Count})",
                new Point(playerList.Position.X + 10, playerList.Position.Y + 10),
                new Font("Arial", 12, FontStyle.Bold),
                Color.White);

            // Player entries
            var playerY = playerList.Position.Y + 40;
            foreach (var player in playerList.Players)
            {
                // Status indicator
                var statusColor = player.IsConnected ? Color.Green : Color.Red;
                overlay.DrawCircle(new Point(playerList.Position.X + 10, playerY + 8), 4, statusColor);

                // Player name
                overlay.DrawText(player.Name,
                    new Point(playerList.Position.X + 25, playerY),
                    new Font("Arial", 10),
                    Color.White);

                // Ping
                var pingColor = GetPingColor(player.Ping);
                overlay.DrawText($"{player.Ping}ms",
                    new Point(playerList.Position.X + 150, playerY),
                    new Font("Arial", 10),
                    pingColor);

                playerY += 25;
            }
        }

        /// <summary>
        /// Render performance display
        /// </summary>
        private void RenderPerformanceDisplay()
        {
            // Background
            overlay.DrawPanel(perfDisplay.Position, perfDisplay.Size, new Color(10, 10, 10, 150));

            var textY = perfDisplay.Position.Y + 10;

            // FPS
            overlay.DrawText($"FPS: {perfDisplay.FPS}",
                new Point(perfDisplay.Position.X + 10, textY),
                new Font("Arial", 10),
                GetFPSColor(perfDisplay.FPS));
            textY += 20;

            // Ping
            overlay.DrawText($"Ping: {perfDisplay.Ping}ms",
                new Point(perfDisplay.Position.X + 10, textY),
                new Font("Arial", 10),
                GetPingColor(perfDisplay.Ping));
            textY += 20;

            // Bandwidth
            overlay.DrawText($"Down: {FormatBandwidth(perfDisplay.DownloadSpeed)}",
                new Point(perfDisplay.Position.X + 10, textY),
                new Font("Arial", 10),
                Color.White);
            textY += 20;

            overlay.DrawText($"Up: {FormatBandwidth(perfDisplay.UploadSpeed)}",
                new Point(perfDisplay.Position.X + 10, textY),
                new Font("Arial", 10),
                Color.White);
            textY += 20;

            // Packet loss
            var lossColor = perfDisplay.PacketLoss > 0 ? Color.Yellow : Color.Green;
            overlay.DrawText($"Loss: {perfDisplay.PacketLoss:F1}%",
                new Point(perfDisplay.Position.X + 10, textY),
                new Font("Arial", 10),
                lossColor);
        }

        /// <summary>
        /// Render notifications
        /// </summary>
        private void RenderNotifications()
        {
            var notificationY = notifications.Position.Y;

            foreach (var notification in notifications.ActiveNotifications)
            {
                var alpha = (int)(255 * notification.Opacity);
                var bgColor = GetNotificationColor(notification.Type, alpha);

                overlay.DrawPanel(new Point(notifications.Position.X, notificationY),
                    new Size(notifications.Size.Width, 40),
                    bgColor);

                overlay.DrawText(notification.Message,
                    new Point(notifications.Position.X + 10, notificationY + 12),
                    new Font("Arial", 11, FontStyle.Bold),
                    Color.FromArgb(alpha, Color.White));

                notificationY += 45;
            }
        }

        /// <summary>
        /// Render button helper
        /// </summary>
        private void RenderButton(string text, Rectangle rect, bool isHovered, Action onClick)
        {
            var bgColor = isHovered ? new Color(60, 60, 60, 255) : new Color(40, 40, 40, 255);
            overlay.DrawPanel(new Point(rect.X, rect.Y), new Size(rect.Width, rect.Height), bgColor);
            overlay.DrawBorder(new Point(rect.X, rect.Y), new Size(rect.Width, rect.Height), Color.Gray, 2);

            var textSize = overlay.MeasureText(text, new Font("Arial", 14));
            var textX = rect.X + (rect.Width - textSize.Width) / 2;
            var textY = rect.Y + (rect.Height - textSize.Height) / 2;

            overlay.DrawText(text, new Point(textX, textY), new Font("Arial", 14), Color.White);

            if (overlay.IsMouseInRect(rect) && overlay.IsMouseClicked())
            {
                onClick?.Invoke();
            }
        }

        /// <summary>
        /// Toggle menu visibility
        /// </summary>
        private void ToggleMenu()
        {
            isMenuVisible = !isMenuVisible;
            mainMenu.IsVisible = isMenuVisible;
        }

        /// <summary>
        /// Focus chat input
        /// </summary>
        private void FocusChat()
        {
            chatWindow.IsInputActive = true;
            isChatVisible = true;
        }

        /// <summary>
        /// Toggle player list
        /// </summary>
        private void TogglePlayerList()
        {
            isPlayerListVisible = !isPlayerListVisible;
        }

        /// <summary>
        /// Toggle performance display
        /// </summary>
        private void TogglePerformanceDisplay()
        {
            isPerfDisplayVisible = !isPerfDisplayVisible;
        }

        /// <summary>
        /// Close current window
        /// </summary>
        private void CloseCurrentWindow()
        {
            if (serverBrowser.IsVisible)
                serverBrowser.IsVisible = false;
            else if (mainMenu.IsVisible)
                mainMenu.IsVisible = false;
        }

        /// <summary>
        /// Show server browser
        /// </summary>
        private void ShowServerBrowser()
        {
            serverBrowser.IsVisible = true;
            RefreshServerList();
        }

        /// <summary>
        /// Show direct connect dialog
        /// </summary>
        private void ShowDirectConnect()
        {
            // Show direct connect dialog
            var dialog = new DirectConnectDialog();
            if (dialog.ShowDialog() == DialogResult.OK)
            {
                OnServerSelected?.Invoke(dialog.Address, dialog.Port);
            }
        }

        /// <summary>
        /// Show settings
        /// </summary>
        private void ShowSettings()
        {
            // Show settings dialog
        }

        /// <summary>
        /// Refresh server list
        /// </summary>
        private async void RefreshServerList()
        {
            serverBrowser.IsRefreshing = true;

            // Query master server for list
            var servers = await QueryMasterServer();

            serverBrowser.Servers.Clear();
            serverBrowser.Servers.AddRange(servers);
            serverBrowser.IsRefreshing = false;
        }

        /// <summary>
        /// Query master server
        /// </summary>
        private async Task<List<ServerInfo>> QueryMasterServer()
        {
            // In production, would query a real master server
            return new List<ServerInfo>
            {
                new ServerInfo { Name = "Test Server 1", Address = "127.0.0.1", Port = 27015, Players = 3, MaxPlayers = 16, Ping = 15 },
                new ServerInfo { Name = "Test Server 2", Address = "192.168.1.100", Port = 27015, Players = 8, MaxPlayers = 16, Ping = 45 }
            };
        }

        /// <summary>
        /// Update window rect
        /// </summary>
        private void UpdateWindowRect()
        {
            RECT rect;
            if (GetWindowRect(gameWindowHandle, out rect))
            {
                gameWindowRect = new Rectangle(0, 0, rect.Right - rect.Left, rect.Bottom - rect.Top);
            }
        }

        /// <summary>
        /// Add chat message
        /// </summary>
        public void AddChatMessage(string sender, string message, ChatMessageType type = ChatMessageType.Player)
        {
            chatWindow.Messages.Add(new ChatMessage
            {
                Sender = sender,
                Text = message,
                Type = type,
                Timestamp = DateTime.Now
            });
        }

        /// <summary>
        /// Show notification
        /// </summary>
        public void ShowNotification(string message, NotificationType type = NotificationType.Info, float duration = 3.0f)
        {
            notifications.Show(message, type, duration);
        }

        /// <summary>
        /// Update player list
        /// </summary>
        public void UpdatePlayerList(List<PlayerInfo> players)
        {
            playerList.Players.Clear();
            playerList.Players.AddRange(players);
        }

        /// <summary>
        /// Update performance stats
        /// </summary>
        public void UpdatePerformanceStats(int fps, int ping, float downloadSpeed, float uploadSpeed, float packetLoss)
        {
            perfDisplay.FPS = fps;
            perfDisplay.Ping = ping;
            perfDisplay.DownloadSpeed = downloadSpeed;
            perfDisplay.UploadSpeed = uploadSpeed;
            perfDisplay.PacketLoss = packetLoss;
        }

        // Helper methods
        private Color GetChatColor(ChatMessageType type)
        {
            switch (type)
            {
                case ChatMessageType.System: return Color.Yellow;
                case ChatMessageType.Error: return Color.Red;
                case ChatMessageType.Admin: return Color.Orange;
                default: return Color.White;
            }
        }

        private Color GetPingColor(int ping)
        {
            if (ping < 50) return Color.Green;
            if (ping < 100) return Color.Yellow;
            if (ping < 150) return Color.Orange;
            return Color.Red;
        }

        private Color GetFPSColor(int fps)
        {
            if (fps >= 60) return Color.Green;
            if (fps >= 30) return Color.Yellow;
            return Color.Red;
        }

        private Color GetNotificationColor(NotificationType type, int alpha)
        {
            switch (type)
            {
                case NotificationType.Error: return Color.FromArgb(alpha, 180, 0, 0);
                case NotificationType.Warning: return Color.FromArgb(alpha, 180, 180, 0);
                case NotificationType.Success: return Color.FromArgb(alpha, 0, 180, 0);
                default: return Color.FromArgb(alpha, 0, 0, 0);
            }
        }

        private string FormatBandwidth(float bytesPerSecond)
        {
            if (bytesPerSecond < 1024)
                return $"{bytesPerSecond:F0} B/s";
            if (bytesPerSecond < 1024 * 1024)
                return $"{bytesPerSecond / 1024:F1} KB/s";
            return $"{bytesPerSecond / (1024 * 1024):F1} MB/s";
        }

        /// <summary>
        /// Cleanup
        /// </summary>
        public void Dispose()
        {
            if (keyboardHook != IntPtr.Zero)
            {
                UnhookWindowsHookEx(keyboardHook);
            }

            overlay?.Dispose();
        }
    }

    // UI Component Classes
    public class MultiplayerMenu
    {
        public Point Position { get; set; }
        public Size Size { get; set; }
        public bool IsVisible { get; set; }

        public bool HostHovered { get; set; }
        public bool BrowseHovered { get; set; }
        public bool DirectHovered { get; set; }
        public bool SettingsHovered { get; set; }
        public bool DisconnectHovered { get; set; }

        public Action OnHostClicked { get; set; }
        public Action OnBrowseClicked { get; set; }
        public Action OnDirectConnectClicked { get; set; }
        public Action OnSettingsClicked { get; set; }
    }

    public class ServerBrowser
    {
        public Point Position { get; set; }
        public Size Size { get; set; }
        public bool IsVisible { get; set; }
        public bool IsRefreshing { get; set; }

        public List<ServerInfo> Servers { get; set; } = new List<ServerInfo>();
        public ServerInfo SelectedServer { get; set; }

        public Action<ServerInfo> OnServerSelected { get; set; }
        public Action OnRefreshClicked { get; set; }
    }

    public class ChatWindow
    {
        public Point Position { get; set; }
        public Size Size { get; set; }
        public bool IsInputActive { get; set; }
        public string CurrentInput { get; set; } = "";

        public List<ChatMessage> Messages { get; set; } = new List<ChatMessage>();

        public Action<string> OnMessageSent { get; set; }
    }

    public class PlayerList
    {
        public Point Position { get; set; }
        public Size Size { get; set; }

        public List<PlayerInfo> Players { get; set; } = new List<PlayerInfo>();
    }

    public class PerformanceDisplay
    {
        public Point Position { get; set; }
        public Size Size { get; set; }

        public int FPS { get; set; }
        public int Ping { get; set; }
        public float DownloadSpeed { get; set; }
        public float UploadSpeed { get; set; }
        public float PacketLoss { get; set; }
    }

    public class NotificationSystem
    {
        public Point Position { get; set; }
        public Size Size { get; set; }

        public List<Notification> ActiveNotifications { get; set; } = new List<Notification>();

        public void Show(string message, NotificationType type, float duration)
        {
            ActiveNotifications.Add(new Notification
            {
                Message = message,
                Type = type,
                Duration = duration,
                StartTime = DateTime.Now
            });
        }
    }

    // Data structures
    public class ServerInfo
    {
        public string Name { get; set; }
        public string Address { get; set; }
        public int Port { get; set; }
        public int Players { get; set; }
        public int MaxPlayers { get; set; }
        public int Ping { get; set; }
        public string Map { get; set; }
        public string GameMode { get; set; }
    }

    public class PlayerInfo
    {
        public string Name { get; set; }
        public int Ping { get; set; }
        public bool IsConnected { get; set; }
        public int Score { get; set; }
    }

    public class ChatMessage
    {
        public string Sender { get; set; }
        public string Text { get; set; }
        public ChatMessageType Type { get; set; }
        public DateTime Timestamp { get; set; }
    }

    public class Notification
    {
        public string Message { get; set; }
        public NotificationType Type { get; set; }
        public float Duration { get; set; }
        public DateTime StartTime { get; set; }
        public float Opacity => Math.Max(0, 1.0f - (float)(DateTime.Now - StartTime).TotalSeconds / Duration);
    }

    public enum ChatMessageType
    {
        Player,
        System,
        Error,
        Admin
    }

    public enum NotificationType
    {
        Info,
        Success,
        Warning,
        Error
    }

    // Placeholder for DirectX overlay
    public class DirectXOverlay : IDisposable
    {
        private IntPtr windowHandle;

        public DirectXOverlay(IntPtr handle)
        {
            windowHandle = handle;
        }

        public bool Initialize() => true;
        public void BeginFrame() { }
        public void EndFrame() { }
        public void DrawPanel(Point position, Size size, Color color) { }
        public void DrawText(string text, Point position, Font font, Color color) { }
        public void DrawCircle(Point center, int radius, Color color) { }
        public void DrawBorder(Point position, Size size, Color color, int thickness) { }
        public Size MeasureText(string text, Font font) => new Size(100, 20);
        public bool IsMouseInRect(Rectangle rect) => false;
        public bool IsMouseClicked() => false;
        public void Dispose() { }
    }

    public class DirectConnectDialog : Form
    {
        public string Address { get; set; }
        public int Port { get; set; }

        public DirectConnectDialog()
        {
            // Initialize dialog
        }
    }
}