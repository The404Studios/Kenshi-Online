using System;
using System.Collections.Generic;
using KenshiMultiplayer.Data;
using KenshiMultiplayer.Game;
using KenshiMultiplayer.Networking;
using KenshiMultiplayer.Utility;

namespace KenshiOnline.Coordinates.Integration
{
    /// <summary>
    /// Main Orchestrator - Ensures all components are properly wired together.
    ///
    /// This is the central coordination point that verifies:
    /// 1. Ring architecture is properly initialized
    /// 2. Memory actuator is connected to game bridge
    /// 3. Network broadcaster is connected to server/client
    /// 4. DataBus pipeline is active
    /// 5. All rings are cycling properly
    ///
    /// Usage:
    ///   var orchestrator = new Orchestrator();
    ///   orchestrator.Initialize(gameBridge);
    ///   var status = orchestrator.VerifyConnections();
    ///   orchestrator.Start();
    /// </summary>
    public class Orchestrator : IDisposable
    {
        private const string LOG_PREFIX = "[Orchestrator] ";

        // Core components
        private KenshiGameBridge _gameBridge;
        private RingCoordinator _coordinator;
        private KenshiMemoryActuator _memoryActuator;
        private NetworkBroadcaster _broadcaster;
        private StateSynchronizer _stateSynchronizer;

        // State
        private bool _isInitialized;
        private bool _isRunning;

        public bool IsInitialized => _isInitialized;
        public bool IsRunning => _isRunning;
        public RingCoordinator Coordinator => _coordinator;

        /// <summary>
        /// Initialize the orchestrator with game bridge.
        /// </summary>
        public bool Initialize(KenshiGameBridge gameBridge, StateSynchronizer stateSynchronizer = null)
        {
            if (_isInitialized)
            {
                Logger.Log(LOG_PREFIX + "Already initialized");
                return true;
            }

            try
            {
                _gameBridge = gameBridge ?? throw new ArgumentNullException(nameof(gameBridge));
                _stateSynchronizer = stateSynchronizer;

                // Create ring coordinator with optimized config
                var config = new CoordinatorConfig
                {
                    TickRateHz = 20,
                    MaxInfosPerCycle = 1000,
                    AcceptThreshold = 0.8f,
                    RejectThreshold = 0.2f,
                    VerificationThreshold = 0.5f,
                    GateConfig = new GateConfig
                    {
                        MaxVelocity = 15f,
                        MaxAcceleration = 30f,
                        BlendRate = 0.15f,
                        SnapThreshold = 5f,
                        AllowedHealthDelta = 0.5f
                    },
                    BusConfig = new BusConfig
                    {
                        MaxQueuedWrites = 10000,
                        EnableCoalescing = true,
                        EnableReadCache = true,
                        ReadCacheTtlTicks = 2
                    }
                };

                _coordinator = new RingCoordinator(config);

                // Create memory actuator
                _memoryActuator = new KenshiMemoryActuator(gameBridge);
                _coordinator.SetMemoryActuator(_memoryActuator);

                // Create network broadcaster
                _broadcaster = new NetworkBroadcaster(_coordinator, stateSynchronizer);

                _isInitialized = true;
                Logger.Log(LOG_PREFIX + "Initialized successfully");
                return true;
            }
            catch (Exception ex)
            {
                Logger.Log(LOG_PREFIX + $"ERROR initializing: {ex.Message}");
                return false;
            }
        }

        /// <summary>
        /// Verify all connections are properly established.
        /// </summary>
        public OrchestratorStatus VerifyConnections()
        {
            var status = new OrchestratorStatus();

            // Check game bridge
            status.GameBridgeConnected = _gameBridge?.IsConnected ?? false;

            // Check coordinator
            status.CoordinatorInitialized = _coordinator != null;

            // Check rings
            if (_coordinator != null)
            {
                status.ContainerRingActive = _coordinator.ContainerRing != null;
                status.InfoRingActive = _coordinator.InfoRing != null;
                status.AuthorityRingActive = _coordinator.AuthorityRing != null;
                status.AttributeRingActive = _coordinator.AttributeRing != null;
                status.DataBusActive = _coordinator.DataBus != null;
            }

            // Check memory actuator
            status.MemoryActuatorConnected = _memoryActuator != null;

            // Check broadcaster
            status.NetworkBroadcasterConnected = _broadcaster != null;

            // Compute overall status
            status.AllSystemsGo = status.CoordinatorInitialized &&
                                  status.ContainerRingActive &&
                                  status.InfoRingActive &&
                                  status.AuthorityRingActive &&
                                  status.AttributeRingActive &&
                                  status.DataBusActive &&
                                  status.MemoryActuatorConnected &&
                                  status.NetworkBroadcasterConnected;

            return status;
        }

        /// <summary>
        /// Start the orchestrator.
        /// </summary>
        public bool Start()
        {
            if (!_isInitialized)
            {
                Logger.Log(LOG_PREFIX + "ERROR: Not initialized");
                return false;
            }

            if (_isRunning)
            {
                Logger.Log(LOG_PREFIX + "Already running");
                return true;
            }

            try
            {
                // Verify connections first
                var status = VerifyConnections();
                if (!status.AllSystemsGo)
                {
                    Logger.Log(LOG_PREFIX + "WARNING: Not all systems ready");
                    LogStatus(status);
                }

                // Start coordinator
                _coordinator.Start();

                // Start broadcaster
                _broadcaster.Start();

                _isRunning = true;
                Logger.Log(LOG_PREFIX + "Started");
                return true;
            }
            catch (Exception ex)
            {
                Logger.Log(LOG_PREFIX + $"ERROR starting: {ex.Message}");
                return false;
            }
        }

        /// <summary>
        /// Stop the orchestrator.
        /// </summary>
        public void Stop()
        {
            if (!_isRunning)
                return;

            _broadcaster?.Stop();
            _coordinator?.Stop();

            _isRunning = false;
            Logger.Log(LOG_PREFIX + "Stopped");
        }

        /// <summary>
        /// Set network callbacks for broadcasting.
        /// </summary>
        public void SetNetworkCallbacks(Action<string, byte[]> sendToClient, Action<byte[]> broadcastToAll)
        {
            _broadcaster?.SetNetworkCallbacks(sendToClient, broadcastToAll);
        }

        /// <summary>
        /// Process an inbound network frame.
        /// </summary>
        public void ProcessInboundFrame(byte[] data, string sourceClientId)
        {
            _broadcaster?.ProcessInboundFrame(data, sourceClientId);
        }

        private void LogStatus(OrchestratorStatus status)
        {
            Logger.Log(LOG_PREFIX + "Connection Status:");
            Logger.Log(LOG_PREFIX + $"  Game Bridge: {(status.GameBridgeConnected ? "OK" : "DISCONNECTED")}");
            Logger.Log(LOG_PREFIX + $"  Coordinator: {(status.CoordinatorInitialized ? "OK" : "NOT INITIALIZED")}");
            Logger.Log(LOG_PREFIX + $"  Container Ring: {(status.ContainerRingActive ? "OK" : "INACTIVE")}");
            Logger.Log(LOG_PREFIX + $"  Info Ring: {(status.InfoRingActive ? "OK" : "INACTIVE")}");
            Logger.Log(LOG_PREFIX + $"  Authority Ring: {(status.AuthorityRingActive ? "OK" : "INACTIVE")}");
            Logger.Log(LOG_PREFIX + $"  Attribute Ring: {(status.AttributeRingActive ? "OK" : "INACTIVE")}");
            Logger.Log(LOG_PREFIX + $"  DataBus: {(status.DataBusActive ? "OK" : "INACTIVE")}");
            Logger.Log(LOG_PREFIX + $"  Memory Actuator: {(status.MemoryActuatorConnected ? "OK" : "DISCONNECTED")}");
            Logger.Log(LOG_PREFIX + $"  Network Broadcaster: {(status.NetworkBroadcasterConnected ? "OK" : "DISCONNECTED")}");
        }

        public void Dispose()
        {
            Stop();
            _broadcaster?.Dispose();
            _coordinator?.Dispose();
            Logger.Log(LOG_PREFIX + "Disposed");
        }
    }

    /// <summary>
    /// Status of all orchestrator connections.
    /// </summary>
    public struct OrchestratorStatus
    {
        public bool GameBridgeConnected;
        public bool CoordinatorInitialized;
        public bool ContainerRingActive;
        public bool InfoRingActive;
        public bool AuthorityRingActive;
        public bool AttributeRingActive;
        public bool DataBusActive;
        public bool MemoryActuatorConnected;
        public bool NetworkBroadcasterConnected;
        public bool AllSystemsGo;

        public override string ToString()
        {
            return AllSystemsGo
                ? "All Systems Go"
                : $"Issues: {GetIssues()}";
        }

        private string GetIssues()
        {
            var issues = new List<string>();
            if (!GameBridgeConnected) issues.Add("GameBridge");
            if (!CoordinatorInitialized) issues.Add("Coordinator");
            if (!ContainerRingActive) issues.Add("ContainerRing");
            if (!InfoRingActive) issues.Add("InfoRing");
            if (!AuthorityRingActive) issues.Add("AuthorityRing");
            if (!AttributeRingActive) issues.Add("AttributeRing");
            if (!DataBusActive) issues.Add("DataBus");
            if (!MemoryActuatorConnected) issues.Add("MemoryActuator");
            if (!NetworkBroadcasterConnected) issues.Add("NetworkBroadcaster");
            return string.Join(", ", issues);
        }
    }
}
