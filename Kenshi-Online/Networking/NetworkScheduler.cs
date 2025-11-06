using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Diagnostics;
using System.Threading;
using System.Threading.Tasks;

namespace KenshiMultiplayer.Networking
{
    /// <summary>
    /// Advanced network scheduler for coordinating client-server-middleware communication
    /// Handles priority queuing, rate limiting, and intelligent batching
    /// </summary>
    public class NetworkScheduler : IDisposable
    {
        public enum MessagePriority
        {
            Critical = 0,   // Immediate processing (auth, critical gameplay)
            High = 1,       // Fast processing (combat, movement)
            Normal = 2,     // Standard processing (chat, state updates)
            Low = 3,        // Deferred processing (statistics, background sync)
            Batch = 4       // Batch processing (bulk operations)
        }

        private class ScheduledMessage
        {
            public string MessageId { get; set; }
            public MessagePriority Priority { get; set; }
            public object Message { get; set; }
            public Action<object> Callback { get; set; }
            public long ScheduledTime { get; set; }
            public long Deadline { get; set; }
            public int RetryCount { get; set; }
            public int MaxRetries { get; set; }
        }

        private readonly ConcurrentDictionary<MessagePriority, ConcurrentQueue<ScheduledMessage>>[] _queues;
        private readonly ConcurrentDictionary<string, ScheduledMessage> _pendingMessages;
        private readonly ConcurrentDictionary<string, long> _rateLimiters;

        private readonly InterpolationEngine _interpolationEngine;
        private readonly CompressionEngine _compressionEngine;

        private readonly Timer[] _processingTimers;
        private readonly SemaphoreSlim[] _processingLocks;

        private readonly int _tickRateMs;
        private readonly bool _isRunning;
        private readonly Stopwatch _stopwatch;

        // Statistics
        private long _messagesProcessed;
        private long _messagesDropped;
        private long _totalLatency;

        public NetworkScheduler(
            int clientTickRateMs = 50,
            int serverTickRateMs = 20,
            int middlewareTickRateMs = 100,
            InterpolationEngine interpolationEngine = null,
            CompressionEngine compressionEngine = null)
        {
            _tickRateMs = clientTickRateMs;
            _isRunning = true;
            _stopwatch = Stopwatch.StartNew();

            _interpolationEngine = interpolationEngine ?? new InterpolationEngine();
            _compressionEngine = compressionEngine ?? new CompressionEngine();

            // Initialize queues for each priority level (3 tiers: client, server, middleware)
            _queues = new ConcurrentDictionary<MessagePriority, ConcurrentQueue<ScheduledMessage>>[3];
            _processingTimers = new Timer[3];
            _processingLocks = new SemaphoreSlim[3];

            for (int tier = 0; tier < 3; tier++)
            {
                _queues[tier] = new ConcurrentDictionary<MessagePriority, ConcurrentQueue<ScheduledMessage>>();
                foreach (MessagePriority priority in Enum.GetValues(typeof(MessagePriority)))
                {
                    _queues[tier][priority] = new ConcurrentQueue<ScheduledMessage>();
                }
                _processingLocks[tier] = new SemaphoreSlim(1, 1);
            }

            _pendingMessages = new ConcurrentDictionary<string, ScheduledMessage>();
            _rateLimiters = new ConcurrentDictionary<string, long>();

            // Start processing timers
            _processingTimers[0] = new Timer(_ => ProcessQueue(0), null, 0, clientTickRateMs);
            _processingTimers[1] = new Timer(_ => ProcessQueue(1), null, 0, serverTickRateMs);
            _processingTimers[2] = new Timer(_ => ProcessQueue(2), null, 0, middlewareTickRateMs);
        }

        /// <summary>
        /// Schedule a message for processing
        /// </summary>
        public string ScheduleMessage(
            object message,
            Action<object> callback,
            MessagePriority priority = MessagePriority.Normal,
            int tier = 0,
            long delayMs = 0,
            long deadlineMs = 0,
            int maxRetries = 3,
            string rateLimitKey = null,
            long rateLimitMs = 0)
        {
            if (!_isRunning) return null;

            // Check rate limiting
            if (!string.IsNullOrEmpty(rateLimitKey))
            {
                long now = _stopwatch.ElapsedMilliseconds;
                if (_rateLimiters.TryGetValue(rateLimitKey, out long lastTime))
                {
                    if (now - lastTime < rateLimitMs)
                    {
                        // Rate limited, drop message
                        Interlocked.Increment(ref _messagesDropped);
                        return null;
                    }
                }
                _rateLimiters[rateLimitKey] = now;
            }

            var scheduledMessage = new ScheduledMessage
            {
                MessageId = Guid.NewGuid().ToString(),
                Priority = priority,
                Message = message,
                Callback = callback,
                ScheduledTime = _stopwatch.ElapsedMilliseconds + delayMs,
                Deadline = deadlineMs > 0 ? _stopwatch.ElapsedMilliseconds + deadlineMs : long.MaxValue,
                RetryCount = 0,
                MaxRetries = maxRetries
            };

            _pendingMessages[scheduledMessage.MessageId] = scheduledMessage;
            _queues[tier][priority].Enqueue(scheduledMessage);

            return scheduledMessage.MessageId;
        }

        /// <summary>
        /// Cancel a scheduled message
        /// </summary>
        public bool CancelMessage(string messageId)
        {
            return _pendingMessages.TryRemove(messageId, out _);
        }

        /// <summary>
        /// Process messages in the queue for a specific tier
        /// </summary>
        private async void ProcessQueue(int tier)
        {
            if (!_isRunning) return;

            await _processingLocks[tier].WaitAsync();
            try
            {
                long now = _stopwatch.ElapsedMilliseconds;
                var batchedMessages = new List<ScheduledMessage>();

                // Process messages by priority
                foreach (MessagePriority priority in Enum.GetValues(typeof(MessagePriority)))
                {
                    var queue = _queues[tier][priority];
                    int processedCount = 0;
                    int maxProcessPerTick = priority == MessagePriority.Critical ? 100 :
                                           priority == MessagePriority.High ? 50 :
                                           priority == MessagePriority.Normal ? 20 : 10;

                    while (processedCount < maxProcessPerTick && queue.TryDequeue(out var scheduledMessage))
                    {
                        // Check if message is still pending
                        if (!_pendingMessages.ContainsKey(scheduledMessage.MessageId))
                            continue;

                        // Check if it's time to process
                        if (scheduledMessage.ScheduledTime > now)
                        {
                            // Re-enqueue for later
                            queue.Enqueue(scheduledMessage);
                            break;
                        }

                        // Check deadline
                        if (scheduledMessage.Deadline < now)
                        {
                            // Message expired
                            _pendingMessages.TryRemove(scheduledMessage.MessageId, out _);
                            Interlocked.Increment(ref _messagesDropped);
                            continue;
                        }

                        // Process batch messages separately
                        if (priority == MessagePriority.Batch)
                        {
                            batchedMessages.Add(scheduledMessage);
                            processedCount++;
                            continue;
                        }

                        // Process message
                        await ProcessMessage(scheduledMessage, tier);
                        processedCount++;
                    }
                }

                // Process batched messages together
                if (batchedMessages.Count > 0)
                {
                    await ProcessBatchedMessages(batchedMessages, tier);
                }
            }
            finally
            {
                _processingLocks[tier].Release();
            }
        }

        /// <summary>
        /// Process a single message
        /// </summary>
        private async Task ProcessMessage(ScheduledMessage scheduledMessage, int tier)
        {
            long startTime = _stopwatch.ElapsedMilliseconds;

            try
            {
                // Execute callback
                await Task.Run(() => scheduledMessage.Callback?.Invoke(scheduledMessage.Message));

                // Remove from pending
                _pendingMessages.TryRemove(scheduledMessage.MessageId, out _);

                // Update statistics
                Interlocked.Increment(ref _messagesProcessed);
                long latency = _stopwatch.ElapsedMilliseconds - startTime;
                Interlocked.Add(ref _totalLatency, latency);
            }
            catch (Exception ex)
            {
                // Retry logic
                if (scheduledMessage.RetryCount < scheduledMessage.MaxRetries)
                {
                    scheduledMessage.RetryCount++;
                    scheduledMessage.ScheduledTime = _stopwatch.ElapsedMilliseconds + (100 * scheduledMessage.RetryCount); // Exponential backoff
                    _queues[tier][scheduledMessage.Priority].Enqueue(scheduledMessage);
                }
                else
                {
                    // Max retries exceeded
                    _pendingMessages.TryRemove(scheduledMessage.MessageId, out _);
                    Interlocked.Increment(ref _messagesDropped);
                    Console.WriteLine($"Message processing failed after {scheduledMessage.MaxRetries} retries: {ex.Message}");
                }
            }
        }

        /// <summary>
        /// Process multiple messages in a batch
        /// </summary>
        private async Task ProcessBatchedMessages(List<ScheduledMessage> messages, int tier)
        {
            try
            {
                // Combine messages for batch processing
                var batch = new List<object>();
                foreach (var msg in messages)
                {
                    batch.Add(msg.Message);
                }

                // Process batch (callback for first message handles the batch)
                if (messages.Count > 0 && messages[0].Callback != null)
                {
                    await Task.Run(() => messages[0].Callback.Invoke(batch));
                }

                // Remove all from pending
                foreach (var msg in messages)
                {
                    _pendingMessages.TryRemove(msg.MessageId, out _);
                    Interlocked.Increment(ref _messagesProcessed);
                }
            }
            catch (Exception ex)
            {
                Console.WriteLine($"Batch processing failed: {ex.Message}");
                Interlocked.Add(ref _messagesDropped, messages.Count);
            }
        }

        /// <summary>
        /// Get scheduler statistics
        /// </summary>
        public (long Processed, long Dropped, double AvgLatency, int Pending) GetStatistics()
        {
            long processed = Interlocked.Read(ref _messagesProcessed);
            long dropped = Interlocked.Read(ref _messagesDropped);
            long totalLatency = Interlocked.Read(ref _totalLatency);
            double avgLatency = processed > 0 ? (double)totalLatency / processed : 0;
            int pending = _pendingMessages.Count;

            return (processed, dropped, avgLatency, pending);
        }

        /// <summary>
        /// Reset statistics
        /// </summary>
        public void ResetStatistics()
        {
            Interlocked.Exchange(ref _messagesProcessed, 0);
            Interlocked.Exchange(ref _messagesDropped, 0);
            Interlocked.Exchange(ref _totalLatency, 0);
        }

        /// <summary>
        /// Get interpolation engine
        /// </summary>
        public InterpolationEngine GetInterpolationEngine() => _interpolationEngine;

        /// <summary>
        /// Get compression engine
        /// </summary>
        public CompressionEngine GetCompressionEngine() => _compressionEngine;

        public void Dispose()
        {
            for (int i = 0; i < _processingTimers.Length; i++)
            {
                _processingTimers[i]?.Dispose();
                _processingLocks[i]?.Dispose();
            }
        }
    }
}
