using System;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Threading;

namespace KenshiOnline.Coordinates
{
    /// <summary>
    /// TickTime - The Time Dimension
    ///
    /// Everything must be indexed by tick (or frame counter), not "whenever."
    /// Without time coordinates, you cannot:
    ///   - Resolve races
    ///   - Reorder packets
    ///   - Reconcile state
    ///
    /// Hard opinion: no tick = no multiplayer.
    ///
    /// This structure combines:
    ///   - Tick: The logical simulation step (deterministic, integer)
    ///   - SubTick: Fractional position within a tick for interpolation
    ///   - WallTime: Optional wall-clock timestamp for debugging/profiling
    /// </summary>
    [StructLayout(LayoutKind.Sequential)]
    public readonly struct TickTime : IEquatable<TickTime>, IComparable<TickTime>
    {
        /// <summary>
        /// The logical tick number. This is the authoritative time coordinate.
        /// All game state is indexed by this value.
        /// </summary>
        public readonly long Tick;

        /// <summary>
        /// Sub-tick fraction [0.0, 1.0) for interpolation between ticks.
        /// 0.0 = start of tick, 0.999 = just before next tick.
        /// </summary>
        public readonly float SubTick;

        /// <summary>
        /// Wall-clock timestamp in milliseconds since epoch.
        /// Used for debugging/latency measurement, NOT for game logic.
        /// </summary>
        public readonly long WallTimeMs;

        /// <summary>
        /// Invalid/zero time constant.
        /// </summary>
        public static readonly TickTime Zero = default;

        public TickTime(long tick, float subTick = 0f, long wallTimeMs = 0)
        {
            Tick = tick;
            SubTick = Math.Clamp(subTick, 0f, 0.9999f);
            WallTimeMs = wallTimeMs > 0 ? wallTimeMs : DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();
        }

        /// <summary>
        /// Create a TickTime at the start of a tick.
        /// </summary>
        public static TickTime AtTick(long tick)
        {
            return new TickTime(tick, 0f);
        }

        /// <summary>
        /// Create a TickTime with current wall time.
        /// </summary>
        public static TickTime Now(long tick, float subTick = 0f)
        {
            return new TickTime(tick, subTick, DateTimeOffset.UtcNow.ToUnixTimeMilliseconds());
        }

        /// <summary>
        /// Get the next tick (start of next simulation step).
        /// </summary>
        public TickTime NextTick() => new TickTime(Tick + 1, 0f, WallTimeMs);

        /// <summary>
        /// Get the previous tick.
        /// </summary>
        public TickTime PrevTick() => new TickTime(Tick - 1, 0f, WallTimeMs);

        /// <summary>
        /// Add ticks to this time.
        /// </summary>
        public TickTime AddTicks(long delta) => new TickTime(Tick + delta, SubTick, WallTimeMs);

        /// <summary>
        /// Get the continuous time value (tick + subtick) for interpolation.
        /// </summary>
        public double ContinuousTime => Tick + SubTick;

        /// <summary>
        /// Calculate tick difference between two times.
        /// </summary>
        public long TicksSince(TickTime other) => Tick - other.Tick;

        /// <summary>
        /// Check if this time is within a range of another time.
        /// </summary>
        public bool IsWithinRange(TickTime other, long tickRange)
        {
            return Math.Abs(Tick - other.Tick) <= tickRange;
        }

        /// <summary>
        /// Interpolate between two tick times.
        /// </summary>
        public static TickTime Lerp(TickTime a, TickTime b, float t)
        {
            double timeA = a.ContinuousTime;
            double timeB = b.ContinuousTime;
            double result = timeA + (timeB - timeA) * t;

            long tick = (long)result;
            float subTick = (float)(result - tick);
            long wallTime = (long)(a.WallTimeMs + (b.WallTimeMs - a.WallTimeMs) * t);

            return new TickTime(tick, subTick, wallTime);
        }

        #region Equality and Comparison

        public bool Equals(TickTime other) => Tick == other.Tick && Math.Abs(SubTick - other.SubTick) < 0.0001f;
        public override bool Equals(object? obj) => obj is TickTime other && Equals(other);
        public override int GetHashCode() => HashCode.Combine(Tick, (int)(SubTick * 10000));

        public int CompareTo(TickTime other)
        {
            int tickCompare = Tick.CompareTo(other.Tick);
            if (tickCompare != 0) return tickCompare;
            return SubTick.CompareTo(other.SubTick);
        }

        public static bool operator ==(TickTime left, TickTime right) => left.Equals(right);
        public static bool operator !=(TickTime left, TickTime right) => !left.Equals(right);
        public static bool operator <(TickTime left, TickTime right) => left.CompareTo(right) < 0;
        public static bool operator >(TickTime left, TickTime right) => left.CompareTo(right) > 0;
        public static bool operator <=(TickTime left, TickTime right) => left.CompareTo(right) <= 0;
        public static bool operator >=(TickTime left, TickTime right) => left.CompareTo(right) >= 0;

        #endregion

        public override string ToString()
        {
            if (Tick == 0) return "T(0)";
            return SubTick > 0.0001f
                ? $"T({Tick}+{SubTick:F3})"
                : $"T({Tick})";
        }
    }

    /// <summary>
    /// A monotonic tick clock that advances simulation time.
    /// This is the source of truth for the time dimension.
    /// </summary>
    public class TickClock
    {
        private long _currentTick;
        private long _startWallTimeMs;
        private readonly Stopwatch _stopwatch;
        private readonly double _tickRateHz;
        private readonly double _tickDurationMs;

        /// <summary>
        /// Current tick number.
        /// </summary>
        public long CurrentTick => Interlocked.Read(ref _currentTick);

        /// <summary>
        /// Tick rate in Hz (ticks per second).
        /// </summary>
        public double TickRateHz => _tickRateHz;

        /// <summary>
        /// Duration of one tick in milliseconds.
        /// </summary>
        public double TickDurationMs => _tickDurationMs;

        /// <summary>
        /// Current TickTime with subtick fraction.
        /// </summary>
        public TickTime Now
        {
            get
            {
                long tick = CurrentTick;
                double elapsedMs = _stopwatch.Elapsed.TotalMilliseconds;
                double expectedMs = tick * _tickDurationMs;
                float subTick = (float)Math.Clamp((elapsedMs - expectedMs) / _tickDurationMs, 0, 0.9999);
                return new TickTime(tick, subTick);
            }
        }

        /// <summary>
        /// Wall-clock time when the clock started.
        /// </summary>
        public long StartTimeMs => _startWallTimeMs;

        /// <summary>
        /// Create a tick clock with the specified tick rate.
        /// </summary>
        /// <param name="tickRateHz">Ticks per second (default 20 = 50ms per tick)</param>
        public TickClock(double tickRateHz = 20.0)
        {
            _tickRateHz = tickRateHz;
            _tickDurationMs = 1000.0 / tickRateHz;
            _stopwatch = new Stopwatch();
            _startWallTimeMs = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();
            _stopwatch.Start();
        }

        /// <summary>
        /// Advance to the next tick. Returns the new TickTime.
        /// </summary>
        public TickTime Advance()
        {
            long newTick = Interlocked.Increment(ref _currentTick);
            return new TickTime(newTick, 0f);
        }

        /// <summary>
        /// Set the tick explicitly (for synchronization with server).
        /// </summary>
        public void SetTick(long tick)
        {
            Interlocked.Exchange(ref _currentTick, tick);
        }

        /// <summary>
        /// Get the expected wall time for a given tick.
        /// </summary>
        public long GetExpectedWallTime(long tick)
        {
            return _startWallTimeMs + (long)(tick * _tickDurationMs);
        }

        /// <summary>
        /// Get the expected tick for a given wall time.
        /// </summary>
        public long GetExpectedTick(long wallTimeMs)
        {
            double elapsed = wallTimeMs - _startWallTimeMs;
            return (long)(elapsed / _tickDurationMs);
        }

        /// <summary>
        /// Calculate how many ticks behind/ahead we are from expected.
        /// Positive = we're behind (need to catch up).
        /// Negative = we're ahead (need to slow down).
        /// </summary>
        public long GetTickDrift()
        {
            long now = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();
            long expectedTick = GetExpectedTick(now);
            return expectedTick - CurrentTick;
        }

        /// <summary>
        /// Reset the clock to a specific state (for resync).
        /// </summary>
        public void Reset(long tick, long wallTimeMs)
        {
            Interlocked.Exchange(ref _currentTick, tick);
            _startWallTimeMs = wallTimeMs - (long)(tick * _tickDurationMs);
            _stopwatch.Restart();
        }
    }

    /// <summary>
    /// A time range for expressing "valid between tick A and tick B".
    /// Useful for authority windows, reconciliation, and event lifetimes.
    /// </summary>
    [StructLayout(LayoutKind.Sequential)]
    public readonly struct TickRange : IEquatable<TickRange>
    {
        public readonly long Start;
        public readonly long End; // Inclusive

        public static readonly TickRange Invalid = new TickRange(0, -1);
        public static readonly TickRange Forever = new TickRange(0, long.MaxValue);

        public TickRange(long start, long end)
        {
            Start = start;
            End = end;
        }

        public static TickRange Single(long tick) => new TickRange(tick, tick);
        public static TickRange From(long start) => new TickRange(start, long.MaxValue);
        public static TickRange Until(long end) => new TickRange(0, end);

        public bool IsValid => End >= Start;
        public long Duration => IsValid ? End - Start + 1 : 0;

        public bool Contains(long tick) => IsValid && tick >= Start && tick <= End;
        public bool Contains(TickTime time) => Contains(time.Tick);

        public bool Overlaps(TickRange other)
        {
            if (!IsValid || !other.IsValid) return false;
            return Start <= other.End && End >= other.Start;
        }

        public TickRange? Intersect(TickRange other)
        {
            if (!Overlaps(other)) return null;
            return new TickRange(Math.Max(Start, other.Start), Math.Min(End, other.End));
        }

        public TickRange Extend(long ticks) => new TickRange(Start, End + ticks);
        public TickRange Shift(long delta) => new TickRange(Start + delta, End + delta);

        public bool Equals(TickRange other) => Start == other.Start && End == other.End;
        public override bool Equals(object? obj) => obj is TickRange other && Equals(other);
        public override int GetHashCode() => HashCode.Combine(Start, End);

        public override string ToString()
        {
            if (!IsValid) return "TickRange(Invalid)";
            if (End == long.MaxValue) return $"TickRange[{Start}..∞)";
            if (Start == End) return $"TickRange[{Start}]";
            return $"TickRange[{Start}..{End}]";
        }
    }
}
