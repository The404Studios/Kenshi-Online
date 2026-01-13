using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;
using KenshiMultiplayer.Networking;
using KenshiMultiplayer.Utility;
using KenshiMultiplayer.Data;

namespace KenshiMultiplayer.Managers
{
    public enum TradeStatus
    {
        Pending,
        InProgress,
        Completed,
        Cancelled,
        Timeout
    }

    public class TradeItem
    {
        public string ItemId { get; set; }
        public string ItemName { get; set; }
        public int Quantity { get; set; }
        public float Condition { get; set; } = 1.0f;
    }

    public class TradeOffer
    {
        public string PlayerId { get; set; }
        public List<TradeItem> Items { get; set; } = new List<TradeItem>();
        public bool IsConfirmed { get; set; } = false;
    }

    public class TradeSession
    {
        public string Id { get; set; } = Guid.NewGuid().ToString();
        public string InitiatorId { get; set; }
        public string TargetId { get; set; }
        public TradeStatus Status { get; set; } = TradeStatus.Pending;
        public DateTime CreatedAt { get; set; } = DateTime.UtcNow;
        public DateTime? CompletedAt { get; set; }
        public TradeOffer InitiatorOffer { get; set; } = new TradeOffer();
        public TradeOffer TargetOffer { get; set; } = new TradeOffer();
        public DateTime LastActivity { get; set; } = DateTime.UtcNow;
    }

    public class TradeManager : IDisposable
    {
        // Trade timeout (5 minutes)
        private readonly TimeSpan TradeTimeout = TimeSpan.FromMinutes(5);

        private readonly Dictionary<string, TradeSession> activeTrades = new Dictionary<string, TradeSession>();
        private readonly Dictionary<string, List<TradeSession>> completedTrades = new Dictionary<string, List<TradeSession>>();

        private TradeSession currentTrade;
        private readonly string dataFilePath;
        private readonly EnhancedClient client;
        private readonly EnhancedServer server;
        private readonly CancellationTokenSource cleanupCancellation = new CancellationTokenSource();
        private bool disposed;

        // Server-side constructor
        public TradeManager(EnhancedServer serverInstance, string dataDirectory = "data")
        {
            server = serverInstance;
            dataFilePath = Path.Combine(dataDirectory, "trades.json");
            Directory.CreateDirectory(dataDirectory);

            LoadData(GetCompletedTrades());

            // Cleanup inactive trades periodically with cancellation support
            _ = RunCleanupLoopAsync(cleanupCancellation.Token);
        }

        private async Task RunCleanupLoopAsync(CancellationToken cancellationToken)
        {
            try
            {
                while (!cancellationToken.IsCancellationRequested)
                {
                    await Task.Delay(TimeSpan.FromMinutes(1), cancellationToken);
                    CleanupInactiveTrades();
                }
            }
            catch (OperationCanceledException)
            {
                // Expected when cancellation is requested
            }
            catch (Exception ex)
            {
                Logger.Log($"[TradeManager] Cleanup loop error: {ex.Message}");
            }
        }

        // Client-side constructor
        public TradeManager(EnhancedClient clientInstance, string dataDirectory = "data")
        {
            client = clientInstance;
            dataFilePath = Path.Combine(dataDirectory, "trades.json");
            Directory.CreateDirectory(dataDirectory);

            LoadData(GetCompletedTrades());

            // Subscribe to client message events
            if (client != null)
            {
                client.MessageReceived += OnMessageReceived;
            }
        }

        private Dictionary<string, List<TradeSession>> GetCompletedTrades()
        {
            return completedTrades;
        }

        private void LoadData(Dictionary<string, List<TradeSession>> completedTrades)
        {
            try
            {
                if (File.Exists(dataFilePath))
                {
                    string json = File.ReadAllText(dataFilePath);
                    var data = JsonSerializer.Deserialize<TradeData>(json);

                    if (data != null)
                    {
                        if (data.ActiveTrades != null)
                        {
                            foreach (var trade in data.ActiveTrades)
                            {
                                activeTrades[trade.Id] = trade;
                            }
                        }

                        if (data.CompletedTrades != null)
                        {
                            completedTrades = data.CompletedTrades;
                        }
                    }

                    Logger.Log($"Loaded {activeTrades.Count} active trades, {completedTrades.Sum(kv => kv.Value.Count)} completed trades");
                }
            }
            catch (Exception ex)
            {
                Logger.Log($"Error loading trade data: {ex.Message}");
            }
        }

        private void SaveData()
        {
            try
            {
                var data = new TradeData
                {
                    ActiveTrades = activeTrades.Values.ToList(),
                    CompletedTrades = completedTrades
                };

                string json = JsonSerializer.Serialize(data, new JsonSerializerOptions { WriteIndented = true });
                File.WriteAllText(dataFilePath, json);
            }
            catch (Exception ex)
            {
                Logger.Log($"Error saving trade data: {ex.Message}");
            }
        }

        // Client message handling
        private void OnMessageReceived(object sender, GameMessage message)
        {
            if (message.Type == MessageType.TradeRequest)
            {
                HandleTradeRequest(message);
            }
            else if (message.Type == MessageType.TradeAccept)
            {
                HandleTradeAccept(message);
            }
            else if (message.Type == MessageType.TradeDecline)
            {
                HandleTradeDecline(message);
            }
            else if (message.Type == MessageType.TradeUpdate)
            {
                HandleTradeUpdate(message);
            }
            else if (message.Type == MessageType.TradeCancel)
            {
                HandleTradeCancel(message);
            }
            else if (message.Type == MessageType.TradeComplete)
            {
                HandleTradeComplete(message);
            }
        }

        // Trade Methods

        // Initiate a trade with another player
        public bool InitiateTrade(string targetUsername)
        {
            if (string.IsNullOrWhiteSpace(targetUsername))
                return false;

            // Don't trade with yourself
            if (targetUsername == client.CurrentUsername)
                return false;

            // Check if already in a trade
            if (currentTrade != null)
                return false;

            // Create the trade request message
            var tradeMessage = new GameMessage
            {
                Type = MessageType.TradeRequest,
                PlayerId = client.CurrentUsername,
                Data = new Dictionary<string, object>
                {
                    { "targetUsername", targetUsername }
                },
                SessionId = client.AuthToken
            };

            // Send the request to the server
            client.SendMessageToServer(tradeMessage);

            return true;
        }

        // Accept a trade request
        public bool AcceptTradeRequest(string tradeId)
        {
            if (string.IsNullOrWhiteSpace(tradeId) || !activeTrades.TryGetValue(tradeId, out var trade))
                return false;

            // Can only accept if you're the target
            if (trade.TargetId != client.CurrentUsername)
                return false;

            // Can only accept pending trades
            if (trade.Status != TradeStatus.Pending)
                return false;

            // Check if already in a trade
            if (currentTrade != null && currentTrade.Id != tradeId)
                return false;

            // Create the accept message
            var acceptMessage = new GameMessage
            {
                Type = MessageType.TradeAccept,
                PlayerId = client.CurrentUsername,
                Data = new Dictionary<string, object>
                {
                    { "tradeId", tradeId }
                },
                SessionId = client.AuthToken
            };

            // Send the accept to the server
            client.SendMessageToServer(acceptMessage);

            // Update local state
            trade.Status = TradeStatus.InProgress;
            trade.LastActivity = DateTime.UtcNow;
            currentTrade = trade;

            SaveData();
            return true;
        }

        // Decline a trade request
        public bool DeclineTradeRequest(string tradeId)
        {
            if (string.IsNullOrWhiteSpace(tradeId) || !activeTrades.TryGetValue(tradeId, out var trade))
                return false;

            // Can only decline if you're the target
            if (trade.TargetId != client.CurrentUsername)
                return false;

            // Can only decline pending trades
            if (trade.Status != TradeStatus.Pending)
                return false;

            // Create the decline message
            var declineMessage = new GameMessage
            {
                Type = MessageType.TradeDecline,
                PlayerId = client.CurrentUsername,
                Data = new Dictionary<string, object>
                {
                    { "tradeId", tradeId }
                },
                SessionId = client.AuthToken
            };

            // Send the decline to the server
            client.SendMessageToServer(declineMessage);

            // Remove from active trades
            activeTrades.Remove(tradeId);

            SaveData();
            return true;
        }

        // Add an item to trade
        public bool AddItemToTrade(string itemId, string itemName, int quantity, float condition = 1.0f)
        {
            if (currentTrade == null || string.IsNullOrWhiteSpace(itemId) || quantity <= 0)
                return false;

            // Check if the trade is in progress
            if (currentTrade.Status != TradeStatus.InProgress)
                return false;

            // Get the player's offer
            var offer = currentTrade.InitiatorId == client.CurrentUsername
                ? currentTrade.InitiatorOffer
                : currentTrade.TargetOffer;

            // Check if already confirmed
            if (offer.IsConfirmed)
                return false;

            // Check if the item is already in the offer
            var existingItem = offer.Items.FirstOrDefault(i => i.ItemId == itemId);
            if (existingItem != null)
            {
                // Update quantity
                existingItem.Quantity += quantity;
            }
            else
            {
                // Add new item
                offer.Items.Add(new TradeItem
                {
                    ItemId = itemId,
                    ItemName = itemName,
                    Quantity = quantity,
                    Condition = condition
                });
            }

            // Reset the confirmation status
            offer.IsConfirmed = false;

            // Update LastActivity
            currentTrade.LastActivity = DateTime.UtcNow;

            // Create the update message
            var updateMessage = new GameMessage
            {
                Type = MessageType.TradeUpdate,
                PlayerId = client.CurrentUsername,
                Data = new Dictionary<string, object>
                {
                    { "tradeId", currentTrade.Id },
                    { "trade", JsonSerializer.Serialize(currentTrade) }
                },
                SessionId = client.AuthToken
            };

            // Send the update to the server
            client.SendMessageToServer(updateMessage);

            SaveData();
            return true;
        }

        // Remove an item from trade
        public bool RemoveItemFromTrade(string itemId)
        {
            if (currentTrade == null || string.IsNullOrWhiteSpace(itemId))
                return false;

            // Check if the trade is in progress
            if (currentTrade.Status != TradeStatus.InProgress)
                return false;

            // Get the player's offer
            var offer = currentTrade.InitiatorId == client.CurrentUsername
                ? currentTrade.InitiatorOffer
                : currentTrade.TargetOffer;

            // Check if already confirmed
            if (offer.IsConfirmed)
                return false;

            // Find and remove the item
            var existingItem = offer.Items.FirstOrDefault(i => i.ItemId == itemId);
            if (existingItem != null)
            {
                offer.Items.Remove(existingItem);
            }
            else
            {
                return false;
            }

            // Reset the confirmation status
            offer.IsConfirmed = false;

            // Update LastActivity
            currentTrade.LastActivity = DateTime.UtcNow;

            // Create the update message
            var updateMessage = new GameMessage
            {
                Type = MessageType.TradeUpdate,
                PlayerId = client.CurrentUsername,
                Data = new Dictionary<string, object>
                {
                    { "tradeId", currentTrade.Id },
                    { "trade", JsonSerializer.Serialize(currentTrade) }
                },
                SessionId = client.AuthToken
            };

            // Send the update to the server
            client.SendMessageToServer(updateMessage);

            SaveData();
            return true;
        }

        // Update item quantity in trade
        public bool UpdateItemQuantity(string itemId, int quantity)
        {
            if (currentTrade == null || string.IsNullOrWhiteSpace(itemId) || quantity <= 0)
                return false;

            // Check if the trade is in progress
            if (currentTrade.Status != TradeStatus.InProgress)
                return false;

            // Get the player's offer
            var offer = currentTrade.InitiatorId == client.CurrentUsername
                ? currentTrade.InitiatorOffer
                : currentTrade.TargetOffer;

            // Check if already confirmed
            if (offer.IsConfirmed)
                return false;

            // Find the item
            var existingItem = offer.Items.FirstOrDefault(i => i.ItemId == itemId);
            if (existingItem != null)
            {
                existingItem.Quantity = quantity;
            }
            else
            {
                return false;
            }

            // Reset the confirmation status
            offer.IsConfirmed = false;

            // Update LastActivity
            currentTrade.LastActivity = DateTime.UtcNow;

            // Create the update message
            var updateMessage = new GameMessage
            {
                Type = MessageType.TradeUpdate,
                PlayerId = client.CurrentUsername,
                Data = new Dictionary<string, object>
                {
                    { "tradeId", currentTrade.Id },
                    { "trade", JsonSerializer.Serialize(currentTrade) }
                },
                SessionId = client.AuthToken
            };

            // Send the update to the server
            client.SendMessageToServer(updateMessage);

            SaveData();
            return true;
        }

        // Confirm the trade offer
        public bool ConfirmTradeOffer()
        {
            if (currentTrade == null)
                return false;

            // Check if the trade is in progress
            if (currentTrade.Status != TradeStatus.InProgress)
                return false;

            // Get the player's offer
            var offer = currentTrade.InitiatorId == client.CurrentUsername
                ? currentTrade.InitiatorOffer
                : currentTrade.TargetOffer;

            // Set confirmation status
            offer.IsConfirmed = true;

            // Update LastActivity
            currentTrade.LastActivity = DateTime.UtcNow;

            // Create the update message
            var updateMessage = new GameMessage
            {
                Type = MessageType.TradeUpdate,
                PlayerId = client.CurrentUsername,
                Data = new Dictionary<string, object>
                {
                    { "tradeId", currentTrade.Id },
                    { "trade", JsonSerializer.Serialize(currentTrade) }
                },
                SessionId = client.AuthToken
            };

            // Send the update to the server
            client.SendMessageToServer(updateMessage);

            // Check if both players have confirmed
            if (currentTrade.InitiatorOffer.IsConfirmed && currentTrade.TargetOffer.IsConfirmed)
            {
                // Create the complete message
                var completeMessage = new GameMessage
                {
                    Type = MessageType.TradeComplete,
                    PlayerId = client.CurrentUsername,
                    Data = new Dictionary<string, object>
                    {
                        { "tradeId", currentTrade.Id }
                    },
                    SessionId = client.AuthToken
                };

                // Send the complete to the server
                client.SendMessageToServer(completeMessage);

                // Update local state
                CompleteTradeLocally(currentTrade.Id);
            }

            SaveData();
            return true;
        }

        // Cancel the trade
        public bool CancelTrade()
        {
            if (currentTrade == null)
                return false;

            // Can't cancel completed trades
            if (currentTrade.Status == TradeStatus.Completed)
                return false;

            // Create the cancel message
            var cancelMessage = new GameMessage
            {
                Type = MessageType.TradeCancel,
                PlayerId = client.CurrentUsername,
                Data = new Dictionary<string, object>
                {
                    { "tradeId", currentTrade.Id }
                },
                SessionId = client.AuthToken
            };

            // Send the cancel to the server
            client.SendMessageToServer(cancelMessage);

            // Update local state
            CancelTradeLocally(currentTrade.Id);

            return true;
        }

        // Get incoming trade requests
        public List<TradeSession> GetIncomingTradeRequests()
        {
            return activeTrades.Values
                .Where(t => t.Status == TradeStatus.Pending && t.TargetId == client.CurrentUsername)
                .OrderByDescending(t => t.CreatedAt)
                .ToList();
        }

        // Get outgoing trade requests
        public List<TradeSession> GetOutgoingTradeRequests()
        {
            return activeTrades.Values
                .Where(t => t.Status == TradeStatus.Pending && t.InitiatorId == client.CurrentUsername)
                .OrderByDescending(t => t.CreatedAt)
                .ToList();
        }

        // Get trade history
        public List<TradeSession> GetTradeHistory(int limit = 10)
        {
            if (!completedTrades.TryGetValue(client.CurrentUsername, out var userTrades))
            {
                return new List<TradeSession>();
            }

            return userTrades
                .OrderByDescending(t => t.CompletedAt)
                .Take(limit)
                .ToList();
        }

        // Get current active trade
        public TradeSession GetCurrentTrade()
        {
            return currentTrade;
        }

        // Local trade management
        private void CompleteTradeLocally(string tradeId)
        {
            if (!activeTrades.TryGetValue(tradeId, out var trade))
                return;

            // Update status
            trade.Status = TradeStatus.Completed;
            trade.CompletedAt = DateTime.UtcNow;

            // Add to completed trades
            AddToCompletedTrades(trade);

            // Remove from active trades
            activeTrades.Remove(tradeId);

            // Clear current trade if it's this one
            if (currentTrade != null && currentTrade.Id == tradeId)
            {
                currentTrade = null;
            }

            SaveData();
        }

        private void CancelTradeLocally(string tradeId)
        {
            if (!activeTrades.TryGetValue(tradeId, out var trade))
                return;

            // Update status
            trade.Status = TradeStatus.Cancelled;
            trade.CompletedAt = DateTime.UtcNow;

            // Remove from active trades
            activeTrades.Remove(tradeId);

            // Clear current trade if it's this one
            if (currentTrade != null && currentTrade.Id == tradeId)
            {
                currentTrade = null;
            }

            SaveData();
        }

        private void AddToCompletedTrades(TradeSession trade)
        {
            // Add to initiator's history
            if (!completedTrades.TryGetValue(trade.InitiatorId, out var initiatorTrades))
            {
                initiatorTrades = new List<TradeSession>();
                completedTrades[trade.InitiatorId] = initiatorTrades;
            }
            initiatorTrades.Add(trade);

            // Add to target's history
            if (!completedTrades.TryGetValue(trade.TargetId, out var targetTrades))
            {
                targetTrades = new List<TradeSession>();
                completedTrades[trade.TargetId] = targetTrades;
            }
            targetTrades.Add(trade);
        }

        // Cleanup inactive trades
        private void CleanupInactiveTrades()
        {
            var now = DateTime.UtcNow;
            var inactiveTrades = activeTrades.Values
                .Where(t => (t.Status == TradeStatus.Pending || t.Status == TradeStatus.InProgress) &&
                       now - t.LastActivity > TradeTimeout)
                .ToList();

            foreach (var trade in inactiveTrades)
            {
                trade.Status = TradeStatus.Timeout;
                trade.CompletedAt = now;

                // Remove from active trades
                activeTrades.Remove(trade.Id);

                // Clear current trade if it's this one
                if (currentTrade != null && currentTrade.Id == trade.Id)
                {
                    currentTrade = null;
                }
            }

            if (inactiveTrades.Count > 0)
            {
                SaveData();
                Logger.Log($"Cleaned up {inactiveTrades.Count} inactive trades");
            }
        }

        // Message handlers
        private void HandleTradeRequest(GameMessage message)
        {
            if (message.Data.TryGetValue("tradeId", out var tradeIdObj) &&
                message.Data.TryGetValue("trade", out var tradeObj))
            {
                string tradeId = tradeIdObj.ToString();
                var trade = JsonSerializer.Deserialize<TradeSession>(tradeObj.ToString());

                // Add to active trades
                activeTrades[tradeId] = trade;

                SaveData();
            }
        }

        private void HandleTradeAccept(GameMessage message)
        {
            if (message.Data.TryGetValue("tradeId", out var tradeIdObj))
            {
                string tradeId = tradeIdObj.ToString();

                if (activeTrades.TryGetValue(tradeId, out var trade))
                {
                    // Update status
                    trade.Status = TradeStatus.InProgress;
                    trade.LastActivity = DateTime.UtcNow;

                    // Set as current trade if we're the initiator
                    if (trade.InitiatorId == client.CurrentUsername)
                    {
                        currentTrade = trade;
                    }

                    SaveData();
                }
            }
        }

        private void HandleTradeDecline(GameMessage message)
        {
            if (message.Data.TryGetValue("tradeId", out var tradeIdObj))
            {
                string tradeId = tradeIdObj.ToString();

                // Remove from active trades
                activeTrades.Remove(tradeId);

                SaveData();
            }
        }

        private void HandleTradeUpdate(GameMessage message)
        {
            if (message.Data.TryGetValue("tradeId", out var tradeIdObj) &&
                message.Data.TryGetValue("trade", out var tradeObj))
            {
                string tradeId = tradeIdObj.ToString();
                var trade = JsonSerializer.Deserialize<TradeSession>(tradeObj.ToString());

                // Update the trade
                activeTrades[tradeId] = trade;

                // Update current trade if we're in this one
                if (currentTrade != null && currentTrade.Id == tradeId)
                {
                    currentTrade = trade;
                }

                SaveData();
            }
        }

        private void HandleTradeCancel(GameMessage message)
        {
            if (message.Data.TryGetValue("tradeId", out var tradeIdObj))
            {
                string tradeId = tradeIdObj.ToString();

                CancelTradeLocally(tradeId);
            }
        }

        private void HandleTradeComplete(GameMessage message)
        {
            if (message.Data.TryGetValue("tradeId", out var tradeIdObj))
            {
                string tradeId = tradeIdObj.ToString();

                CompleteTradeLocally(tradeId);
            }
        }

        public void Dispose()
        {
            if (disposed)
                return;

            disposed = true;

            // Cancel the cleanup loop
            cleanupCancellation.Cancel();
            cleanupCancellation.Dispose();

            // Unsubscribe from client events
            if (client != null)
            {
                client.MessageReceived -= OnMessageReceived;
            }

            // Save data before disposing
            SaveData();

            Logger.Log("[TradeManager] Disposed");
        }
    }

    public class TradeData
    {
        public List<TradeSession> ActiveTrades { get; set; } = new List<TradeSession>();
        public Dictionary<string, List<TradeSession>> CompletedTrades { get; set; } = new Dictionary<string, List<TradeSession>>();
    }
}