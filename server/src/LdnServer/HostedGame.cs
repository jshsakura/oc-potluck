using LanPlayServer.Network;
using LanPlayServer.Network.Types;
using LanPlayServer.Stats;
using LanPlayServer.Utils;
using Ryujinx.HLE.HOS.Services.Ldn.Types;
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Linq;
using System.Net;
using System.Runtime.CompilerServices;
using System.Text;
using System.Threading.Tasks;

namespace LanPlayServer
{
    enum GameLockReason
    {
        None,
        UpdateNetworkInfo,
        SetOwner,
        Connect,
        ConnectNode,
        ConnectBroadcast,
        ConnectProxy,
        ConnectFinal,
        Reject,
        SetAcceptPolicy,
        SetAdvertiseData,
        HandleExternalProxyState,
        Disconnect,
        DisconnectDHCP,
        DisconnectInfoRemove,
        DisconnectBroadcast,
        Close,
        RoutingMessage,
        BroadcastNetworkInfo,
    }

    public class HostedGame : INotifyPropertyChanged
    {
        public event PropertyChangedEventHandler PropertyChanged;

        private const uint NetworkBaseAddress = 0x0a720000; // 10.114.0.0 (our "virtual network")
        private const uint NetworkSubnetMask  = 0xffff0000; // 255.255.0.0

        public const int MaxNodes           = 8;
        public const int MaxAdvertiseData   = 0x180;
        private const int MaxProxyDataSize  = 0xffff; // The largest UDP payload a game can hand to a socket.

        private readonly object _lock;
        private GameLockReason _lockReason;
        private readonly List<LdnSession> _players;
        // Copy of _players for the relay path, replaced under _lock whenever membership changes.
        private volatile LdnSession[] _playerSnapshot = [];
        private readonly VirtualDhcp      _dhcp;

        private bool                _closed;
        private ExternalProxyConfig _externalConfig;
        private ExternalProxyConfig _privateConfig;

        private NetworkInfo _info;
        public NetworkInfo Info
        {
            get
            {
                return _info;
            }
        }

        public string Id { get; }
        public long CreatedAt { get; }

        public bool Closing { get; set; }

        public LdnSession Owner { get; private set; }
        public string OwnerId => Owner.StringId;

        public string Passphrase { get; private set; }

        private string _gameVersion;
        private bool _isP2P;

        public string GameVersion
        {
            get
            {
                return _gameVersion;
            }
            private set
            {
                if (_gameVersion != value)
                {
                    _gameVersion = value;
                    NotifyPropertyChanged();
                }
            }
        }

        public int Players
        {
            get
            {
                return _players.Count;
            }
        }

        public bool IsP2P
        {
            get => _isP2P;
            private set
            {
                if (_isP2P != value)
                {
                    _isP2P = value;
                    NotifyPropertyChanged();
                }
            }
        }

        public HostedGame(string id, NetworkInfo info, AddressList dhcpConfig)
        {
            Id = id;
            CreatedAt = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();

            _lock = new object();
            _players = [];
            _dhcp = new VirtualDhcp(NetworkBaseAddress, NetworkSubnetMask, dhcpConfig);

            UpdateNetworkInfo(info);
        }

        public void UpdateNetworkInfo(NetworkInfo info)
        {
            lock (_lock)
            {
                _lockReason = GameLockReason.UpdateNetworkInfo;

                _info = info;

                NotifyPropertyChanged(nameof(Info));

                _lockReason = GameLockReason.None;
            }
        }

        private byte[] AddressTo16Byte(IPAddress address)
        {
            byte[] ipBytes  = new byte[16];
            byte[] srcBytes = address.GetAddressBytes();

            Array.Copy(srcBytes, 0, ipBytes, 0, srcBytes.Length);

            return ipBytes;
        }

        public void SetOwner(LdnSession session, RyuNetworkConfig request)
        {
            lock (_lock)
            {
                _lockReason = GameLockReason.SetOwner;

                try
                {
                    Owner = session;
                    Passphrase = session.Passphrase;

                    GameVersion = Encoding.UTF8.GetString(request.GameVersion.AsSpan().ToArray(), 0, request.GameVersion.Length).Trim('\0');

                    if (request.ExternalProxyPort != 0)
                    {
                        IsP2P = true;

                        IPAddress address = (session.Socket.RemoteEndPoint as IPEndPoint).Address;

                        _externalConfig = new ExternalProxyConfig()
                        {
                            AddressFamily = address.AddressFamily,
                            ProxyPort = request.ExternalProxyPort,
                        };

                        AddressTo16Byte(address).CopyTo(_externalConfig.ProxyIp.AsSpan());

                        _privateConfig = new ExternalProxyConfig()
                        {
                            ProxyIp = request.PrivateIp,
                            AddressFamily = request.AddressFamily,
                            ProxyPort = request.InternalProxyPort
                        };
                    }
                }
                finally
                {
                    _lockReason = GameLockReason.None;
                }
            }
        }

        private void InitExternalProxy(LdnSession session)
        {
            IPAddress  address          = (session.Socket.RemoteEndPoint as IPEndPoint).Address;
            Span<byte> addressBytes     = AddressTo16Byte(address).AsSpan();
            bool       sessionIsPrivate = address.AddressFamily == _externalConfig.AddressFamily && addressBytes.SequenceEqual(_externalConfig.ProxyIp.AsSpan());
            byte[]     token            = Convert.FromHexString(Guid.NewGuid().ToString().Replace("-", ""));

            // The proxy host needs to know about the new joiner.

            ExternalProxyToken tokenMsg = new()
            {
                VirtualIp     = session.IpAddress,
                AddressFamily = address.AddressFamily
            };

            token.CopyTo(tokenMsg.Token.AsSpan());

            if (!sessionIsPrivate)
            {
                addressBytes.CopyTo(tokenMsg.PhysicalIp.AsSpan());
            }

            Owner.SendAsync(RyuLdnProtocol.Encode(PacketId.ExternalProxyToken, tokenMsg));

            // Tell the joiner about the new proxy host.

            ExternalProxyConfig configCopy = sessionIsPrivate ? _privateConfig : _externalConfig;

            token.CopyTo(configCopy.Token.AsSpan());

            session.SendAsync(RyuLdnProtocol.Encode(PacketId.ExternalProxy, configCopy));
        }

        public bool Connect(LdnSession session, NodeInfo node)
        {
            lock (_lock)
            {
                try
                {
                    _lockReason = GameLockReason.Connect;

                    if (_closed || _players.Contains(session) || _info.Ldn.NodeCount >= _info.Ldn.NodeCountMax)
                    {
                        return false;
                    }

                    // Find the slot before touching anything, so a full room changes nothing.
                    int nodeId = LocateEmptyNode();

                    if (nodeId < 0)
                    {
                        return false;
                    }

                    uint ip = _dhcp.RequestIpV4(session.MacAddress.AsSpan());
                    if (!session.SetIpV4(ip, NetworkSubnetMask, !IsP2P))
                    {
                        _dhcp.ReturnIpV4(ip);
                    }

                    node.Ipv4Address = session.IpAddress;

                    _lockReason = GameLockReason.ConnectNode;

                    // Add the client to the network info.
                    _info.Ldn.NodeCount++;

                    node.NodeId = (byte)nodeId;
                    session.NodeId = nodeId;

                    _info.Ldn.Nodes[nodeId] = node;

                    _lockReason = GameLockReason.ConnectProxy;
                    if (IsP2P)
                    {
                        InitExternalProxy(session);
                    }

                    _lockReason = GameLockReason.ConnectBroadcast;
                    BroadcastNetworkInfoInLock();

                    session.CurrentGame = this;

                    _players.Add(session);
                    _playerSnapshot = _players.ToArray();
                    NotifyPropertyChanged(nameof(Players));

                    _lockReason = GameLockReason.ConnectFinal;
                    session.SendAsync(RyuLdnProtocol.Encode(PacketId.Connected, _info));

                }
                finally
                {
                    _lockReason = GameLockReason.None;
                }
            }

            return true;
        }

        public void RemoveFromInfo(uint ip)
        {
            ref LdnNetworkInfo ldn     = ref _info.Ldn;
            bool               removed = false;

            // Slots are not compacted, so a member can sit past NodeCount: check them all.
            for (int i = 0; i < ldn.Nodes.Length; i++)
            {
                if (ldn.Nodes[i].IsConnected != 0 && ldn.Nodes[i].Ipv4Address == ip)
                {
                    ldn.NodeCount--;
                    ldn.Nodes[i].IsConnected = 0;
                    removed = true;
                }

                /*
                if (removed)
                {
                    // Move the next client into this space. Keep doing this repeatedly.
                    if (i + 1 < ldn.NodeCountMax)
                    {
                        ldn.Nodes[i] = ldn.Nodes[i + 1];
                    }
                    else
                    {
                        ldn.Nodes[i] = new NodeInfo();
                    }
                }
                */
            }
        }

        private int LocateEmptyNode()
        {
            Array8<NodeInfo> nodes = Info.Ldn.Nodes;

            for (int i = 0; i < nodes.Length; i++)
            {
                if (nodes[i].IsConnected == 0)
                {
                    return i;
                }
            }

            return -1;
        }

        // Proxy handlers

        private void RouteMessage(LdnSession sender, ref ProxyInfo info, Action<LdnSession> action)
        {
            if (info.SourceIpV4 == 0)
            {
                // If they sent from a connection bound on 0.0.0.0, make others see it as them.
                info.SourceIpV4 = sender.IpAddress;
            }
            else if (info.SourceIpV4 != sender.IpAddress)
            {
                // Can't pretend to be somebody else.
                return;
            }

            uint destIp = info.DestIpV4;

            if (destIp == 0xc0a800ff)
            {
                destIp = _dhcp.BroadcastAddress;
            }

            bool isBroadcast = destIp == _dhcp.BroadcastAddress;



            LdnSession[] players = _playerSnapshot;

            if (Array.IndexOf(players, sender) < 0)
            {
                // Only members of this room may send into it.
                return;
            }

            if (isBroadcast)
            {
                foreach (LdnSession player in players)
                {
                    action(player);
                }
            }
            else
            {
                LdnSession target = Array.Find(players, player => player.IpAddress == destIp);

                if (target != null)
                {
                    action(target);
                }
            }
        }

        public void HandleReject(LdnSession sender, LdnHeader header, RejectRequest reject)
        {
            if (sender == Owner)
            {
                lock (_lock)
                {
                    _lockReason = GameLockReason.Reject;

                    // Node slots are not compacted, so look the member up by its node ID rather than
                    // comparing the ID with the head count. The host cannot reject itself.
                    LdnSession target = _players.FirstOrDefault(player => player.NodeId == reject.NodeId && player != Owner);

                    if (target == null)
                    {
                        sender.SendAsync(RyuLdnProtocol.Encode(PacketId.NetworkError, new NetworkErrorMessage() { Error = NetworkError.RejectFailed }));
                    }
                    else
                    {
                        Disconnect(target, false);
                    }

                    _lockReason = GameLockReason.None;
                }
            }
            else
            {
                sender.SendAsync(RyuLdnProtocol.Encode(PacketId.NetworkError, new NetworkErrorMessage() { Error = NetworkError.RejectFailed }));
            }

            sender.SendAsync(RyuLdnProtocol.Encode(PacketId.RejectReply));
        }

        public void HandleSetAcceptPolicy(LdnSession sender, LdnHeader header, SetAcceptPolicyRequest policy)
        {
            if (sender == Owner)
            {
                lock (_lock)
                {
                    _lockReason = GameLockReason.SetAcceptPolicy;

                    _info.Ldn.StationAcceptPolicy = (byte)policy.StationAcceptPolicy;

                    BroadcastNetworkInfoInLock();

                    _lockReason = GameLockReason.None;
                }
            }
        }

        public void HandleSetAdvertiseData(LdnSession sender, LdnHeader header, byte[] data)
        {
            if (sender == Owner)
            {
                lock (_lock)
                {
                    _lockReason = GameLockReason.SetAdvertiseData;

                    if (data.Length > MaxAdvertiseData)
                    {
                        // The size field would describe bytes the network info cannot hold.
                        _lockReason = GameLockReason.None;
                        return;
                    }

                    _info.Ldn.AdvertiseDataSize = (ushort)data.Length;

                    Array.Resize(ref data, MaxAdvertiseData);

                    data.CopyTo(_info.Ldn.AdvertiseData.AsSpan());

                    BroadcastNetworkInfoInLock();

                    _lockReason = GameLockReason.None;
                }
            }
        }

        public void HandleExternalProxyState(LdnSession sender, LdnHeader header, ExternalProxyConnectionState state)
        {
            lock (_lock)
            {
                _lockReason = GameLockReason.HandleExternalProxyState;

                if (sender != Owner)
                {
                    // Only the owner can update external proxy state.
                    _lockReason = GameLockReason.None;
                    return;
                }

                LdnSession player = _players.FirstOrDefault(player => player.IpAddress == state.IpAddress);

                if (player != null)
                {
                    if (!state.Connected)
                    {
                        // Indicate that the player is no longer connected to the game.

                        player.SendAsync(RyuLdnProtocol.Encode(PacketId.Disconnect, new DisconnectMessage()));

                        Disconnect(player, true);
                    }
                }
                _lockReason = GameLockReason.None;
            }
        }

        // Each handler encodes its frame once, after RouteMessage has fixed the source
        // address, and sends that same frame to every target. SendAsync copies it.

        public void HandleProxyDisconnect(LdnSession sender, LdnHeader header, ProxyDisconnectMessage message)
        {
            byte[] frame = null;

            RouteMessage(sender, ref message.Info, (target) =>
            {
                target.SendAsync(frame ??= RyuLdnProtocol.Encode(PacketId.ProxyDisconnect, message));
            });
        }

        public void HandleProxyData(LdnSession sender, LdnHeader header, ProxyDataHeader message, ReadOnlyMemory<byte> data)
        {
            if (message.DataLength != data.Length || data.Length > MaxProxyDataSize)
            {
                // The length field must describe the payload, or receivers misread the stream.
                return;
            }

            byte[] frame = null;

            RouteMessage(sender, ref message.Info, (target) =>
            {
                target.SendAsync(frame ??= RyuLdnProtocol.Encode(PacketId.ProxyData, message, data.Span));
            });
        }

        public void HandleProxyConnectReply(LdnSession sender, LdnHeader header, ProxyConnectResponse message)
        {
            byte[] frame = null;

            RouteMessage(sender, ref message.Info, (target) =>
            {
                target.SendAsync(frame ??= RyuLdnProtocol.Encode(PacketId.ProxyConnectReply, message));
            });
        }

        public void HandleProxyConnect(LdnSession sender, LdnHeader header, ProxyConnectRequest message)
        {
            byte[] frame = null;

            RouteMessage(sender, ref message.Info, (target) =>
            {
                target.SendAsync(frame ??= RyuLdnProtocol.Encode(PacketId.ProxyConnect, message));
            });
        }

        // ip is the address the session had in this room. It is captured by the caller
        // because the session may already be joining another room by the time this runs.
        private void DisconnectInternal(LdnSession session, uint ip, bool expected)
        {
            lock (_lock)
            {
                _lockReason = GameLockReason.Disconnect;

                try
                {
                    if (!_players.Remove(session))
                    {
                        // Already gone: never return its address or node twice.
                        return;
                    }

                    _playerSnapshot = _players.ToArray();
                    NotifyPropertyChanged(nameof(Players));

                    if (session.CurrentGame == this)
                    {
                        session.CurrentGame = null;
                    }

                    if (IsP2P && !expected)
                    {
                        Owner?.SendAsync(RyuLdnProtocol.Encode(PacketId.ExternalProxyState, new ExternalProxyConnectionState
                        {
                            IpAddress = ip,
                            Connected = false
                        }));
                    }

                    _lockReason = GameLockReason.DisconnectDHCP;

                    _dhcp.ReturnIpV4(ip);

                    _lockReason = GameLockReason.DisconnectInfoRemove;

                    // Remove the client from the network info.
                    RemoveFromInfo(ip);

                    _lockReason = GameLockReason.DisconnectBroadcast;

                    BroadcastNetworkInfoInLock();
                }
                catch (Exception e)
                {
                    Console.WriteLine("SUPER FATAL ERROR: " + e);
                }
                finally
                {
                    _lockReason = GameLockReason.None;
                }
            }
        }

        public void Disconnect(LdnSession session, bool expected)
        {
            if (session == null)
            {
                return;
            }

            uint ip = session.IpAddress;

            if (session.CurrentGame == this)
            {
                session.CurrentGame = null;
            }

            Task.Run(() => DisconnectInternal(session, ip, expected));
        }

        /// <summary>
        /// Removes the session now, for a session that is about to join another room.
        /// The caller must not hold another game's lock.
        /// </summary>
        public void Leave(LdnSession session)
        {
            if (session.CurrentGame == this)
            {
                session.CurrentGame = null;
            }

            DisconnectInternal(session, session.IpAddress, false);
        }

        // NOTE: Unused.
        private void BroadcastNetworkInfo()
        {
            lock (_lock)
            {
                _lockReason = GameLockReason.BroadcastNetworkInfo;
                BroadcastNetworkInfoInLock();
            }
        }

        private void BroadcastNetworkInfoInLock()
        {
            BroadcastInLock(RyuLdnProtocol.Encode(PacketId.SyncNetwork, _info));
        }

        private void BroadcastInLock(byte[] buffer)
        {
            foreach (LdnSession player in _players)
            {
                player.SendAsync(buffer);
            }
        }

        public void Close()
        {
            if (_closed)
            {
                return;
            }
            Closing = true;
            Task.Run(CloseInternal);
        }

        private void CloseInternal()
        {
            if (_closed)
            {
                return;
            }
            lock (_lock)
            {
                if (!_closed)
                {
                    _lockReason = GameLockReason.Close;

                    Console.WriteLine($"CLOSING: {Id}");

                    _closed = true;

                    BroadcastInLock(RyuLdnProtocol.Encode(PacketId.Disconnect, new DisconnectMessage()));

                    _lockReason = GameLockReason.None;
                }
            }
            Statistics.RemoveGameAnalytics(this);
        }

        private void NotifyPropertyChanged([CallerMemberName] string propertyName = "")
        {
            PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));
        }

        public void TestReadLock()
        {
            if (_lockReason != GameLockReason.None)
            {
                Console.WriteLine($"{Id} locked: {_lockReason}");
            }
        }
    }
}