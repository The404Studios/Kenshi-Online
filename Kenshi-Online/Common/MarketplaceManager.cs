using KenshiMultiplayer.Auth;
using KenshiMultiplayer.Networking;
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.Json;
using System.Threading.Tasks;

namespace KenshiMultiplayer.Common
{
    public class MarketListing
    {
        public string Id { get; set; } = Guid.NewGuid().ToString();
        public string SellerId { get; set; }
        public string SellerName { get; set; }
        public string ItemId { get; set; }
        public string ItemName { get; set; }
        public int Quantity { get; set; }
        public int Price { get; set; }
        public float ItemCondition { get; set; } = 1.0f;
        public DateTime ListedAt { get; set; } = DateTime.UtcNow;
        public DateTime? ExpiresAt { get; set; }
        public bool IsSold { get; set; } = false;
        public DateTime? SoldAt { get; set; }
        public string BuyerId { get; set; }
        public string BuyerName { get; set; }
    }

    public class MarketplaceManager
    {
        private readonly Dictionary<string, MarketListing> listings = new Dictionary<string, MarketListing>();
        private readonly string dataFilePath;
        private readonly EnhancedClient client;
        private readonly EnhancedServer server;

        // Server-side constructor
        public MarketplaceManager(EnhancedServer serverInstance, string dataDirectory = "data")
        {
            server = serverInstance;
            dataFilePath = Path.Combine(dataDirectory, "marketplace.json");
            Directory.CreateDirectory(dataDirectory);

            LoadData();

            // Cleanup expired listings periodically
            Task.Run(async () => {
                while (true)
                {
                    await Task.Delay(TimeSpan.FromHours(1));
                    CleanupExpiredListings();
                }
            });
        }

        // Client-side constructor
        public MarketplaceManager(EnhancedClient clientInstance, string dataDirectory = "data")
        {
            client = clientInstance;
            dataFilePath = Path.Combine(dataDirectory, "marketplace.json");
            Directory.CreateDirectory(dataDirectory);

            LoadData();

            // Subscribe to client message events
            if (client != null)
            {
                client.MessageReceived += OnMessageReceived;
            }
        }

        private void LoadData()
        {
            try
            {
                if (File.Exists(dataFilePath))
                {
                    string json = File.ReadAllText(dataFilePath);
                    var data = JsonSerializer.Deserialize<MarketplaceData>(json);

                    if (data?.Listings != null)
                    {
                        foreach (var listing in data.Listings)
                        {
                            listings[listing.Id] = listing;
                        }
                    }

                    Logger.Log($"Loaded {listings.Count} marketplace listings");
                }
            }
            catch (Exception ex)
            {
                Logger.Log($"Error loading marketplace data: {ex.Message}");
            }
        }

        private void SaveData()
        {
            try
            {
                var data = new MarketplaceData
                {
                    Listings = listings.Values.ToList()
                };

                string json = JsonSerializer.Serialize(data, new JsonSerializerOptions { WriteIndented = true });
                File.WriteAllText(dataFilePath, json);
            }
            catch (Exception ex)
            {
                Logger.Log($"Error saving marketplace data: {ex.Message}");
            }
        }

        // Client message handling
        private void OnMessageReceived(object sender, GameMessage message)
        {
            if (message.Type == MessageType.MarketplaceUpdate)
            {
                HandleMarketplaceUpdate(message);
            }
            else if (message.Type == MessageType.MarketplacePurchase)
            {
                HandleMarketplacePurchase(message);
            }
        }

        // Create a new listing
        public bool CreateListing(string itemId, string itemName, int quantity, int price, float condition = 1.0f, TimeSpan? duration = null)
        {
            if (string.IsNullOrWhiteSpace(itemId) || quantity <= 0 || price <= 0)
                return false;

            // Check if player has the item in inventory
            // This would be implemented to check the actual inventory
            // For now, we'll assume they have it

            var listing = new MarketListing
            {
                SellerId = client.CurrentUsername,
                SellerName = client.CurrentUsername,
                ItemId = itemId,
                ItemName = itemName,
                Quantity = quantity,
                Price = price,
                ItemCondition = condition,
                ListedAt = DateTime.UtcNow
            };

            if (duration.HasValue)
            {
                listing.ExpiresAt = DateTime.UtcNow.Add(duration.Value);
            }

            // Add to local listings
            listings[listing.Id] = listing;

            // Create the listing message
            var listingMessage = new GameMessage
            {
                Type = MessageType.MarketplaceCreate,
                PlayerId = client.CurrentUsername,
                Data = new Dictionary<string, object>
                {
                    { "listing", JsonSerializer.Serialize(listing) }
                },
                SessionId = client.AuthToken
            };

            // Send the listing to the server
            client.SendMessageToServer(listingMessage);

            SaveData();
            return true;
        }

        // Purchase an item listing
        public bool PurchaseListing(string listingId)
        {
            if (string.IsNullOrWhiteSpace(listingId) || !listings.TryGetValue(listingId, out var listing))
                return false;

            if (listing.IsSold || listing.ExpiresAt.HasValue && listing.ExpiresAt.Value < DateTime.UtcNow)
                return false;

            // Don't buy your own listing
            if (listing.SellerId == client.CurrentUsername)
                return false;

            // Check if player has enough currency
            // This would be implemented to check the actual currency
            // For now, we'll assume they have enough

            // Create the purchase message
            var purchaseMessage = new GameMessage
            {
                Type = MessageType.MarketplacePurchase,
                PlayerId = client.CurrentUsername,
                Data = new Dictionary<string, object>
                {
                    { "listingId", listingId }
                },
                SessionId = client.AuthToken
            };

            // Send the purchase to the server
            client.SendMessageToServer(purchaseMessage);

            // Update local state (this would normally happen after server confirmation)
            listing.IsSold = true;
            listing.SoldAt = DateTime.UtcNow;
            listing.BuyerId = client.CurrentUsername;
            listing.BuyerName = client.CurrentUsername;

            SaveData();
            return true;
        }

        // Cancel a listing (only the seller can cancel)
        public bool CancelListing(string listingId)
        {
            if (string.IsNullOrWhiteSpace(listingId) || !listings.TryGetValue(listingId, out var listing))
                return false;

            // Can only cancel your own listings
            if (listing.SellerId != client.CurrentUsername)
                return false;

            // Can't cancel sold listings
            if (listing.IsSold)
                return false;

            // Create the cancel message
            var cancelMessage = new GameMessage
            {
                Type = MessageType.MarketplaceCancel,
                PlayerId = client.CurrentUsername,
                Data = new Dictionary<string, object>
                {
                    { "listingId", listingId }
                },
                SessionId = client.AuthToken
            };

            // Send the cancel to the server
            client.SendMessageToServer(cancelMessage);

            // Remove from local listings
            listings.Remove(listingId);

            SaveData();
            return true;
        }

        // Get active listings
        public List<MarketListing> GetActiveListings()
        {
            return listings.Values
                .Where(l => !l.IsSold && (!l.ExpiresAt.HasValue || l.ExpiresAt.Value > DateTime.UtcNow))
                .OrderByDescending(l => l.ListedAt)
                .ToList();
        }

        // Get my listings (both active and sold)
        public List<MarketListing> GetMyListings()
        {
            return listings.Values
                .Where(l => l.SellerId == client.CurrentUsername)
                .OrderByDescending(l => l.ListedAt)
                .ToList();
        }

        // Get my purchases
        public List<MarketListing> GetMyPurchases()
        {
            return listings.Values
                .Where(l => l.IsSold && l.BuyerId == client.CurrentUsername)
                .OrderByDescending(l => l.SoldAt)
                .ToList();
        }

        // Search listings
        public List<MarketListing> SearchListings(string searchTerm)
        {
            if (string.IsNullOrWhiteSpace(searchTerm))
                return GetActiveListings();

            searchTerm = searchTerm.ToLower();

            return listings.Values
                .Where(l => !l.IsSold &&
                       (!l.ExpiresAt.HasValue || l.ExpiresAt.Value > DateTime.UtcNow) &&
                       (l.ItemName.ToLower().Contains(searchTerm) ||
                        l.SellerName.ToLower().Contains(searchTerm)))
                .OrderByDescending(l => l.ListedAt)
                .ToList();
        }

        // Cleanup expired listings
        private void CleanupExpiredListings()
        {
            var now = DateTime.UtcNow;
            var expiredListings = listings.Values
                .Where(l => !l.IsSold && l.ExpiresAt.HasValue && l.ExpiresAt.Value < now)
                .ToList();

            foreach (var listing in expiredListings)
            {
                listings.Remove(listing.Id);
            }

            if (expiredListings.Count > 0)
            {
                SaveData();
                Logger.Log($"Cleaned up {expiredListings.Count} expired marketplace listings");
            }
        }

        // Message handlers
        private void HandleMarketplaceUpdate(GameMessage message)
        {
            if (message.Data.TryGetValue("listings", out var listingsObj))
            {
                var updatedListings = JsonSerializer.Deserialize<List<MarketListing>>(listingsObj.ToString());

                foreach (var listing in updatedListings)
                {
                    listings[listing.Id] = listing;
                }

                SaveData();
            }
        }

        private void HandleMarketplacePurchase(GameMessage message)
        {
            if (message.Data.TryGetValue("listingId", out var listingIdObj) &&
                message.Data.TryGetValue("success", out var successObj))
            {
                string listingId = listingIdObj.ToString();
                bool success = (bool)successObj;

                if (success && listings.TryGetValue(listingId, out var listing))
                {
                    // Update the listing status
                    listing.IsSold = true;
                    listing.SoldAt = DateTime.UtcNow;
                    listing.BuyerId = message.PlayerId;
                    listing.BuyerName = message.PlayerId;

                    SaveData();
                }
            }
        }
    }

    public class MarketplaceData
    {
        public List<MarketListing> Listings { get; set; } = new List<MarketListing>();
    }
}