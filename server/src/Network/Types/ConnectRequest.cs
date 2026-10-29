using Ryujinx.HLE.HOS.Services.Ldn.Types;
using System.Runtime.InteropServices;

namespace LanPlayServer.Network.Types
{
    [StructLayout(LayoutKind.Sequential, Size = 0x4FC)]
    public struct ConnectRequest
    {
        public SecurityConfig SecurityConfig;
        public UserConfig     UserConfig;
        public uint           LocalCommunicationVersion;
        public uint           OptionUnknown;
        public NetworkInfo    NetworkInfo;
    }
}