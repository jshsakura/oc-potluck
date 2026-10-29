using LanPlayServer.Network;
using LanPlayServer.Network.Types;
using LanPlayServer.Utils;
using NetCoreServer;
using Ryujinx.HLE.HOS.Services.Ldn.Types;
using System;
using System.Diagnostics;
using System.Net;
using System.Net.Sockets;
using System.Text.RegularExpressions;
using System.Threading;
using System.Threading.Tasks;

namespace LanPlayServer
{
    public partial class LdnSession : TcpSession
    {
        private const int ExternalProxyTimeout = 2;
        private const int InitializeDeadlineMs = 30000;
        // Bytes that may wait to go out to one client. A client that reads slower than its room
        // talks is closed when it passes this, instead of growing the server's memory without end.
        private const int MaxSendBacklog = 2 * 1024 * 1024;

        public HostedGame CurrentGame { get; set; }
        public Array6<byte> MacAddress { get; private set; }
        public uint IpAddress { get; private set; }
        public uint RealIpAddress { get; private set; }
        public string Passphrase { get; private set; } = "";

        public string StringId => Id.ToString().Replace("-", "");

        private readonly LdnServer _tcpServer;
        private readonly RyuLdnProtocol _protocol;
        private NetworkInfo[] _scanBuffer = new NetworkInfo[1];

        private readonly long _createdTicks = Stopwatch.GetTimestamp();
        private long _lastMessageTicks = Stopwatch.GetTimestamp();
        private int _waitingPingId = -1;
        private byte _pingId = 0;

        /// <summary>
        /// Node ID when in a game. This does not change while the user is still in that game.
        /// </summary>
        public int NodeId { get; set; }

        private bool _initialized = false;
        private bool _disconnected = false;
        private readonly object _connectionLock = new();

        private bool _connected = false;

        public LdnSession(LdnServer server) : base(server)
        {
            _tcpServer = server;
            OptionSendBufferLimit = MaxSendBacklog;

            MacAddress = new Array6<byte>();

            Random.Shared.NextBytes(MacAddress.AsSpan());

            _protocol = new RyuLdnProtocol();

            _protocol.Initialize += HandleInitialize;
            _protocol.Passphrase += HandlePassphrase;
            _protocol.CreateAccessPoint += HandleCreateAccessPoint;
            _protocol.CreateAccessPointPrivate += HandleCreateAccessPointPrivate;
            _protocol.Reject += HandleReject;
            _protocol.SetAcceptPolicy += HandleSetAcceptPolicy;
            _protocol.SetAdvertiseData += HandleSetAdvertiseData;
            _protocol.Scan += HandleScan;
            _protocol.Connect += HandleConnect;
            _protocol.ConnectPrivate += HandleConnectPrivate;
            _protocol.Disconnected += HandleDisconnect;

            _protocol.ProxyConnect += HandleProxyConnect;
            _protocol.ProxyConnectReply += HandleProxyConnectReply;
            _protocol.ProxyData += HandleProxyData;
            _protocol.ProxyDisconnect += HandleProxyDisconnect;

            _protocol.ExternalProxyState += HandleExternalProxyState;
            _protocol.Ping += HandlePing;
            _protocol.Faulted += _ => Disconnect();

            //_protocol.Any += HandleAny;
        }

        //private void HandleAny(LdnHeader obj)
        //{
        //    Console.WriteLine($"  ({PrintIp()}) -> {(PacketId)obj.Type}");
        //}

        #region Overrides

        protected override void OnConnected()
        {
            if (!_connected)
            {
                try
                {
                    RealIpAddress = GetSessionIp();
                    var ipToCheck = ((IPEndPoint)Socket.RemoteEndPoint).Address;
                    if (BanList.IsBanned(ipToCheck))
                    {
                        Console.WriteLine($"Banned IP tried to connect: {ipToCheck}");
                        Disconnect();
                        return;
                    }
                }
                catch
                {
                    Console.WriteLine("IP unavailable!");
                    // Already disconnected?
                }

                Console.WriteLine($"LDN TCP session with Id {Id} connected! ({PrintIp()})");

                _connected = true;
            }
        }

        protected override void OnDisconnected()
        {
            Task.Run(() =>
            {
                lock (_connectionLock)
                {
                    _disconnected = true;
                    DisconnectFromGame();
                }

                Console.WriteLine($"LDN TCP session with Id {Id} disconnected! ({PrintIp()})");

                _protocol.Dispose();
            });
        }

        protected override void OnReceived(byte[] buffer, long offset, long size)
        {
            try
            {
                OnConnected();

                // Only a finished message counts as activity, so a frame sent a byte
                // at a time cannot keep an idle session alive.
                if (_protocol.Read(buffer, (int)offset, (int)size) > 0)
                {
                    _lastMessageTicks = Stopwatch.GetTimestamp();
                }
            }
            catch (Exception e)
            {
                Console.WriteLine($"Closing session with Id {Id} due to exception: {e}");

                Disconnect();
            }
        }

        protected override void OnError(SocketError error)
        {
            if (error == SocketError.NoBufferSpaceAvailable)
            {
                // SendAsync refused a frame: the client is more than MaxSendBacklog behind
                if (Interlocked.Exchange(ref _droppedForBacklog, 1) == 0)
                {
                    Console.WriteLine($"Closing session with Id {Id}: it fell {MaxSendBacklog / 1024} KiB behind on what it was sent.");
                    Task.Run(Disconnect);
                }
                return;
            }

            Console.WriteLine($"LDN TCP session caught an error with code {error}");
        }

        private int _droppedForBacklog;

        #endregion

        #region Utilities

        private string PrintIp()
        {
            return
                $"{RealIpAddress >> 24}.{(RealIpAddress >> 16) & 0xFF}.{(RealIpAddress >> 8) & 0xFF}.{RealIpAddress & 0xFF}";
        }

        private static long ElapsedMs(long since)
        {
            return (Stopwatch.GetTimestamp() - since) / (Stopwatch.Frequency / 1000);
        }

        public void Ping()
        {
            if (!_initialized && ElapsedMs(_createdTicks) > InitializeDeadlineMs)
            {
                Console.WriteLine($"Closing session with Id {Id}: never initialized.");
                Task.Run(Disconnect);
            }
            else if (_waitingPingId != -1)
            {
                // The last ping was not responded to. Force a disconnect (async).
                Console.WriteLine($"Closing session with Id {Id} due to idle.");
                Task.Run(Disconnect);
            }
            else
            {
                if (ElapsedMs(_lastMessageTicks) > LdnServer.InactivityPingFrequency)
                {
                    byte pingId = _pingId++;

                    _waitingPingId = pingId;

                    SendAsync(RyuLdnProtocol.Encode(PacketId.Ping, new PingMessage { Id = pingId, Requester = 0 }));
                }
            }
        }

        private void DisconnectFromGame()
        {
            HostedGame game = CurrentGame;

            game?.Disconnect(this, false);

            CloseIfOwner(game);
        }

        /// <summary>
        /// Leaves the current game before the session joins another one, so it is never
        /// a member of two rooms. Runs synchronously: the old room must forget this
        /// session before its address changes.
        /// </summary>
        private void LeaveCurrentGame()
        {
            HostedGame game = CurrentGame;

            game?.Leave(this);

            CloseIfOwner(game);
        }

        private void CloseIfOwner(HostedGame game)
        {
            if (game?.Owner == this)
            {
                game.Closing = true;
                _tcpServer.CloseGame(game.Id);
            }
        }

        private uint GetSessionIp()
        {
            IPAddress remoteIp = ((IPEndPoint)Socket.RemoteEndPoint).Address;
            byte[] bytes = remoteIp.GetAddressBytes();

            Array.Reverse(bytes);

            return BitConverter.ToUInt32(bytes);
        }

        public bool SetIpV4(uint ip, uint subnet, bool internalProxy)
        {
            if (!_tcpServer.UseProxy)
                return false;

            IpAddress = ip;

            if (internalProxy)
            {
                ProxyConfig config = new() { ProxyIp = ip, ProxySubnetMask = subnet };

                // Tell the client about the proxy configuration.
                SendAsync(RyuLdnProtocol.Encode(PacketId.ProxyConfig, config));
            }

            return true;
        }

        private bool IsProxyReachable(ushort port)
        {
            // Attempt to establish a connection to the p2p server owned by the host.
            // We don't need to send anything, just establish a TCP connection.
            // If that is not possible, then their external proxy isn't reachable from the internet.

            IPEndPoint searchEndpoint;
            try
            {
                searchEndpoint = Socket.RemoteEndPoint as IPEndPoint;
            }
            catch
            {
                return false;
            }

            IPEndPoint ep = new(searchEndpoint.Address, port);

            using Socket probe = new(ep.AddressFamily, SocketType.Stream, ProtocolType.Tcp);

            try
            {
                // Wait on the connect itself instead of spinning on a flag.
                return probe.ConnectAsync(ep).Wait(TimeSpan.FromSeconds(ExternalProxyTimeout)) && probe.Connected;
            }
            catch (AggregateException)
            {
                return false;
            }
        }

        #endregion
    }
}