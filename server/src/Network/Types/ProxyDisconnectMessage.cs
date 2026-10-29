using System.Runtime.InteropServices;

namespace LanPlayServer.Network.Types
{
    [StructLayout(LayoutKind.Sequential, Size = 0x14)]
    public struct ProxyDisconnectMessage
    {
        public ProxyInfo Info;
        public int       DisconnectReason;
    }
}
