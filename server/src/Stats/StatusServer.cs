using LanPlayServer.Stats.Types;
using LanPlayServer.Utils;
using NetCoreServer;
using System;
using System.Collections.Generic;
using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Threading;
using System.Text.Json.Serialization;

namespace LanPlayServer.Stats
{
    public sealed record RoomStatus(
        string Game,
        string TitleId,
        int Players,
        int MaxPlayers,
        bool IsPublic,
        bool IsJoinable,
        string[] Names,
        long CreatedAt); // Unix milliseconds, as HostedGame keeps it

    public sealed record ServerStatus(long UpdatedAt, int TotalRooms, int TotalPlayers, RoomStatus[] Rooms);

    [JsonSourceGenerationOptions(WriteIndented = false)]
    [JsonSerializable(typeof(ServerStatus))]
    internal partial class ServerStatusSerializerContext : JsonSerializerContext
    {
    }

    /// <summary>
    /// Read-only HTTP view of the open rooms. It answers /rooms, and with a page
    /// loaded also the page's files and /api/rooms, the path the page asks. It never
    /// names players in a room with a passphrase, and is meant to sit on a loopback
    /// port behind a reverse proxy that adds TLS.
    /// </summary>
    public sealed class StatusServer : HttpServer
    {
        public const string RoomsPath = "/rooms";
        public const string PageRoomsPath = "/api/rooms";

        // The page refreshes every few seconds; rebuilding at most this often keeps a
        // crowd of viewers from costing more than one viewer.
        private const int SnapshotMaxAgeMs = 2000;

        private static readonly ServerStatusSerializerContext SerializerContext = new(JsonHelper.GetDefaultSerializerOptions(false));

        private readonly Func<HostedGame[]> _games;
        private readonly object _snapshotLock = new();
        private byte[] _snapshot;
        private long _snapshotAt;

        // A request must arrive whole this soon, and a kept-alive connection is closed
        // after this much silence, so slow or idle connections cannot pile up.
        internal const long RequestTimeoutMs = 10_000;
        internal const long IdleTimeoutMs = 30_000;
        private const int SweepIntervalMs = 2_000;

        private readonly Timer _sweep;

        public StatusServer(IPAddress address, int port, Func<HostedGame[]> games, StatusPage page = null) : base(address, port)
        {
            _games = games;
            Page = page;
            _sweep = new Timer(_ => Sweep(), null, SweepIntervalMs, SweepIntervalMs);
        }

        public StatusGuard Guard { get; } = new();

        /// <summary>
        /// Loopback and private peers are a proxy on this host or the LAN: their
        /// X-Real-IP names the viewer, and one such peer may carry many viewers.
        /// A public peer is the viewer itself and its headers are never believed.
        /// </summary>
        internal static bool IsLocalPeer(IPAddress address)
        {
            if (address.IsIPv4MappedToIPv6)
            {
                address = address.MapToIPv4();
            }
            if (IPAddress.IsLoopback(address))
            {
                return true;
            }
            if (address.AddressFamily == AddressFamily.InterNetworkV6)
            {
                return address.IsIPv6UniqueLocal || address.IsIPv6LinkLocal;
            }
            byte[] b = address.GetAddressBytes();
            return b[0] == 10 || (b[0] == 172 && (b[1] & 0xF0) == 16) || (b[0] == 192 && b[1] == 168);
        }

        private void Sweep()
        {
            long now = Environment.TickCount64;
            foreach (TcpSession session in Sessions.Values)
            {
                if (session is StatusSession status && status.IsOverdue(now))
                {
                    session.Disconnect();
                }
            }
        }

        protected override void Dispose(bool disposingManagedResources)
        {
            if (disposingManagedResources)
            {
                _sweep.Dispose();
            }
            base.Dispose(disposingManagedResources);
        }

        /// <summary>The status page, or null when only the room list is served.</summary>
        public StatusPage Page { get; }

        internal byte[] Snapshot()
        {
            lock (_snapshotLock)
            {
                long now = Environment.TickCount64;

                if (_snapshot == null || now - _snapshotAt >= SnapshotMaxAgeMs)
                {
                    _snapshot = Encoding.UTF8.GetBytes(JsonHelper.Serialize(Build(_games()), SerializerContext.ServerStatus));
                    _snapshotAt = now;
                }

                return _snapshot;
            }
        }

        internal static ServerStatus Build(HostedGame[] games)
        {
            var rooms = new List<RoomStatus>(games.Length);
            int players = 0;

            foreach (HostedGame game in games)
            {
                GameAnalytics room = GameAnalytics.FromGame(game);

                if (game.Closing || room.PlayerCount <= 0)
                {
                    continue;
                }

                rooms.Add(new RoomStatus(
                    room.GameName,
                    room.TitleId,
                    room.PlayerCount,
                    room.MaxPlayerCount,
                    room.IsPublic,
                    room.Status == "Joinable",
                    room.IsPublic ? [.. room.Players] : [],
                    room.CreatedAt));
                players += room.PlayerCount;
            }

            rooms.Sort((a, b) => b.Players != a.Players ? b.Players.CompareTo(a.Players) : a.CreatedAt.CompareTo(b.CreatedAt));

            return new ServerStatus(DateTimeOffset.UtcNow.ToUnixTimeSeconds(), rooms.Count, players, [.. rooms]);
        }

        protected override TcpSession CreateSession() => new StatusSession(this);

        protected override void OnError(SocketError error) => Console.WriteLine($"Status HTTP server caught an error with code {error}");
    }

    internal sealed class StatusSession : HttpSession
    {
        // A GET needs no body; anything this long is not a request for this page.
        private const long MaxRequestBytes = 16 * 1024;

        private readonly StatusServer _server;
        private string _client = "";
        private bool _local;
        private bool _admitted;
        private long _pendingBytes;
        private long _requestStartMs;
        private long _lastActivityMs = Environment.TickCount64;

        public StatusSession(StatusServer server) : base(server)
        {
            _server = server;
        }

        internal bool IsOverdue(long now) =>
            (_pendingBytes > 0 && now - Interlocked.Read(ref _requestStartMs) > StatusServer.RequestTimeoutMs) ||
            now - Interlocked.Read(ref _lastActivityMs) > StatusServer.IdleTimeoutMs;

        protected override void OnConnected()
        {
            IPAddress peer = (Socket.RemoteEndPoint as IPEndPoint)?.Address;
            _client = peer?.ToString() ?? "";
            _local = peer != null && StatusServer.IsLocalPeer(peer);
            _admitted = _server.Guard.TryOpen(_client, perClient: !_local);
            if (!_admitted)
            {
                Disconnect();
            }
        }

        protected override void OnDisconnected()
        {
            if (_admitted)
            {
                _admitted = false;
                _server.Guard.Close(_client);
            }
        }

        protected override void OnReceived(byte[] buffer, long offset, long size)
        {
            long now = Environment.TickCount64;
            if (_pendingBytes == 0)
            {
                Interlocked.Exchange(ref _requestStartMs, now);
            }
            Interlocked.Exchange(ref _lastActivityMs, now);
            _pendingBytes += size;
            if (_pendingBytes > MaxRequestBytes)
            {
                Disconnect();
                return;
            }
            base.OnReceived(buffer, offset, size);
        }

        /// <summary>The viewer's address: a local proxy's X-Real-IP, else the peer.</summary>
        private string RateKey(HttpRequest request)
        {
            if (!_local)
            {
                return _client;
            }
            for (long i = 0; i < request.Headers; i++)
            {
                (string name, string value) = request.Header((int)i);
                if (name.Equals("X-Real-IP", StringComparison.OrdinalIgnoreCase) && IPAddress.TryParse(value.Trim(), out IPAddress address))
                {
                    return address.ToString();
                }
            }
            return _client;
        }

        // The page loads nothing from elsewhere but its fonts.
        private const string PagePolicy = "default-src 'none'; script-src 'self'; style-src 'self' https://cdn.jsdelivr.net https://fonts.googleapis.com; font-src 'self' https://cdn.jsdelivr.net https://fonts.gstatic.com; img-src 'self'; connect-src 'self'; base-uri 'none'; form-action 'none'; frame-ancestors 'none'";

        protected override void OnReceivedRequest(HttpRequest request)
        {
            _pendingBytes = 0;
            if (!_server.Guard.TryRequest(RateKey(request)))
            {
                Begin(429, "text/plain; charset=utf-8", "no-store");
                Response.SetHeader("Retry-After", "1");
                Response.SetBody("Too many requests");
                SendResponseAsync(Response);
                return;
            }

            if (request.Method != "GET")
            {
                SendResponseAsync(Response.MakeErrorResponse(405, "Method not allowed"));
                return;
            }

            if (request.Url == StatusServer.RoomsPath || (_server.Page != null && request.Url == StatusServer.PageRoomsPath))
            {
                Begin(200, "application/json; charset=utf-8", "no-store");
                Response.SetBody(_server.Snapshot());
                SendResponseAsync(Response);
                return;
            }

            if (_server.Page == null || !_server.Page.TryGet(request.Url, out StatusPage.PageFile file))
            {
                SendResponseAsync(Response.MakeErrorResponse(404, "Not found"));
                return;
            }

            // HTML is checked each visit so a new release shows at once; the rest is versioned by the page.
            Begin(200, file.ContentType, StatusPage.IsHtml(file) ? "no-cache" : "public, max-age=3600");
            Response.SetHeader("Content-Security-Policy", PagePolicy);
            Response.SetHeader("Referrer-Policy", "no-referrer");
            Response.SetHeader("Permissions-Policy", "camera=(), microphone=(), geolocation=()");
            Response.SetBody(file.Body);
            SendResponseAsync(Response);
        }

        private void Begin(int status, string contentType, string cacheControl)
        {
            Response.Clear();
            Response.SetBegin(status);
            Response.SetHeader("Content-Type", contentType);
            Response.SetHeader("Cache-Control", cacheControl);
            Response.SetHeader("X-Content-Type-Options", "nosniff");
        }

        protected override void OnReceivedRequestError(HttpRequest request, string error)
        {
            _pendingBytes = 0;
            Disconnect();
        }
    }
}
