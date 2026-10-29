using LanPlayServer.Utils;
using System.Runtime.InteropServices;

namespace Ryujinx.HLE.HOS.Services.Ldn.Types
{
    [StructLayout(LayoutKind.Sequential, Size = 0xC)]
    public struct AddressEntry
    {
        public uint         Ipv4Address;
        public Array6<byte> MacAddress;
        public ushort       Reserved;
    }

    [StructLayout(LayoutKind.Sequential, Size = 0x60)]
    public struct AddressList
    {
        public Array8<AddressEntry> Addresses;
    }
}