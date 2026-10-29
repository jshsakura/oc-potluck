using System.Runtime.InteropServices;

namespace LanPlayServer.Network.Types
{
    public enum NetworkError : int
    {
        None,

        PortUnreachable,

        TooManyPlayers,
        VersionTooLow,
        VersionTooHigh,

        ConnectFailure,
        ConnectNotFound,
        ConnectTimeout,
        ConnectRejected,

        RejectFailed,

        BannedByServer = 127,

        Unknown = -1
    }

    [StructLayout(LayoutKind.Sequential, Size = 0x4)]
    public struct NetworkErrorMessage
    {
        public NetworkError Error;
    }
}