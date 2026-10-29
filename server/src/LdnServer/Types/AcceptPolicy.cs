namespace Ryujinx.HLE.HOS.Services.Ldn.Types
{
    public enum AcceptPolicy : byte
    {
        AcceptAll,
        RejectAll,
        BlackList,
        WhiteList
    }
}
