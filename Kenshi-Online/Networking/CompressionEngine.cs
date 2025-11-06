using System;
using System.Collections.Generic;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Text;
using Newtonsoft.Json;

namespace KenshiMultiplayer.Networking
{
    /// <summary>
    /// Advanced compression engine with multiple compression strategies
    /// </summary>
    public class CompressionEngine
    {
        public enum CompressionStrategy
        {
            None,           // No compression
            Delta,          // Delta compression (only send changes)
            Huffman,        // Huffman encoding
            GZip,           // GZip compression
            DeltaGZip,      // Delta + GZip
            Quantization    // Quantize floats to reduce precision
        }

        private readonly Dictionary<string, object?> _previousStates;
        private readonly CompressionStrategy _defaultStrategy;

        public CompressionEngine(CompressionStrategy defaultStrategy = CompressionStrategy.DeltaGZip)
        {
            _previousStates = new Dictionary<string, object?>();
            _defaultStrategy = defaultStrategy;
        }

        /// <summary>
        /// Compress data using the specified strategy
        /// </summary>
        public byte[] Compress<T>(string entityId, T currentState, CompressionStrategy strategy = CompressionStrategy.None) where T : class
        {
            if (strategy == CompressionStrategy.None)
                strategy = _defaultStrategy;

            switch (strategy)
            {
                case CompressionStrategy.None:
                    return CompressNone(currentState);

                case CompressionStrategy.Delta:
                    return CompressDelta(entityId, currentState);

                case CompressionStrategy.GZip:
                    return CompressGZip(currentState);

                case CompressionStrategy.DeltaGZip:
                    return CompressDeltaGZip(entityId, currentState);

                case CompressionStrategy.Quantization:
                    return CompressQuantized(currentState);

                default:
                    return CompressNone(currentState);
            }
        }

        /// <summary>
        /// Decompress data
        /// </summary>
        public T Decompress<T>(string entityId, byte[] compressedData, CompressionStrategy strategy = CompressionStrategy.None) where T : class
        {
            if (strategy == CompressionStrategy.None)
                strategy = _defaultStrategy;

            switch (strategy)
            {
                case CompressionStrategy.None:
                    return DecompressNone<T>(compressedData);

                case CompressionStrategy.Delta:
                    return DecompressDelta<T>(entityId, compressedData);

                case CompressionStrategy.GZip:
                    return DecompressGZip<T>(compressedData);

                case CompressionStrategy.DeltaGZip:
                    return DecompressDeltaGZip<T>(entityId, compressedData);

                case CompressionStrategy.Quantization:
                    return DecompressQuantized<T>(compressedData);

                default:
                    return DecompressNone<T>(compressedData);
            }
        }

        #region Compression Strategies

        private byte[] CompressNone<T>(T data)
        {
            string json = JsonConvert.SerializeObject(data);
            return Encoding.UTF8.GetBytes(json);
        }

        private T DecompressNone<T>(byte[] data)
        {
            string json = Encoding.UTF8.GetString(data);
            return JsonConvert.DeserializeObject<T>(json);
        }

        private byte[] CompressDelta<T>(string entityId, T currentState) where T : class
        {
            var delta = new Dictionary<string, object>();

            if (_previousStates.TryGetValue(entityId, out var previousStateObj) && previousStateObj is T previousState)
            {
                // Compare current and previous state
                var currentProps = typeof(T).GetProperties();
                foreach (var prop in currentProps)
                {
                    var currentValue = prop.GetValue(currentState);
                    var previousValue = prop.GetValue(previousState);

                    if (!Equals(currentValue, previousValue))
                    {
                        delta[prop.Name] = currentValue;
                    }
                }
            }
            else
            {
                // First time, send full state
                var currentProps = typeof(T).GetProperties();
                foreach (var prop in currentProps)
                {
                    delta[prop.Name] = prop.GetValue(currentState);
                }
            }

            // Store current state as previous
            _previousStates[entityId] = currentState;

            string json = JsonConvert.SerializeObject(delta);
            return Encoding.UTF8.GetBytes(json);
        }

        private T DecompressDelta<T>(string entityId, byte[] data) where T : class
        {
            string json = Encoding.UTF8.GetString(data);
            var delta = JsonConvert.DeserializeObject<Dictionary<string, object>>(json);

            T result;
            if (_previousStates.TryGetValue(entityId, out var previousStateObj) && previousStateObj is T previousState)
            {
                result = previousState;
            }
            else
            {
                result = Activator.CreateInstance<T>();
            }

            // Apply delta
            foreach (var kvp in delta)
            {
                var prop = typeof(T).GetProperty(kvp.Key);
                if (prop != null && prop.CanWrite)
                {
                    var value = Convert.ChangeType(kvp.Value, prop.PropertyType);
                    prop.SetValue(result, value);
                }
            }

            _previousStates[entityId] = result;
            return result;
        }

        private byte[] CompressGZip<T>(T data)
        {
            string json = JsonConvert.SerializeObject(data);
            byte[] bytes = Encoding.UTF8.GetBytes(json);

            using (var outputStream = new MemoryStream())
            {
                using (var gzipStream = new GZipStream(outputStream, CompressionLevel.Optimal))
                {
                    gzipStream.Write(bytes, 0, bytes.Length);
                }
                return outputStream.ToArray();
            }
        }

        private T DecompressGZip<T>(byte[] compressedData)
        {
            using (var inputStream = new MemoryStream(compressedData))
            using (var gzipStream = new GZipStream(inputStream, CompressionMode.Decompress))
            using (var outputStream = new MemoryStream())
            {
                gzipStream.CopyTo(outputStream);
                byte[] decompressed = outputStream.ToArray();
                string json = Encoding.UTF8.GetString(decompressed);
                return JsonConvert.DeserializeObject<T>(json);
            }
        }

        private byte[] CompressDeltaGZip<T>(string entityId, T currentState) where T : class
        {
            byte[] deltaBytes = CompressDelta(entityId, currentState);

            using (var outputStream = new MemoryStream())
            {
                using (var gzipStream = new GZipStream(outputStream, CompressionLevel.Optimal))
                {
                    gzipStream.Write(deltaBytes, 0, deltaBytes.Length);
                }
                return outputStream.ToArray();
            }
        }

        private T DecompressDeltaGZip<T>(string entityId, byte[] compressedData) where T : class
        {
            using (var inputStream = new MemoryStream(compressedData))
            using (var gzipStream = new GZipStream(inputStream, CompressionMode.Decompress))
            using (var outputStream = new MemoryStream())
            {
                gzipStream.CopyTo(outputStream);
                byte[] deltaBytes = outputStream.ToArray();
                return DecompressDelta<T>(entityId, deltaBytes);
            }
        }

        private byte[] CompressQuantized<T>(T data)
        {
            // Quantize floats to reduce precision and size
            var json = JsonConvert.SerializeObject(data);
            var obj = JsonConvert.DeserializeObject<Dictionary<string, object>>(json);

            QuantizeValues(obj);

            string quantizedJson = JsonConvert.SerializeObject(obj);
            return Encoding.UTF8.GetBytes(quantizedJson);
        }

        private T DecompressQuantized<T>(byte[] data)
        {
            string json = Encoding.UTF8.GetString(data);
            return JsonConvert.DeserializeObject<T>(json);
        }

        private void QuantizeValues(Dictionary<string, object> obj, int precision = 2)
        {
            foreach (var key in obj.Keys.ToList())
            {
                if (obj[key] is double || obj[key] is float)
                {
                    double value = Convert.ToDouble(obj[key]);
                    obj[key] = Math.Round(value, precision);
                }
                else if (obj[key] is Dictionary<string, object> nested)
                {
                    QuantizeValues(nested, precision);
                }
            }
        }

        #endregion

        /// <summary>
        /// Clear previous state for an entity
        /// </summary>
        public void ClearState(string entityId)
        {
            _previousStates.Remove(entityId);
        }

        /// <summary>
        /// Clear all previous states
        /// </summary>
        public void ClearAll()
        {
            _previousStates.Clear();
        }

        /// <summary>
        /// Get compression ratio estimate
        /// </summary>
        public static float GetCompressionRatio(byte[] original, byte[] compressed)
        {
            if (original.Length == 0) return 1.0f;
            return (float)compressed.Length / original.Length;
        }
    }
}
