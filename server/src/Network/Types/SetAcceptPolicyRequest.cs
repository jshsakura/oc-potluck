using Ryujinx.HLE.HOS.Services.Ldn.Types;
using System.Runtime.InteropServices;

namespace LanPlayServer.Network.Types
{
    [StructLayout(LayoutKind.Sequential, Size = 0x1, Pack = 1)]
    public struct SetAcceptPolicyRequest
    {
        public AcceptPolicy StationAcceptPolicy;
    }
}
