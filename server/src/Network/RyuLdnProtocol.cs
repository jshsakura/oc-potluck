using LanPlayServer.Network.Types;
using Ryujinx.HLE.HOS.Services.Ldn.Types;
using System;
using System.Buffers;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Channels;
using System.Threading.Tasks;

namespace LanPlayServer.Network
{
    class RyuLdnProtocol : IDisposable
    {
        private const byte CurrentProtocolVersion = 1;
        private const int  Magic                  = ('R' << 0) | ('L' << 8) | ('D' << 16) | ('N' << 24);
        private const int  MaxPacketSize          = 131072;

        // A session whose handlers fall this far behind its socket is flooding the server.
        private const long MaxPendingBytes    = 4 * 1024 * 1024;
        private const int  MaxPendingMessages = 4096;

        private readonly int _headerSize = Marshal.SizeOf<LdnHeader>();

        // Most frames fit here. A larger one borrows a pooled buffer only while it is assembled,
        // so an idle session holds 4 KiB instead of the full packet limit.
        private const int SmallBufferSize = 4096;

        private readonly byte[] _smallBuffer = new byte[SmallBufferSize];
        private byte[] _buffer;
        private int    _bufferEnd = 0;

        // Client Packets.
        public event Action<LdnHeader, InitializeMessage> Initialize;
        public event Action<LdnHeader, PassphraseMessage> Passphrase;
        public event Action<LdnHeader, NetworkInfo> Connected;
        public event Action<LdnHeader, NetworkInfo> SyncNetwork;
        public event Action<LdnHeader, NetworkInfo> ScanReply;
        public event Action<LdnHeader> ScanReplyEnd;
        public event Action<LdnHeader, DisconnectMessage> Disconnected;

        // External Proxy Packets.
        public event Action<LdnHeader, ExternalProxyConfig> ExternalProxy;
        public event Action<LdnHeader, ExternalProxyConnectionState> ExternalProxyState;
        public event Action<LdnHeader, ExternalProxyToken> ExternalProxyToken;

        // Server Packets.
        public event Action<LdnHeader, CreateAccessPointRequest, byte[]> CreateAccessPoint;
        public event Action<LdnHeader, CreateAccessPointPrivateRequest, byte[]> CreateAccessPointPrivate;
        public event Action<LdnHeader, RejectRequest> Reject;
        public event Action<LdnHeader> RejectReply;
        public event Action<LdnHeader, SetAcceptPolicyRequest> SetAcceptPolicy;
        public event Action<LdnHeader, byte[]> SetAdvertiseData;
        public event Action<LdnHeader, ConnectRequest> Connect;
        public event Action<LdnHeader, ConnectPrivateRequest> ConnectPrivate;
        public event Action<LdnHeader, ScanFilter> Scan;

        // Proxy Packets.
        public event Action<LdnHeader, ProxyConfig> ProxyConfig;
        public event Action<LdnHeader, ProxyConnectRequest> ProxyConnect;
        public event Action<LdnHeader, ProxyConnectResponse> ProxyConnectReply;
        public event Action<LdnHeader, ProxyDataHeader, ReadOnlyMemory<byte>> ProxyData;
        public event Action<LdnHeader, ProxyDisconnectMessage> ProxyDisconnect;

        // Lifecycle Packets.
        public event Action<LdnHeader, NetworkErrorMessage> NetworkError;
        public event Action<LdnHeader, PingMessage> Ping;

        public event Action<LdnHeader> Any;

        // Raised on the consumer thread when a handler throws, so the session can be closed.
        public event Action<Exception> Faulted;

        // Messages wait here in order and are handled one at a time on the thread pool,
        // so a session costs no thread of its own while it is quiet.
        private readonly Channel<(LdnHeader Header, byte[] Data)> _messages =
            Channel.CreateBounded<(LdnHeader, byte[])>(new BoundedChannelOptions(MaxPendingMessages)
            {
                SingleReader = true,
                SingleWriter = true,
                FullMode     = BoundedChannelFullMode.Wait,
            });

        private volatile bool _active = true;
        private long _pendingBytes;
        // Messages queued and not yet handled. Only Read queues, so when this is 0 nothing of
        // this session is ahead of the message Read has in hand.
        private int _unfinished;

        public RyuLdnProtocol()
        {
            _buffer   = _smallBuffer;
            _ = Task.Run(RunAsync);
        }

        private async Task RunAsync()
        {
            await foreach ((LdnHeader header, byte[] data) in _messages.Reader.ReadAllAsync())
            {
                Interlocked.Add(ref _pendingBytes, -data.Length);

                try
                {
                    if (_active)
                    {
                        DecodeAndHandle(header, data);
                    }
                }
                catch (Exception e)
                {
                    Fault(header, e);
                }
                finally
                {
                    Interlocked.Decrement(ref _unfinished);
                }
            }
        }

        // A malformed message: stop reading from this session rather than keep paying for it.
        private void Fault(LdnHeader header, Exception e)
        {
            Console.WriteLine($"Closing session after malformed {(PacketId)header.Type}: {e.Message}");

            _active = false;
            Faulted?.Invoke(e);
        }

        /// <summary>
        /// Game data with nothing of its session waiting: relay it on the receiving thread, straight
        /// from the receive buffer. It skips the queue and a copy, and the order stays as sent,
        /// because no earlier message of this session is still unhandled.
        /// </summary>
        private void HandleProxyDataNow(LdnHeader header)
        {
            ReadOnlyMemory<byte> message = new(_buffer, _headerSize, header.DataSize);

            try
            {
                Any?.Invoke(header);
                ProxyDataHeader packet = MemoryMarshal.Read<ProxyDataHeader>(message.Span);
                ProxyData?.Invoke(header, packet, message.Slice(WireSize<ProxyDataHeader>.Value));
            }
            catch (Exception e)
            {
                Fault(header, e);
            }
        }

        private void Reset()
        {
            _bufferEnd = 0;

            if (_buffer != _smallBuffer)
            {
                ArrayPool<byte>.Shared.Return(_buffer);
                _buffer = _smallBuffer;
            }
        }

        private void EnsureCapacity(int size)
        {
            if (size <= _buffer.Length)
            {
                return;
            }

            byte[] larger = ArrayPool<byte>.Shared.Rent(size);
            Array.Copy(_buffer, larger, _bufferEnd);

            if (_buffer != _smallBuffer)
            {
                ArrayPool<byte>.Shared.Return(_buffer);
            }

            _buffer = larger;
        }

        /// <returns>The number of complete messages the data finished.</returns>
        public int Read(byte[] data, int offset, int size)
        {
            int index    = 0;
            int complete = 0;

            while (index < size)
            {
                if (_bufferEnd < _headerSize)
                {
                    // Assemble the header first.

                    int copyable = Math.Min(size - index, Math.Min(size, _headerSize - _bufferEnd));

                    Array.Copy(data, index + offset, _buffer, _bufferEnd, copyable);

                    index      += copyable;
                    _bufferEnd += copyable;
                }

                if (_bufferEnd >= _headerSize)
                {
                    // The header is available. Make sure we received all the data (size specified in the header)

                    LdnHeader ldnHeader = MemoryMarshal.Read<LdnHeader>(_buffer);

                    if (ldnHeader.Magic != Magic)
                    {
                        throw new InvalidOperationException("Invalid magic number in received packet.");
                    }

                    if (ldnHeader.Version != CurrentProtocolVersion)
                    {
                        throw new InvalidOperationException($"Protocol version mismatch. Expected ${CurrentProtocolVersion}, was ${ldnHeader.Version}.");
                    }

                    if (ldnHeader.DataSize < 0 || ldnHeader.DataSize >= MaxPacketSize - _headerSize)
                    {
                        throw new InvalidOperationException($"Max packet size { MaxPacketSize } exceeded.");
                    }

                    int finalSize = _headerSize + ldnHeader.DataSize;

                    EnsureCapacity(finalSize);

                    int copyable = Math.Min(size - index, Math.Min(size, finalSize - _bufferEnd));

                    Array.Copy(data, index + offset, _buffer, _bufferEnd, copyable);

                    index      += copyable;
                    _bufferEnd += copyable;

                    if (finalSize == _bufferEnd)
                    {
                        // The full packet has been retrieved. Game data goes straight out when
                        // nothing is ahead of it; everything else is decoded on the session's queue.

                        if ((PacketId)ldnHeader.Type == PacketId.ProxyData && _active && Volatile.Read(ref _unfinished) == 0)
                        {
                            HandleProxyDataNow(ldnHeader);
                        }
                        else
                        {
                            byte[] ldnData = new byte[ldnHeader.DataSize];

                            Array.Copy(_buffer, _headerSize, ldnData, 0, ldnData.Length);

                            DecodeAndHandleDeferred(ldnHeader, ldnData);
                        }

                        Reset();
                        complete++;
                    }
                }
            }

            return complete;
        }

        private static T ParseDefault<T>(byte[] data) where T : struct
        {
            return MemoryMarshal.Read<T>(data);
        }

        private static (T, byte[]) ParseWithData<T>(byte[] data) where T: struct
        {
            int size = Marshal.SizeOf<T>();

            byte[] remainder = new byte[data.Length - size];

            if (remainder.Length > 0)
            {
                Array.Copy(data, size, remainder, 0, remainder.Length);
            }

            return (ParseDefault<T>(data), remainder);
        }

        private void DecodeAndHandleDeferred(LdnHeader header, byte[] data)
        {
            Interlocked.Increment(ref _unfinished);

            if (Interlocked.Add(ref _pendingBytes, data.Length) > MaxPendingBytes ||
                !_messages.Writer.TryWrite((header, data)))
            {
                throw new InvalidOperationException("Receive queue limit exceeded.");
            }
        }

        private void DecodeAndHandle(LdnHeader header, byte[] data)
        {
            Any?.Invoke(header);

            switch ((PacketId)header.Type)
            {
                // Client Packets.
                case PacketId.Initialize:
                    {
                        Initialize?.Invoke(header, ParseDefault<InitializeMessage>(data));

                        break;
                    }
                case PacketId.Passphrase:
                    {
                        Passphrase?.Invoke(header, ParseDefault<PassphraseMessage>(data));

                        break;
                    }
                case PacketId.Connected:
                    {
                        Connected?.Invoke(header, ParseDefault<NetworkInfo>(data));

                        break;
                    }
                case PacketId.SyncNetwork:
                    {
                        SyncNetwork?.Invoke(header, ParseDefault<NetworkInfo>(data));

                        break;
                    }
                case PacketId.ScanReply:
                    {
                        ScanReply?.Invoke(header, ParseDefault<NetworkInfo>(data));

                        break;
                    }

                case PacketId.ScanReplyEnd:
                    {
                        ScanReplyEnd?.Invoke(header);

                        break;
                    }
                case PacketId.Disconnect:
                    {
                        Disconnected?.Invoke(header, ParseDefault<DisconnectMessage>(data));

                        break;
                    }

                // External Proxy Packets.
                case PacketId.ExternalProxy:
                    {
                        ExternalProxy?.Invoke(header, ParseDefault<ExternalProxyConfig>(data));

                        break;
                    }
                case PacketId.ExternalProxyState:
                    {
                        ExternalProxyState?.Invoke(header, ParseDefault<ExternalProxyConnectionState>(data));

                        break;
                    }
                case PacketId.ExternalProxyToken:
                    {
                        ExternalProxyToken?.Invoke(header, ParseDefault<ExternalProxyToken>(data));

                        break;
                    }

                // Server Packets.
                case PacketId.CreateAccessPoint:
                    {
                        (CreateAccessPointRequest packet, byte[] extraData) = ParseWithData<CreateAccessPointRequest>(data);
                        CreateAccessPoint?.Invoke(header, packet, extraData);
                        break;
                    }
                case PacketId.CreateAccessPointPrivate:
                    {
                        (CreateAccessPointPrivateRequest packet, byte[] extraData) = ParseWithData<CreateAccessPointPrivateRequest>(data);
                        CreateAccessPointPrivate?.Invoke(header, packet, extraData);
                        break;
                    }
                case PacketId.Reject:
                    {
                        Reject?.Invoke(header, ParseDefault<RejectRequest>(data));

                        break;
                    }
                case PacketId.RejectReply:
                    {
                        RejectReply?.Invoke(header);

                        break;
                    }
                case PacketId.SetAcceptPolicy:
                    {
                        SetAcceptPolicy?.Invoke(header, ParseDefault<SetAcceptPolicyRequest>(data));

                        break;
                    }
                case PacketId.SetAdvertiseData:
                    {
                        SetAdvertiseData?.Invoke(header, data);

                        break;
                    }
                case PacketId.Connect:
                    {
                        Connect?.Invoke(header, ParseDefault<ConnectRequest>(data));

                        break;
                    }
                case PacketId.ConnectPrivate:
                    {
                        ConnectPrivate?.Invoke(header, ParseDefault<ConnectPrivateRequest>(data));

                        break;
                    }
                case PacketId.Scan:
                    {
                        Scan?.Invoke(header, ParseDefault<ScanFilter>(data));

                        break;
                    }

                // Proxy Packets
                case PacketId.ProxyConfig:
                    {
                        ProxyConfig?.Invoke(header, ParseDefault<ProxyConfig>(data));

                        break;
                    }
                case PacketId.ProxyConnect:
                    {
                        ProxyConnect?.Invoke(header, ParseDefault<ProxyConnectRequest>(data));

                        break;
                    }
                case PacketId.ProxyConnectReply:
                    {
                        ProxyConnectReply?.Invoke(header, ParseDefault<ProxyConnectResponse>(data));

                        break;
                    }
                case PacketId.ProxyData:
                    {
                        // The payload is handed on as a view of the message, not copied out of it
                        ProxyDataHeader packet = ParseDefault<ProxyDataHeader>(data);

                        ProxyData?.Invoke(header, packet, data.AsMemory(WireSize<ProxyDataHeader>.Value));

                        break;
                    }
                case PacketId.ProxyDisconnect:
                    {
                        ProxyDisconnect?.Invoke(header, ParseDefault<ProxyDisconnectMessage>(data));

                        break;
                    }

                // Lifecycle Packets.
                case PacketId.Ping:
                    {
                        Ping?.Invoke(header, ParseDefault<PingMessage>(data));

                        break;
                    }
                case PacketId.NetworkError:
                    {
                        NetworkError?.Invoke(header, ParseDefault<NetworkErrorMessage>(data));

                        break;
                    }
            }
        }

        private static LdnHeader GetHeader(PacketId type, int dataSize)
        {
            return new LdnHeader()
            {
                Magic    = Magic,
                Version  = CurrentProtocolVersion,
                Type     = (byte)type,
                DataSize = dataSize
            };
        }

        // Sizes of the wire structs, worked out once instead of on every packet
        private static class WireSize<T> where T : struct
        {
            public static readonly int Value = Marshal.SizeOf<T>();
        }

        private static readonly int HeaderSize = WireSize<LdnHeader>.Value;

        public static byte[] Encode(PacketId type)
        {
            LdnHeader header = GetHeader(type, 0);

            byte[] result = new byte[HeaderSize];

            MemoryMarshal.Write(result, in header);

            return result;
        }

        // One allocation per frame: the header and the struct are written straight into it.
        public static byte[] Encode<T>(PacketId type, T packet) where T: struct
        {
            int size = WireSize<T>.Value;
            LdnHeader header = GetHeader(type, size);

            byte[] result = new byte[HeaderSize + size];
            MemoryMarshal.Write(result, in header);
            MemoryMarshal.Write(result.AsSpan(HeaderSize), in packet);

            return result;
        }

        public static byte[] Encode<T>(PacketId type, T packet, byte[] data) where T: struct
        {
            return Encode(type, packet, new ReadOnlySpan<byte>(data));
        }

        public static byte[] Encode<T>(PacketId type, T packet, ReadOnlySpan<byte> data) where T: struct
        {
            int size = WireSize<T>.Value;
            LdnHeader header = GetHeader(type, size + data.Length);

            byte[] result = new byte[HeaderSize + size + data.Length];
            MemoryMarshal.Write(result, in header);
            MemoryMarshal.Write(result.AsSpan(HeaderSize), in packet);
            data.CopyTo(result.AsSpan(HeaderSize + size));

            return result;
        }

        public void Dispose()
        {
            // No wait for the consumer: blocking a pool thread on another pool task starves the
            // pool when many sessions drop at once. Completing the writer ends it on its own.
            _active = false;
            _messages.Writer.TryComplete();
        }
    }
}