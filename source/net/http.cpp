#include "net/http.h"

#include "core/log.h"
#include "core/util.h"

#include <switch.h>

#include <curl/curl.h>

#include <cstdio>
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

    size_t fileWriteCallback(char* data, size_t size, size_t nmemb, void* userdata)
    {
        return fwrite(data, size, nmemb, static_cast<FILE*>(userdata));
    }

    struct ProgressState {
        const Http::ProgressFn* fn;
    };

    int progressCallback(void* userdata, curl_off_t dlTotal, curl_off_t dlNow, curl_off_t,
        curl_off_t)
    {
        ProgressState* state = static_cast<ProgressState*>(userdata);
        if (!state || !state->fn || !*state->fn)
            return 0;
        // Non-zero aborts the transfer, which is what a false return asks for.
        return (*state->fn)(uint64_t(dlNow < 0 ? 0 : dlNow), uint64_t(dlTotal < 0 ? 0 : dlTotal))
            ? 0
            : 1;
    }

    // Everything every request wants, in one place. Split out when the updater
    // added a second and third caller: three copies of the TLS setup would have
    // been three chances for one of them to quietly stop verifying.
    void applyCommonOptions(CURL* curl, const std::string& url, long timeoutMs)
    {
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeoutMs);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, timeoutMs);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "nx-plaza/" APP_VERSION);
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(curl, CURLOPT_PROTOCOLS, CURLPROTO_HTTP | CURLPROTO_HTTPS);
        curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);

        if (url.compare(0, 8, "https://") == 0) {
            if (fileExists(Http::caBundlePath()))
                curl_easy_setopt(curl, CURLOPT_CAINFO, Http::caBundlePath());
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
#ifdef CURLOPT_CA_CACHE_TIMEOUT
            curl_easy_setopt(curl, CURLOPT_CA_CACHE_TIMEOUT, 24L * 60L * 60L);
#endif
        }
    }

    // A handle of its own, for traffic that is not the plaza.
    //
    // The shared handle exists to keep one connection to one server warm. A
    // one-off call to a different host has nothing to reuse from it and, worse,
    // would evict what is there: at launch the update check and the first
    // check-in raced for the same handle and tore down each other's TLS
    // session, on a console that re-parses a 189 KB CA bundle off the SD card
    // for every handshake. A private handle costs one connection and lets the
    // two run at once instead of queueing behind a mutex.
    CURL* privateHandle() { return curl_easy_init(); }

    // Takes the shared handle when it is free, or a private one when it is not.
    // Caller cleans up when `disposable` comes back true.
    CURL* acquire(bool& disposable)
    {
        CURL* curl = g_handle;
        disposable = false;
        if (curl) {
            curl_easy_reset(curl);
        } else {
            curl = curl_easy_init();
            disposable = true;
        }
        return curl;
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

    // Keeps the connection, the TLS session cache and the DNS cache.
    bool disposable = false;
    CURL* curl = acquire(disposable);
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

    // No system trust store on the console, keepalive because check-ins are
    // twenty seconds apart, and the CA store cached where curl is new enough.
    applyCommonOptions(curl, url, timeoutMs);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response.body);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);

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


HttpResponse Http::get(const std::string& url, const std::vector<std::string>& extraHeaders,
    long timeoutMs)
{
    HttpResponse response;

    if (!g_ready) {
        response.error = "network not initialised";
        return response;
    }
    if (url.empty()) {
        response.error = "no address";
        return response;
    }

    // Deliberately not the shared handle, and so deliberately not serialised
    // against the plaza: see privateHandle().
    CURL* curl = privateHandle();
    if (!curl) {
        response.error = "curl_easy_init failed";
        return response;
    }

    curl_slist* headers = nullptr;
    for (const std::string& line : extraHeaders)
        headers = curl_slist_append(headers, line.c_str());

    applyCommonOptions(curl, url, timeoutMs);
    if (headers)
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response.body);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);

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

    if (headers)
        curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return response;
}

bool Http::download(const std::string& url, const std::string& destPath, std::string* errorOut,
    const ProgressFn& onProgress, long timeoutMs)
{
    auto fail = [errorOut](const char* what) {
        if (errorOut)
            *errorOut = what;
        return false;
    };

    if (!g_ready)
        return fail("network not initialised");
    if (url.empty() || destPath.empty())
        return fail("no address");

    FILE* out = fopen(destPath.c_str(), "wb");
    if (!out)
        return fail("could not open the destination file");

    CURL* curl = privateHandle();
    if (!curl) {
        fclose(out);
        remove(destPath.c_str());
        return fail("curl_easy_init failed");
    }

    ProgressState progress { &onProgress };

    applyCommonOptions(curl, url, timeoutMs);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, fileWriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, out);
    // A release asset is served as a redirect to a different host.
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progressCallback);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &progress);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);

    CURLcode code = curl_easy_perform(curl);
    long status = 0;
    if (code == CURLE_OK)
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);

    // Flushed and closed before anything judges the file: the last block is
    // still in stdio's buffer until this returns.
    bool closed = fclose(out) == 0;

    curl_easy_cleanup(curl);

    if (code != CURLE_OK) {
        remove(destPath.c_str());
        std::string message = code == CURLE_ABORTED_BY_CALLBACK
            ? std::string("cancelled")
            : std::string(curl_easy_strerror(code));
        if (errorOut)
            *errorOut = message;
        return false;
    }
    if (!closed) {
        remove(destPath.c_str());
        return fail("the download could not be written to the SD card");
    }
    if (status < 200 || status >= 300) {
        remove(destPath.c_str());
        if (errorOut)
            *errorOut = format("the server answered %ld", status);
        return false;
    }
    return true;
}

} // namespace nxp
