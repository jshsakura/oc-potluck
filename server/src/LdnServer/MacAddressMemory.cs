using LanPlayServer.Utils;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;

namespace LanPlayServer
{
    /// <summary>
    /// Remembers the MAC address given to each session, so a client that reconnects
    /// with its old session ID gets the same one back. Entries expire, and the table
    /// never grows past a fixed size however many sessions come and go.
    /// </summary>
    public class MacAddressMemory
    {
        private const int  MaxRemembered    = 8192;
        private const long RetentionSeconds = 6 * 3600;

        private readonly Dictionary<string, (Array6<byte> Mac, long Ticks)> _idToAddress = new();
        // Several IDs share a MAC once a client reconnects, so count the holders of each.
        private readonly Dictionary<string, int> _macHolders = new();
        private readonly Random _random = new();
        private readonly object _lock = new();

        private Array6<byte> GetNewMac()
        {
            Array6<byte> mac = new();

            do
            {
                _random.NextBytes(mac.AsSpan());
            }
            while (_macHolders.ContainsKey(Convert.ToHexString(mac.AsSpan())));

            return mac;
        }

        private void Remember(string id, Array6<byte> mac, long now)
        {
            if (_idToAddress.Remove(id, out var previous))
            {
                Release(previous.Mac);
            }

            _idToAddress[id] = (mac, now);

            string key = Convert.ToHexString(mac.AsSpan());
            _macHolders[key] = _macHolders.GetValueOrDefault(key) + 1;
        }

        private void Release(Array6<byte> mac)
        {
            string key = Convert.ToHexString(mac.AsSpan());

            if (_macHolders.TryGetValue(key, out int holders) && holders > 1)
            {
                _macHolders[key] = holders - 1;
            }
            else
            {
                _macHolders.Remove(key);
            }
        }

        private void Prune(long now)
        {
            long cutoff = now - RetentionSeconds * Stopwatch.Frequency;

            List<string> expired = _idToAddress.Where(entry => entry.Value.Ticks < cutoff).Select(entry => entry.Key).ToList();

            if (_idToAddress.Count - expired.Count >= MaxRemembered)
            {
                // Still full: drop the oldest half as well.
                expired = _idToAddress.OrderBy(entry => entry.Value.Ticks).Take(MaxRemembered / 2).Select(entry => entry.Key).ToList();
            }

            foreach (string id in expired)
            {
                _idToAddress.Remove(id, out var entry);
                Release(entry.Mac);
            }
        }

        public Array6<byte> TryFind(string id, Span<byte> macAddress, string newId)
        {
            lock (_lock)
            {
                long now = Stopwatch.GetTimestamp();

                if (_idToAddress.Count >= MaxRemembered)
                {
                    Prune(now);
                }

                if (!_idToAddress.TryGetValue(id, out var known) || !known.Mac.AsSpan().SequenceEqual(macAddress))
                {
                    known.Mac = GetNewMac();
                }

                Remember(newId, known.Mac, now);

                return known.Mac;
            }
        }
    }
}
