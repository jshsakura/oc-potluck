using System.Collections.Generic;
using System.Text.Json.Serialization;

namespace LanPlayServer.Utils
{
    [JsonSourceGenerationOptions(WriteIndented = false)]
    [JsonSerializable(typeof(List<Game>))]
    internal partial class GameSerializerContext : JsonSerializerContext
    {
    }
}