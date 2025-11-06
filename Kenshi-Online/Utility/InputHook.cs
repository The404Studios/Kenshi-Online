using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Numerics;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;
using KenshiMultiplayer.Networking;
using KenshiMultiplayer.Managers;
using KenshiMultiplayer.Game;

namespace KenshiMultiplayer.Utility
{
    /// <summary>
    /// Hooks into Kenshi's input system to synchronize player inputs across multiplayer
    /// </summary>
    public class InputHook
    {
        private Process kenshiProcess;
        private IntPtr processHandle;
        private KenshiOffsets offsets;
        private NetworkManager? networkManager;
        private bool isHooked;

        // Input state tracking
        private readonly Dictionary<string, InputState> playerInputStates;
        private InputState localInputState;

        // Throttling for network efficiency
        private DateTime lastInputBroadcast;
        private readonly TimeSpan broadcastInterval = TimeSpan.FromMilliseconds(50); // 20Hz

        public InputHook(NetworkManager? networkManager = null)
        {
            this.networkManager = networkManager;
            playerInputStates = new Dictionary<string, InputState>();
            localInputState = new InputState();
            lastInputBroadcast = DateTime.UtcNow;
            offsets = new KenshiOffsets();
        }

        /// <summary>
        /// Hook into Kenshi's input system
        /// </summary>
        public bool Hook(Process process, KenshiOffsets gameOffsets)
        {
            try
            {
                kenshiProcess = process;
                offsets = gameOffsets;
                processHandle = OpenProcess(ProcessAccessFlags.All, false, kenshiProcess.Id);

                if (processHandle == IntPtr.Zero)
                {
                    Logger.Log("Failed to open process for input hooking");
                    return false;
                }

                // Install input hooks
                InstallMouseHook();
                InstallKeyboardHook();

                isHooked = true;
                Logger.Log("Successfully hooked input system");

                // Start input monitoring
                Task.Run(() => MonitorInputLoop());

                return true;
            }
            catch (Exception ex)
            {
                Logger.Log($"Input hook failed: {ex.Message}");
                return false;
            }
        }

        /// <summary>
        /// Install mouse input hook
        /// </summary>
        private void InstallMouseHook()
        {
            // Hook mouse position and button reads
            IntPtr mouseInputOffset = IntPtr.Add(kenshiProcess.MainModule.BaseAddress, (int)offsets.MousePositionOffset);
            Logger.Log($"Installed mouse hook at 0x{mouseInputOffset.ToString("X")}");
        }

        /// <summary>
        /// Install keyboard input hook
        /// </summary>
        private void InstallKeyboardHook()
        {
            // Hook keyboard state reads
            IntPtr keyboardOffset = IntPtr.Add(kenshiProcess.MainModule.BaseAddress, (int)offsets.KeyboardStateOffset);
            Logger.Log($"Installed keyboard hook at 0x{keyboardOffset.ToString("X")}");
        }

        /// <summary>
        /// Read current input state from game memory
        /// </summary>
        private InputState ReadLocalInput()
        {
            if (!isHooked || processHandle == IntPtr.Zero)
                return new InputState();

            try
            {
                var state = new InputState
                {
                    Timestamp = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds()
                };

                // Read mouse position
                IntPtr mousePtr = IntPtr.Add(kenshiProcess.MainModule.BaseAddress, (int)offsets.MousePositionOffset);
                byte[] mouseData = new byte[16]; // Vector2 + buttons
                ReadProcessMemory(processHandle, mousePtr, mouseData, mouseData.Length, out _);

                state.MouseX = BitConverter.ToSingle(mouseData, 0);
                state.MouseY = BitConverter.ToSingle(mouseData, 4);
                state.MouseButtons = BitConverter.ToInt32(mouseData, 8);

                // Read keyboard state
                IntPtr keyboardPtr = IntPtr.Add(kenshiProcess.MainModule.BaseAddress, (int)offsets.KeyboardStateOffset);
                state.KeyState = new byte[256];
                ReadProcessMemory(processHandle, keyboardPtr, state.KeyState, 256, out _);

                return state;
            }
            catch (Exception ex)
            {
                Logger.Log($"Failed to read input state: {ex.Message}");
                return new InputState();
            }
        }

        /// <summary>
        /// Monitor input and broadcast changes
        /// </summary>
        private async Task MonitorInputLoop()
        {
            while (isHooked)
            {
                try
                {
                    // Read local input
                    var currentInput = ReadLocalInput();

                    // Check if input changed significantly
                    if (InputChanged(localInputState, currentInput))
                    {
                        localInputState = currentInput;

                        // Broadcast if enough time passed
                        if ((DateTime.UtcNow - lastInputBroadcast) >= broadcastInterval)
                        {
                            BroadcastInput(currentInput);
                            lastInputBroadcast = DateTime.UtcNow;
                        }
                    }

                    await Task.Delay(16); // ~60Hz monitoring
                }
                catch (Exception ex)
                {
                    Logger.Log($"Input monitoring error: {ex.Message}");
                    await Task.Delay(1000);
                }
            }
        }

        /// <summary>
        /// Check if input changed significantly
        /// </summary>
        private bool InputChanged(InputState oldState, InputState newState)
        {
            // Check mouse movement (threshold to avoid micro-movements)
            float mouseDelta = Math.Abs(newState.MouseX - oldState.MouseX) + Math.Abs(newState.MouseY - oldState.MouseY);
            if (mouseDelta > 5.0f)
                return true;

            // Check mouse buttons
            if (newState.MouseButtons != oldState.MouseButtons)
                return true;

            // Check important keys (W, A, S, D, Space, Shift, Ctrl)
            int[] importantKeys = { 0x57, 0x41, 0x53, 0x44, 0x20, 0x10, 0x11 };
            foreach (int key in importantKeys)
            {
                if (newState.KeyState[key] != oldState.KeyState[key])
                    return true;
            }

            return false;
        }

        /// <summary>
        /// Broadcast input to other players
        /// </summary>
        private void BroadcastInput(InputState state)
        {
            if (networkManager == null)
                return;

            try
            {
                var message = new GameMessage
                {
                    Type = "input_update",
                    SenderId = "local_player", // Would get actual player ID
                    Data = new Dictionary<string, object>
                    {
                        { "mouseX", state.MouseX },
                        { "mouseY", state.MouseY },
                        { "mouseButtons", state.MouseButtons },
                        { "keys", CompressKeyState(state.KeyState) },
                        { "timestamp", state.Timestamp }
                    }
                };

                networkManager.BroadcastToAll(message);
            }
            catch (Exception ex)
            {
                Logger.Log($"Failed to broadcast input: {ex.Message}");
            }
        }

        /// <summary>
        /// Compress key state (only send pressed keys)
        /// </summary>
        private List<int> CompressKeyState(byte[] keyState)
        {
            var pressedKeys = new List<int>();
            for (int i = 0; i < keyState.Length; i++)
            {
                if ((keyState[i] & 0x80) != 0) // Key is down
                {
                    pressedKeys.Add(i);
                }
            }
            return pressedKeys;
        }

        /// <summary>
        /// Apply remote player input (for spectating/debugging)
        /// </summary>
        public void ApplyRemoteInput(string playerId, InputState state)
        {
            playerInputStates[playerId] = state;
            // In a full implementation, this could influence prediction or display
        }

        /// <summary>
        /// Get input state for a player
        /// </summary>
        public InputState? GetPlayerInput(string playerId)
        {
            return playerInputStates.TryGetValue(playerId, out InputState? state) ? state : null;
        }

        /// <summary>
        /// Unhook input system
        /// </summary>
        public void Unhook()
        {
            if (!isHooked)
                return;

            isHooked = false;

            if (processHandle != IntPtr.Zero)
            {
                CloseHandle(processHandle);
                processHandle = IntPtr.Zero;
            }

            Logger.Log("Input hooks removed");
        }

        #region Win32 API

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern IntPtr OpenProcess(ProcessAccessFlags processAccess, bool bInheritHandle, int processId);

        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool ReadProcessMemory(IntPtr hProcess, IntPtr lpBaseAddress, byte[] lpBuffer, int dwSize, out IntPtr lpNumberOfBytesRead);

        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool CloseHandle(IntPtr hObject);

        [Flags]
        private enum ProcessAccessFlags : uint
        {
            All = 0x001F0FFF,
            Terminate = 0x00000001,
            CreateThread = 0x00000002,
            VirtualMemoryOperation = 0x00000008,
            VirtualMemoryRead = 0x00000010,
            VirtualMemoryWrite = 0x00000020,
            DuplicateHandle = 0x00000040,
            CreateProcess = 0x000000080,
            SetQuota = 0x00000100,
            SetInformation = 0x00000200,
            QueryInformation = 0x00000400,
            QueryLimitedInformation = 0x00001000,
            Synchronize = 0x00100000
        }

        #endregion
    }

    /// <summary>
    /// Input state for a player
    /// </summary>
    public class InputState
    {
        public float MouseX { get; set; }
        public float MouseY { get; set; }
        public int MouseButtons { get; set; }
        public byte[] KeyState { get; set; } = new byte[256];
        public long Timestamp { get; set; }

        /// <summary>
        /// Check if a specific key is pressed
        /// </summary>
        public bool IsKeyPressed(int virtualKeyCode)
        {
            if (virtualKeyCode < 0 || virtualKeyCode >= KeyState.Length)
                return false;

            return (KeyState[virtualKeyCode] & 0x80) != 0;
        }

        /// <summary>
        /// Get all pressed keys
        /// </summary>
        public List<int> GetPressedKeys()
        {
            var pressed = new List<int>();
            for (int i = 0; i < KeyState.Length; i++)
            {
                if ((KeyState[i] & 0x80) != 0)
                    pressed.Add(i);
            }
            return pressed;
        }
    }
}
