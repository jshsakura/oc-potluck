using System;
using System.Collections.Generic;
using System.IO;
using System.Text;

namespace LanPlayServer.Stats
{
    /// <summary>
    /// The status page's files, read once at start. Requests are answered from this
    /// table only, so no request path ever reaches the file system.
    /// </summary>
    public sealed class StatusPage
    {
        public const string OriginPlaceholder = "__SITE_ORIGIN__";

        // Anything bigger is not part of the page and is left out.
        private const long MaxFileBytes = 2 * 1024 * 1024;

        private static readonly Dictionary<string, string> ContentTypes = new(StringComparer.OrdinalIgnoreCase)
        {
            [".html"] = "text/html; charset=utf-8",
            [".css"] = "text/css; charset=utf-8",
            [".js"] = "text/javascript; charset=utf-8",
            [".json"] = "application/json; charset=utf-8",
            [".svg"] = "image/svg+xml",
            [".png"] = "image/png",
            [".ttf"] = "font/ttf",
            [".txt"] = "text/plain; charset=utf-8",
        };

        public sealed record PageFile(byte[] Body, string ContentType);

        private readonly Dictionary<string, PageFile> _files;

        private StatusPage(Dictionary<string, PageFile> files)
        {
            _files = files;
        }

        public int Count => _files.Count;

        /// <param name="root">Folder holding index.html and its assets.</param>
        /// <param name="origin">Replaces the placeholder in HTML, for the share card; empty leaves paths relative.</param>
        public static StatusPage Load(string root, string origin)
        {
            var files = new Dictionary<string, PageFile>(StringComparer.Ordinal);
            string full = Path.GetFullPath(root);

            foreach (string path in Directory.EnumerateFiles(full, "*", SearchOption.AllDirectories))
            {
                string relative = Path.GetRelativePath(full, path).Replace('\\', '/');
                if (relative.StartsWith('.') || relative.Contains("/.") || !ContentTypes.TryGetValue(Path.GetExtension(path), out string type))
                {
                    continue;
                }
                if (new FileInfo(path).Length > MaxFileBytes)
                {
                    continue;
                }

                byte[] body = File.ReadAllBytes(path);
                if (type.StartsWith("text/html", StringComparison.Ordinal))
                {
                    body = Encoding.UTF8.GetBytes(Encoding.UTF8.GetString(body).Replace(OriginPlaceholder, origin));
                }
                files["/" + relative] = new PageFile(body, type);
            }

            if (!files.ContainsKey("/index.html"))
            {
                throw new FileNotFoundException($"The status page needs index.html in {full}");
            }
            return new StatusPage(files);
        }

        /// <summary>Looks a request URL up; the query string is ignored and "/" means index.html.</summary>
        public bool TryGet(string url, out PageFile file)
        {
            int query = url.IndexOfAny(['?', '#']);
            string path = query >= 0 ? url[..query] : url;
            if (path.Length == 0 || path == "/")
            {
                path = "/index.html";
            }
            return _files.TryGetValue(path, out file);
        }

        public static bool IsHtml(PageFile file) => file.ContentType.StartsWith("text/html", StringComparison.Ordinal);
    }
}
