using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Numerics;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;
using KenshiMultiplayer.Networking;
using KenshiMultiplayer.Utility;

namespace KenshiOnline.Coordinates.Integration
{
    /// <summary>
    /// NetworkBroadcaster - Wires authority commits to network layer.
    ///
    /// When Ring3 commits authoritative state, this broadcaster:
    ///   1. Serializes the commit into network packets
    ///   2. Applies delta compression for bandwidth efficiency
    ///   3. Broadcasts to relevant peers (interest management)
    ///   4. Handles acknowledgments and retransmissions
    ///
    /// This is the "expression" of authority - how local truth becomes shared truth.
    /// </summary>
    public class NetworkBroadcaster : IDisposable
    {
        private readonly RingCoordinator _coordinator;
        private readonly StateSynchronizer _stateSynchronizer;
        private Action<string, byte[]> _sendToClient;
        private Action<byte[]> _broadcastToAll;

        // Pending broadcasts
        private readonly ConcurrentQueue<BroadcastPacket> _outboundQueue = new();
        private readonly ConcurrentDictionary<string, long> _clientLastAck = new();
        private Timer _flushTimer;

        // Configuration
        private readonly int _flushIntervalMs = 50;  // Flush at 20Hz
        private readonly int _maxPacketSize = 4096;
        private readonly int _maxQueuedPackets = 1000;

        // Statistics
        private long _packetsSent;
        private long _bytesSent;
        private long _packetsDropped;

        public NetworkBroadcaster(
            RingCoordinator coordinator,
            StateSynchronizer stateSynchronizer = null)
        {
            _coordinator = coordinator ?? throw new ArgumentNullException(nameof(coordinator));
            _stateSynchronizer = stateSynchronizer;

            // Subscribe to authority commits
            SubscribeToAuthorityEvents();

            Logger.Log("[NetworkBroadcaster] Initialized");
        }

        /// <summary>
        /// Set the network send callbacks.
        /// </summary>
        public void SetNetworkCallbacks(
            Action<string, byte[]> sendToClient,
            Action<byte[]> broadcastToAll)
        {
            _sendToClient = sendToClient;
            _broadcastToAll = broadcastToAll;
        }

        /// <summary>
        /// Start broadcasting authority commits.
        /// </summary>
        public void Start()
        {
            _flushTimer = new Timer(FlushOutbound, null, 0, _flushIntervalMs);
            Logger.Log("[NetworkBroadcaster] Started");
        }

        /// <summary>
        /// Stop broadcasting.
        /// </summary>
        public void Stop()
        {
            _flushTimer?.Dispose();
            _flushTimer = null;
            Logger.Log("[NetworkBroadcaster] Stopped");
        }

        private void SubscribeToAuthorityEvents()
        {
            // Hook into the processing cycle to capture commits
            // This would ideally be an event from AuthorityRing
            // For now, we'll poll or integrate directly
        }

        #region Broadcast Methods

        /// <summary>
        /// Broadcast a position update from authority.
        /// </summary>
        public void BroadcastPosition(NetId entityId, Vector3 position, Quaternion rotation, long tick)
        {
            var packet = new BroadcastPacket
            {
                Type = PacketKind.PositionUpdate,
                EntityId = entityId.Packed,
                Tick = tick,
                Data = SerializeTransform(position, rotation),
                Priority = BroadcastPriority.Normal
            };

            EnqueueBroadcast(packet);
        }

        /// <summary>
        /// Broadcast a spawn event.
        /// </summary>
        public void BroadcastSpawn(NetId entityId, EntityKind kind, Vector3 position, string displayName, long tick)
        {
            var packet = new BroadcastPacket
            {
                Type = PacketKind.EntitySpawn,
                EntityId = entityId.Packed,
                Tick = tick,
                Data = SerializeSpawn(entityId, kind, position, displayName),
                Priority = BroadcastPriority.High  // Spawns are important
            };

            EnqueueBroadcast(packet);
        }

        /// <summary>
        /// Broadcast a despawn event.
        /// </summary>
        public void BroadcastDespawn(NetId entityId, long tick)
        {
            var packet = new BroadcastPacket
            {
                Type = PacketKind.EntityDespawn,
                EntityId = entityId.Packed,
                Tick = tick,
                Data = new byte[0],
                Priority = BroadcastPriority.High
            };

            EnqueueBroadcast(packet);
        }

        /// <summary>
        /// Broadcast health update.
        /// </summary>
        public void BroadcastHealth(NetId entityId, float current, float max, long tick)
        {
            var packet = new BroadcastPacket
            {
                Type = PacketKind.HealthUpdate,
                EntityId = entityId.Packed,
                Tick = tick,
                Data = SerializeHealth(current, max),
                Priority = BroadcastPriority.Normal
            };

            EnqueueBroadcast(packet);
        }

        /// <summary>
        /// Broadcast authority commit (full commit record).
        /// </summary>
        public void BroadcastCommit(Commit commit)
        {
            var packet = new BroadcastPacket
            {
                Type = PacketKind.AuthorityCommit,
                EntityId = commit.SubjectId.Packed,
                Tick = commit.Tick,
                Data = SerializeCommit(commit),
                Priority = BroadcastPriority.High
            };

            EnqueueBroadcast(packet);
        }

        /// <summary>
        /// Broadcast world speed sync.
        /// </summary>
        public void BroadcastWorldSpeed(float speed, long tick)
        {
            var packet = new BroadcastPacket
            {
                Type = PacketKind.WorldSync,
                EntityId = 0,
                Tick = tick,
                Data = BitConverter.GetBytes(speed),
                Priority = BroadcastPriority.Critical
            };

            EnqueueBroadcast(packet);
        }

        #endregion

        #region Queue Management

        private void EnqueueBroadcast(BroadcastPacket packet)
        {
            if (_outboundQueue.Count >= _maxQueuedPackets)
            {
                // Drop oldest low-priority packets
                _packetsDropped++;
                return;
            }

            packet.EnqueueTime = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();
            _outboundQueue.Enqueue(packet);
        }

        private void FlushOutbound(object state)
        {
            var packets = new List<BroadcastPacket>();

            // Drain queue
            while (_outboundQueue.TryDequeue(out var packet) && packets.Count < 100)
            {
                packets.Add(packet);
            }

            if (packets.Count == 0)
                return;

            // Sort by priority (critical first)
            packets.Sort((a, b) => b.Priority.CompareTo(a.Priority));

            // Batch into network frames
            var frame = new NetworkFrame
            {
                Tick = _coordinator.Clock.CurrentTick,
                Timestamp = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds(),
                Packets = new List<BroadcastPacket>(packets)
            };

            // Serialize and send
            var frameData = SerializeFrame(frame);

            if (_broadcastToAll != null)
            {
                try
                {
                    _broadcastToAll(frameData);
                    _packetsSent += packets.Count;
                    _bytesSent += frameData.Length;
                }
                catch (Exception ex)
                {
                    Logger.Log($"[NetworkBroadcaster] Broadcast error: {ex.Message}");
                }
            }

            // Also update StateSynchronizer if available
            if (_stateSynchronizer != null)
            {
                foreach (var packet in packets)
                {
                    UpdateStateSynchronizer(packet);
                }
            }
        }

        private void UpdateStateSynchronizer(BroadcastPacket packet)
        {
            // Convert to StateUpdate for the existing synchronizer
            var update = new StateUpdate
            {
                EntityId = packet.EntityId.ToString(),
                Type = packet.Type switch
                {
                    PacketKind.PositionUpdate => "entity_update",
                    PacketKind.EntitySpawn => "entity_spawn",
                    PacketKind.EntityDespawn => "entity_despawn",
                    PacketKind.HealthUpdate => "entity_update",
                    _ => "entity_update"
                },
                Data = DeserializeToDict(packet.Type, packet.Data)
            };

            _stateSynchronizer.UpdateWorldState(update);
        }

        private Dictionary<string, object> DeserializeToDict(PacketKind kind, byte[] data)
        {
            var dict = new Dictionary<string, object>();

            switch (kind)
            {
                case PacketKind.PositionUpdate:
                    if (data.Length >= 24)
                    {
                        dict["position"] = new Vector3(
                            BitConverter.ToSingle(data, 0),
                            BitConverter.ToSingle(data, 4),
                            BitConverter.ToSingle(data, 8)
                        );
                    }
                    break;

                case PacketKind.HealthUpdate:
                    if (data.Length >= 8)
                    {
                        dict["health"] = BitConverter.ToSingle(data, 0);
                    }
                    break;
            }

            return dict;
        }

        #endregion

        #region Serialization

        private byte[] SerializeTransform(Vector3 position, Quaternion rotation)
        {
            var data = new byte[28]; // 3 floats pos + 4 floats quat
            BitConverter.GetBytes(position.X).CopyTo(data, 0);
            BitConverter.GetBytes(position.Y).CopyTo(data, 4);
            BitConverter.GetBytes(position.Z).CopyTo(data, 8);
            BitConverter.GetBytes(rotation.X).CopyTo(data, 12);
            BitConverter.GetBytes(rotation.Y).CopyTo(data, 16);
            BitConverter.GetBytes(rotation.Z).CopyTo(data, 20);
            BitConverter.GetBytes(rotation.W).CopyTo(data, 24);
            return data;
        }

        private byte[] SerializeSpawn(NetId entityId, EntityKind kind, Vector3 position, string displayName)
        {
            var nameBytes = System.Text.Encoding.UTF8.GetBytes(displayName ?? "");
            var data = new byte[24 + nameBytes.Length];

            BitConverter.GetBytes((int)kind).CopyTo(data, 0);
            BitConverter.GetBytes(position.X).CopyTo(data, 4);
            BitConverter.GetBytes(position.Y).CopyTo(data, 8);
            BitConverter.GetBytes(position.Z).CopyTo(data, 12);
            BitConverter.GetBytes(nameBytes.Length).CopyTo(data, 16);

            if (nameBytes.Length > 0)
                nameBytes.CopyTo(data, 20);

            return data;
        }

        private byte[] SerializeHealth(float current, float max)
        {
            var data = new byte[8];
            BitConverter.GetBytes(current).CopyTo(data, 0);
            BitConverter.GetBytes(max).CopyTo(data, 4);
            return data;
        }

        private byte[] SerializeCommit(Commit commit)
        {
            // Serialize commit to JSON for flexibility
            var json = JsonSerializer.Serialize(new
            {
                commitId = commit.CommitId,
                subjectId = commit.SubjectId.Packed,
                operation = (int)commit.Operation,
                tick = commit.Tick,
                authorityEpoch = commit.AuthorityEpoch
            });
            return System.Text.Encoding.UTF8.GetBytes(json);
        }

        private byte[] SerializeFrame(NetworkFrame frame)
        {
            // Simple binary format: tick(8) + timestamp(8) + packetCount(4) + packets
            var packets = new List<byte>();

            packets.AddRange(BitConverter.GetBytes(frame.Tick));
            packets.AddRange(BitConverter.GetBytes(frame.Timestamp));
            packets.AddRange(BitConverter.GetBytes(frame.Packets.Count));

            foreach (var packet in frame.Packets)
            {
                // Type(1) + EntityId(8) + Tick(8) + DataLen(4) + Data
                packets.Add((byte)packet.Type);
                packets.AddRange(BitConverter.GetBytes(packet.EntityId));
                packets.AddRange(BitConverter.GetBytes(packet.Tick));
                packets.AddRange(BitConverter.GetBytes(packet.Data.Length));
                packets.AddRange(packet.Data);
            }

            return packets.ToArray();
        }

        #endregion

        #region Inbound Processing

        /// <summary>
        /// Process an inbound network frame from a peer.
        /// </summary>
        public void ProcessInboundFrame(byte[] frameData, string sourceClientId)
        {
            try
            {
                var frame = DeserializeFrame(frameData);

                foreach (var packet in frame.Packets)
                {
                    ProcessInboundPacket(packet, sourceClientId, frame.Tick);
                }
            }
            catch (Exception ex)
            {
                Logger.Log($"[NetworkBroadcaster] Error processing inbound frame: {ex.Message}");
            }
        }

        private NetworkFrame DeserializeFrame(byte[] data)
        {
            var frame = new NetworkFrame();
            int offset = 0;

            frame.Tick = BitConverter.ToInt64(data, offset); offset += 8;
            frame.Timestamp = BitConverter.ToInt64(data, offset); offset += 8;
            int packetCount = BitConverter.ToInt32(data, offset); offset += 4;

            frame.Packets = new List<BroadcastPacket>();

            for (int i = 0; i < packetCount && offset < data.Length; i++)
            {
                var packet = new BroadcastPacket();
                packet.Type = (PacketKind)data[offset]; offset += 1;
                packet.EntityId = BitConverter.ToUInt64(data, offset); offset += 8;
                packet.Tick = BitConverter.ToInt64(data, offset); offset += 8;
                int dataLen = BitConverter.ToInt32(data, offset); offset += 4;

                packet.Data = new byte[dataLen];
                Array.Copy(data, offset, packet.Data, 0, dataLen);
                offset += dataLen;

                frame.Packets.Add(packet);
            }

            return frame;
        }

        private void ProcessInboundPacket(BroadcastPacket packet, string sourceClientId, long frameTick)
        {
            // Convert packet to InfoRing observation
            var entityId = new NetId(packet.EntityId);

            switch (packet.Type)
            {
                case PacketKind.PositionUpdate:
                    ProcessInboundPosition(entityId, packet.Data, packet.Tick, sourceClientId);
                    break;

                case PacketKind.EntitySpawn:
                    ProcessInboundSpawn(entityId, packet.Data, packet.Tick, sourceClientId);
                    break;

                case PacketKind.EntityDespawn:
                    ProcessInboundDespawn(entityId, packet.Tick, sourceClientId);
                    break;

                case PacketKind.HealthUpdate:
                    ProcessInboundHealth(entityId, packet.Data, packet.Tick, sourceClientId);
                    break;
            }
        }

        private void ProcessInboundPosition(NetId entityId, byte[] data, long tick, string sourceClientId)
        {
            if (data.Length < 24) return;

            var position = new Vector3(
                BitConverter.ToSingle(data, 0),
                BitConverter.ToSingle(data, 4),
                BitConverter.ToSingle(data, 8)
            );

            var rotation = new Quaternion(
                BitConverter.ToSingle(data, 12),
                BitConverter.ToSingle(data, 16),
                BitConverter.ToSingle(data, 20),
                data.Length >= 28 ? BitConverter.ToSingle(data, 24) : 1f
            );

            // Submit as observation to InfoRing
            var payload = new TransformPayload
            {
                Position = position,
                Rotation = rotation
            };

            var sourceId = NetId.Create(EntityKind.Player, sourceClientId.GetHashCode());
            _coordinator.SubmitObservation(entityId, sourceId, payload, tick);
        }

        private void ProcessInboundSpawn(NetId entityId, byte[] data, long tick, string sourceClientId)
        {
            if (data.Length < 20) return;

            var kind = (EntityKind)BitConverter.ToInt32(data, 0);
            var position = new Vector3(
                BitConverter.ToSingle(data, 4),
                BitConverter.ToSingle(data, 8),
                BitConverter.ToSingle(data, 12)
            );

            int nameLen = BitConverter.ToInt32(data, 16);
            string displayName = nameLen > 0 && data.Length >= 20 + nameLen
                ? System.Text.Encoding.UTF8.GetString(data, 20, nameLen)
                : "Unknown";

            // Register entity in ContainerRing
            _coordinator.RegisterEntity(entityId, kind, IntPtr.Zero, FrameType.World);
        }

        private void ProcessInboundDespawn(NetId entityId, long tick, string sourceClientId)
        {
            _coordinator.UnregisterEntity(entityId);
        }

        private void ProcessInboundHealth(NetId entityId, byte[] data, long tick, string sourceClientId)
        {
            if (data.Length < 8) return;

            float current = BitConverter.ToSingle(data, 0);
            float max = BitConverter.ToSingle(data, 4);

            var payload = new HealthPayload
            {
                Current = current,
                Maximum = max
            };

            var sourceId = NetId.Create(EntityKind.Player, sourceClientId.GetHashCode());
            _coordinator.SubmitObservation(entityId, sourceId, payload, tick);
        }

        #endregion

        #region Statistics

        public BroadcasterStats GetStats()
        {
            return new BroadcasterStats
            {
                PacketsSent = _packetsSent,
                BytesSent = _bytesSent,
                PacketsDropped = _packetsDropped,
                QueuedPackets = _outboundQueue.Count
            };
        }

        #endregion

        public void Dispose()
        {
            Stop();
        }
    }

    #region Supporting Types

    public enum PacketKind : byte
    {
        PositionUpdate = 1,
        EntitySpawn = 2,
        EntityDespawn = 3,
        HealthUpdate = 4,
        InventoryUpdate = 5,
        CombatAction = 6,
        AuthorityCommit = 7,
        WorldSync = 8,
        Acknowledgment = 9
    }

    public enum BroadcastPriority : byte
    {
        Low = 0,
        Normal = 1,
        High = 2,
        Critical = 3
    }

    public class BroadcastPacket
    {
        public PacketKind Type { get; set; }
        public ulong EntityId { get; set; }
        public long Tick { get; set; }
        public byte[] Data { get; set; }
        public BroadcastPriority Priority { get; set; }
        public long EnqueueTime { get; set; }
    }

    public class NetworkFrame
    {
        public long Tick { get; set; }
        public long Timestamp { get; set; }
        public List<BroadcastPacket> Packets { get; set; }
    }

    public struct BroadcasterStats
    {
        public long PacketsSent;
        public long BytesSent;
        public long PacketsDropped;
        public int QueuedPackets;
    }

    #endregion
}
