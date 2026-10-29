using System.Collections.Generic;
using System.Globalization;
using System.Text.Json.Serialization;

namespace LanPlayServer.Utils
{
    public class Game
    {
        [JsonPropertyName("id")]
        public string IdString { get; set; }

        [JsonPropertyName("name")]
        public string Name { get; set; }

        public ulong? GetId()
        {
            if (string.IsNullOrWhiteSpace(IdString) || !ulong.TryParse(IdString[2..], NumberStyles.HexNumber, null, out ulong result))
            {
                return null;
            }

            return result;
        }
    }

    public static class GameList
    {
        // TODO: populate using some online list or something.
        // We still need to be able to provide game specific info, as we might want to parse AdvertiseData or do special things for different games.
        // (eg. a mode to regulate framerate to keep sync in Mario Kart would be nice)

        private static readonly GameSerializerContext GameJsonContext = new(JsonHelper.GetDefaultSerializerOptions(false));

        private static readonly Dictionary<ulong, Game> Games = new();

        public static void Initialize(string jsonString) {
            List<Game> data = JsonHelper.Deserialize(jsonString, GameJsonContext.ListGame);

            foreach (Game game in data)
            {
                ulong? gameId = game.GetId();

                if (!gameId.HasValue)
                {
                    continue;
                }

                Games[gameId.Value] = game;
            }
        }

        public static Game GetGameById(ulong id)
        {
            Games.TryGetValue(id, out Game result);

            return result;
        }
    }
}