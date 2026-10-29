using System;
using System.Collections.Generic;

namespace LanPlayServer.Stats
{
    /// <summary>
    /// Limits for the status port, so it can face the internet without a proxy and a
    /// flood of viewers never slows the relay sharing its process. Thread-safe.
    /// </summary>
    public sealed class StatusGuard
    {
        public const int MaxSessions = 256;
        public const int MaxSessionsPerClient = 8;

        // A page visit is a handful of files, then one room list every ten seconds.
        private const double RequestsPerSecond = 5;
        private const double Burst = 40;

        // Clients remembered at once; idle ones are dropped first.
        private const int MaxClients = 4096;
        private const long ForgetAfterMs = 60_000;

        private sealed class Client
        {
            public int Sessions;
            public double Tokens = Burst;
            public long StampMs;
        }

        private readonly object _lock = new();
        private readonly Dictionary<string, Client> _clients = new(StringComparer.Ordinal);
        private readonly Func<long> _nowMs;
        private int _sessions;

        public StatusGuard(Func<long> nowMs = null)
        {
            _nowMs = nowMs ?? (() => Environment.TickCount64);
        }

        /// <summary>Admits a connection from <paramref name="client"/>, or refuses it when full.</summary>
        /// <param name="perClient">False behind a proxy, whose one address carries every viewer.</param>
        public bool TryOpen(string client, bool perClient)
        {
            lock (_lock)
            {
                if (_sessions >= MaxSessions)
                {
                    return false;
                }
                Client entry = Find(client);
                if (entry == null || (perClient && entry.Sessions >= MaxSessionsPerClient))
                {
                    return false;
                }
                entry.Sessions++;
                _sessions++;
                return true;
            }
        }

        public void Close(string client)
        {
            lock (_lock)
            {
                if (_clients.TryGetValue(client, out Client entry) && entry.Sessions > 0)
                {
                    entry.Sessions--;
                    _sessions--;
                }
            }
        }

        /// <summary>Spends one request from the client's budget; false means answer 429.</summary>
        public bool TryRequest(string client)
        {
            lock (_lock)
            {
                Client entry = Find(client);
                if (entry == null)
                {
                    return false;
                }
                long now = _nowMs();
                entry.Tokens = Math.Min(Burst, entry.Tokens + (now - entry.StampMs) * RequestsPerSecond / 1000.0);
                entry.StampMs = now;
                if (entry.Tokens < 1)
                {
                    return false;
                }
                entry.Tokens--;
                return true;
            }
        }

        public int Sessions
        {
            get { lock (_lock) { return _sessions; } }
        }

        private Client Find(string client)
        {
            if (_clients.TryGetValue(client, out Client entry))
            {
                return entry;
            }
            if (_clients.Count >= MaxClients)
            {
                Forget();
                if (_clients.Count >= MaxClients)
                {
                    return null;
                }
            }
            entry = new Client { StampMs = _nowMs() };
            _clients[client] = entry;
            return entry;
        }

        private void Forget()
        {
            long now = _nowMs();
            var idle = new List<string>();
            foreach (KeyValuePair<string, Client> pair in _clients)
            {
                if (pair.Value.Sessions == 0 && now - pair.Value.StampMs >= ForgetAfterMs)
                {
                    idle.Add(pair.Key);
                }
            }
            foreach (string key in idle)
            {
                _clients.Remove(key);
            }
        }
    }
}
