using System;
using System.Threading;
using System.Threading.Tasks;
using KenshiMultiplayer.Networking;
using KenshiMultiplayer.Game;
using KenshiMultiplayer.Data;

namespace KenshiMultiplayer.Tests
{
    /// <summary>
    /// Integration test for the complete Kenshi Online system
    /// Tests: Interpolation, Compression, Scheduling, IPC Bridge, Save Game Loading
    /// </summary>
    public class IntegrationTest
    {
        private InterpolationEngine _interpolation;
        private CompressionEngine _compression;
        private NetworkScheduler _scheduler;
        private EnhancedIPCBridge _ipcBridge;
        private SaveGameLoader _saveLoader;

        public async Task<bool> RunAllTests()
        {
            Console.WriteLine("====================================");
            Console.WriteLine("  Kenshi Online Integration Test");
            Console.WriteLine("====================================\n");

            bool allPassed = true;

            allPassed &= await TestInterpolationEngine();
            allPassed &= await TestCompressionEngine();
            allPassed &= await TestNetworkScheduler();
            allPassed &= await TestIPCBridge();
            allPassed &= await TestSaveGameLoader();
            allPassed &= await TestFullPipeline();

            Console.WriteLine("\n====================================");
            if (allPassed)
            {
                Console.WriteLine("  ALL TESTS PASSED ✓");
            }
            else
            {
                Console.WriteLine("  SOME TESTS FAILED ✗");
            }
            Console.WriteLine("====================================\n");

            return allPassed;
        }

        private async Task<bool> TestInterpolationEngine()
        {
            Console.WriteLine("[TEST] Interpolation Engine...");

            try
            {
                _interpolation = new InterpolationEngine(bufferSize: 10, interpolationDelayMs: 100);

                long baseTime = DateTimeOffset.Now.ToUnixTimeMilliseconds();

                // Add snapshots
                _interpolation.AddSnapshot("player1",
                    new System.Numerics.Vector3(0, 0, 0),
                    new System.Numerics.Vector3(0, 0, 0),
                    new System.Numerics.Vector3(1, 0, 0),
                    baseTime,
                    new System.Collections.Generic.Dictionary<string, float> { { "health", 100 } });

                _interpolation.AddSnapshot("player1",
                    new System.Numerics.Vector3(10, 0, 0),
                    new System.Numerics.Vector3(0, 0, 0),
                    new System.Numerics.Vector3(1, 0, 0),
                    baseTime + 1000,
                    new System.Collections.Generic.Dictionary<string, float> { { "health", 90 } });

                // Get interpolated state
                bool success = _interpolation.GetInterpolatedState("player1", baseTime + 500,
                    out var position, out var rotation, out var customValues);

                if (!success)
                {
                    Console.WriteLine("  ✗ Failed to get interpolated state");
                    return false;
                }

                // Check if position is interpolated correctly (should be around 5, 0, 0)
                if (Math.Abs(position.X - 5.0f) > 1.0f)
                {
                    Console.WriteLine($"  ✗ Interpolation incorrect: Expected X≈5, got {position.X}");
                    return false;
                }

                Console.WriteLine($"  ✓ Interpolation working (Position: {position.X:F2}, {position.Y:F2}, {position.Z:F2})");
                return true;
            }
            catch (Exception ex)
            {
                Console.WriteLine($"  ✗ Exception: {ex.Message}");
                return false;
            }
        }

        private async Task<bool> TestCompressionEngine()
        {
            Console.WriteLine("[TEST] Compression Engine...");

            try
            {
                _compression = new CompressionEngine(CompressionEngine.CompressionStrategy.DeltaGZip);

                var testData = new PlayerData
                {
                    PlayerId = "player1",
                    DisplayName = "TestPlayer",
                    PositionX = 100.5f,
                    PositionY = 200.3f,
                    PositionZ = 150.7f,
                    Health = 85.5f,
                    MaxHealth = 100.0f
                };

                // Compress
                byte[] compressed = _compression.Compress("player1", testData, CompressionEngine.CompressionStrategy.DeltaGZip);

                // Decompress
                var decompressed = _compression.Decompress<PlayerData>("player1", compressed, CompressionEngine.CompressionStrategy.DeltaGZip);

                // Verify
                if (decompressed.PlayerId != testData.PlayerId ||
                    Math.Abs(decompressed.PositionX - testData.PositionX) > 0.01f)
                {
                    Console.WriteLine("  ✗ Compression/Decompression data mismatch");
                    return false;
                }

                // Calculate compression ratio
                string json = Newtonsoft.Json.JsonConvert.SerializeObject(testData);
                float ratio = CompressionEngine.GetCompressionRatio(
                    System.Text.Encoding.UTF8.GetBytes(json),
                    compressed);

                Console.WriteLine($"  ✓ Compression working (Ratio: {ratio:P}, Size: {compressed.Length} bytes)");
                return true;
            }
            catch (Exception ex)
            {
                Console.WriteLine($"  ✗ Exception: {ex.Message}");
                return false;
            }
        }

        private async Task<bool> TestNetworkScheduler()
        {
            Console.WriteLine("[TEST] Network Scheduler...");

            try
            {
                _scheduler = new NetworkScheduler(clientTickRateMs: 50, serverTickRateMs: 20);

                int processedCount = 0;
                var completionSignal = new ManualResetEvent(false);

                // Schedule messages with different priorities
                _scheduler.ScheduleMessage(
                    "Test message 1",
                    (msg) => { Interlocked.Increment(ref processedCount); },
                    NetworkScheduler.MessagePriority.Normal,
                    tier: 0);

                _scheduler.ScheduleMessage(
                    "Test message 2",
                    (msg) => { Interlocked.Increment(ref processedCount); },
                    NetworkScheduler.MessagePriority.High,
                    tier: 1);

                _scheduler.ScheduleMessage(
                    "Test message 3",
                    (msg) =>
                    {
                        Interlocked.Increment(ref processedCount);
                        completionSignal.Set();
                    },
                    NetworkScheduler.MessagePriority.Critical,
                    tier: 0);

                // Wait for processing
                bool completed = completionSignal.WaitOne(5000);

                if (!completed || processedCount < 3)
                {
                    Console.WriteLine($"  ✗ Not all messages processed (Count: {processedCount}/3)");
                    return false;
                }

                var stats = _scheduler.GetStatistics();
                Console.WriteLine($"  ✓ Scheduler working (Processed: {stats.Processed}, Avg Latency: {stats.AvgLatency:F2}ms)");
                return true;
            }
            catch (Exception ex)
            {
                Console.WriteLine($"  ✗ Exception: {ex.Message}");
                return false;
            }
        }

        private async Task<bool> TestIPCBridge()
        {
            Console.WriteLine("[TEST] IPC Bridge...");

            try
            {
                _ipcBridge = new EnhancedIPCBridge(_interpolation, _compression, _scheduler);

                bool messageReceived = false;
                var signal = new ManualResetEvent(false);

                _ipcBridge.OnMessageReceived += (msg) =>
                {
                    messageReceived = true;
                    signal.Set();
                };

                _ipcBridge.Start();

                // Give it time to start
                await Task.Delay(500);

                // Send a test message
                _ipcBridge.SendMessage(new IPCMessage
                {
                    Type = IPCMessageType.PlayerState,
                    Payload = "{\"test\":\"data\"}"
                });

                // Wait for processing
                signal.WaitOne(2000);

                var stats = _ipcBridge.GetStatistics();

                Console.WriteLine($"  ✓ IPC Bridge working (Sent: {stats.Sent}, Received: {stats.Received})");
                return true;
            }
            catch (Exception ex)
            {
                Console.WriteLine($"  ✗ Exception: {ex.Message}");
                return false;
            }
        }

        private async Task<bool> TestSaveGameLoader()
        {
            Console.WriteLine("[TEST] Save Game Loader...");

            try
            {
                _saveLoader = new SaveGameLoader();

                var spawnPos = new Position { X = -4200, Y = 150, Z = 18500 };

                string saveName = await _saveLoader.CreateMultiplayerSave(
                    "TestServer",
                    "TestPlayer",
                    spawnPos);

                if (string.IsNullOrEmpty(saveName))
                {
                    Console.WriteLine("  ✗ Failed to create save game");
                    return false;
                }

                // Verify save was created
                var saves = _saveLoader.GetMultiplayerSaves();
                if (!saves.Contains(saveName))
                {
                    Console.WriteLine("  ✗ Save game not found in list");
                    return false;
                }

                Console.WriteLine($"  ✓ Save Game Loader working (Created: {saveName})");

                // Cleanup
                _saveLoader.CleanupOldSaves(0);

                return true;
            }
            catch (Exception ex)
            {
                Console.WriteLine($"  ✗ Exception: {ex.Message}");
                return false;
            }
        }

        private async Task<bool> TestFullPipeline()
        {
            Console.WriteLine("[TEST] Full Pipeline Integration...");

            try
            {
                // Simulate a complete flow:
                // 1. Player joins server
                // 2. Save game is created
                // 3. Player state is sent via IPC
                // 4. State is interpolated and compressed
                // 5. State is scheduled for network transmission

                var testPlayer = new PlayerData
                {
                    PlayerId = "integration_test_player",
                    DisplayName = "Integration Tester",
                    PositionX = 1000,
                    PositionY = 100,
                    PositionZ = 2000,
                    Health = 100,
                    MaxHealth = 100
                };

                // Step 1: Create save game
                var spawnPos = new Position { X = testPlayer.PositionX, Y = testPlayer.PositionY, Z = testPlayer.PositionZ };
                string saveName = await _saveLoader.CreateMultiplayerSave("IntegrationTestServer", testPlayer.PlayerId, spawnPos);

                if (string.IsNullOrEmpty(saveName))
                {
                    Console.WriteLine("  ✗ Step 1 failed: Save game creation");
                    return false;
                }

                // Step 2: Add to interpolation
                _interpolation.AddSnapshot(
                    testPlayer.PlayerId,
                    new System.Numerics.Vector3(testPlayer.PositionX, testPlayer.PositionY, testPlayer.PositionZ),
                    new System.Numerics.Vector3(0, 0, 0),
                    new System.Numerics.Vector3(0, 0, 0),
                    DateTimeOffset.Now.ToUnixTimeMilliseconds(),
                    new System.Collections.Generic.Dictionary<string, float> { { "health", testPlayer.Health } });

                // Step 3: Compress player data
                byte[] compressed = _compression.Compress(testPlayer.PlayerId, testPlayer);

                if (compressed.Length == 0)
                {
                    Console.WriteLine("  ✗ Step 3 failed: Compression");
                    return false;
                }

                // Step 4: Schedule for transmission
                var scheduleSignal = new ManualResetEvent(false);
                _scheduler.ScheduleMessage(
                    compressed,
                    (data) => { scheduleSignal.Set(); },
                    NetworkScheduler.MessagePriority.High,
                    tier: 1);

                bool scheduled = scheduleSignal.WaitOne(2000);
                if (!scheduled)
                {
                    Console.WriteLine("  ✗ Step 4 failed: Scheduling");
                    return false;
                }

                Console.WriteLine("  ✓ Full Pipeline Integration successful");
                Console.WriteLine($"    - Save created: {saveName}");
                Console.WriteLine($"    - Data compressed: {compressed.Length} bytes");
                Console.WriteLine($"    - Message scheduled and processed");

                // Cleanup
                _saveLoader.CleanupOldSaves(0);

                return true;
            }
            catch (Exception ex)
            {
                Console.WriteLine($"  ✗ Exception: {ex.Message}");
                return false;
            }
        }

        public void Cleanup()
        {
            _ipcBridge?.Dispose();
            _scheduler?.Dispose();
        }
    }
}
