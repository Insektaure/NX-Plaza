#include "core/log.h"

#include "core/util.h"

#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <string>
#include <vector>

namespace nxp {

namespace {
    // A session logs a line or two every twenty seconds, forever, so a file
    // left running is a file that grows for as long as the console is on. At
    // the cap the current file becomes plaza.log.1 and a fresh one starts:
    // half a megabyte on disk at worst, and the recent history - which is the
    // half worth having when something has just gone wrong - is always there.
    constexpr long kMaxBytes = 256 * 1024;

    // Lines from before the settings were read. Bounded: this exists to carry
    // a startup across the moment the store loads, not to be a second log.
    constexpr size_t kMaxEarlyLines = 64;

    FILE* g_file = nullptr;
    bool g_enabled = false;
    bool g_settled = false; // the stored setting has been applied at least once
    long g_written = 0;
    std::vector<std::string> g_early;
    std::mutex g_mutex;

    std::string path() { return dataPath("plaza.log"); }
    std::string oldPath() { return dataPath("plaza.log.1"); }

    // Caller holds the lock.
    void closeFile()
    {
        if (g_file) {
            fclose(g_file);
            g_file = nullptr;
        }
        g_written = 0;
    }

    // Caller holds the lock. Truncates: a log is about this run.
    bool openFile()
    {
        ensureDataDir();
        g_file = fopen(path().c_str(), "w");
        g_written = 0;
        return g_file != nullptr;
    }

    // Caller holds the lock.
    void writeLine(const char* line, int length)
    {
        if (!g_file)
            return;

        if (g_written + length > kMaxBytes) {
            closeFile();
            // rename() will not replace an existing file on FAT, so the older
            // generation goes first.
            remove(oldPath().c_str());
            rename(path().c_str(), oldPath().c_str());
            if (!openFile())
                return;
        }

        fputs(line, g_file);
        fflush(g_file);
        g_written += length;
    }
}

void logInit()
{
    // Deliberately opens nothing. The setting that decides whether there is a
    // file at all lives in the store, which has not loaded yet.
    ensureDataDir();
}

void logExit()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    closeFile();
    g_enabled = false;
}

bool logFileEnabled()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_enabled;
}

std::string logFilePath()
{
    return path();
}

void logSetFileEnabled(bool on)
{
    std::vector<std::string> early;
    {
        std::lock_guard<std::mutex> lock(g_mutex);

        if (on && !g_file) {
            if (openFile()) {
                g_enabled = true;
                early.swap(g_early); // written below, still under the lock
            } else {
                g_enabled = false;
            }
        } else if (!on) {
            closeFile();
            g_enabled = false;
        }

        // Whichever way it went, the startup buffer has served its purpose.
        if (!g_settled) {
            g_settled = true;
            g_early.clear();
            g_early.shrink_to_fit();
        }

        for (const std::string& line : early)
            writeLine(line.c_str(), static_cast<int>(line.size()));
    }
}

void logPrint(const char* fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf) - 2, fmt, ap);
    va_end(ap);
    if (n < 0)
        return;
    if (static_cast<size_t>(n) > sizeof(buf) - 2)
        n = static_cast<int>(sizeof(buf) - 2);
    buf[n] = '\n';
    buf[n + 1] = '\0';

    std::lock_guard<std::mutex> lock(g_mutex);

    // stdout always: it costs nothing when nobody is listening, and it is what
    // nxlink shows during development.
    fputs(buf, stdout);

    if (g_file) {
        writeLine(buf, n + 1);
    } else if (!g_settled && g_early.size() < kMaxEarlyLines) {
        g_early.emplace_back(buf, static_cast<size_t>(n + 1));
    }
}

} // namespace nxp
