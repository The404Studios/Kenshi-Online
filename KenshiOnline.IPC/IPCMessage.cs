using System;

namespace KenshiOnline.IPC
{
    /// <summary>
    /// IPC Message structure - must match C++ MessageProtocol.h
    /// </summary>
    public class IPCMessage
    {
        public MessageType Type { get; set; }
        public uint Sequence { get; set; }
        public ulong Timestamp { get; set; }
        public string Payload { get; set; }

        public IPCMessage()
        {
            Timestamp = (ulong)DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();
        }

        public IPCMessage(MessageType type, string payload) : this()
        {
            Type = type;
            Payload = payload ?? string.Empty;
        }

        public override string ToString()
        {
            return $"[{Type}] Seq:{Sequence} Time:{Timestamp} Payload:{Payload?.Substring(0, Math.Min(50, Payload?.Length ?? 0))}...";
        }
    }

    /// <summary>
    /// Message types - must match C++ enum
    /// </summary>
    public enum MessageType : uint
    {
        // Client → Server (UI → Backend)
        AUTHENTICATE_REQUEST = 1,
        SERVER_LIST_REQUEST = 2,
        CONNECT_SERVER_REQUEST = 3,
        DISCONNECT_REQUEST = 4,
        PLAYER_UPDATE = 5,
        CHAT_MESSAGE = 6,

        // Server → Client (Backend → UI)
        AUTH_RESPONSE = 100,
        SERVER_LIST_RESPONSE = 101,
        CONNECTION_STATUS = 102,
        GAME_STATE_UPDATE = 103,
        PLAYER_UPDATE_BROADCAST = 104,
        CHAT_MESSAGE_BROADCAST = 105,
        ERROR_MESSAGE = 199,
    }

    /// <summary>
    /// Connection status codes - must match C++ enum
    /// </summary>
    public enum ConnectionStatus : uint
    {
        DISCONNECTED = 0,
        CONNECTING = 1,
        CONNECTED = 2,
        AUTHENTICATED = 3,
        ERROR_AUTH_FAILED = 100,
        ERROR_SERVER_FULL = 101,
        ERROR_TIMEOUT = 102,
        ERROR_VERSION_MISMATCH = 103,
    }
}
