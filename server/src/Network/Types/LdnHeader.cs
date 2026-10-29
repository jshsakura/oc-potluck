using System.Runtime.InteropServices;

namespace LanPlayServer.Network.Types
{
    [StructLayout(LayoutKind.Sequential, Size = 0xA)]
    public struct LdnHeader
    {
        public uint Magic;
        public byte Type;
        public byte Version;
        public int  DataSize;
    }
}
