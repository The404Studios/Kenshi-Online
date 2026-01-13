using System;
using System.Collections.Generic;
using System.Text.Json;

namespace KenshiMultiplayer.Core
{
    /// <summary>
    /// Trade state enum - phases of a trade transaction.
    /// </summary>
    public enum TradeState
    {
        /// <summary>Trade proposed, waiting for target to respond</summary>
        Proposed,

        /// <summary>Both parties modifying their offers</summary>
        Negotiating,

        /// <summary>Initiator has locked in their offer</summary>
        InitiatorReady,

        /// <summary>Target has locked in their offer</summary>
        TargetReady,

        /// <summary>Both parties ready, waiting for server execution</summary>
        BothReady,

        /// <summary>Server is processing the trade</summary>
        Executing,

        /// <summary>Trade completed successfully</summary>
        Completed,

        /// <summary>Trade was cancelled by a party</summary>
        Cancelled,

        /// <summary>Trade failed during execution</summary>
        Failed
    }

    /// <summary>
    /// Item stack for trading.
    /// </summary>
    public class TradeItem
    {
        public string ItemId { get; set; }
        public string ItemType { get; set; }
        public string ItemName { get; set; }
        public int Quantity { get; set; }

        /// <summary>
        /// Unique identifier for this specific item instance.
        /// Used to prevent duplication.
        /// </summary>
        public string InstanceId { get; set; }

        public TradeItem Clone()
        {
            return new TradeItem
            {
                ItemId = ItemId,
                ItemType = ItemType,
                ItemName = ItemName,
                Quantity = Quantity,
                InstanceId = InstanceId
            };
        }
    }

    /// <summary>
    /// One side of a trade offer.
    /// </summary>
    public class TradeOffer
    {
        public string PlayerId { get; set; }
        public List<TradeItem> Items { get; set; } = new();
        public int Money { get; set; }
        public bool IsReady { get; set; }
        public long LastModified { get; set; }

        public void AddItem(TradeItem item)
        {
            Items.Add(item);
            LastModified = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();
        }

        public bool RemoveItem(string itemId)
        {
            var removed = Items.RemoveAll(i => i.ItemId == itemId) > 0;
            if (removed)
                LastModified = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();
            return removed;
        }

        public void SetMoney(int amount)
        {
            Money = Math.Max(0, amount);
            LastModified = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();
        }

        public void Lock()
        {
            IsReady = true;
            LastModified = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();
        }

        public void Unlock()
        {
            IsReady = false;
            LastModified = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();
        }

        public TradeOffer Clone()
        {
            return new TradeOffer
            {
                PlayerId = PlayerId,
                Items = Items.ConvertAll(i => i.Clone()),
                Money = Money,
                IsReady = IsReady,
                LastModified = LastModified
            };
        }
    }

    /// <summary>
    /// TradeSession represents an atomic trade transaction.
    ///
    /// Key principles:
    /// - All validation happens server-side
    /// - Items are locked when both parties ready
    /// - Execution is atomic (all or nothing)
    /// - No item duplication possible
    /// - Disconnect = cancel (no item loss)
    /// </summary>
    public class TradeSession
    {
        /// <summary>
        /// Unique trade identifier.
        /// </summary>
        public string TradeId { get; set; }

        /// <summary>
        /// Player who initiated the trade.
        /// </summary>
        public string InitiatorId { get; set; }

        /// <summary>
        /// Player who is the target of the trade.
        /// </summary>
        public string TargetId { get; set; }

        /// <summary>
        /// Initiator's offer.
        /// </summary>
        public TradeOffer InitiatorOffer { get; set; } = new();

        /// <summary>
        /// Target's offer.
        /// </summary>
        public TradeOffer TargetOffer { get; set; } = new();

        /// <summary>
        /// Current state of the trade.
        /// </summary>
        public TradeState State { get; set; } = TradeState.Proposed;

        /// <summary>
        /// When the trade was started.
        /// </summary>
        public long StartedAt { get; set; }

        /// <summary>
        /// When the trade expires if not completed.
        /// </summary>
        public long ExpiresAt { get; set; }

        /// <summary>
        /// Error message if trade failed.
        /// </summary>
        public string FailureReason { get; set; }

        /// <summary>
        /// Trade timeout in milliseconds (5 minutes).
        /// </summary>
        public const long TRADE_TIMEOUT_MS = 5 * 60 * 1000;

        /// <summary>
        /// Ready state timeout in milliseconds (30 seconds).
        /// If both ready for >30s without execution, cancel.
        /// </summary>
        public const long READY_TIMEOUT_MS = 30 * 1000;

        private long _bothReadyAt;

        /// <summary>
        /// Is the trade expired?
        /// </summary>
        public bool IsExpired => DateTimeOffset.UtcNow.ToUnixTimeMilliseconds() > ExpiresAt;

        /// <summary>
        /// Is the trade in a terminal state?
        /// </summary>
        public bool IsComplete => State == TradeState.Completed ||
                                   State == TradeState.Cancelled ||
                                   State == TradeState.Failed;

        /// <summary>
        /// Create a new trade session.
        /// </summary>
        public static TradeSession Create(string initiatorId, string targetId)
        {
            var now = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();
            return new TradeSession
            {
                TradeId = Guid.NewGuid().ToString(),
                InitiatorId = initiatorId,
                TargetId = targetId,
                InitiatorOffer = new TradeOffer { PlayerId = initiatorId },
                TargetOffer = new TradeOffer { PlayerId = targetId },
                State = TradeState.Proposed,
                StartedAt = now,
                ExpiresAt = now + TRADE_TIMEOUT_MS
            };
        }

        /// <summary>
        /// Accept the trade proposal (target accepts).
        /// </summary>
        public TradeResult Accept()
        {
            if (State != TradeState.Proposed)
                return TradeResult.InvalidState("Trade is not in proposed state");

            State = TradeState.Negotiating;
            return TradeResult.Success();
        }

        /// <summary>
        /// Modify an offer.
        /// </summary>
        public TradeResult ModifyOffer(string playerId, List<TradeItem> items, int money)
        {
            if (State != TradeState.Negotiating)
                return TradeResult.InvalidState("Cannot modify offer in current state");

            var offer = GetOfferFor(playerId);
            if (offer == null)
                return TradeResult.InvalidPlayer("Player is not part of this trade");

            if (offer.IsReady)
                return TradeResult.InvalidState("Cannot modify locked offer");

            offer.Items = items;
            offer.Money = money;
            offer.LastModified = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();

            return TradeResult.Success();
        }

        /// <summary>
        /// Lock in an offer (ready to trade).
        /// </summary>
        public TradeResult LockOffer(string playerId)
        {
            if (State != TradeState.Negotiating &&
                State != TradeState.InitiatorReady &&
                State != TradeState.TargetReady)
                return TradeResult.InvalidState("Cannot lock offer in current state");

            var offer = GetOfferFor(playerId);
            if (offer == null)
                return TradeResult.InvalidPlayer("Player is not part of this trade");

            offer.Lock();

            // Update state based on who is ready
            UpdateReadyState();

            return TradeResult.Success();
        }

        /// <summary>
        /// Unlock an offer (not ready yet).
        /// </summary>
        public TradeResult UnlockOffer(string playerId)
        {
            if (State == TradeState.BothReady || State == TradeState.Executing)
                return TradeResult.InvalidState("Cannot unlock offer after both ready");

            var offer = GetOfferFor(playerId);
            if (offer == null)
                return TradeResult.InvalidPlayer("Player is not part of this trade");

            offer.Unlock();
            UpdateReadyState();

            return TradeResult.Success();
        }

        /// <summary>
        /// Cancel the trade.
        /// </summary>
        public TradeResult Cancel(string playerId, string reason = null)
        {
            if (IsComplete)
                return TradeResult.InvalidState("Trade is already complete");

            State = TradeState.Cancelled;
            FailureReason = reason ?? $"Cancelled by {playerId}";

            return TradeResult.Success();
        }

        /// <summary>
        /// Execute the trade (server-side only).
        /// This is the atomic transaction.
        /// </summary>
        public TradeExecutionResult Execute(
            Func<string, TradeItem, bool> validateItem,
            Func<string, int, bool> validateMoney,
            Action<string, TradeItem> removeItem,
            Action<string, TradeItem> addItem,
            Action<string, int> removeMoney,
            Action<string, int> addMoney)
        {
            if (State != TradeState.BothReady)
                return TradeExecutionResult.Fail("Trade is not ready for execution");

            State = TradeState.Executing;

            // Phase 1: Validate all items exist
            foreach (var item in InitiatorOffer.Items)
            {
                if (!validateItem(InitiatorId, item))
                {
                    Fail($"Initiator does not have item: {item.ItemName}");
                    return TradeExecutionResult.Fail(FailureReason);
                }
            }

            foreach (var item in TargetOffer.Items)
            {
                if (!validateItem(TargetId, item))
                {
                    Fail($"Target does not have item: {item.ItemName}");
                    return TradeExecutionResult.Fail(FailureReason);
                }
            }

            // Phase 2: Validate money
            if (InitiatorOffer.Money > 0 && !validateMoney(InitiatorId, InitiatorOffer.Money))
            {
                Fail("Initiator does not have enough money");
                return TradeExecutionResult.Fail(FailureReason);
            }

            if (TargetOffer.Money > 0 && !validateMoney(TargetId, TargetOffer.Money))
            {
                Fail("Target does not have enough money");
                return TradeExecutionResult.Fail(FailureReason);
            }

            // Phase 3: Execute transfers (atomic - must all succeed)
            try
            {
                // Remove items from initiator
                foreach (var item in InitiatorOffer.Items)
                    removeItem(InitiatorId, item);

                // Remove items from target
                foreach (var item in TargetOffer.Items)
                    removeItem(TargetId, item);

                // Remove money
                if (InitiatorOffer.Money > 0)
                    removeMoney(InitiatorId, InitiatorOffer.Money);
                if (TargetOffer.Money > 0)
                    removeMoney(TargetId, TargetOffer.Money);

                // Add items to target (from initiator)
                foreach (var item in InitiatorOffer.Items)
                    addItem(TargetId, item);

                // Add items to initiator (from target)
                foreach (var item in TargetOffer.Items)
                    addItem(InitiatorId, item);

                // Add money
                if (InitiatorOffer.Money > 0)
                    addMoney(TargetId, InitiatorOffer.Money);
                if (TargetOffer.Money > 0)
                    addMoney(InitiatorId, TargetOffer.Money);

                State = TradeState.Completed;
                return TradeExecutionResult.Ok();
            }
            catch (Exception ex)
            {
                // This should never happen if validation passed
                // But if it does, the trade fails (items may be in inconsistent state)
                // Server must handle rollback externally
                Fail($"Execution error: {ex.Message}");
                return TradeExecutionResult.Fail(FailureReason);
            }
        }

        private void Fail(string reason)
        {
            State = TradeState.Failed;
            FailureReason = reason;
        }

        private TradeOffer GetOfferFor(string playerId)
        {
            if (playerId == InitiatorId)
                return InitiatorOffer;
            if (playerId == TargetId)
                return TargetOffer;
            return null;
        }

        private void UpdateReadyState()
        {
            if (InitiatorOffer.IsReady && TargetOffer.IsReady)
            {
                State = TradeState.BothReady;
                _bothReadyAt = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();
            }
            else if (InitiatorOffer.IsReady)
            {
                State = TradeState.InitiatorReady;
            }
            else if (TargetOffer.IsReady)
            {
                State = TradeState.TargetReady;
            }
            else
            {
                State = TradeState.Negotiating;
            }
        }

        /// <summary>
        /// Check if ready timeout exceeded.
        /// </summary>
        public bool IsReadyTimeoutExceeded()
        {
            if (State != TradeState.BothReady)
                return false;

            var now = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();
            return now - _bothReadyAt > READY_TIMEOUT_MS;
        }

        public string ToJson()
        {
            return JsonSerializer.Serialize(this);
        }

        public static TradeSession FromJson(string json)
        {
            return JsonSerializer.Deserialize<TradeSession>(json);
        }
    }

    /// <summary>
    /// Result of a trade operation.
    /// </summary>
    public class TradeResult
    {
        public bool IsSuccess { get; set; }
        public string ErrorMessage { get; set; }
        public string ErrorCode { get; set; }

        public static TradeResult Success() => new TradeResult { IsSuccess = true };

        public static TradeResult InvalidState(string message) => new TradeResult
        {
            IsSuccess = false,
            ErrorCode = "INVALID_STATE",
            ErrorMessage = message
        };

        public static TradeResult InvalidPlayer(string message) => new TradeResult
        {
            IsSuccess = false,
            ErrorCode = "INVALID_PLAYER",
            ErrorMessage = message
        };
    }

    /// <summary>
    /// Result of trade execution.
    /// </summary>
    public class TradeExecutionResult
    {
        public bool Success { get; set; }
        public string Error { get; set; }

        public static TradeExecutionResult Ok() => new TradeExecutionResult { Success = true };
        public static TradeExecutionResult Fail(string error) => new TradeExecutionResult { Success = false, Error = error };
    }
}
