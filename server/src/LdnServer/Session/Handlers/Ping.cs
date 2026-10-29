using LanPlayServer.Network;
using LanPlayServer.Network.Types;

namespace LanPlayServer
{
    public partial class LdnSession
    {
        private void HandlePing(LdnHeader header, PingMessage ping)
        {
            if (ping.Requester == 0 && ping.Id == _waitingPingId)
            {
                // A response from this client. Still alive, reset the _waitingPingID. (getting the message will also reset the timer)
                _waitingPingId = -1;
            }
            else if (ping.Requester == 1)
            {
                // A client measuring its latency: send the ping straight back. Ryujinx clients
                // never send one, and the session's queue limit bounds a flood of them.
                SendAsync(RyuLdnProtocol.Encode(PacketId.Ping, ping));
            }
        }
    }
}