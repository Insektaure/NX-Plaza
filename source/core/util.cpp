#include "core/util.h"

#include <switch.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <sys/stat.h>

namespace nxp {

static const char* kDataDir = "sdmc:/switch/nx-plaza/";

const char* dataDir()
{
    return kDataDir;
}

bool ensureDataDir()
{
    mkdir("sdmc:/switch", 0777);
    int rc = mkdir("sdmc:/switch/nx-plaza", 0777);
    return rc == 0 || errno == EEXIST;
}

std::string dataPath(const char* name)
{
    return std::string(kDataDir) + name;
}

bool readWholeFile(const std::string& path, std::string& out)
{
    FILE* f = fopen(path.c_str(), "rb");
    if (!f)
        return false;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);
    if (size < 0) {
        fclose(f);
        return false;
    }

    out.resize(static_cast<size_t>(size));
    size_t read = size > 0 ? fread(&out[0], 1, static_cast<size_t>(size), f) : 0;
    fclose(f);
    out.resize(read);
    return true;
}

namespace {
    std::string g_exePath;
}

void setExecutablePath(const char* path)
{
    if (!path || !*path)
        return;
    // Only ever a path to an NRO on the SD card. Anything else - a title id, an
    // empty argv from an unusual loader - is not something to overwrite.
    std::string candidate = path;
    if (candidate.size() > 4
        && strcasecmp(candidate.c_str() + candidate.size() - 4, ".nro") == 0)
        g_exePath = candidate;
}

const std::string& executablePath() { return g_exePath; }

bool writeWholeFileAtomic(const std::string& path, const std::string& contents)
{
    // Write to a sibling temp file and rename, so a crash or a yanked SD card
    // cannot leave a half-written pass database behind.
    std::string tmp = path + ".tmp";
    FILE* f = fopen(tmp.c_str(), "wb");
    if (!f)
        return false;

    bool ok = contents.empty() || fwrite(contents.data(), 1, contents.size(), f) == contents.size();
    if (fflush(f) != 0)
        ok = false;
    fclose(f);

    if (!ok) {
        remove(tmp.c_str());
        return false;
    }

    remove(path.c_str());
    if (rename(tmp.c_str(), path.c_str()) != 0) {
        remove(tmp.c_str());
        return false;
    }
    return true;
}

bool fileExists(const std::string& path)
{
    struct stat st;
    return stat(path.c_str(), &st) == 0;
}

uint64_t nowUnix()
{
    return static_cast<uint64_t>(time(nullptr));
}

uint64_t monotonicMs()
{
    return armTicksToNs(armGetSystemTick()) / 1000000ULL;
}

std::string relativeTime(uint64_t then, uint64_t now)
{
    if (then == 0)
        return "unknown";
    if (now < then)
        now = then;

    uint64_t delta = now - then;
    if (delta < 60)
        return "just now";
    if (delta < 3600)
        return format("%llum ago", (unsigned long long)(delta / 60));
    if (delta < 24 * 3600)
        return format("%lluh ago", (unsigned long long)(delta / 3600));

    time_t t0 = static_cast<time_t>(then);
    struct tm a {};
    localtime_r(&t0, &a);

    int dayDiff = static_cast<int>(delta / (24 * 3600));
    if (dayDiff <= 1)
        return "Yesterday";
    if (delta < 7 * 24 * 3600)
        return format("%d days ago", dayDiff);

    static const char* months[12] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
    return format("%d %s", a.tm_mday, months[a.tm_mon % 12]);
}

std::string weekdayShort(uint64_t when)
{
    static const char* days[7] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
    time_t t = static_cast<time_t>(when);
    struct tm tmv {};
    localtime_r(&t, &tmv);
    return days[tmv.tm_wday % 7];
}

std::string format(const char* fmt, ...)
{
    char stackBuf[256];

    va_list ap;
    va_start(ap, fmt);
    int needed = vsnprintf(stackBuf, sizeof(stackBuf), fmt, ap);
    va_end(ap);

    if (needed < 0)
        return std::string();
    if (static_cast<size_t>(needed) < sizeof(stackBuf))
        return std::string(stackBuf, static_cast<size_t>(needed));

    std::string out;
    out.resize(static_cast<size_t>(needed) + 1);
    va_start(ap, fmt);
    vsnprintf(&out[0], out.size(), fmt, ap);
    va_end(ap);
    out.resize(static_cast<size_t>(needed));
    return out;
}

std::string toHex(const void* data, size_t len)
{
    static const char* digits = "0123456789abcdef";
    const uint8_t* p = static_cast<const uint8_t*>(data);
    std::string out;
    out.reserve(len * 2);
    for (size_t i = 0; i < len; i++) {
        out.push_back(digits[p[i] >> 4]);
        out.push_back(digits[p[i] & 0xF]);
    }
    return out;
}

static int hexVal(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

bool fromHex(const std::string& hex, void* out, size_t outLen)
{
    if (hex.size() != outLen * 2)
        return false;
    uint8_t* p = static_cast<uint8_t*>(out);
    for (size_t i = 0; i < outLen; i++) {
        int hi = hexVal(hex[i * 2]);
        int lo = hexVal(hex[i * 2 + 1]);
        if (hi < 0 || lo < 0)
            return false;
        p[i] = static_cast<uint8_t>((hi << 4) | lo);
    }
    return true;
}

std::string trim(const std::string& s)
{
    size_t a = 0, b = s.size();
    while (a < b && static_cast<unsigned char>(s[a]) <= ' ')
        a++;
    while (b > a && static_cast<unsigned char>(s[b - 1]) <= ' ')
        b--;
    return s.substr(a, b - a);
}

std::string titleCase(const std::string& s)
{
    std::string out = s;
    bool atWordStart = true;
    for (char& c : out) {
        if (atWordStart && c >= 'a' && c <= 'z')
            c = static_cast<char>(c - 32);
        atWordStart = c == ' ' || c == '\t' || c == '/';
    }
    return out;
}

bool utf8Next(const char*& p, const char* end, uint32_t& cp)
{
    if (p >= end)
        return false;

    uint8_t c = static_cast<uint8_t>(*p);
    int extra;
    uint32_t value;

    if (c < 0x80) {
        cp = c;
        p++;
        return true;
    } else if ((c & 0xE0) == 0xC0) {
        extra = 1;
        value = c & 0x1Fu;
    } else if ((c & 0xF0) == 0xE0) {
        extra = 2;
        value = c & 0x0Fu;
    } else if ((c & 0xF8) == 0xF0) {
        extra = 3;
        value = c & 0x07u;
    } else {
        // Stray continuation byte: consume it and report a replacement char.
        p++;
        cp = 0xFFFD;
        return true;
    }

    if (p + 1 + extra > end) {
        p = end;
        cp = 0xFFFD;
        return true;
    }

    for (int i = 1; i <= extra; i++) {
        uint8_t cc = static_cast<uint8_t>(p[i]);
        if ((cc & 0xC0) != 0x80) {
            p += 1;
            cp = 0xFFFD;
            return true;
        }
        value = (value << 6) | (cc & 0x3Fu);
    }

    p += 1 + extra;
    cp = value;
    return true;
}

size_t utf8Length(const std::string& s)
{
    const char* p = s.data();
    const char* end = p + s.size();
    size_t n = 0;
    uint32_t cp;
    while (utf8Next(p, end, cp))
        n++;
    return n;
}

std::string clampUtf8(const std::string& s, size_t maxCodepoints)
{
    const char* p = s.data();
    const char* end = p + s.size();
    size_t n = 0;
    uint32_t cp;
    while (p < end) {
        const char* before = p;
        if (!utf8Next(p, end, cp))
            break;
        if (++n > maxCodepoints)
            return std::string(s.data(), static_cast<size_t>(before - s.data()));
    }
    return s;
}

void sha256Over(const std::vector<std::string>& parts, uint8_t out[32])
{
    Sha256Context ctx;
    sha256ContextCreate(&ctx);
    for (const std::string& part : parts)
        sha256ContextUpdate(&ctx, part.data(), part.size());
    sha256ContextGetHash(&ctx, out);
}

void randomBytes(void* out, size_t len)
{
    // csrng is the hardware-backed generator; randomGet is a userspace CSPRNG
    // seeded from it and always available, so it is the safe fallback.
    if (R_FAILED(csrngGetRandomBytes(out, len)))
        randomGet(out, len);
}

std::string randomHex(size_t bytes)
{
    uint8_t buf[64];
    if (bytes > sizeof(buf))
        bytes = sizeof(buf);
    randomBytes(buf, bytes);
    return toHex(buf, bytes);
}

uint32_t fnv1a(const std::string& s)
{
    uint32_t h = 2166136261u;
    for (char c : s) {
        h ^= static_cast<uint8_t>(c);
        h *= 16777619u;
    }
    return h;
}

} // namespace nxp
