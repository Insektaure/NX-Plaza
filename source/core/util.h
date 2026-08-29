#pragma once

#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace nxp {

// The app's name, as the Makefile spells it.
#ifndef APP_TITLE
#define APP_TITLE "nx-plaza"
#endif
constexpr const char* kAppName = APP_TITLE;

// The plaza this build talks to, from the Makefile.
//
// A released build has one server and no way to change it: the address is not a
// setting, it is what the app is. Building with DEBUG=1 makes it editable in
// Settings, for pointing a console at a plaza on your own machine.
#ifndef PLAZA_SERVER
#define PLAZA_SERVER "https://api.nxplaza.net"
#endif
constexpr const char* kPlazaServer = PLAZA_SERVER;

// Whether the server address can be changed at runtime.
#ifdef NXP_DEBUG
constexpr bool kServerIsEditable = true;
#else
constexpr bool kServerIsEditable = false;
#endif


// ---------------------------------------------------------------- filesystem

// Root of everything this app keeps on the SD card. Trailing slash included.
const char* dataDir();

// mkdir -p for dataDir(). Safe to call repeatedly.
bool ensureDataDir();

// Path inside dataDir(), e.g. dataPath("config.json").
std::string dataPath(const char* name);

bool readWholeFile(const std::string& path, std::string& out);
bool writeWholeFileAtomic(const std::string& path, const std::string& contents);
bool fileExists(const std::string& path);

// --------------------------------------------------------------------- time

// Wall clock, unix seconds.
uint64_t nowUnix();

// Monotonic milliseconds since boot; unaffected by clock changes.
uint64_t monotonicMs();

// "12m ago", "3h ago", "Yesterday", "4 Jun" - the relative stamps the inbox
// uses. `now` is passed in so a whole list renders consistently.
std::string relativeTime(uint64_t then, uint64_t now);

// "Mon", "Tue", ... for the weekly strip.
std::string weekdayShort(uint64_t when);

// ------------------------------------------------------------------ strings

std::string format(const char* fmt, ...);
std::string toHex(const void* data, size_t len);
bool fromHex(const std::string& hex, void* out, size_t outLen);
std::string trim(const std::string& s);
// Uppercases the first letter of each word. ASCII only: any other byte is left
// exactly as it was, so UTF-8 passes through untouched.
std::string titleCase(const std::string& s);
std::string clampUtf8(const std::string& s, size_t maxCodepoints);
size_t utf8Length(const std::string& s);
bool utf8Next(const char*& p, const char* end, uint32_t& cp);

// ------------------------------------------------------------------- crypto

// SHA-256 over `parts` concatenated.
void sha256Over(const std::vector<std::string>& parts, uint8_t out[32]);

// Cryptographically secure random bytes.
void randomBytes(void* out, size_t len);

// Lowercase hex string of `bytes` random bytes.
std::string randomHex(size_t bytes);

// Deterministic 32-bit hash, used to derive procedural portraits and colours
// from an id. Not a security primitive.
uint32_t fnv1a(const std::string& s);

} // namespace nxp
