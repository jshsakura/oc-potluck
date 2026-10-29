using Ryujinx.HLE.HOS.Services.Ldn.Types;
using System.Runtime.InteropServices;

namespace LanPlayServer.Network.Types
{
    [StructLayout(LayoutKind.Sequential, Size = 0x8)]
    public struct RejectRequest
    {
        public uint NodeId;
        public DisconnectReason DisconnectReason;

        public RejectRequest(DisconnectReason disconnectReason, uint nodeId)
        {
            DisconnectReason = disconnectReason;
            NodeId = nodeId;
        }
    }
}
