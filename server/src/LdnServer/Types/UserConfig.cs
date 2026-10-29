using LanPlayServer.Utils;
using System.Runtime.InteropServices;

namespace Ryujinx.HLE.HOS.Services.Ldn.Types
{
    [StructLayout(LayoutKind.Sequential, Size = 0x30, Pack = 1)]
    public struct UserConfig
    {
        public Array33<byte> UserName;
        public Array15<byte> Unknown1;
    }
}