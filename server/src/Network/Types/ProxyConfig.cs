using System.Runtime.InteropServices;

namespace LanPlayServer.Network.Types
{
    [StructLayout(LayoutKind.Sequential, Size = 0x8)]
    public struct ProxyConfig
    {
        public uint ProxyIp;
        public uint ProxySubnetMask;
    }
}