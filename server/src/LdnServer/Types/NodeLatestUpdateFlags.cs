using System;

namespace Ryujinx.HLE.HOS.Services.Ldn.Types
{
    [Flags]
    public enum NodeLatestUpdateFlags : byte
    {
        None = 0,
        Connect = 1 << 0,
        Disconnect = 1 << 1
    }
}