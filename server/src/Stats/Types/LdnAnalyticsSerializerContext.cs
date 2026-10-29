using System.Text.Json.Serialization;

namespace LanPlayServer.Stats.Types
{
    [JsonSourceGenerationOptions(WriteIndented = false)]
    [JsonSerializable(typeof(LdnAnalytics))]
    internal partial class LdnAnalyticsSerializerContext : JsonSerializerContext
    {

    }
}