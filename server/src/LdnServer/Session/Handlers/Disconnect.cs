using LanPlayServer.Network.Types;

namespace LanPlayServer
{
    public partial class LdnSession
    {
        // Synchronous, so a Connect right behind it finds the room already without this session.
        private void HandleDisconnect(LdnHeader header, DisconnectMessage message) => LeaveCurrentGame();
    }
}