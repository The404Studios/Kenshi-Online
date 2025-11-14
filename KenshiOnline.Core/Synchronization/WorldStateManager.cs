using System;
using System.Collections.Generic;

namespace KenshiOnline.Core.Synchronization
{
    /// <summary>
    /// World state data
    /// </summary>
    public class WorldState
    {
        // Time
        public float GameTime { get; set; } // In-game time (0-24 hours)
        public int GameDay { get; set; } // Days since start
        public float TimeScale { get; set; } = 1.0f; // Game speed multiplier

        // Weather
        public string Weather { get; set; } = "Clear";
        public float WindSpeed { get; set; }
        public float WindDirection { get; set; }
        public float Temperature { get; set; } = 20f; // Celsius

        // Environment
        public float Fog { get; set; }
        public float Sandstorm { get; set; }
        public float Rainfall { get; set; }

        // Global flags
        public Dictionary<string, bool> GlobalFlags { get; set; }
        public Dictionary<string, int> GlobalCounters { get; set; }

        // Server settings
        public float GameSpeedMultiplier { get; set; } = 1.0f;
        public bool PauseGame { get; set; } = false;

        public WorldState()
        {
            GlobalFlags = new Dictionary<string, bool>();
            GlobalCounters = new Dictionary<string, int>();
            GameTime = 12.0f; // Start at noon
            GameDay = 1;
            Weather = "Clear";
        }

        public Dictionary<string, object> Serialize()
        {
            return new Dictionary<string, object>
            {
                ["gameTime"] = GameTime,
                ["gameDay"] = GameDay,
                ["timeScale"] = TimeScale,
                ["weather"] = Weather,
                ["windSpeed"] = WindSpeed,
                ["windDirection"] = WindDirection,
                ["temperature"] = Temperature,
                ["fog"] = Fog,
                ["sandstorm"] = Sandstorm,
                ["rainfall"] = Rainfall,
                ["globalFlags"] = GlobalFlags,
                ["globalCounters"] = GlobalCounters,
                ["gameSpeedMultiplier"] = GameSpeedMultiplier,
                ["pauseGame"] = PauseGame
            };
        }

        public void Deserialize(Dictionary<string, object> data)
        {
            if (data.TryGetValue("gameTime", out var gameTime))
                GameTime = Convert.ToSingle(gameTime);
            if (data.TryGetValue("gameDay", out var gameDay))
                GameDay = Convert.ToInt32(gameDay);
            if (data.TryGetValue("timeScale", out var timeScale))
                TimeScale = Convert.ToSingle(timeScale);
            if (data.TryGetValue("weather", out var weather))
                Weather = weather.ToString();
            if (data.TryGetValue("windSpeed", out var windSpeed))
                WindSpeed = Convert.ToSingle(windSpeed);
            if (data.TryGetValue("windDirection", out var windDirection))
                WindDirection = Convert.ToSingle(windDirection);
            if (data.TryGetValue("temperature", out var temperature))
                Temperature = Convert.ToSingle(temperature);
            if (data.TryGetValue("fog", out var fog))
                Fog = Convert.ToSingle(fog);
            if (data.TryGetValue("sandstorm", out var sandstorm))
                Sandstorm = Convert.ToSingle(sandstorm);
            if (data.TryGetValue("rainfall", out var rainfall))
                Rainfall = Convert.ToSingle(rainfall);
            if (data.TryGetValue("globalFlags", out var globalFlags) && globalFlags is Dictionary<string, object> flagsDict)
            {
                GlobalFlags.Clear();
                foreach (var kvp in flagsDict)
                {
                    GlobalFlags[kvp.Key] = Convert.ToBoolean(kvp.Value);
                }
            }
            if (data.TryGetValue("globalCounters", out var globalCounters) && globalCounters is Dictionary<string, object> countersDict)
            {
                GlobalCounters.Clear();
                foreach (var kvp in countersDict)
                {
                    GlobalCounters[kvp.Key] = Convert.ToInt32(kvp.Value);
                }
            }
            if (data.TryGetValue("gameSpeedMultiplier", out var gameSpeedMultiplier))
                GameSpeedMultiplier = Convert.ToSingle(gameSpeedMultiplier);
            if (data.TryGetValue("pauseGame", out var pauseGame))
                PauseGame = Convert.ToBoolean(pauseGame);
        }
    }

    /// <summary>
    /// Manages global world state and synchronization
    /// Server-authoritative world state
    /// </summary>
    public class WorldStateManager
    {
        private WorldState _worldState;
        private bool _isDirty;
        private readonly object _lock = new object();

        // Time progression
        private float _accumulatedTime;
        private const float RealSecondsPerGameHour = 60f; // 1 minute = 1 hour

        // Weather system
        private float _weatherChangeTimer;
        private const float WeatherChangeInterval = 3600f; // Change weather every hour (game time)
        private readonly Random _random;

        // Events
        public event Action<WorldState> OnWorldStateChanged;

        public WorldState State
        {
            get
            {
                lock (_lock)
                {
                    return _worldState;
                }
            }
        }

        public bool IsDirty
        {
            get
            {
                lock (_lock)
                {
                    return _isDirty;
                }
            }
        }

        public WorldStateManager()
        {
            _worldState = new WorldState();
            _random = new Random();
        }

        #region Time Management

        /// <summary>
        /// Update world state (call every frame)
        /// </summary>
        public void Update(float deltaTime)
        {
            if (_worldState.PauseGame)
                return;

            lock (_lock)
            {
                // Apply game speed multiplier
                float adjustedDelta = deltaTime * _worldState.GameSpeedMultiplier;

                // Update game time
                _accumulatedTime += adjustedDelta;
                float hoursToAdd = _accumulatedTime / RealSecondsPerGameHour;

                if (hoursToAdd >= 0.01f) // Update in small increments
                {
                    _worldState.GameTime += hoursToAdd;
                    _accumulatedTime = 0;

                    // Handle day rollover
                    if (_worldState.GameTime >= 24.0f)
                    {
                        _worldState.GameDay++;
                        _worldState.GameTime -= 24.0f;
                    }

                    _isDirty = true;
                }

                // Update weather
                UpdateWeather(adjustedDelta);
            }
        }

        /// <summary>
        /// Set game speed
        /// </summary>
        public void SetGameSpeed(float multiplier)
        {
            lock (_lock)
            {
                _worldState.GameSpeedMultiplier = Math.Max(0f, Math.Min(10f, multiplier));
                _isDirty = true;
                OnWorldStateChanged?.Invoke(_worldState);
            }
        }

        /// <summary>
        /// Pause/unpause game
        /// </summary>
        public void SetPaused(bool paused)
        {
            lock (_lock)
            {
                _worldState.PauseGame = paused;
                _isDirty = true;
                OnWorldStateChanged?.Invoke(_worldState);
            }
        }

        /// <summary>
        /// Set game time
        /// </summary>
        public void SetGameTime(float hour)
        {
            lock (_lock)
            {
                _worldState.GameTime = Math.Max(0f, Math.Min(24f, hour));
                _isDirty = true;
                OnWorldStateChanged?.Invoke(_worldState);
            }
        }

        /// <summary>
        /// Advance to next day
        /// </summary>
        public void AdvanceDay()
        {
            lock (_lock)
            {
                _worldState.GameDay++;
                _worldState.GameTime = 8.0f; // Start at 8 AM
                _isDirty = true;
                OnWorldStateChanged?.Invoke(_worldState);
            }
        }

        #endregion

        #region Weather Management

        /// <summary>
        /// Update weather system
        /// </summary>
        private void UpdateWeather(float deltaTime)
        {
            _weatherChangeTimer += deltaTime;

            if (_weatherChangeTimer >= WeatherChangeInterval)
            {
                _weatherChangeTimer = 0;
                ChangeWeather();
            }

            // Gradually reduce weather effects
            _worldState.Fog = Math.Max(0, _worldState.Fog - deltaTime * 0.01f);
            _worldState.Sandstorm = Math.Max(0, _worldState.Sandstorm - deltaTime * 0.01f);
            _worldState.Rainfall = Math.Max(0, _worldState.Rainfall - deltaTime * 0.01f);
        }

        /// <summary>
        /// Change weather randomly
        /// </summary>
        private void ChangeWeather()
        {
            var weatherTypes = new[] { "Clear", "Cloudy", "Foggy", "Rainy", "Sandstorm", "Windy" };
            var newWeather = weatherTypes[_random.Next(weatherTypes.Length)];

            _worldState.Weather = newWeather;

            switch (newWeather)
            {
                case "Clear":
                    _worldState.Fog = 0;
                    _worldState.Rainfall = 0;
                    _worldState.Sandstorm = 0;
                    _worldState.WindSpeed = _random.Next(0, 10);
                    break;

                case "Cloudy":
                    _worldState.Fog = _random.Next(10, 30) / 100f;
                    _worldState.Rainfall = 0;
                    _worldState.Sandstorm = 0;
                    _worldState.WindSpeed = _random.Next(5, 15);
                    break;

                case "Foggy":
                    _worldState.Fog = _random.Next(50, 100) / 100f;
                    _worldState.Rainfall = 0;
                    _worldState.Sandstorm = 0;
                    _worldState.WindSpeed = _random.Next(0, 5);
                    break;

                case "Rainy":
                    _worldState.Fog = _random.Next(20, 40) / 100f;
                    _worldState.Rainfall = _random.Next(50, 100) / 100f;
                    _worldState.Sandstorm = 0;
                    _worldState.WindSpeed = _random.Next(10, 25);
                    _worldState.Temperature -= 5;
                    break;

                case "Sandstorm":
                    _worldState.Fog = _random.Next(30, 60) / 100f;
                    _worldState.Rainfall = 0;
                    _worldState.Sandstorm = _random.Next(70, 100) / 100f;
                    _worldState.WindSpeed = _random.Next(30, 50);
                    _worldState.Temperature += 5;
                    break;

                case "Windy":
                    _worldState.Fog = 0;
                    _worldState.Rainfall = 0;
                    _worldState.Sandstorm = 0;
                    _worldState.WindSpeed = _random.Next(20, 40);
                    break;
            }

            _worldState.WindDirection = _random.Next(0, 360);
            _isDirty = true;
        }

        /// <summary>
        /// Set weather manually
        /// </summary>
        public void SetWeather(string weatherType)
        {
            lock (_lock)
            {
                _worldState.Weather = weatherType;
                _weatherChangeTimer = 0; // Reset timer
                ChangeWeather(); // Apply weather effects
                OnWorldStateChanged?.Invoke(_worldState);
            }
        }

        #endregion

        #region Global Flags & Counters

        /// <summary>
        /// Set global flag
        /// </summary>
        public void SetGlobalFlag(string flagName, bool value)
        {
            lock (_lock)
            {
                _worldState.GlobalFlags[flagName] = value;
                _isDirty = true;
                OnWorldStateChanged?.Invoke(_worldState);
            }
        }

        /// <summary>
        /// Get global flag
        /// </summary>
        public bool GetGlobalFlag(string flagName)
        {
            lock (_lock)
            {
                return _worldState.GlobalFlags.TryGetValue(flagName, out var value) && value;
            }
        }

        /// <summary>
        /// Set global counter
        /// </summary>
        public void SetGlobalCounter(string counterName, int value)
        {
            lock (_lock)
            {
                _worldState.GlobalCounters[counterName] = value;
                _isDirty = true;
                OnWorldStateChanged?.Invoke(_worldState);
            }
        }

        /// <summary>
        /// Increment global counter
        /// </summary>
        public int IncrementGlobalCounter(string counterName, int amount = 1)
        {
            lock (_lock)
            {
                if (!_worldState.GlobalCounters.ContainsKey(counterName))
                    _worldState.GlobalCounters[counterName] = 0;

                _worldState.GlobalCounters[counterName] += amount;
                _isDirty = true;
                OnWorldStateChanged?.Invoke(_worldState);
                return _worldState.GlobalCounters[counterName];
            }
        }

        /// <summary>
        /// Get global counter
        /// </summary>
        public int GetGlobalCounter(string counterName)
        {
            lock (_lock)
            {
                return _worldState.GlobalCounters.TryGetValue(counterName, out var value) ? value : 0;
            }
        }

        #endregion

        #region Synchronization

        /// <summary>
        /// Get world state snapshot
        /// </summary>
        public Dictionary<string, object> GetSnapshot()
        {
            lock (_lock)
            {
                _isDirty = false;
                return _worldState.Serialize();
            }
        }

        /// <summary>
        /// Apply world state from server
        /// </summary>
        public void ApplySnapshot(Dictionary<string, object> data)
        {
            lock (_lock)
            {
                _worldState.Deserialize(data);
                OnWorldStateChanged?.Invoke(_worldState);
            }
        }

        /// <summary>
        /// Clear dirty flag
        /// </summary>
        public void ClearDirty()
        {
            lock (_lock)
            {
                _isDirty = false;
            }
        }

        #endregion

        #region Utility

        /// <summary>
        /// Check if it's daytime
        /// </summary>
        public bool IsDaytime()
        {
            lock (_lock)
            {
                return _worldState.GameTime >= 6.0f && _worldState.GameTime < 18.0f;
            }
        }

        /// <summary>
        /// Check if it's nighttime
        /// </summary>
        public bool IsNighttime()
        {
            return !IsDaytime();
        }

        /// <summary>
        /// Get time of day string
        /// </summary>
        public string GetTimeOfDayString()
        {
            lock (_lock)
            {
                int hours = (int)_worldState.GameTime;
                int minutes = (int)((_worldState.GameTime - hours) * 60);
                return $"{hours:D2}:{minutes:D2}";
            }
        }

        /// <summary>
        /// Get formatted date string
        /// </summary>
        public string GetDateString()
        {
            lock (_lock)
            {
                return $"Day {_worldState.GameDay}";
            }
        }

        #endregion
    }
}
