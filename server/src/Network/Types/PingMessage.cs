using System.Runtime.InteropServices;

namespace LanPlayServer.Network.Types
{
    [StructLayout(LayoutKind.Sequential, Size = 0x2)]
    public struct PingMessage
    {
        public byte Requester;
        public byte Id;
    }
}