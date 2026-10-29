using LanPlayServer.Network;
using LanPlayServer.Network.Types;
using Ryujinx.HLE.HOS.Services.Ldn.Types;
using System;
using System.Diagnostics;
using System.Runtime.InteropServices;

namespace LanPlayServer
{
    public partial class LdnSession
    {
        // A repeated scan with the same filter inside this window gets the previous result
        // instead of a new pass over every room. The handler never sleeps: that would stall
        // the session's other messages, relay data included.
        private const int ScanReuseMs = 200;

        private long        _lastScanTicks = long.MinValue;
        private ScanFilter  _lastScanFilter;
        private int         _lastScanCount;
        private string      _lastScanPassphrase;
        private HostedGame  _lastScanExclude;

        private void HandleScan(LdnHeader ldnPacket, ScanFilter filter)
        {
            if (!_initialized)
            {
                SendAsync(RyuLdnProtocol.Encode(PacketId.ScanReplyEnd));
                return;
            }

            bool sameFilter = MemoryMarshal.AsBytes(new ReadOnlySpan<ScanFilter>(in filter))
                .SequenceEqual(MemoryMarshal.AsBytes(new ReadOnlySpan<ScanFilter>(in _lastScanFilter)));

            // The result also depends on the channel and on the room this session is in.
            bool sameScan = sameFilter && _lastScanPassphrase == Passphrase && _lastScanExclude == CurrentGame;

            if (!sameScan || _lastScanTicks == long.MinValue || ElapsedMs(_lastScanTicks) >= ScanReuseMs)
            {
                _lastScanCount      = _tcpServer.Scan(ref _scanBuffer, filter, Passphrase, CurrentGame);
                _lastScanFilter     = filter;
                _lastScanPassphrase = Passphrase;
                _lastScanExclude    = CurrentGame;
                _lastScanTicks      = Stopwatch.GetTimestamp();
            }

            int games = _lastScanCount;

            for (int i = 0; i < games; i++)
            {
                NetworkInfo info = _scanBuffer[i];

                SendAsync(RyuLdnProtocol.Encode(PacketId.ScanReply, info));
            }

            SendAsync(RyuLdnProtocol.Encode(PacketId.ScanReplyEnd));
        }
    }
}