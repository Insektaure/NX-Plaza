#pragma once

#include <functional>
#include <string>
#include <vector>

namespace nxp {

struct HttpResponse {
    long status = 0;
    std::string body;
    std::string error; // transport-level failure, empty when we got a reply

    bool ok() const { return error.empty() && status >= 200 && status < 300; }
};

// Minimal JSON-over-HTTP client.
//
// One curl easy handle for the life of the app, reset between requests rather
// than recreated. A fresh handle per request looked tidier and cost a TCP
// connect, a full TLS handshake and a re-read of the CA bundle from the SD
// card every single time - twice during launch, before the app could show
// itself as connected. Resetting keeps the connection, the TLS session and the
// DNS cache.
//
// Calls are serialised on an internal mutex, so the handle is never shared
// mid-transfer.
class Http {
public:
    // Brings up the socket driver and curl. Call once, from main.
    static bool globalInit();
    static void globalExit();

    // True once globalInit() has succeeded.
    static bool available();

    static HttpResponse postJson(const std::string& url, const std::string& body,
        const std::string& deviceId, const std::string& token, long timeoutMs = 8000);

    // A plain GET, body held in memory and capped the same way postJson's is.
    // `extraHeaders` are whole header lines, "Name: value".
    static HttpResponse get(const std::string& url,
        const std::vector<std::string>& extraHeaders = {}, long timeoutMs = 8000);

    // Streams a GET straight to a file, for a body too big to want in memory.
    //
    // Redirects are followed here and nowhere else: a release asset is handed
    // out as a redirect to a different host, so refusing to follow one would
    // mean never downloading anything.
    //
    // `onProgress` is called from the transfer with bytes so far and the total
    // (0 when the server did not say); returning false aborts the download and
    // removes the partial file. It may be empty.
    using ProgressFn = std::function<bool(uint64_t got, uint64_t total)>;
    static bool download(const std::string& url, const std::string& destPath,
        std::string* errorOut, const ProgressFn& onProgress = {}, long timeoutMs = 120000);

    // Where a CA bundle has to be dropped for https:// server URLs to work.
    // The console has no trust store we can borrow, so verification stays on
    // and fails loudly rather than being silently disabled.
    static const char* caBundlePath();
};

} // namespace nxp
