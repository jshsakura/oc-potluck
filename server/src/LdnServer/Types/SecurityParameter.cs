using LanPlayServer.Utils;
using System.Runtime.InteropServices;

namespace Ryujinx.HLE.HOS.Services.Ldn.Types
{
    [StructLayout(LayoutKind.Sequential, Size = 0x20, Pack = 1)]
    public struct SecurityParameter
    {
        public Array16<byte> Data;
        public Array16<byte> SessionId;
    }
}