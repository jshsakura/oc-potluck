namespace Ryujinx.HLE.HOS.Services.Ldn.Types
{
    public enum DisconnectReason : uint
    {
        None,
        DisconnectedByUser,
        DisconnectedBySystem,
        DestroyedByUser,
        DestroyedBySystem,
        Rejected,
        SignalLost
    }
}