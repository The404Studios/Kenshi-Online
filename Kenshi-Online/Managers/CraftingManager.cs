using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;
using KenshiMultiplayer.Data;
using KenshiMultiplayer.Networking;

namespace KenshiMultiplayer.Managers
{
    /// <summary>
    /// Manages crafting system for players
    /// </summary>
    public class CraftingManager
    {
        private readonly Dictionary<string, CraftingRecipe> recipes;
        private readonly Dictionary<string, CraftingQueue> playerQueues;
        private readonly NetworkManager? networkManager;

        public CraftingManager(NetworkManager? networkManager = null)
        {
            this.networkManager = networkManager;
            recipes = new Dictionary<string, CraftingRecipe>();
            playerQueues = new Dictionary<string, CraftingQueue>();

            InitializeRecipes();
        }

        /// <summary>
        /// Initialize default crafting recipes
        /// </summary>
        private void InitializeRecipes()
        {
            // Basic materials
            recipes["iron_bar"] = new CraftingRecipe
            {
                RecipeId = "iron_bar",
                ResultItemId = "iron_bar",
                ResultQuantity = 1,
                Ingredients = new List<CraftingIngredient>
                {
                    new CraftingIngredient
                    {
                        ItemId = "iron_ore",
                        ItemName = "Iron Ore",
                        Quantity = 2,
                        IsConsumed = true
                    }
                },
                RequiredStation = CraftingStation.Forge,
                RequiredSkillLevel = 0,
                CraftingTime = 5.0f,
                ExperienceGained = 10
            };

            recipes["iron_sword"] = new CraftingRecipe
            {
                RecipeId = "iron_sword",
                ResultItemId = "iron_sword",
                ResultQuantity = 1,
                Ingredients = new List<CraftingIngredient>
                {
                    new CraftingIngredient
                    {
                        ItemId = "iron_bar",
                        ItemName = "Iron Bar",
                        Quantity = 3,
                        IsConsumed = true
                    },
                    new CraftingIngredient
                    {
                        ItemId = "leather",
                        ItemName = "Leather",
                        Quantity = 1,
                        IsConsumed = true
                    }
                },
                RequiredStation = CraftingStation.Forge,
                RequiredSkillLevel = 10,
                CraftingTime = 15.0f,
                ExperienceGained = 50
            };

            recipes["bandage"] = new CraftingRecipe
            {
                RecipeId = "bandage",
                ResultItemId = "bandage",
                ResultQuantity = 5,
                Ingredients = new List<CraftingIngredient>
                {
                    new CraftingIngredient
                    {
                        ItemId = "cloth",
                        ItemName = "Cloth",
                        Quantity = 2,
                        IsConsumed = true
                    }
                },
                RequiredStation = CraftingStation.Workbench,
                RequiredSkillLevel = 0,
                CraftingTime = 3.0f,
                ExperienceGained = 5
            };

            recipes["medkit"] = new CraftingRecipe
            {
                RecipeId = "medkit",
                ResultItemId = "medkit",
                ResultQuantity = 1,
                Ingredients = new List<CraftingIngredient>
                {
                    new CraftingIngredient
                    {
                        ItemId = "bandage",
                        ItemName = "Bandage",
                        Quantity = 3,
                        IsConsumed = true
                    },
                    new CraftingIngredient
                    {
                        ItemId = "herbs",
                        ItemName = "Medicinal Herbs",
                        Quantity = 2,
                        IsConsumed = true
                    }
                },
                RequiredStation = CraftingStation.Pharmacy,
                RequiredSkillLevel = 15,
                CraftingTime = 10.0f,
                ExperienceGained = 30
            };

            recipes["bread"] = new CraftingRecipe
            {
                RecipeId = "bread",
                ResultItemId = "bread",
                ResultQuantity = 3,
                Ingredients = new List<CraftingIngredient>
                {
                    new CraftingIngredient
                    {
                        ItemId = "wheat",
                        ItemName = "Wheat",
                        Quantity = 5,
                        IsConsumed = true
                    }
                },
                RequiredStation = CraftingStation.CookingStove,
                RequiredSkillLevel = 0,
                CraftingTime = 5.0f,
                ExperienceGained = 10
            };
        }

        /// <summary>
        /// Start crafting an item
        /// </summary>
        public bool StartCrafting(string playerId, string recipeId, PlayerData player, CraftingStation availableStation)
        {
            // Check if recipe exists
            if (!recipes.TryGetValue(recipeId, out CraftingRecipe? recipe))
            {
                NotifyPlayer(playerId, "error", "Recipe not found");
                return false;
            }

            // Check if player has correct station
            if (recipe.RequiredStation != CraftingStation.None && recipe.RequiredStation != availableStation)
            {
                NotifyPlayer(playerId, "error", $"Requires {recipe.RequiredStation}");
                return false;
            }

            // Check skill level
            string skillName = GetSkillForStation(recipe.RequiredStation);
            if (player.Skills.TryGetValue(skillName, out float skillLevel))
            {
                if (skillLevel < recipe.RequiredSkillLevel)
                {
                    NotifyPlayer(playerId, "error", $"Requires {skillName} level {recipe.RequiredSkillLevel}");
                    return false;
                }
            }
            else if (recipe.RequiredSkillLevel > 0)
            {
                NotifyPlayer(playerId, "error", $"Requires {skillName} skill");
                return false;
            }

            // Check if player has ingredients
            foreach (var ingredient in recipe.Ingredients)
            {
                if (!player.HasItem(ingredient.ItemId, ingredient.Quantity))
                {
                    NotifyPlayer(playerId, "error", $"Missing {ingredient.Quantity}x {ingredient.ItemName}");
                    return false;
                }
            }

            // Consume ingredients
            foreach (var ingredient in recipe.Ingredients.Where(i => i.IsConsumed))
            {
                player.UpdateInventory(ingredient.ItemId, -ingredient.Quantity);
            }

            // Add to crafting queue
            if (!playerQueues.ContainsKey(playerId))
            {
                playerQueues[playerId] = new CraftingQueue();
            }

            var craftingItem = new CraftingQueueItem
            {
                RecipeId = recipeId,
                Recipe = recipe,
                StartTime = DateTime.UtcNow,
                CompletionTime = DateTime.UtcNow.AddSeconds(recipe.CraftingTime)
            };

            playerQueues[playerId].Items.Add(craftingItem);

            Console.WriteLine($"Player {playerId} started crafting {recipe.ResultItemId}");
            NotifyPlayer(playerId, "crafting_started", $"Crafting {recipe.ResultItemId}...");

            // Start async completion check
            _ = CheckCraftingCompletion(playerId, craftingItem);

            return true;
        }

        /// <summary>
        /// Check for crafting completion
        /// </summary>
        private async Task CheckCraftingCompletion(string playerId, CraftingQueueItem item)
        {
            var delay = item.CompletionTime - DateTime.UtcNow;
            if (delay.TotalMilliseconds > 0)
            {
                await Task.Delay(delay);
            }

            CompleteCrafting(playerId, item);
        }

        /// <summary>
        /// Complete a crafting operation
        /// </summary>
        private void CompleteCrafting(string playerId, CraftingQueueItem item)
        {
            if (!playerQueues.TryGetValue(playerId, out CraftingQueue? queue))
                return;

            // Remove from queue
            queue.Items.Remove(item);

            // This would integrate with actual player inventory
            Console.WriteLine($"Player {playerId} completed crafting {item.Recipe.ResultItemId}");

            NotifyPlayer(playerId, "crafting_completed",
                $"Crafted {item.Recipe.ResultQuantity}x {item.Recipe.ResultItemId}");

            // Note: In a real implementation, we would add the item to player inventory here
            // and grant experience to the relevant skill
        }

        /// <summary>
        /// Cancel a crafting operation
        /// </summary>
        public bool CancelCrafting(string playerId, string recipeId)
        {
            if (!playerQueues.TryGetValue(playerId, out CraftingQueue? queue))
                return false;

            var item = queue.Items.FirstOrDefault(i => i.RecipeId == recipeId);
            if (item == null)
                return false;

            queue.Items.Remove(item);

            // Refund partial ingredients (50% if more than halfway complete)
            var elapsed = DateTime.UtcNow - item.StartTime;
            var totalTime = item.CompletionTime - item.StartTime;
            var progress = elapsed.TotalSeconds / totalTime.TotalSeconds;

            if (progress < 0.5)
            {
                // Refund ingredients - would integrate with player inventory
                Console.WriteLine($"Refunding ingredients to {playerId}");
            }

            Console.WriteLine($"Player {playerId} cancelled crafting {recipeId}");
            NotifyPlayer(playerId, "crafting_cancelled", $"Cancelled crafting {recipeId}");

            return true;
        }

        /// <summary>
        /// Get all recipes available to a player
        /// </summary>
        public List<CraftingRecipe> GetAvailableRecipes(PlayerData player, CraftingStation availableStation)
        {
            var available = new List<CraftingRecipe>();

            foreach (var recipe in recipes.Values)
            {
                // Check station requirement
                if (recipe.RequiredStation != CraftingStation.None &&
                    recipe.RequiredStation != availableStation)
                    continue;

                // Check skill requirement
                string skillName = GetSkillForStation(recipe.RequiredStation);
                if (player.Skills.TryGetValue(skillName, out float skillLevel))
                {
                    if (skillLevel >= recipe.RequiredSkillLevel)
                    {
                        available.Add(recipe);
                    }
                }
                else if (recipe.RequiredSkillLevel == 0)
                {
                    available.Add(recipe);
                }
            }

            return available;
        }

        /// <summary>
        /// Get crafting queue for a player
        /// </summary>
        public List<CraftingQueueItem> GetPlayerQueue(string playerId)
        {
            if (!playerQueues.TryGetValue(playerId, out CraftingQueue? queue))
                return new List<CraftingQueueItem>();

            return new List<CraftingQueueItem>(queue.Items);
        }

        /// <summary>
        /// Check if player can craft a recipe
        /// </summary>
        public bool CanCraft(string recipeId, PlayerData player, CraftingStation availableStation)
        {
            if (!recipes.TryGetValue(recipeId, out CraftingRecipe? recipe))
                return false;

            // Check station
            if (recipe.RequiredStation != CraftingStation.None &&
                recipe.RequiredStation != availableStation)
                return false;

            // Check skill
            string skillName = GetSkillForStation(recipe.RequiredStation);
            if (player.Skills.TryGetValue(skillName, out float skillLevel))
            {
                if (skillLevel < recipe.RequiredSkillLevel)
                    return false;
            }
            else if (recipe.RequiredSkillLevel > 0)
            {
                return false;
            }

            // Check ingredients
            foreach (var ingredient in recipe.Ingredients)
            {
                if (!player.HasItem(ingredient.ItemId, ingredient.Quantity))
                    return false;
            }

            return true;
        }

        /// <summary>
        /// Register a custom crafting recipe
        /// </summary>
        public void RegisterRecipe(CraftingRecipe recipe)
        {
            recipes[recipe.RecipeId] = recipe;
            Console.WriteLine($"Registered crafting recipe: {recipe.RecipeId}");
        }

        /// <summary>
        /// Get skill name for a crafting station
        /// </summary>
        private string GetSkillForStation(CraftingStation station)
        {
            return station switch
            {
                CraftingStation.Forge => "Smithing",
                CraftingStation.Workbench => "Engineering",
                CraftingStation.ResearchBench => "Science",
                CraftingStation.CookingStove => "Cooking",
                CraftingStation.Tailoring => "Tailoring",
                CraftingStation.Pharmacy => "Medicine",
                CraftingStation.Farm => "Farming",
                CraftingStation.Mine => "Mining",
                _ => "General"
            };
        }

        /// <summary>
        /// Notify player of crafting events
        /// </summary>
        private void NotifyPlayer(string playerId, string eventType, string message)
        {
            if (networkManager == null)
            {
                Console.WriteLine($"[{playerId}] {eventType}: {message}");
                return;
            }

            var gameMessage = new GameMessage
            {
                Type = MessageType.Notification,
                SenderId = "system",
                TargetId = playerId,
                Data = new Dictionary<string, object>
                {
                    { "eventType", eventType },
                    { "message", message },
                    { "category", "crafting" }
                }
            };

            try
            {
                networkManager.SendToPlayer(playerId, gameMessage);
            }
            catch (Exception ex)
            {
                Console.WriteLine($"Failed to send crafting notification: {ex.Message}");
            }
        }

        /// <summary>
        /// Get crafting statistics for a player
        /// </summary>
        public CraftingStatistics GetStatistics(string playerId)
        {
            if (!playerQueues.TryGetValue(playerId, out CraftingQueue? queue))
            {
                return new CraftingStatistics();
            }

            return new CraftingStatistics
            {
                ActiveCrafts = queue.Items.Count,
                TotalRecipesKnown = recipes.Count
            };
        }
    }

    /// <summary>
    /// Crafting queue for a player
    /// </summary>
    public class CraftingQueue
    {
        public List<CraftingQueueItem> Items { get; set; } = new List<CraftingQueueItem>();
    }

    /// <summary>
    /// Item in crafting queue
    /// </summary>
    public class CraftingQueueItem
    {
        public string RecipeId { get; set; } = string.Empty;
        public CraftingRecipe Recipe { get; set; } = new CraftingRecipe();
        public DateTime StartTime { get; set; }
        public DateTime CompletionTime { get; set; }
        public float Progress => Math.Min(1.0f,
            (float)(DateTime.UtcNow - StartTime).TotalSeconds /
            (float)(CompletionTime - StartTime).TotalSeconds);
    }

    /// <summary>
    /// Crafting statistics
    /// </summary>
    public class CraftingStatistics
    {
        public int ActiveCrafts { get; set; }
        public int TotalRecipesKnown { get; set; }
    }
}
