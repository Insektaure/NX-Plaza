#pragma once

#include <string>

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

    // Where a CA bundle has to be dropped for https:// server URLs to work.
    // The console has no trust store we can borrow, so verification stays on
    // and fails loudly rather than being silently disabled.
    static const char* caBundlePath();
};

} // namespace nxp
