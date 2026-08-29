#include "core/play_history.h"

#include "core/log.h"
#include "core/util.h"

#include <switch.h>
#include <zlib.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <mutex>
#include <vector>

namespace nxp {

namespace {
    constexpr size_t kKeepCount = 5;

    // How many installed titles to consider.
    //
    // This was 64 back when each title cost its own play-statistics call. The
    // batched query asks about a whole page at once, so a page now costs two
    // IPC calls whatever is on it, and the old reason to keep this small is
    // gone. Meanwhile the cap is a real limit, not a budget: records do not
    // arrive in play order, so a title past the end is never offered however
    // recently it was played - and a real console hit it.
    constexpr s32 kMaxRecords = 256;
    constexpr s32 kRecordBatch = 32;

    // Written by the loader thread, read by the drawing thread. Everything
    // that crosses between them is behind this.
    std::mutex g_mutex;
    Thread g_thread {};
    bool g_started = false;
    bool g_running = false; // the loader thread exists and must be joined
    bool g_ready = false;
    std::vector<PlayedTitle> g_titles;

    // Counts, so the log can say where the time went rather than only how much.
    int g_statQueries = 0;
    int g_controlReads = 0;
    int g_cacheHits = 0;
    int g_statFailures = 0;
    int g_blankNames = 0;
    bool g_truncated = false;
    bool g_listFailed = false;

    struct Candidate {
        u64 applicationId;
        u64 lastPlayed; // PosixTime
    };

    // Firmware 21 compressed the NACP's language table.
    void normalizeNacp(NacpStruct* nacp)
    {
        constexpr size_t kLanguageDataSize = 0x3000;
        constexpr size_t kFormatOffset = 0x3215;
        constexpr size_t kCompressedOffset = sizeof(u16);
        constexpr size_t kDecompressedCount = 32;
        constexpr size_t kLegacyCount = 16;

        static_assert(sizeof(NacpStruct) > kFormatOffset, "NACP is smaller than expected");
        static_assert(sizeof(NacpLanguageEntry) * kLegacyCount == kLanguageDataSize,
            "the language table is not where it was");

        u8* raw = reinterpret_cast<u8*>(nacp);
        if (raw[kFormatOffset] != 1)
            return; // the old, uncompressed layout

        u16 compressedSize = 0;
        std::memcpy(&compressedSize, raw, sizeof(compressedSize));
        if (compressedSize == 0 || compressedSize > kLanguageDataSize - kCompressedOffset) {
            LOG("play history: NACP says %u compressed bytes; not usable", compressedSize);
            return;
        }

        std::unique_ptr<NacpLanguageEntry[]> out(new NacpLanguageEntry[kDecompressedCount]);
        std::memset(out.get(), 0, sizeof(NacpLanguageEntry) * kDecompressedCount);

        z_stream stream {};
        if (inflateInit2(&stream, -15) != Z_OK) { // raw deflate, no zlib header
            LOG("play history: could not start NACP decompression");
            return;
        }
        stream.next_in = raw + kCompressedOffset;
        stream.avail_in = compressedSize;
        stream.next_out = reinterpret_cast<Bytef*>(out.get());
        stream.avail_out = sizeof(NacpLanguageEntry) * kDecompressedCount;
        int result = inflate(&stream, Z_FINISH);
        inflateEnd(&stream);

        if (result != Z_STREAM_END) {
            LOG("play history: NACP decompression failed (%d)", result);
            return;
        }

        bool hasLegacy = false;
        for (size_t i = 0; i < kLegacyCount && !hasLegacy; i++)
            hasLegacy = out[i].name[0] != '\0';

        std::memset(raw, 0, kLanguageDataSize);
        if (hasLegacy) {
            // Every original slot, so picking by system language still works.
            std::memcpy(raw, out.get(), kLanguageDataSize);
            return;
        }

        // Only one of the new slots is filled in. Put it where anything reading
        // the old layout will find it.
        for (size_t i = kLegacyCount; i < kDecompressedCount; i++) {
            if (out[i].name[0] != '\0') {
                std::memcpy(raw, &out[i], sizeof(NacpLanguageEntry));
                return;
            }
        }
    }

    // Well-formed UTF-8?
    //
    // Worth checking rather than assuming. A NACP language slot that was never
    // filled in can still hold a stray byte, and one console handed back a name
    // that was a single 0x97 - a continuation byte with nothing in front of it.
    // That is not a short name, it is not a name.
    bool validUtf8(const char* text, size_t length)
    {
        for (size_t i = 0; i < length;) {
            unsigned char c = static_cast<unsigned char>(text[i]);
            size_t extra;
            if (c < 0x80)
                extra = 0;
            else if ((c & 0xE0) == 0xC0 && c >= 0xC2)
                extra = 1;
            else if ((c & 0xF0) == 0xE0)
                extra = 2;
            else if ((c & 0xF8) == 0xF0 && c <= 0xF4)
                extra = 3;
            else
                return false; // a continuation byte or an illegal lead

            for (size_t k = 1; k <= extra; k++) {
                if (i + k >= length)
                    return false;
                if ((static_cast<unsigned char>(text[i + k]) & 0xC0) != 0x80)
                    return false;
            }
            i += extra + 1;
        }
        return true;
    }

    // One language slot, if it holds something usable.
    std::string entryName(const NacpLanguageEntry* entry)
    {
        if (entry == nullptr)
            return {};

        size_t length = strnlen(entry->name, sizeof(entry->name));
        if (length == 0 || !validUtf8(entry->name, length))
            return {};

        std::string name(entry->name, length);
        size_t begin = name.find_first_not_of(" \t\r\n");
        if (begin == std::string::npos)
            return {};
        size_t end = name.find_last_not_of(" \t\r\n");
        return name.substr(begin, end - begin + 1);
    }

    // The best name this NACP has: the console's own language when that slot is
    // filled in, and any of the other fifteen when it is not.
    //
    // nacpGetLanguageEntry picks by system language and does not check that what
    // it picked says anything. A Japanese-only title on an English console came
    // back with a one-byte slot, which was then shown as a blank row.
    std::string bestName(NacpStruct* nacp)
    {
        NacpLanguageEntry* preferred = nullptr;
        if (R_SUCCEEDED(nacpGetLanguageEntry(nacp, &preferred))) {
            std::string name = entryName(preferred);
            if (!name.empty())
                return name;
        }

        // Reached through the layout rather than the member name: libnx renamed
        // NacpStruct::lang to lang_data.lang, and which of those compiles
        // depends on how new the toolchain is. The table has been the first
        // 0x3000 bytes of the struct in every version, and the asserts below
        // fail loudly if that ever stops being true.
        static_assert(offsetof(NacpStruct, isbn) == 0x3000,
            "the NACP language table is no longer the first 0x3000 bytes");
        static_assert(sizeof(NacpLanguageEntry) * 16 == 0x3000,
            "sixteen language entries no longer fill the table");

        const NacpLanguageEntry* entries
            = reinterpret_cast<const NacpLanguageEntry*>(nacp);
        for (size_t i = 0; i < 16; i++) {
            std::string name = entryName(&entries[i]);
            if (!name.empty())
                return name;
        }
        return {};
    }

    // Does this NACP carry a usable name in any language?
    bool nacpHasName(NacpStruct* nacp)
    {
        return !bestName(nacp).empty();
    }

    // The display name for a title, or "" when the console cannot supply one.
    std::string titleName(u64 applicationId, NsApplicationControlData* buffer)
    {
        // Cache first. Either source hands back the title's 128 KB icon along
        // with the name - there is no call that returns only the name - but
        // the cached one does not go to storage for it. Anything the cache does
        // not hold is read the slow way, so this only ever saves time.
        // Cleared every time. The buffer is reused across titles, and a read
        // that half-fills it would otherwise leave the previous title's name in
        // place - which reads as the right sort of value and is the wrong one.
        std::memset(buffer, 0, sizeof(NsApplicationControlData));

        u64 actual = 0;
        g_controlReads++;
        Result rc = nsGetApplicationControlData(NsApplicationControlSource_CacheOnly,
            applicationId, buffer, sizeof(NsApplicationControlData), &actual);

        bool cached = R_SUCCEEDED(rc) && actual >= sizeof(buffer->nacp)
            && nacpHasName(&buffer->nacp);
        if (cached) {
            g_cacheHits++;
        } else {
            // Either nothing came back, or what came back had no name in it.
            // The cache can hold a thin entry; storage is the authority.
            std::memset(buffer, 0, sizeof(NsApplicationControlData));
            rc = nsGetApplicationControlData(NsApplicationControlSource_Storage,
                applicationId, buffer, sizeof(NsApplicationControlData), &actual);
            if (R_FAILED(rc) || actual < sizeof(buffer->nacp))
                return {};
        }

        normalizeNacp(&buffer->nacp);
        return bestName(&buffer->nacp);
    }

    // Installed titles, newest-played first, never-played dropped.
    std::vector<Candidate> playedTitles()
    {
        std::vector<Candidate> out;

        std::unique_ptr<NsApplicationRecord[]> records(new NsApplicationRecord[kRecordBatch]);
        std::vector<u64> ids;
        std::vector<PdmLastPlayTime> times;

        for (s32 offset = 0; offset < kMaxRecords; offset += kRecordBatch) {
            s32 count = 0;
            Result rc = nsListApplicationRecord(records.get(), kRecordBatch, offset, &count);
            if (R_FAILED(rc)) {
                LOG("play history: nsListApplicationRecord failed (0x%x)", rc);
                g_listFailed = true;
                break;
            }
            if (count <= 0)
                break;

            ids.clear();
            for (s32 i = 0; i < count; i++) {
                if (records[i].application_id != 0)
                    ids.push_back(records[i].application_id);
            }

            if (!ids.empty()) {
                times.assign(ids.size(), PdmLastPlayTime {});
                s32 got = 0;
                g_statQueries++;
                rc = pdmqryQueryLastPlayTime(true, times.data(), ids.data(),
                    static_cast<s32>(ids.size()), &got);

                if (R_FAILED(rc)) {
                    g_statFailures++;
                    LOG("play history: pdmqryQueryLastPlayTime failed (0x%x)", rc);
                } else {
                    for (s32 i = 0; i < got; i++) {
                        // Installed but never opened: on the console, not in a
                        // history. The flag is what says which.
                        if (!times[i].flag || times[i].timestamp_user == 0)
                            continue;
                        out.push_back(Candidate { times[i].application_id,
                            pdmPlayTimestampToPosix(times[i].timestamp_user) });
                    }
                }
            }

            if (count < kRecordBatch)
                break;

            // More installed than the cap allows for. Worth saying: it is the
            // one condition under which a game the owner played this morning
            // can be missing from the list.
            if (offset + kRecordBatch >= kMaxRecords)
                g_truncated = true;
        }

        std::sort(out.begin(), out.end(), [](const Candidate& a, const Candidate& b) {
            return a.lastPlayed > b.lastPlayed;
        });
        return out;
    }
}

namespace {

// Runs on the loader thread, and is the only thing that touches the services.
void loadNow()
{
    uint64_t startedMs = monotonicMs();
    std::vector<PlayedTitle> found;

    // Both services are opened for the length of this call rather than the life
    // of the app: the history is read once, and a session held open for hours to
    // answer one question is a session held open for nothing.
    Result rc = nsInitialize();
    if (R_FAILED(rc)) {
        LOG("play history: ns unavailable (0x%x)", rc);
        return;
    }
    rc = pdmqryInitialize();
    if (R_FAILED(rc)) {
        LOG("play history: pdm:qry unavailable (0x%x)", rc);
        nsExit();
        return;
    }

    std::vector<Candidate> played = playedTitles();

    if (played.empty()) {
        pdmqryExit();
        // Three different states, and telling a person "nothing played on this
        // console" when the truth is "this launch mode is not allowed to ask"
        // sends them looking in the wrong place. Applet mode - launched over
        // the album rather than in place of a game - is refused both services
        // on most firmware.
        if (g_listFailed)
            LOG("play history: cannot list titles here (applet mode?)");
        else if (g_statQueries > 0 && g_statFailures == g_statQueries)
            LOG("play history: pdm refused every batch (applet mode?)");
        else
            LOG("play history: nothing played on this console");
        nsExit();
        return;
    }

    // 144 KB. Far too large for the stack, and needed once.
    std::unique_ptr<NsApplicationControlData> buffer(new NsApplicationControlData());

    for (const Candidate& candidate : played) {
        if (found.size() >= kKeepCount)
            break;

        std::string name = titleName(candidate.applicationId, buffer.get());
        if (name.empty()) {
            g_blankNames++;
            LOG("play history: %016llx has no name; skipped",
                static_cast<unsigned long long>(candidate.applicationId));
            continue;
        }

        // How long this console has spent in it. Console-wide rather than per
        // account, which is both what the uid-free call gives and the more
        // honest number for a pass: it is the console that crosses paths.
        uint32_t hours = 0;
        PdmPlayStatistics stats {};
        if (R_SUCCEEDED(pdmqryQueryPlayStatisticsByApplicationId(
                candidate.applicationId, true, &stats))) {
            constexpr u64 kNsPerHour = 1000000000ULL * 3600ULL;
            hours = static_cast<uint32_t>(std::min<u64>(stats.playtime / kNsPerHour, 99999));
        }

        LOG("play history: %016llx \"%s\" (%zu bytes, %uh)",
            static_cast<unsigned long long>(candidate.applicationId), name.c_str(),
            name.size(), hours);
        found.push_back(PlayedTitle { candidate.applicationId, name, hours });
    }

    pdmqryExit();
    nsExit();

    // The control-data read is the expensive one: it hands back the title's
    // 128 KB icon along with the name, and there is no call that returns only
    // the name.
    LOG("play history: %zu named of %zu played in %llu ms "
        "(%d play-time batches, %d failed, %d control reads, %d cached, "
        "%d unnamed, %d KB copied)%s",
        found.size(), played.size(),
        static_cast<unsigned long long>(monotonicMs() - startedMs),
        g_statQueries, g_statFailures, g_controlReads, g_cacheHits, g_blankNames,
        static_cast<int>(g_controlReads * sizeof(NsApplicationControlData) / 1024),
        g_truncated ? " [more than 64 titles installed; the rest were not looked at]" : "");

    std::lock_guard<std::mutex> lock(g_mutex);
    g_titles = std::move(found);
}

void threadEntry(void*)
{
    loadNow();
    std::lock_guard<std::mutex> lock(g_mutex);
    g_ready = true;
}

} // namespace

void beginPlayHistoryLoad()
{
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_started)
            return;
        g_started = true;
    }

    // Low priority and a small stack: this is housekeeping, and it must never
    // compete with drawing. Larger numbers are lower priority on this platform,
    // so 0x3B sits below the main thread's 0x2C.
    Result rc = threadCreate(&g_thread, threadEntry, nullptr, nullptr, 32 * 1024, 0x3B, -2);
    if (R_SUCCEEDED(rc)) {
        rc = threadStart(&g_thread);
        if (R_FAILED(rc))
            threadClose(&g_thread);
    }

    if (R_SUCCEEDED(rc)) {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_running = true;
        return;
    }

    // No thread available. Read it here instead: slower than it should be, but
    // a console that cannot spawn a thread should still get its list. Nothing
    // is locked across this - loadNow takes the lock itself, at the end.
    LOG("play history: no loader thread (0x%x); reading inline", rc);
    loadNow();

    std::lock_guard<std::mutex> lock(g_mutex);
    g_ready = true;
}

bool playHistoryReady()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_ready;
}

std::vector<PlayedTitle> recentlyPlayed()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_titles;
}

void endPlayHistory()
{
    bool joinable = false;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        joinable = g_running;
        g_running = false;
    }

    // Outside the lock: the thread takes it on its way out, and waiting for it
    // while holding it would be waiting for something that cannot finish.
    if (joinable) {
        threadWaitForExit(&g_thread);
        threadClose(&g_thread);
    }

    std::lock_guard<std::mutex> lock(g_mutex);
    g_titles.clear();
    g_started = false;
    g_ready = false;
}

} // namespace nxp
