namespace Ryujinx.HLE.HOS.Services.Ldn.Types
{
    public enum NetworkState
    {
        None,
        Initialized,
        AccessPoint,
        AccessPointCreated,
        Station,
        StationConnected,
        Error
    }
}