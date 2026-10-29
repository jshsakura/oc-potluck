using System.Runtime.InteropServices;

namespace LanPlayServer.Network.Types
{
    [StructLayout(LayoutKind.Sequential, Size = 0x10)]
    public struct ProxyConnectResponse
    {
        public ProxyInfo Info;
    }
}
