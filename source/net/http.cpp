#include "net/http.h"

#include "core/log.h"
#include "core/util.h"

#include <switch.h>

#include <curl/curl.h>

#include <mutex>

namespace nxp {

namespace {
    bool g_ready = false;
    bool g_socketsUp = false;

    // One handle for the life of the app, reused for every request.
    //
    // curl_easy_reset() clears the options but *keeps* the live connection,
    // the TLS session cache and the DNS cache, which is the whole point: a
    // fresh handle per request meant every call paid for a TCP connect, a full
    // TLS handshake, and mbedTLS parsing the CA bundle off the SD card again.
    // At launch that happened twice back to back - hello, then the first
    // check-in - which is most of the wait before the app goes green.
    CURL* g_handle = nullptr;

    // Every caller today is the one sync worker thread, so this is never
    // contended. It exists because a curl handle is not thread-safe and this
    // is a static entry point: a second caller arriving later should queue,
    // not corrupt the first one's transfer.
    std::mutex g_handleMutex;

    size_t writeCallback(char* data, size_t size, size_t nmemb, void* userdata)
    {
        std::string* out = static_cast<std::string*>(userdata);
        size_t total = size * nmemb;

        // A hostile or broken server must not be able to make us allocate
        // forever; passes are tiny and a full exchange is a few kilobytes.
        constexpr size_t kMaxBody = 256 * 1024;
        if (out->size() + total > kMaxBody)
            return 0;

        out->append(data, total);
        return total;
    }
}

bool Http::globalInit()
{
    if (g_ready)
        return true;

    Result rc = socketInitializeDefault();
    if (R_FAILED(rc)) {
        LOG("http: socketInitializeDefault failed (0x%x)", rc);
        return false;
    }
    g_socketsUp = true;

    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        LOG("http: curl_global_init failed");
        socketExit();
        g_socketsUp = false;
        return false;
    }

    g_handle = curl_easy_init();
    if (!g_handle)
        LOG("http: no shared handle; falling back to one per request");

    g_ready = true;

    // The linked libcurl, not whatever `curl --version` reports in a shell:
    // the console links switch-curl from devkitPro's portlibs, and only this
    // number decides whether the CA cache below is compiled in.
#ifdef CURLOPT_CA_CACHE_TIMEOUT
    const char* caCache = "CA cache on";
#else
    const char* caCache = "no CA cache (libcurl < 7.87; the bundle is parsed per handshake)";
#endif
    LOG("http: ready (%s, %s)", curl_version(), caCache);
    return true;
}

void Http::globalExit()
{
    if (g_ready) {
        {
            std::lock_guard<std::mutex> lock(g_handleMutex);
            if (g_handle) {
                curl_easy_cleanup(g_handle);
                g_handle = nullptr;
            }
        }
        curl_global_cleanup();
        g_ready = false;
    }
    if (g_socketsUp) {
        socketExit();
        g_socketsUp = false;
    }
}

bool Http::available()
{
    return g_ready;
}

const char* Http::caBundlePath()
{
    static std::string path = dataPath("cacert.pem");
    return path.c_str();
}

HttpResponse Http::postJson(const std::string& url, const std::string& body,
    const std::string& deviceId, const std::string& token, long timeoutMs)
{
    HttpResponse response;

    if (!g_ready) {
        response.error = "network not initialised";
        return response;
    }
    if (url.empty()) {
        response.error = "no server configured";
        return response;
    }

    // Held for the whole transfer: the handle carries the connection and the
    // TLS session, so it cannot be shared mid-flight.
    std::lock_guard<std::mutex> lock(g_handleMutex);

    CURL* curl = g_handle;
    bool disposable = false;
    if (curl) {
        // Keeps the connection, the TLS session cache and the DNS cache.
        curl_easy_reset(curl);
    } else {
        curl = curl_easy_init();
        disposable = true;
    }
    if (!curl) {
        response.error = "curl_easy_init failed";
        return response;
    }

    std::string authHeader = "Authorization: Bearer " + token;
    std::string idHeader = "X-Plaza-Id: " + deviceId;

    curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "Accept: application/json");
    headers = curl_slist_append(headers, "Expect:");
    if (!token.empty())
        headers = curl_slist_append(headers, authHeader.c_str());
    if (!deviceId.empty())
        headers = curl_slist_append(headers, idHeader.c_str());

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response.body);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeoutMs);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, timeoutMs);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "nx-plaza/" APP_VERSION);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS, CURLPROTO_HTTP | CURLPROTO_HTTPS);

    // Check-ins are twenty seconds apart and most servers close an idle
    // connection before then, so this mostly matters for the two calls at
    // launch. When the connection has gone, the preserved TLS session still
    // turns the next handshake into an abbreviated one.
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);

    // No system trust store on the console: point mbedTLS at a bundle the user
    // dropped next to the config, and leave verification enabled either way.
    if (url.compare(0, 8, "https://") == 0) {
        if (fileExists(caBundlePath()))
            curl_easy_setopt(curl, CURLOPT_CAINFO, caBundlePath());
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);

        // Reuse the parsed CA store instead of re-reading the bundle on every
        // handshake. Added in curl 7.87; the toolchain here ships 7.69, where
        // this compiles out and the bundle is parsed per handshake - which is
        // why a small bundle is still worth having.
#ifdef CURLOPT_CA_CACHE_TIMEOUT
        curl_easy_setopt(curl, CURLOPT_CA_CACHE_TIMEOUT, 24L * 60L * 60L);
#endif
    }

    CURLcode code = curl_easy_perform(curl);
    if (code == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.status);
    } else {
        response.error = curl_easy_strerror(code);
        if (code == CURLE_SSL_CACERT || code == CURLE_PEER_FAILED_VERIFICATION) {
            response.error += " (drop a CA bundle at ";
            response.error += caBundlePath();
            response.error += ")";
        }
    }

    curl_slist_free_all(headers);
    if (disposable)
        curl_easy_cleanup(curl);
    return response;
}

} // namespace nxp
