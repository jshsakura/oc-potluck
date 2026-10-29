using System;
using System.Net;
using LanPlayServer.Stats;
using LanPlayServer.Utils;
using System.IO;
using System.Net.Sockets;
using System.Runtime.InteropServices;
using System.Text.RegularExpressions;
using System.Threading;

namespace LanPlayServer
{
    static class Program
    {
        private static readonly IPAddress Host = IPAddress.Parse(Environment.GetEnvironmentVariable("LDN_HOST") ?? "0.0.0.0");
        private static readonly int Port = int.Parse(Environment.GetEnvironmentVariable("LDN_PORT") ?? "30456");
        private static readonly string GamelistPath = Environment.GetEnvironmentVariable("LDN_GAMELIST_PATH") ?? "gamelist.json";
        private static readonly string RedisSocketPath = Environment.GetEnvironmentVariable("LDN_REDIS_SOCKET") ?? "";
        private static readonly string RedisHost = Environment.GetEnvironmentVariable("LDN_REDIS_HOST") ?? "127.0.0.1";
        private static readonly int RedisPort = int.Parse(Environment.GetEnvironmentVariable("LDN_REDIS_PORT") ?? "6379");

        // 0 leaves the status view off. Publish it only on a loopback port behind a proxy.
        private static readonly IPAddress StatusHost = IPAddress.Parse(Environment.GetEnvironmentVariable("LDN_STATUS_HOST") ?? "127.0.0.1");
        private static readonly int StatusPort = int.Parse(Environment.GetEnvironmentVariable("LDN_STATUS_PORT") ?? "0");

        // The status page is served from the status port when LDN_WEB_ENABLED is true.
        // LDN_SITE_ORIGIN (https://host) points the share card at the public address.
        private static readonly bool WebEnabled = IsTrue(Environment.GetEnvironmentVariable("LDN_WEB_ENABLED"));
        private static readonly string WebRoot = Environment.GetEnvironmentVariable("LDN_WEB_ROOT") ?? "web";
        private static readonly string SiteOrigin = Environment.GetEnvironmentVariable("LDN_SITE_ORIGIN") ?? "";

        private static readonly ManualResetEventSlim StopEvent = new();

        private static LdnServer _ldnServer;
        private static StatusServer _statusServer;

        private static bool IsTrue(string value) =>
            value != null && (value.Equals("true", StringComparison.OrdinalIgnoreCase) || value == "1" || value.Equals("yes", StringComparison.OrdinalIgnoreCase));

        /// <summary>Loads the page, or returns null and says why; the relay runs either way.</summary>
        private static StatusPage LoadStatusPage()
        {
            string origin = SiteOrigin.TrimEnd('/');
            if (origin.Length > 0 && !Regex.IsMatch(origin, "^https?://[A-Za-z0-9.-]+(:[0-9]{1,5})?$"))
            {
                Console.WriteLine($"LDN_SITE_ORIGIN must look like https://host; ignoring \"{SiteOrigin}\"");
                origin = "";
            }
            try
            {
                StatusPage page = StatusPage.Load(WebRoot, origin);
                Console.WriteLine($"Loaded the status page ({page.Count} files) from {WebRoot}");
                return page;
            }
            catch (Exception e) when (e is IOException || e is UnauthorizedAccessException)
            {
                Console.WriteLine($"The status page is off: {e.Message}");
                return null;
            }
        }

        static void Main()
        {
            // Relay work runs on the thread pool. Start it with room for a burst so a busy
            // moment never waits for the pool to add threads, which it does slowly.
            ThreadPool.GetMinThreads(out int workers, out int io);
            int floor = Environment.ProcessorCount * 4;
            ThreadPool.SetMinThreads(Math.Max(workers, floor), Math.Max(io, floor));

            Console.CancelKeyPress += (_, _) => StopEvent.Set();
            PosixSignalRegistration.Create(PosixSignal.SIGINT, _ => StopEvent.Set());
            PosixSignalRegistration.Create(PosixSignal.SIGHUP, _ => StopEvent.Set());
            PosixSignalRegistration.Create(PosixSignal.SIGQUIT, _ => StopEvent.Set());
            PosixSignalRegistration.Create(PosixSignal.SIGTERM, _ => StopEvent.Set());

            Console.WriteLine();
            Console.WriteLine( "__________                     __ .__                  .____         .___        ");
            Console.WriteLine(@"\______   \ ___.__. __ __     |__||__|  ____  ___  ___ |    |      __| _/  ____  ");
            Console.WriteLine(@" |       _/<   |  ||  |  \    |  ||  | /    \ \  \/  / |    |     / __ |  /    \ ");
            Console.WriteLine(@" |    |   \ \___  ||  |  /    |  ||  ||   |  \ >    <  |    |___ / /_/ | |   |  \");
            Console.WriteLine(@" |____|_  / / ____||____/ /\__|  ||__||___|  //__/\_ \ |_______ \\____ | |___|  /");
            Console.WriteLine(@"        \/  \/            \______|         \/       \/         \/     \/      \/ ");
            Console.WriteLine();
            Console.WriteLine( "_________________________________________________________________________________");
            Console.WriteLine();
            Console.WriteLine("- Information");

            Console.Write($"\tReading '{GamelistPath}'...");
            GameList.Initialize(File.ReadAllText(GamelistPath));
            Console.WriteLine(" Done!");

            _ldnServer = new(Host, Port);

            var bannedIPs = BanList.GetBannedIPs();

            Console.WriteLine($"Loaded {bannedIPs.Count} banned IPs");

            Console.Write($"\tLdnServer (port: {Port}) starting...");
            _ldnServer.Start();
            Console.WriteLine(" Done!");

            if (WebEnabled && StatusPort <= 0)
            {
                Console.WriteLine("LDN_WEB_ENABLED needs LDN_STATUS_PORT; the status page is off");
            }

            if (StatusPort > 0)
            {
                StatusPage page = WebEnabled ? LoadStatusPage() : null;
                Console.Write($"\tStatus view (port: {StatusPort}, page: {(page != null ? "on" : "off")}) starting...");
                _statusServer = new(StatusHost, StatusPort, _ldnServer.All, page);
                _statusServer.Start();
                Console.WriteLine(" Done!");
            }

            bool usingUnixSocket = !string.IsNullOrWhiteSpace(RedisSocketPath);
            EndPoint redisEndpoint;

            if (usingUnixSocket)
            {
                redisEndpoint = new UnixDomainSocketEndPoint(RedisSocketPath);
            }
            else
            {
                if (!IPAddress.TryParse(RedisHost, out IPAddress ipAddress))
                {
                    ipAddress = Dns.GetHostEntry(RedisHost!).AddressList[0];
                }

                redisEndpoint = new IPEndPoint(ipAddress, RedisPort);
            }

            Console.Write($"\tRedis analytics starting (using unix socket: {usingUnixSocket})...");
            StatsDumper.Start(redisEndpoint);
            Console.WriteLine(" Done!");

            StopEvent.Wait();

            StatsDumper.Stop();
            _statusServer?.Dispose();
            _ldnServer.Dispose();
        }
    }
}