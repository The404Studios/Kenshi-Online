using System;
using System.Collections.Generic;
using System.Linq;
using KenshiMultiplayer.Data;
using KenshiMultiplayer.Networking;
using KenshiMultiplayer.Utility;

namespace KenshiMultiplayer.Managers
{
    /// <summary>
    /// Manages dialog interactions between players and NPCs
    /// </summary>
    public class DialogManager
    {
        private readonly Dictionary<string, DialogData> activeDialogs;
        private readonly Dictionary<string, List<DialogData>> npcDialogs;
        private readonly NetworkManager? networkManager;

        public DialogManager(NetworkManager? networkManager = null)
        {
            this.networkManager = networkManager;
            activeDialogs = new Dictionary<string, DialogData>();
            npcDialogs = new Dictionary<string, List<DialogData>>();

            InitializeNPCDialogs();
        }

        /// <summary>
        /// Initialize default NPC dialog trees
        /// </summary>
        private void InitializeNPCDialogs()
        {
            // Example: Trader dialog
            npcDialogs["trader_generic"] = new List<DialogData>
            {
                new DialogData
                {
                    DialogId = "trader_greeting",
                    SpeakerId = "trader",
                    Text = "Welcome, traveler. What can I do for you?",
                    Options = new List<DialogOption>
                    {
                        new DialogOption
                        {
                            OptionId = "opt_trade",
                            Text = "I'd like to trade.",
                            Action = DialogAction.Trade,
                            NextDialogId = "trader_trade"
                        },
                        new DialogOption
                        {
                            OptionId = "opt_info",
                            Text = "Tell me about this area.",
                            Action = DialogAction.Info,
                            NextDialogId = "trader_info"
                        },
                        new DialogOption
                        {
                            OptionId = "opt_goodbye",
                            Text = "Goodbye.",
                            Action = DialogAction.EndDialog,
                            NextDialogId = ""
                        }
                    },
                    RequiredRelationship = -100,
                    RequiredFaction = ""
                },
                new DialogData
                {
                    DialogId = "trader_trade",
                    SpeakerId = "trader",
                    Text = "Here's what I have available.",
                    Options = new List<DialogOption>
                    {
                        new DialogOption
                        {
                            OptionId = "opt_back",
                            Text = "Actually, let me think about it.",
                            Action = DialogAction.Back,
                            NextDialogId = "trader_greeting"
                        }
                    },
                    RequiredRelationship = -100,
                    RequiredFaction = ""
                },
                new DialogData
                {
                    DialogId = "trader_info",
                    SpeakerId = "trader",
                    Text = "This area is relatively safe, but watch out for bandits on the roads.",
                    Options = new List<DialogOption>
                    {
                        new DialogOption
                        {
                            OptionId = "opt_more",
                            Text = "Tell me more about the bandits.",
                            Action = DialogAction.Info,
                            NextDialogId = "trader_bandits"
                        },
                        new DialogOption
                        {
                            OptionId = "opt_back",
                            Text = "Thanks for the info.",
                            Action = DialogAction.Back,
                            NextDialogId = "trader_greeting"
                        }
                    },
                    RequiredRelationship = -100,
                    RequiredFaction = ""
                }
            };

            // Example: Guard dialog
            npcDialogs["guard_generic"] = new List<DialogData>
            {
                new DialogData
                {
                    DialogId = "guard_greeting",
                    SpeakerId = "guard",
                    Text = "Halt! State your business.",
                    Options = new List<DialogOption>
                    {
                        new DialogOption
                        {
                            OptionId = "opt_pass",
                            Text = "Just passing through.",
                            Action = DialogAction.Info,
                            NextDialogId = "guard_pass"
                        },
                        new DialogOption
                        {
                            OptionId = "opt_recruit",
                            Text = "I'm looking to join your faction.",
                            Action = DialogAction.Recruit,
                            NextDialogId = "guard_recruit",
                            RequiredRelationship = 0
                        },
                        new DialogOption
                        {
                            OptionId = "opt_leave",
                            Text = "My apologies, I'll leave.",
                            Action = DialogAction.EndDialog,
                            NextDialogId = ""
                        }
                    },
                    RequiredRelationship = -50,
                    RequiredFaction = ""
                }
            };
        }

        /// <summary>
        /// Start a dialog between player and NPC
        /// </summary>
        public bool StartDialog(string playerId, string npcId, string npcType)
        {
            if (!npcDialogs.TryGetValue(npcType, out List<DialogData>? dialogs) || dialogs.Count == 0)
            {
                Console.WriteLine($"No dialogs found for NPC type: {npcType}");
                return false;
            }

            // Get first dialog in tree
            var initialDialog = dialogs.FirstOrDefault();
            if (initialDialog == null)
                return false;

            // Create unique dialog instance
            var dialogInstance = CloneDialog(initialDialog);
            dialogInstance.DialogId = $"{npcId}_{playerId}_{DateTimeOffset.UtcNow.ToUnixTimeMilliseconds()}";

            // Store active dialog
            activeDialogs[$"{playerId}_{npcId}"] = dialogInstance;

            Console.WriteLine($"Started dialog between {playerId} and {npcId}");
            NotifyDialogUpdate(playerId, dialogInstance, "started");

            return true;
        }

        /// <summary>
        /// Select a dialog option
        /// </summary>
        public bool SelectOption(string playerId, string npcId, string optionId)
        {
            string dialogKey = $"{playerId}_{npcId}";

            if (!activeDialogs.TryGetValue(dialogKey, out DialogData? currentDialog))
            {
                Console.WriteLine($"No active dialog for {dialogKey}");
                return false;
            }

            // Find selected option
            var selectedOption = currentDialog.Options.FirstOrDefault(o => o.OptionId == optionId);
            if (selectedOption == null)
            {
                Console.WriteLine($"Invalid option: {optionId}");
                return false;
            }

            // Handle action
            HandleDialogAction(playerId, npcId, selectedOption);

            // Navigate to next dialog
            if (!string.IsNullOrEmpty(selectedOption.NextDialogId))
            {
                // Find next dialog from NPC's dialog tree
                var npcType = GetNPCType(npcId);
                if (npcDialogs.TryGetValue(npcType, out List<DialogData>? dialogs))
                {
                    var nextDialog = dialogs.FirstOrDefault(d => d.DialogId == selectedOption.NextDialogId);
                    if (nextDialog != null)
                    {
                        var nextInstance = CloneDialog(nextDialog);
                        nextInstance.DialogId = $"{npcId}_{playerId}_{DateTimeOffset.UtcNow.ToUnixTimeMilliseconds()}";
                        activeDialogs[dialogKey] = nextInstance;

                        NotifyDialogUpdate(playerId, nextInstance, "updated");
                        return true;
                    }
                }
            }

            // End dialog if no next dialog
            EndDialog(playerId, npcId);
            return true;
        }

        /// <summary>
        /// Handle dialog action
        /// </summary>
        private void HandleDialogAction(string playerId, string npcId, DialogOption option)
        {
            switch (option.Action)
            {
                case DialogAction.Trade:
                    Console.WriteLine($"Opening trade window for {playerId} with {npcId}");
                    // This would integrate with TradeManager
                    NotifyAction(playerId, "trade", npcId);
                    break;

                case DialogAction.Recruit:
                    Console.WriteLine($"Attempting recruitment for {playerId}");
                    // This would integrate with FactionSystem
                    NotifyAction(playerId, "recruit", npcId);
                    break;

                case DialogAction.Quest:
                    Console.WriteLine($"Offering quest to {playerId}");
                    // This would integrate with QuestManager
                    NotifyAction(playerId, "quest", npcId);
                    break;

                case DialogAction.Attack:
                    Console.WriteLine($"{playerId} attacked {npcId} during dialog");
                    NotifyAction(playerId, "attack", npcId);
                    break;

                case DialogAction.Give:
                    Console.WriteLine($"{playerId} giving item to {npcId}");
                    NotifyAction(playerId, "give", npcId);
                    break;

                case DialogAction.EndDialog:
                case DialogAction.Back:
                case DialogAction.Info:
                default:
                    // No special action needed
                    break;
            }
        }

        /// <summary>
        /// End a dialog
        /// </summary>
        public bool EndDialog(string playerId, string npcId)
        {
            string dialogKey = $"{playerId}_{npcId}";

            if (activeDialogs.Remove(dialogKey))
            {
                Console.WriteLine($"Ended dialog between {playerId} and {npcId}");
                NotifyDialogUpdate(playerId, null, "ended");
                return true;
            }

            return false;
        }

        /// <summary>
        /// Get current dialog for player and NPC
        /// </summary>
        public DialogData? GetActiveDialog(string playerId, string npcId)
        {
            string dialogKey = $"{playerId}_{npcId}";
            return activeDialogs.TryGetValue(dialogKey, out DialogData? dialog) ? dialog : null;
        }

        /// <summary>
        /// Register custom NPC dialog tree
        /// </summary>
        public void RegisterNPCDialogs(string npcType, List<DialogData> dialogs)
        {
            npcDialogs[npcType] = dialogs;
            Console.WriteLine($"Registered {dialogs.Count} dialogs for NPC type: {npcType}");
        }

        /// <summary>
        /// Get NPC type from ID (would integrate with actual NPC system)
        /// </summary>
        private string GetNPCType(string npcId)
        {
            // This is a placeholder - would query actual NPC data
            if (npcId.Contains("trader"))
                return "trader_generic";
            if (npcId.Contains("guard"))
                return "guard_generic";

            return "generic";
        }

        /// <summary>
        /// Clone dialog for instance
        /// </summary>
        private DialogData CloneDialog(DialogData template)
        {
            return new DialogData
            {
                DialogId = template.DialogId,
                SpeakerId = template.SpeakerId,
                Text = template.Text,
                Options = template.Options.Select(o => new DialogOption
                {
                    OptionId = o.OptionId,
                    Text = o.Text,
                    Action = o.Action,
                    NextDialogId = o.NextDialogId,
                    RequiredRelationship = o.RequiredRelationship,
                    RequiredItem = o.RequiredItem
                }).ToList(),
                RequiredRelationship = template.RequiredRelationship,
                RequiredFaction = template.RequiredFaction,
                RequiredItem = template.RequiredItem
            };
        }

        /// <summary>
        /// Notify player of dialog updates
        /// </summary>
        private void NotifyDialogUpdate(string playerId, DialogData? dialog, string updateType)
        {
            if (networkManager == null)
                return;

            var data = new Dictionary<string, object>
            {
                { "updateType", updateType }
            };

            if (dialog != null)
            {
                data["dialogId"] = dialog.DialogId;
                data["text"] = dialog.Text;
                data["options"] = dialog.Options;
                data["speakerId"] = dialog.SpeakerId;
            }

            var message = new GameMessage
            {
                Type = "dialog",
                SenderId = "system",
                TargetId = playerId,
                Data = data
            };

            try
            {
                networkManager.SendToPlayer(playerId, message);
            }
            catch (Exception ex)
            {
                Console.WriteLine($"Failed to send dialog notification: {ex.Message}");
            }
        }

        /// <summary>
        /// Notify player of dialog action
        /// </summary>
        private void NotifyAction(string playerId, string action, string npcId)
        {
            if (networkManager == null)
                return;

            var message = new GameMessage
            {
                Type = "dialog_action",
                SenderId = "system",
                TargetId = playerId,
                Data = new Dictionary<string, object>
                {
                    { "action", action },
                    { "npcId", npcId }
                }
            };

            try
            {
                networkManager.SendToPlayer(playerId, message);
            }
            catch (Exception ex)
            {
                Console.WriteLine($"Failed to send dialog action: {ex.Message}");
            }
        }

        /// <summary>
        /// Get dialog statistics
        /// </summary>
        public DialogStatistics GetStatistics()
        {
            return new DialogStatistics
            {
                ActiveDialogs = activeDialogs.Count,
                RegisteredNPCTypes = npcDialogs.Count
            };
        }
    }

    /// <summary>
    /// Dialog statistics
    /// </summary>
    public class DialogStatistics
    {
        public int ActiveDialogs { get; set; }
        public int RegisteredNPCTypes { get; set; }
    }
}
