using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Linq;
using KenshiOnline.Core.Entities;

namespace KenshiOnline.Core.Trading
{
    /// <summary>
    /// Trade status
    /// </summary>
    public enum TradeStatus
    {
        Pending,
        BothAccepted,
        Completed,
        Cancelled,
        Expired
    }

    /// <summary>
    /// Trade offer item
    /// </summary>
    public class TradeItem
    {
        public Guid ItemId { get; set; }
        public string ItemName { get; set; }
        public int Quantity { get; set; }
        public Dictionary<string, object> ItemData { get; set; }

        public TradeItem()
        {
            ItemData = new Dictionary<string, object>();
        }

        public Dictionary<string, object> Serialize()
        {
            return new Dictionary<string, object>
            {
                ["itemId"] = ItemId.ToString(),
                ["itemName"] = ItemName ?? "",
                ["quantity"] = Quantity,
                ["itemData"] = ItemData
            };
        }
    }

    /// <summary>
    /// Trade session between two players
    /// </summary>
    public class TradeSession
    {
        public string TradeId { get; set; }
        public string Player1Id { get; set; }
        public string Player1Name { get; set; }
        public string Player2Id { get; set; }
        public string Player2Name { get; set; }

        // Offered items
        public List<TradeItem> Player1Offers { get; set; }
        public List<TradeItem> Player2Offers { get; set; }

        // Money offers
        public int Player1Money { get; set; }
        public int Player2Money { get; set; }

        // Acceptance status
        public bool Player1Accepted { get; set; }
        public bool Player2Accepted { get; set; }

        // Trade status
        public TradeStatus Status { get; set; }

        // Timestamps
        public DateTime CreatedAt { get; set; }
        public DateTime ExpiresAt { get; set; }
        public DateTime? CompletedAt { get; set; }

        public TradeSession()
        {
            TradeId = Guid.NewGuid().ToString();
            Player1Offers = new List<TradeItem>();
            Player2Offers = new List<TradeItem>();
            Status = TradeStatus.Pending;
            CreatedAt = DateTime.UtcNow;
            ExpiresAt = CreatedAt.AddMinutes(5); // 5 minute expiration
        }

        public bool IsExpired => DateTime.UtcNow > ExpiresAt && Status == TradeStatus.Pending;
        public bool BothAccepted => Player1Accepted && Player2Accepted;

        public Dictionary<string, object> Serialize()
        {
            return new Dictionary<string, object>
            {
                ["tradeId"] = TradeId,
                ["player1Id"] = Player1Id ?? "",
                ["player1Name"] = Player1Name ?? "",
                ["player2Id"] = Player2Id ?? "",
                ["player2Name"] = Player2Name ?? "",
                ["player1Offers"] = Player1Offers.Select(i => i.Serialize()).ToList(),
                ["player2Offers"] = Player2Offers.Select(i => i.Serialize()).ToList(),
                ["player1Money"] = Player1Money,
                ["player2Money"] = Player2Money,
                ["player1Accepted"] = Player1Accepted,
                ["player2Accepted"] = Player2Accepted,
                ["status"] = Status.ToString(),
                ["createdAt"] = CreatedAt.ToString("o"),
                ["expiresAt"] = ExpiresAt.ToString("o"),
                ["completedAt"] = CompletedAt?.ToString("o") ?? "",
                ["isExpired"] = IsExpired
            };
        }
    }

    /// <summary>
    /// Trading system for player-to-player item exchanges
    /// </summary>
    public class TradingSystem
    {
        private readonly ConcurrentDictionary<string, TradeSession> _tradeSessions;
        private readonly ConcurrentDictionary<string, string> _playerTrades; // PlayerId -> TradeId
        private readonly object _lock = new object();

        // Settings
        public bool EnableTrading { get; set; } = true;
        public int MaxTradeDistance { get; set; } = 10; // meters
        public float TradeTimeout { get; set; } = 300f; // 5 minutes

        // Events
        public event Action<TradeSession> OnTradeStarted;
        public event Action<TradeSession> OnTradeCompleted;
        public event Action<TradeSession> OnTradeCancelled;
        public event Action<TradeSession, string> OnTradeItemAdded;
        public event Action<TradeSession, string> OnTradeAccepted;

        // Statistics
        public int ActiveTrades => _tradeSessions.Count;
        public int TotalTradesCompleted { get; private set; }
        public int TotalTradesCancelled { get; private set; }

        public TradingSystem()
        {
            _tradeSessions = new ConcurrentDictionary<string, TradeSession>();
            _playerTrades = new ConcurrentDictionary<string, string>();
        }

        #region Trade Management

        /// <summary>
        /// Start trade between two players
        /// </summary>
        public TradeSession StartTrade(string player1Id, string player1Name, string player2Id, string player2Name)
        {
            if (!EnableTrading)
                return null;

            // Check if either player is already trading
            if (_playerTrades.ContainsKey(player1Id) || _playerTrades.ContainsKey(player2Id))
                return null;

            // Create trade session
            var trade = new TradeSession
            {
                Player1Id = player1Id,
                Player1Name = player1Name,
                Player2Id = player2Id,
                Player2Name = player2Name
            };

            _tradeSessions[trade.TradeId] = trade;
            _playerTrades[player1Id] = trade.TradeId;
            _playerTrades[player2Id] = trade.TradeId;

            OnTradeStarted?.Invoke(trade);

            return trade;
        }

        /// <summary>
        /// Get trade session
        /// </summary>
        public TradeSession GetTrade(string tradeId)
        {
            _tradeSessions.TryGetValue(tradeId, out var trade);
            return trade;
        }

        /// <summary>
        /// Get player's active trade
        /// </summary>
        public TradeSession GetPlayerTrade(string playerId)
        {
            if (_playerTrades.TryGetValue(playerId, out var tradeId))
            {
                return GetTrade(tradeId);
            }
            return null;
        }

        /// <summary>
        /// Cancel trade
        /// </summary>
        public bool CancelTrade(string tradeId, string playerId)
        {
            if (!_tradeSessions.TryGetValue(tradeId, out var trade))
                return false;

            // Verify player is part of trade
            if (trade.Player1Id != playerId && trade.Player2Id != playerId)
                return false;

            // Mark as cancelled
            trade.Status = TradeStatus.Cancelled;

            // Clean up
            _playerTrades.TryRemove(trade.Player1Id, out _);
            _playerTrades.TryRemove(trade.Player2Id, out _);
            _tradeSessions.TryRemove(tradeId, out _);

            TotalTradesCancelled++;
            OnTradeCancelled?.Invoke(trade);

            return true;
        }

        #endregion

        #region Trade Items

        /// <summary>
        /// Add item to trade
        /// </summary>
        public bool AddItem(string tradeId, string playerId, Guid itemId, string itemName, int quantity = 1)
        {
            if (!_tradeSessions.TryGetValue(tradeId, out var trade))
                return false;

            // Reset acceptance when items change
            trade.Player1Accepted = false;
            trade.Player2Accepted = false;

            var tradeItem = new TradeItem
            {
                ItemId = itemId,
                ItemName = itemName,
                Quantity = quantity
            };

            // Add to appropriate player's offers
            if (playerId == trade.Player1Id)
            {
                trade.Player1Offers.Add(tradeItem);
            }
            else if (playerId == trade.Player2Id)
            {
                trade.Player2Offers.Add(tradeItem);
            }
            else
            {
                return false;
            }

            OnTradeItemAdded?.Invoke(trade, playerId);

            return true;
        }

        /// <summary>
        /// Remove item from trade
        /// </summary>
        public bool RemoveItem(string tradeId, string playerId, Guid itemId)
        {
            if (!_tradeSessions.TryGetValue(tradeId, out var trade))
                return false;

            // Reset acceptance when items change
            trade.Player1Accepted = false;
            trade.Player2Accepted = false;

            // Remove from appropriate player's offers
            if (playerId == trade.Player1Id)
            {
                var item = trade.Player1Offers.FirstOrDefault(i => i.ItemId == itemId);
                if (item != null)
                {
                    trade.Player1Offers.Remove(item);
                    return true;
                }
            }
            else if (playerId == trade.Player2Id)
            {
                var item = trade.Player2Offers.FirstOrDefault(i => i.ItemId == itemId);
                if (item != null)
                {
                    trade.Player2Offers.Remove(item);
                    return true;
                }
            }

            return false;
        }

        /// <summary>
        /// Set money offer
        /// </summary>
        public bool SetMoneyOffer(string tradeId, string playerId, int amount)
        {
            if (!_tradeSessions.TryGetValue(tradeId, out var trade))
                return false;

            // Reset acceptance when offers change
            trade.Player1Accepted = false;
            trade.Player2Accepted = false;

            // Set money for appropriate player
            if (playerId == trade.Player1Id)
            {
                trade.Player1Money = Math.Max(0, amount);
            }
            else if (playerId == trade.Player2Id)
            {
                trade.Player2Money = Math.Max(0, amount);
            }
            else
            {
                return false;
            }

            return true;
        }

        #endregion

        #region Trade Acceptance

        /// <summary>
        /// Accept trade
        /// </summary>
        public bool AcceptTrade(string tradeId, string playerId)
        {
            if (!_tradeSessions.TryGetValue(tradeId, out var trade))
                return false;

            // Mark player as accepted
            if (playerId == trade.Player1Id)
            {
                trade.Player1Accepted = true;
            }
            else if (playerId == trade.Player2Id)
            {
                trade.Player2Accepted = true;
            }
            else
            {
                return false;
            }

            OnTradeAccepted?.Invoke(trade, playerId);

            // If both accepted, complete trade
            if (trade.BothAccepted)
            {
                return CompleteTrade(tradeId);
            }

            return true;
        }

        /// <summary>
        /// Unaccept trade
        /// </summary>
        public bool UnacceptTrade(string tradeId, string playerId)
        {
            if (!_tradeSessions.TryGetValue(tradeId, out var trade))
                return false;

            // Unmark player as accepted
            if (playerId == trade.Player1Id)
            {
                trade.Player1Accepted = false;
            }
            else if (playerId == trade.Player2Id)
            {
                trade.Player2Accepted = false;
            }
            else
            {
                return false;
            }

            return true;
        }

        /// <summary>
        /// Complete trade (internal)
        /// </summary>
        private bool CompleteTrade(string tradeId)
        {
            if (!_tradeSessions.TryGetValue(tradeId, out var trade))
                return false;

            // Verify both players accepted
            if (!trade.BothAccepted)
                return false;

            // Mark as completed
            trade.Status = TradeStatus.Completed;
            trade.CompletedAt = DateTime.UtcNow;

            // Clean up
            _playerTrades.TryRemove(trade.Player1Id, out _);
            _playerTrades.TryRemove(trade.Player2Id, out _);

            // Keep in sessions for a bit for history
            // Will be cleaned up by Update()

            TotalTradesCompleted++;
            OnTradeCompleted?.Invoke(trade);

            return true;
        }

        #endregion

        #region Validation

        /// <summary>
        /// Validate trade items (ensure players own them)
        /// </summary>
        public bool ValidateTrade(string tradeId, Func<string, Guid, bool> validateOwnership)
        {
            if (!_tradeSessions.TryGetValue(tradeId, out var trade))
                return false;

            // Validate player 1's items
            foreach (var item in trade.Player1Offers)
            {
                if (!validateOwnership(trade.Player1Id, item.ItemId))
                    return false;
            }

            // Validate player 2's items
            foreach (var item in trade.Player2Offers)
            {
                if (!validateOwnership(trade.Player2Id, item.ItemId))
                    return false;
            }

            return true;
        }

        #endregion

        #region Maintenance

        /// <summary>
        /// Update trading system (cleanup expired/completed trades)
        /// </summary>
        public void Update()
        {
            var now = DateTime.UtcNow;
            var toRemove = new List<string>();

            foreach (var kvp in _tradeSessions)
            {
                var trade = kvp.Value;

                // Remove expired pending trades
                if (trade.IsExpired)
                {
                    trade.Status = TradeStatus.Expired;
                    _playerTrades.TryRemove(trade.Player1Id, out _);
                    _playerTrades.TryRemove(trade.Player2Id, out _);
                    toRemove.Add(kvp.Key);
                    OnTradeCancelled?.Invoke(trade);
                }

                // Remove old completed trades (keep for 1 minute)
                if (trade.Status == TradeStatus.Completed && trade.CompletedAt.HasValue)
                {
                    if ((now - trade.CompletedAt.Value).TotalSeconds > 60)
                    {
                        toRemove.Add(kvp.Key);
                    }
                }
            }

            foreach (var tradeId in toRemove)
            {
                _tradeSessions.TryRemove(tradeId, out _);
            }
        }

        #endregion

        #region Statistics

        /// <summary>
        /// Get trading statistics
        /// </summary>
        public Dictionary<string, object> GetStatistics()
        {
            return new Dictionary<string, object>
            {
                ["activeTrades"] = ActiveTrades,
                ["totalCompleted"] = TotalTradesCompleted,
                ["totalCancelled"] = TotalTradesCancelled,
                ["tradingEnabled"] = EnableTrading,
                ["maxTradeDistance"] = MaxTradeDistance
            };
        }

        #endregion
    }
}
