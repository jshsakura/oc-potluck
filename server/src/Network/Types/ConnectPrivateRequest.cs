using Ryujinx.HLE.HOS.Services.Ldn.Types;
using System.Runtime.InteropServices;

namespace LanPlayServer.Network.Types
{
    [StructLayout(LayoutKind.Sequential, Size = 0xBC)]
    public struct ConnectPrivateRequest
    {
        public SecurityConfig SecurityConfig;
        public SecurityParameter SecurityParameter;
        public UserConfig UserConfig;
        public uint LocalCommunicationVersion;
        public uint OptionUnknown;
        public NetworkConfig NetworkConfig;
    }
}