using System;
using System.Diagnostics;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using MemorySharp;
using KenshiMultiplayer;

namespace KenshiMultiplayer
{
    /// <summary>
    /// Handles the integration between Kenshi's memory and the multiplayer system
    /// </summary>
    public class KenshiMemoryIntegration : IDisposable
    {
        private MemorySharp memory;
        private Process kenshiProcess;
        private EnhancedClient networkClient;
        private CancellationTokenSource cancellationToken;
        private Task syncTask;
        
        // Base addresses for key Kenshi structures (these will need to be found through reverse engineering)
        private IntPtr playerCharacterPtr = IntPtr.Zero;
        private IntPtr worldStatePtr = IntPtr.Zero;
        
        // Offsets for Character class properties (from the Character.h file)
        private readonly int CHARACTER_POS_X_OFFSET = 0x3C8; // Example - must be determined
        private readonly int CHARACTER_POS_Y_OFFSET = 0x3CC; // Example - must be determined
        private readonly int CHARACTER_POS_Z_OFFSET = 0x3D0; // Example - must be determined
        private readonly int CHARACTER_ROT_Z_OFFSET = 0x3D8; // Example - must be determined
        private readonly int CHARACTER_HEALTH_OFFSET = 0x458; // Based on medical system offset
        
        // Sync frequency settings
        private readonly int POSITION_SYNC_MS = 100;  // 10 times per second
        private readonly int INVENTORY_SYNC_MS = 1000; // Once per second
        private readonly int HEALTH_SYNC_MS = 500;    // Twice per second
        
        // Sync state tracking
        private DateTime lastPositionSync = DateTime.MinValue;
        private DateTime lastInventorySync = DateTime.MinValue; 
        private DateTime lastHealthSync = DateTime.MinValue;
        private Position lastSyncedPosition = new Position();
        private int lastSyncedHealth = -1;
        
        public KenshiMemoryIntegration(EnhancedClient client)
        {
            networkClient = client;
        }
        
        public bool ConnectToKenshi()
        {
            try
            {
                var processes = Process.GetProcessesByName("kenshi_x64");
                if (processes.Length == 0)
                {
                    processes = Process.GetProcessesByName("kenshi");
                }
                
                if (processes.Length == 0)
                {
                    Console.WriteLine("Kenshi process not found. Is the game running?");
                    return false;
                }
                
                kenshiProcess = processes[0];
                memory = new MemorySharp(kenshiProcess);
                
                // Find the base pointers for important structures
                FindBasePointers();
                
                if (playerCharacterPtr == IntPtr.Zero)
                {
                    Console.WriteLine("Failed to find player character pointer");
                    return false;
                }
                
                Console.WriteLine("Successfully connected to Kenshi process");
                
                // Start the sync task
                cancellationToken = new CancellationTokenSource();
                syncTask = Task.Run(() => SyncLoop(cancellationToken.Token), cancellationToken.Token);
                
                return true;
            }
            catch (Exception ex)
            {
                Console.WriteLine($"Error connecting to Kenshi: {ex.Message}");
                return false;
            }
        }
        
        private void FindBasePointers()
        {
            // This is a placeholder for the actual pointer finding logic
            // In a real implementation, you would use signature scanning or known offsets
            
            // Example pattern scanning for finding the player character pointer
            // playerCharacterPtr = memory.PatternScan("48 8B 05 ? ? ? ? 48 8B 48 ? 48 85 C9 74 ? 48 8B 01");
            
            // For now, we'll use a placeholder address for testing
            playerCharacterPtr = IntPtr.Zero; // This needs to be populated with actual value
            
            // TODO: Implement proper memory scanning to find key pointers
        }
        
        private async void SyncLoop(CancellationToken token)
        {
            while (!token.IsCancellationRequested)
            {
                try
                {
                    // Check if Kenshi is still running
                    if (kenshiProcess.HasExited)
                    {
                        Console.WriteLine("Kenshi process has exited. Stopping sync.");
                        break;
                    }
                    
                    var now = DateTime.Now;
                    
                    // Sync position if needed
                    if ((now - lastPositionSync).TotalMilliseconds >= POSITION_SYNC_MS)
                    {
                        SyncPosition();
                        lastPositionSync = now;
                    }
                    
                    // Sync health if needed
                    if ((now - lastHealthSync).TotalMilliseconds >= HEALTH_SYNC_MS)
                    {
                        SyncHealth();
                        lastHealthSync = now;
                    }
                    
                    // Sync inventory if needed
                    if ((now - lastInventorySync).TotalMilliseconds >= INVENTORY_SYNC_MS)
                    {
                        SyncInventory();
                        lastInventorySync = now;
                    }
                    
                    // Small delay to prevent high CPU usage
                    await Task.Delay(20, token);
                }
                catch (Exception ex)
                {
                    Console.WriteLine($"Error in sync loop: {ex.Message}");
                    await Task.Delay(1000, token); // Longer delay on error
                }
            }
        }
        
        private void SyncPosition()
        {
            if (playerCharacterPtr == IntPtr.Zero) return;
            
            try
            {
                // Read position from memory
                float x = memory.Read<float>(playerCharacterPtr + CHARACTER_POS_X_OFFSET);
                float y = memory.Read<float>(playerCharacterPtr + CHARACTER_POS_Y_OFFSET);
                float z = memory.Read<float>(playerCharacterPtr + CHARACTER_POS_Z_OFFSET);
                float rotation = memory.Read<float>(playerCharacterPtr + CHARACTER_ROT_Z_OFFSET);
                
                var position = new Position(x, y, z, 0, 0, rotation);
                
                // Only sync if position has changed significantly
                if (position.HasChangedSignificantly(lastSyncedPosition))
                {
                    networkClient.UpdatePosition(position.X, position.Y);
                    lastSyncedPosition = position;
                }
            }
            catch (Exception ex)
            {
                Console.WriteLine($"Error syncing position: {ex.Message}");
            }
        }
        
        private void SyncHealth()
        {
            if (playerCharacterPtr == IntPtr.Zero) return;
            
            try
            {
                // Find the medical system pointer
                IntPtr medicalSystemPtr = memory.Read<IntPtr>(playerCharacterPtr + CHARACTER_HEALTH_OFFSET);
                if (medicalSystemPtr == IntPtr.Zero) return;
                
                // Read current and max health
                int currentHealth = ReadCharacterHealth(medicalSystemPtr);
                int maxHealth = ReadCharacterMaxHealth(medicalSystemPtr);
                
                // Only sync if health has changed
                if (currentHealth != lastSyncedHealth)
                {
                    networkClient.UpdateHealth(currentHealth, maxHealth);
                    lastSyncedHealth = currentHealth;
                }
            }
            catch (Exception ex)
            {
                Console.WriteLine($"Error syncing health: {ex.Message}");
            }
        }
        
        private int ReadCharacterHealth(IntPtr medicalSystemPtr)
        {
            // Placeholder - actual implementation would read from MedicalSystem class
            // Use the MedicalSystem structure from the headers to determine accurate offsets
            return 100; // Example value
        }
        
        private int ReadCharacterMaxHealth(IntPtr medicalSystemPtr)
        {
            // Placeholder - actual implementation would read from MedicalSystem class
            return 100; // Example value
        }
        
        private void SyncInventory()
        {
            if (playerCharacterPtr == IntPtr.Zero) return;
            
            try
            {
                // Read inventory pointer
                IntPtr inventoryPtr = memory.Read<IntPtr>(playerCharacterPtr + 0x2E8); // Based on inventory offset
                if (inventoryPtr == IntPtr.Zero) return;
                
                // Inventory sync is more complex and would require more detailed implementation
                // This is just a placeholder for the concept
            }
            catch (Exception ex)
            {
                Console.WriteLine($"Error syncing inventory: {ex.Message}");
            }
        }
        
        public void Dispose()
        {
            cancellationToken?.Cancel();
            try
            {
                syncTask?.Wait(1000);
            }
            catch { }
            
            memory?.Dispose();
        }
    }
}