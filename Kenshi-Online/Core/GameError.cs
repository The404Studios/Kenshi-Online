using System;
using System.Collections.Generic;
using System.Text.Json;

namespace KenshiMultiplayer.Core
{
    /// <summary>
    /// Error categories for classification and handling.
    /// </summary>
    public enum ErrorCategory
    {
        /// <summary>Network connectivity issues</summary>
        Network,

        /// <summary>Authentication/authorization failures</summary>
        Auth,

        /// <summary>Invalid input or request validation</summary>
        Validation,

        /// <summary>Game state conflicts or inconsistencies</summary>
        State,

        /// <summary>Version compatibility issues</summary>
        Version,

        /// <summary>Missing resources or files</summary>
        Resource,

        /// <summary>Internal server/client errors</summary>
        Internal
    }

    /// <summary>
    /// Structured error for user-facing error messages.
    ///
    /// Every error shown to a user MUST have:
    /// 1. What happened (clear, non-technical)
    /// 2. Why it happened (if known)
    /// 3. What to do next (actionable)
    /// </summary>
    public class GameError
    {
        /// <summary>
        /// Unique error identifier for logging/tracking.
        /// </summary>
        public string ErrorId { get; set; }

        /// <summary>
        /// Error category for classification.
        /// </summary>
        public ErrorCategory Category { get; set; }

        /// <summary>
        /// Machine-readable error code.
        /// Example: "VERSION_MISMATCH", "CONN_TIMEOUT"
        /// </summary>
        public string Code { get; set; }

        /// <summary>
        /// Human-readable error message for display.
        /// Should be understandable by non-technical users.
        /// </summary>
        public string Message { get; set; }

        /// <summary>
        /// Technical details for logging/debugging.
        /// Not shown to users.
        /// </summary>
        public string Details { get; set; }

        /// <summary>
        /// Can the user retry this operation?
        /// </summary>
        public bool Recoverable { get; set; }

        /// <summary>
        /// Suggested recovery action for the user.
        /// Example: "Update your mod to version 1.2"
        /// </summary>
        public string RecoveryAction { get; set; }

        /// <summary>
        /// When this error occurred.
        /// </summary>
        public long Timestamp { get; set; }

        /// <summary>
        /// Additional context data.
        /// </summary>
        public Dictionary<string, object> Context { get; set; } = new();

        /// <summary>
        /// Create a new error with current timestamp.
        /// </summary>
        public static GameError Create(ErrorCategory category, string code, string message)
        {
            return new GameError
            {
                ErrorId = Guid.NewGuid().ToString(),
                Category = category,
                Code = code,
                Message = message,
                Timestamp = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds(),
                Recoverable = true
            };
        }

        /// <summary>
        /// Add technical details.
        /// </summary>
        public GameError WithDetails(string details)
        {
            Details = details;
            return this;
        }

        /// <summary>
        /// Set recoverability and action.
        /// </summary>
        public GameError WithRecovery(bool recoverable, string action = null)
        {
            Recoverable = recoverable;
            RecoveryAction = action;
            return this;
        }

        /// <summary>
        /// Add context data.
        /// </summary>
        public GameError WithContext(string key, object value)
        {
            Context[key] = value;
            return this;
        }

        /// <summary>
        /// Get formatted message for display.
        /// </summary>
        public string GetDisplayMessage()
        {
            var lines = new List<string> { Message };

            if (!string.IsNullOrEmpty(RecoveryAction))
            {
                lines.Add("");
                lines.Add($"Try: {RecoveryAction}");
            }

            return string.Join("\n", lines);
        }

        public string ToJson()
        {
            return JsonSerializer.Serialize(this);
        }

        public static GameError FromJson(string json)
        {
            return JsonSerializer.Deserialize<GameError>(json);
        }
    }

    /// <summary>
    /// Pre-defined error factory for common errors.
    /// Ensures consistent error messages across the application.
    /// </summary>
    public static class GameErrors
    {
        // ============================================
        // NETWORK ERRORS
        // ============================================

        public static GameError ConnectionRefused(string host, int port)
        {
            return GameError.Create(ErrorCategory.Network, "CONN_REFUSED",
                "Could not connect to the server.")
                .WithDetails($"Connection refused: {host}:{port}")
                .WithRecovery(true, "Check that the server is running and the IP/port are correct")
                .WithContext("Host", host)
                .WithContext("Port", port);
        }

        public static GameError ConnectionTimeout(string host, int port, int timeoutMs)
        {
            return GameError.Create(ErrorCategory.Network, "CONN_TIMEOUT",
                "Connection to server timed out.")
                .WithDetails($"Timeout after {timeoutMs}ms connecting to {host}:{port}")
                .WithRecovery(true, "Check your internet connection and try again")
                .WithContext("Host", host)
                .WithContext("Port", port);
        }

        public static GameError ConnectionLost(string reason)
        {
            return GameError.Create(ErrorCategory.Network, "CONN_LOST",
                "Lost connection to the server.")
                .WithDetails(reason)
                .WithRecovery(true, "Check your internet connection. Attempting to reconnect...");
        }

        public static GameError ServerUnreachable()
        {
            return GameError.Create(ErrorCategory.Network, "SERVER_UNREACHABLE",
                "Server is not reachable.")
                .WithRecovery(true, "The server may be offline. Try again later.");
        }

        // ============================================
        // AUTH ERRORS
        // ============================================

        public static GameError InvalidCredentials()
        {
            return GameError.Create(ErrorCategory.Auth, "AUTH_INVALID",
                "Invalid username or password.")
                .WithRecovery(true, "Check your credentials and try again");
        }

        public static GameError SessionExpired()
        {
            return GameError.Create(ErrorCategory.Auth, "AUTH_EXPIRED",
                "Your session has expired.")
                .WithRecovery(true, "Please reconnect to the server");
        }

        public static GameError NotAuthorized(string action)
        {
            return GameError.Create(ErrorCategory.Auth, "AUTH_FORBIDDEN",
                $"You are not authorized to {action}.")
                .WithRecovery(false, null);
        }

        public static GameError Banned(string reason)
        {
            return GameError.Create(ErrorCategory.Auth, "AUTH_BANNED",
                "You have been banned from this server.")
                .WithDetails(reason)
                .WithRecovery(false, "Contact the server administrator if you believe this is an error");
        }

        // ============================================
        // VERSION ERRORS
        // ============================================

        public static GameError KenshiVersionMismatch(string clientVersion, string serverVersion)
        {
            return GameError.Create(ErrorCategory.Version, "VERSION_KENSHI",
                $"Kenshi version mismatch. Server requires {serverVersion}, you have {clientVersion}.")
                .WithDetails($"Client: {clientVersion}, Server: {serverVersion}")
                .WithRecovery(false, $"Update Kenshi to version {serverVersion}")
                .WithContext("ClientVersion", clientVersion)
                .WithContext("ServerVersion", serverVersion);
        }

        public static GameError ModVersionMismatch(string clientVersion, string serverVersion)
        {
            return GameError.Create(ErrorCategory.Version, "VERSION_MOD",
                $"Mod version mismatch. Server requires {serverVersion}, you have {clientVersion}.")
                .WithDetails($"Client: {clientVersion}, Server: {serverVersion}")
                .WithRecovery(false, $"Update Kenshi Online mod to version {serverVersion}")
                .WithContext("ClientVersion", clientVersion)
                .WithContext("ServerVersion", serverVersion);
        }

        public static GameError ProtocolMismatch(string clientProtocol, string serverProtocol)
        {
            return GameError.Create(ErrorCategory.Version, "VERSION_PROTOCOL",
                "Network protocol mismatch. Please update the mod.")
                .WithDetails($"Client protocol: {clientProtocol}, Server protocol: {serverProtocol}")
                .WithRecovery(false, "Download the latest version of the mod");
        }

        public static GameError UnsupportedKenshiVersion(string version)
        {
            return GameError.Create(ErrorCategory.Version, "VERSION_UNSUPPORTED",
                $"Kenshi version {version} is not supported by this mod.")
                .WithRecovery(false, "Check the mod's supported versions list");
        }

        // ============================================
        // STATE ERRORS
        // ============================================

        public static GameError WorldHashMismatch()
        {
            return GameError.Create(ErrorCategory.State, "WORLD_MISMATCH",
                "Your world state does not match the server.")
                .WithRecovery(true, "The host may need to share the save file, or start fresh");
        }

        public static GameError SessionFull(int maxPlayers)
        {
            return GameError.Create(ErrorCategory.State, "SESSION_FULL",
                $"This session is full ({maxPlayers} players maximum).")
                .WithRecovery(false, "Wait for a player to leave or try another session");
        }

        public static GameError SessionClosed()
        {
            return GameError.Create(ErrorCategory.State, "SESSION_CLOSED",
                "This session has ended.")
                .WithRecovery(false, "The host has closed the session");
        }

        public static GameError EntityNotFound(string entityId)
        {
            return GameError.Create(ErrorCategory.State, "ENTITY_NOT_FOUND",
                "The requested entity does not exist.")
                .WithDetails($"Entity ID: {entityId}")
                .WithRecovery(true, "The entity may have been destroyed. Refresh your view.");
        }

        public static GameError Desync(string component)
        {
            return GameError.Create(ErrorCategory.State, "DESYNC",
                $"Your game state is out of sync ({component}).")
                .WithRecovery(true, "Requesting resync from server...");
        }

        // ============================================
        // VALIDATION ERRORS
        // ============================================

        public static GameError InvalidRequest(string reason)
        {
            return GameError.Create(ErrorCategory.Validation, "INVALID_REQUEST",
                "Your request could not be processed.")
                .WithDetails(reason)
                .WithRecovery(true, "Please try again");
        }

        public static GameError SpeedViolation(float speed, float maxSpeed)
        {
            return GameError.Create(ErrorCategory.Validation, "SPEED_VIOLATION",
                "Movement speed violation detected.")
                .WithDetails($"Speed: {speed:F2}, Max: {maxSpeed:F2}")
                .WithRecovery(true, "Your position has been corrected");
        }

        public static GameError RateLimited(string action)
        {
            return GameError.Create(ErrorCategory.Validation, "RATE_LIMITED",
                $"Too many {action} requests. Please slow down.")
                .WithRecovery(true, "Wait a moment and try again");
        }

        public static GameError ItemNotOwned(string itemId)
        {
            return GameError.Create(ErrorCategory.Validation, "ITEM_NOT_OWNED",
                "You do not own this item.")
                .WithDetails($"Item ID: {itemId}")
                .WithRecovery(false, null);
        }

        public static GameError InsufficientFunds(int required, int available)
        {
            return GameError.Create(ErrorCategory.Validation, "INSUFFICIENT_FUNDS",
                $"Not enough money. Required: {required}, Available: {available}")
                .WithRecovery(false, null);
        }

        // ============================================
        // TRADE ERRORS
        // ============================================

        public static GameError TradeCancelled(string reason)
        {
            return GameError.Create(ErrorCategory.State, "TRADE_CANCELLED",
                "The trade was cancelled.")
                .WithDetails(reason)
                .WithRecovery(true, "You can start a new trade");
        }

        public static GameError TradeFailed(string reason)
        {
            return GameError.Create(ErrorCategory.State, "TRADE_FAILED",
                "The trade could not be completed.")
                .WithDetails(reason)
                .WithRecovery(true, "No items were exchanged. You can try again.");
        }

        public static GameError TradeTimeout()
        {
            return GameError.Create(ErrorCategory.State, "TRADE_TIMEOUT",
                "The trade timed out.")
                .WithRecovery(true, "The trade took too long. You can start a new trade.");
        }

        // ============================================
        // RESOURCE ERRORS
        // ============================================

        public static GameError GameNotFound()
        {
            return GameError.Create(ErrorCategory.Resource, "GAME_NOT_FOUND",
                "Could not find Kenshi game process.")
                .WithRecovery(true, "Make sure Kenshi is running before starting the multiplayer mod");
        }

        public static GameError DllNotFound(string dllPath)
        {
            return GameError.Create(ErrorCategory.Resource, "DLL_NOT_FOUND",
                "Required mod files are missing.")
                .WithDetails($"Missing: {dllPath}")
                .WithRecovery(false, "Reinstall the mod");
        }

        public static GameError SaveNotFound(string savePath)
        {
            return GameError.Create(ErrorCategory.Resource, "SAVE_NOT_FOUND",
                "Could not find save file.")
                .WithDetails($"Path: {savePath}")
                .WithRecovery(true, "The save file may have been deleted or moved");
        }

        // ============================================
        // INTERNAL ERRORS
        // ============================================

        public static GameError Internal(string message, Exception ex = null)
        {
            return GameError.Create(ErrorCategory.Internal, "INTERNAL_ERROR",
                "An internal error occurred.")
                .WithDetails(ex?.ToString() ?? message)
                .WithRecovery(true, "Please report this issue to the developers");
        }

        public static GameError InjectionFailed(string reason)
        {
            return GameError.Create(ErrorCategory.Internal, "INJECTION_FAILED",
                "Failed to inject mod into Kenshi.")
                .WithDetails(reason)
                .WithRecovery(true, "Try running as administrator, or check antivirus settings");
        }
    }

    /// <summary>
    /// Error response wrapper for network messages.
    /// </summary>
    public class ErrorResponse
    {
        public bool IsError => Error != null;
        public GameError Error { get; set; }

        public static ErrorResponse Ok() => new ErrorResponse();
        public static ErrorResponse Fail(GameError error) => new ErrorResponse { Error = error };
    }
}
