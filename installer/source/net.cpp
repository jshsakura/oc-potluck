#include <netdb.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include "net.hpp"
#include <curl/curl.h>
#include <cstdio>
#include <json-c/json.h>
#include <switch.h>
#include "logic.hpp"

namespace net {

namespace {
constexpr size_t MaxApiBytes = 1 << 20;
constexpr const char* UserAgent = "potluck-installer/0.2";

size_t toString(char* data, size_t size, size_t count, void* user) {
    auto* out = static_cast<std::string*>(user);
    if (out->size() + size * count > MaxApiBytes) return 0;
    out->append(data, size * count);
    return size * count;
}
// The zip goes to a file, never past MaxZipBytes: a short count makes curl stop the transfer.
struct Sink {
    FILE* file;
    long written = 0;
    bool tooBig = false;
};
size_t toFile(char* data, size_t size, size_t count, void* user) {
    auto* sink = static_cast<Sink*>(user);
    const size_t bytes = size * count;
    if (sink->written + long(bytes) > inst::MaxZipBytes) { sink->tooBig = true; return 0; }
    sink->written += long(bytes);
    return fwrite(data, 1, bytes, sink->file);
}
struct Cancel {
    const std::function<bool(long, long)>* progress;
};
int onProgress(void* user, curl_off_t total, curl_off_t now, curl_off_t, curl_off_t) {
    auto* c = static_cast<Cancel*>(user);
    return (*c->progress)(long(now), long(total)) ? 0 : 1;
}
CURL* handle(const std::string& url) {
    CURL* c = curl_easy_init();
    if (!c) return nullptr;
    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    curl_easy_setopt(c, CURLOPT_USERAGENT, UserAgent);
    curl_easy_setopt(c, CURLOPT_CAINFO, "romfs:/cacert.pem");
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(c, CURLOPT_PROTOCOLS, CURLPROTO_HTTPS);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(c, CURLOPT_LOW_SPEED_LIMIT, 1024L);
    curl_easy_setopt(c, CURLOPT_LOW_SPEED_TIME, 30L);
    return c;
}
} // namespace

bool init() {
    if (R_FAILED(socketInitializeDefault())) return false;
    nifmInitialize(NifmServiceType_User);
    return curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK;
}

void exit() {
    curl_global_cleanup();
    nifmExit();
    socketExit();
}

Latest fetchLatest() {
    Latest latest;
    CURL* c = handle(std::string("https://api.github.com/repos/") + inst::Repo + "/releases/latest");
    if (!c) { latest.error = "curl"; return latest; }
    std::string body;
    curl_slist* headers = curl_slist_append(nullptr, "Accept: application/vnd.github+json");
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, toString);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &body);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 30L);
    const CURLcode rc = curl_easy_perform(c);
    long status = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &status);
    if (rc != CURLE_OK) latest.error = curl_easy_strerror(rc);
    else if (status != 200) latest.error = "HTTP " + std::to_string(status);
    curl_slist_free_all(headers);
    curl_easy_cleanup(c);
    if (!latest.error.empty()) return latest;

    json_object* root = json_tokener_parse(body.c_str());
    json_object *tag = nullptr, *assets = nullptr;
    if (root && json_object_object_get_ex(root, "tag_name", &tag) && json_object_object_get_ex(root, "assets", &assets) &&
        json_object_get_type(assets) == json_type_array) {
        latest.tag = json_object_get_string(tag);
        for (size_t i = 0; i < json_object_array_length(assets); ++i) {
            json_object *asset = json_object_array_get_idx(assets, i), *name = nullptr, *url = nullptr, *size = nullptr;
            if (!json_object_object_get_ex(asset, "name", &name) || !json_object_object_get_ex(asset, "browser_download_url", &url)) continue;
            if (!inst::isReleaseZip(json_object_get_string(name)) || !inst::releaseUrlOk(json_object_get_string(url))) continue;
            latest.url = json_object_get_string(url);
            if (json_object_object_get_ex(asset, "size", &size)) latest.size = long(json_object_get_int64(size));
            break;
        }
    }
    if (root) json_object_put(root);
    if (latest.tag.empty() || latest.url.empty()) { latest.error = "no release zip"; return latest; }
    latest.ok = true;
    return latest;
}

bool download(const std::string& url, const std::string& path, long expected,
              const std::function<bool(long, long)>& progress, std::string& error) {
    if (!inst::releaseUrlOk(url)) { error = "url"; return false; }
    if (expected > inst::MaxZipBytes) { error = "size"; return false; }
    FILE* out = fopen(path.c_str(), "wb");
    if (!out) { error = "file"; return false; }
    CURL* c = handle(url);
    if (!c) { fclose(out); error = "curl"; return false; }
    Cancel cancel{&progress};
    Sink sink{out};
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, toFile);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &sink);
    curl_easy_setopt(c, CURLOPT_MAXFILESIZE_LARGE, curl_off_t(inst::MaxZipBytes));
    curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, onProgress);
    curl_easy_setopt(c, CURLOPT_XFERINFODATA, &cancel);
    const CURLcode rc = curl_easy_perform(c);
    long status = 0;
    curl_off_t got = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_getinfo(c, CURLINFO_SIZE_DOWNLOAD_T, &got);
    curl_easy_cleanup(c);
    const bool closed = fclose(out) == 0;
    if (sink.tooBig || rc == CURLE_FILESIZE_EXCEEDED) error = "size";
    else if (rc == CURLE_ABORTED_BY_CALLBACK) error = "cancel";
    else if (rc != CURLE_OK) error = curl_easy_strerror(rc);
    else if (status != 200) error = "HTTP " + std::to_string(status);
    else if (!closed) error = "file";
    else if (got <= 0 || got > inst::MaxZipBytes || (expected > 0 && got != expected)) error = "size";
    if (error.empty()) return true;
    remove(path.c_str());
    return false;
}

int probe(const std::string& host, int port, int timeoutMs) {
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* list = nullptr;
    const int lookup = getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &list);
    if (lookup != 0 || !list) {
        if (list) freeaddrinfo(list);
        if (lookup == EAI_AGAIN || lookup == EAI_FAIL) return ProbeDnsDown;
        // Say so when the name exists but only has an IPv6 address, which the module cannot use
        hints.ai_family = AF_INET6;
        addrinfo* v6 = nullptr;
        const bool ipv6Only = getaddrinfo(host.c_str(), nullptr, &hints, &v6) == 0 && v6;
        if (v6) freeaddrinfo(v6);
        return ipv6Only ? ProbeIpv6Only : ProbeNotFound;
    }
    int result = ProbeFailed;
    const int fd = socket(list->ai_family, list->ai_socktype, list->ai_protocol);
    if (fd >= 0) {
        fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
        const u64 start = armGetSystemTick();
        int rc = connect(fd, list->ai_addr, list->ai_addrlen);
        if (rc != 0 && errno == EINPROGRESS) {
            fd_set writable;
            FD_ZERO(&writable);
            FD_SET(fd, &writable);
            timeval tv{timeoutMs / 1000, (timeoutMs % 1000) * 1000};
            int err = 0;
            socklen_t len = sizeof(err);
            if (select(fd + 1, nullptr, &writable, nullptr, &tv) > 0 && getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) == 0) {
                if (err == 0) rc = 0;
                else if (err == ECONNREFUSED) result = ProbeRefused;
            }
        } else if (rc != 0 && errno == ECONNREFUSED) {
            result = ProbeRefused;
        }
        if (rc == 0) result = int(armTicksToNs(armGetSystemTick() - start) / 1000000);
        close(fd);
    }
    freeaddrinfo(list);
    return result;
}

} // namespace net
