#include "net/update.h"

#include "core/json.h"
#include "core/log.h"
#include "core/util.h"
#include "net/http.h"

#include <switch.h>

#include <minizip/unzip.h>

#include <atomic>
#include <cerrno>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <vector>

// Set by the Makefile, beside PLAZA_SERVER. The fallback is here so the file
// still compiles on its own, not as a second place to configure this.
#ifndef PLAZA_REPO
#define PLAZA_REPO "Insektaure/NX-Plaza"
#endif

namespace nxp {

namespace {
    const char* kApi = "https://api.github.com/repos/" PLAZA_REPO "/releases/latest";

    // Staged next to the rest of our data rather than beside the running NRO:
    // the app owns this folder, and a half-finished download has no business
    // appearing in the user's homebrew menu.
    const char* kDownloadFile = "update.part";     // whatever the release published
    const char* kStagedFile = "update.nro.part";  // the NRO, once we have one
    const char* kBackupFile = "update.nro.backup";

    // A release NRO is a few megabytes. This is a guard against a redirect to
    // something enormous, not a real limit.
    constexpr uint64_t kMaxAssetBytes = 64ull * 1024 * 1024;

    std::mutex g_mutex;
    std::atomic<bool> g_busy { false };
    std::atomic<bool> g_installing { false };

    UpdateState g_state = UpdateState::Idle;
    std::string g_version;
    std::string g_message;
    std::string g_assetUrl;
    std::string g_assetName; // decides how the download is unpacked
    std::atomic<float> g_progress { 0.0f };
    std::atomic<bool> g_announce { false };
    std::atomic<bool> g_restart { false };
    // Set when the app is closing. Only the download honours it: once the copy
    // over the running NRO has started, stopping half way is the one thing more
    // dangerous than finishing.
    std::atomic<bool> g_cancel { false };

    Thread g_thread {};
    bool g_threadLive = false;

    void setState(UpdateState state, const std::string& message)
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_state = state;
        g_message = message;
    }

    // ---- versions

    std::vector<int> parts(const std::string& raw)
    {
        std::vector<int> out;
        size_t i = 0;
        // Tags are conventionally "v1.2.3"; the v is decoration.
        if (i < raw.size() && (raw[i] == 'v' || raw[i] == 'V'))
            i++;
        int value = 0;
        bool any = false;
        for (; i <= raw.size(); i++) {
            if (i < raw.size() && raw[i] >= '0' && raw[i] <= '9') {
                // Saturates rather than wrapping: a garbage tag should sort
                // predictably, not become a negative number.
                if (value < 100000)
                    value = value * 10 + (raw[i] - '0');
                any = true;
                continue;
            }
            if (any)
                out.push_back(value);
            value = 0;
            any = false;
            // Anything that is not a dot ends the version: "1.2.3-beta4" is
            // 1.2.3, not 1.2.3.4.
            if (i < raw.size() && raw[i] != '.')
                break;
        }
        return out;
    }

    // ---- files

    bool endsWithNoCase(const std::string& s, const char* suffix)
    {
        size_t n = strlen(suffix);
        return s.size() >= n && strcasecmp(s.c_str() + s.size() - n, suffix) == 0;
    }

    // Pulls the app's own NRO out of a release zip and writes it to `to`.
    //
    // Only one file ever comes out, and it goes to a path we chose: the name
    // inside the archive is used to decide *whether* to extract an entry and
    // never as somewhere to write. An archive cannot talk us into scattering
    // files across the SD card, however its entries are named.
    bool extractNro(const std::string& from, const std::string& to)
    {
        unzFile zip = unzOpen(from.c_str());
        if (!zip) {
            LOG("update: the download is not a readable zip");
            return false;
        }

        bool found = false;
        int step = unzGoToFirstFile(zip);
        while (step == UNZ_OK && !found) {
            unz_file_info64 info {};
            char name[512] {};
            if (unzGetCurrentFileInfo64(zip, &info, name, sizeof(name) - 1, nullptr, 0, nullptr, 0)
                != UNZ_OK)
                break;

            std::string entry(name);
            if (endsWithNoCase(entry, ".nro") && info.uncompressed_size <= kMaxAssetBytes)
                found = true;
            else
                step = unzGoToNextFile(zip);
        }

        if (!found) {
            LOG("update: the zip holds no .nro");
            unzClose(zip);
            return false;
        }

        if (unzOpenCurrentFile(zip) != UNZ_OK) {
            unzClose(zip);
            return false;
        }

        FILE* out = fopen(to.c_str(), "wb");
        if (!out) {
            unzCloseCurrentFile(zip);
            unzClose(zip);
            return false;
        }

        std::vector<char> buffer(64 * 1024);
        bool ok = true;
        uint64_t written = 0;
        for (;;) {
            int got = unzReadCurrentFile(zip, buffer.data(), unsigned(buffer.size()));
            if (got < 0) {
                ok = false;
                break;
            }
            if (got == 0)
                break;
            // The header's size is a claim; this is the actual byte count, and
            // a zip bomb has to be stopped by what comes out, not what it said.
            written += uint64_t(got);
            if (written > kMaxAssetBytes) {
                LOG("update: the zip expands to more than we will accept");
                ok = false;
                break;
            }
            if (fwrite(buffer.data(), 1, size_t(got), out) != size_t(got)) {
                ok = false;
                break;
            }
        }

        if (fflush(out) != 0)
            ok = false;
        if (fclose(out) != 0)
            ok = false;
        unzCloseCurrentFile(zip);
        unzClose(zip);

        if (!ok)
            remove(to.c_str());
        return ok;
    }

    // fopen(path, "wb") is O_CREAT|O_TRUNC, and asking this filesystem to
    // truncate-on-open the NRO it is currently running comes back EIO. Creating
    // the file (harmless when it exists), opening what is there for writing,
    // and setting the length explicitly never asks for that truncation, and is
    // how the homebrew that does this successfully does it.
    bool overwriteFile(const std::string& from, const std::string& to, std::string* whyOut)
    {
        auto fail = [&](const char* step, Result rc) {
            std::string why = rc != 0 ? format("%s (0x%x)", step, rc)
                                      : format("%s: %s", step, strerror(errno));
            LOG("update: replacing %s failed at %s", to.c_str(), why.c_str());
            if (whyOut)
                *whyOut = why;
            return false;
        };

        FILE* in = fopen(from.c_str(), "rb");
        if (!in)
            return fail("opening the new version", 0);

        fseek(in, 0, SEEK_END);
        long total = ftell(in);
        rewind(in);
        if (total <= 0) {
            fclose(in);
            return fail("the new version is empty", 0);
        }

        FsFileSystem* sd = fsdevGetDeviceFileSystem("sdmc:");
        if (!sd) {
            fclose(in);
            return fail("finding the SD card", 0);
        }

        // The native API takes a path from the root of the filesystem, so the
        // device prefix that argv[0] carries has to come off.
        std::string path = to;
        size_t colon = path.find(':');
        if (colon != std::string::npos)
            path.erase(0, colon + 1);

        // Fails when it already exists, which is the usual case here and not a
        // problem: a genuine failure surfaces when it is opened below.
        fsFsCreateFile(sd, path.c_str(), total, 0);

        FsFile file {};
        Result rc = fsFsOpenFile(sd, path.c_str(), FsOpenMode_Write, &file);
        if (R_FAILED(rc)) {
            fclose(in);
            return fail("opening it for writing", rc);
        }

        // Explicitly, rather than by truncating on open. Shrinks as well as
        // grows, so a smaller build does not leave the tail of a larger one.
        rc = fsFileSetSize(&file, total);
        if (R_FAILED(rc)) {
            fsFileClose(&file);
            fclose(in);
            return fail("setting its length", rc);
        }

        std::vector<char> buffer(64 * 1024);
        s64 offset = 0;
        bool ok = true;
        const char* step = "writing";
        while (offset < total) {
            size_t got = fread(buffer.data(), 1, buffer.size(), in);
            if (got == 0) {
                ok = feof(in) == 0 ? false : (offset == total);
                step = "reading the new version";
                break;
            }
            rc = fsFileWrite(&file, offset, buffer.data(), got, FsWriteOption_None);
            if (R_FAILED(rc)) {
                ok = false;
                break;
            }
            offset += s64(got);
        }

        fsFileClose(&file);
        fclose(in);

        // Without this the write can still be sitting in the filesystem's own
        // buffers, and this particular file has to survive the app relaunching.
        if (ok) {
            Result commit = fsFsCommit(sd);
            if (R_FAILED(commit))
                return fail("committing it to the card", commit);
        }

        if (!ok)
            return fail(step, rc);
        return true;
    }

    bool copyFile(const std::string& from, const std::string& to, std::string* whyOut = nullptr)
    {
        auto fail = [&](const char* step) {
            std::string why = format("%s: %s", step, strerror(errno));
            LOG("update: copy %s -> %s failed at %s", from.c_str(), to.c_str(), why.c_str());
            if (whyOut)
                *whyOut = why;
            return false;
        };

        FILE* in = fopen(from.c_str(), "rb");
        if (!in)
            return fail("opening the new version");

        // Only ever writes the backup, which is ours and is never open
        // elsewhere. Anything that has to land on the running NRO goes through
        // overwriteFile() instead, which does not truncate on open.
        remove(to.c_str());

        FILE* out = fopen(to.c_str(), "wb");
        if (!out) {
            fclose(in);
            return fail("creating the file to write over");
        }

        std::vector<char> buffer(64 * 1024);
        bool ok = true;
        const char* step = "copying";
        while (ok) {
            size_t got = fread(buffer.data(), 1, buffer.size(), in);
            if (got == 0) {
                if (feof(in))
                    break;
                ok = false;
                step = "reading the new version";
                break;
            }
            if (fwrite(buffer.data(), 1, got, out) != got) {
                ok = false;
                step = "writing";
                break;
            }
        }

        // A card that filled up reports it here or at the close, not earlier.
        if (ok && fflush(out) != 0) {
            ok = false;
            step = "flushing";
        }
        if (fclose(out) != 0 && ok) {
            ok = false;
            step = "closing";
        }
        fclose(in);

        if (!ok) {
            remove(to.c_str());
            return fail(step);
        }
        return true;
    }
}

// ------------------------------------------------------------------ versions

int compareVersions(const std::string& a, const std::string& b)
{
    std::vector<int> left = parts(a);
    std::vector<int> right = parts(b);
    size_t count = std::max(left.size(), right.size());
    for (size_t i = 0; i < count; i++) {
        // A missing component is a zero, so 1.2 and 1.2.0 are one version.
        int l = i < left.size() ? left[i] : 0;
        int r = i < right.size() ? right[i] : 0;
        if (l != r)
            return l < r ? -1 : 1;
    }
    return 0;
}

// ------------------------------------------------------------------ NRO/NACP

std::string nroDisplayVersion(const std::string& path)
{
    FILE* f = fopen(path.c_str(), "rb");
    if (!f)
        return std::string();

    auto done = [&f](const char* why) {
        if (why)
            LOG("update: %s", why);
        fclose(f);
        return std::string();
    };

    // The NRO header sits after the 0x10-byte start stub.
    NroHeader header {};
    if (fseek(f, sizeof(NroStart), SEEK_SET) != 0
        || fread(&header, 1, sizeof(header), f) != sizeof(header))
        return done("the staged file is too short to be an NRO");
    if (header.magic != NROHEADER_MAGIC)
        return done("the staged file is not an NRO");

    // The asset section, which carries the icon, the NACP and the RomFS, is
    // appended after the code image.
    NroAssetHeader assets {};
    if (fseek(f, long(header.size), SEEK_SET) != 0
        || fread(&assets, 1, sizeof(assets), f) != sizeof(assets))
        return done("the staged NRO has no asset section");
    if (assets.magic != NROASSETHEADER_MAGIC)
        return done("the staged NRO has no asset section");
    if (assets.nacp.size < sizeof(NacpStruct))
        return done("the staged NRO carries no NACP");

    NacpStruct nacp {};
    if (fseek(f, long(header.size + assets.nacp.offset), SEEK_SET) != 0
        || fread(&nacp, 1, sizeof(nacp), f) != sizeof(nacp))
        return done("the staged NRO's NACP could not be read");

    fclose(f);

    // The field is a fixed-size buffer and is not required to be terminated.
    char version[sizeof(nacp.display_version) + 1] {};
    memcpy(version, nacp.display_version, sizeof(nacp.display_version));
    return std::string(version);
}

// ------------------------------------------------------------------- the job

Update& Update::get()
{
    static Update instance;
    return instance;
}

UpdateState Update::state() const
{
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_state;
}

std::string Update::version() const
{
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_version;
}

std::string Update::message() const
{
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_message;
}

float Update::progress() const { return g_progress.load(); }
bool Update::announce() const { return g_announce.load(); }
bool Update::wantsRestart() const { return g_restart.load(); }

void Update::threadEntry(void* arg)
{
    static_cast<Update*>(arg)->run();
}

void Update::run()
{
    if (g_installing.load())
        installNow();
    else
        checkNow();
    g_busy.store(false);
}

bool Update::spawn()
{
    // One at a time. A second press while a check is in flight is a no-op
    // rather than a second worker fighting over the same curl handle.
    bool expected = false;
    if (!g_busy.compare_exchange_strong(expected, true))
        return false;

    if (g_threadLive) {
        threadWaitForExit(&g_thread);
        threadClose(&g_thread);
        g_threadLive = false;
    }

    // Same reasoning as the play-history loader: housekeeping must never
    // compete with drawing, and larger numbers are lower priority here.
    Result rc = threadCreate(&g_thread, threadEntry, this, nullptr, 64 * 1024, 0x3B, -2);
    if (R_SUCCEEDED(rc)) {
        rc = threadStart(&g_thread);
        if (R_FAILED(rc))
            threadClose(&g_thread);
    }
    if (R_FAILED(rc)) {
        LOG("update: no worker thread (0x%x)", rc);
        g_busy.store(false);
        setState(UpdateState::Failed, "This console would not start the update worker.");
        return false;
    }

    g_threadLive = true;
    return true;
}

void Update::beginCheck(bool announce)
{
    // Nothing is stamped on the state until the worker is actually ours to
    // start: a check arriving mid-download must not relabel the download.
    if (g_busy.load())
        return;

    g_announce.store(announce);
    g_installing.store(false);
    setState(UpdateState::Checking, "Checking for updates");
    spawn();
}

void Update::beginInstall()
{
    if (state() != UpdateState::Available || g_busy.load())
        return;
    g_installing.store(true);
    g_progress.store(0.0f);
    setState(UpdateState::Downloading, "Downloading");
    spawn();
}

void Update::shutdown()
{
    // Asks a download in flight to give up rather than making the user wait out
    // a transfer they have already quit.
    g_cancel.store(true);
    if (g_threadLive) {
        threadWaitForExit(&g_thread);
        threadClose(&g_thread);
        g_threadLive = false;
    }
}

void Update::checkNow()
{
    // Twenty seconds, not the eight a plaza call gets. This is a cold
    // handshake to a host we have never spoken to, and curl on this toolchain
    // predates CURLOPT_CA_CACHE_TIMEOUT, so it re-reads and re-parses the whole
    // CA bundle off the SD card first. Eight seconds was not enough for that,
    // and the failure read as "timeout was reached" with nothing to act on.
    HttpResponse response = Http::get(kApi,
        { "Accept: application/vnd.github+json", "X-GitHub-Api-Version: 2022-11-28" },
        20000);

    if (!response.ok()) {
        std::string why = response.error.empty()
            ? format("the release server answered %ld", response.status)
            : response.error;
        LOG("update: check failed: %s", why.c_str());
        setState(UpdateState::Failed, why);
        return;
    }

    std::string parseError;
    json_t* root = js::parse(response.body, &parseError);
    if (!root) {
        setState(UpdateState::Failed, "The release list could not be read.");
        return;
    }

    std::string tag = js::getStr(root, "tag_name");
    if (tag.empty()) {
        json_decref(root);
        setState(UpdateState::Failed, "The release list carried no version.");
        return;
    }

    // Pick the asset before deciding anything, so "there is a newer release but
    // nothing to install" is distinguishable from "you are up to date".
    std::string url;
    std::string assetName;
    if (json_t* assets = js::getArr(root, "assets")) {
        size_t index;
        json_t* asset;
        int best = 0;
        json_array_foreach(assets, index, asset)
        {
            std::string name = js::getStr(asset, "name");
            std::string link = js::getStr(asset, "browser_download_url");
            if (name.empty() || link.empty())
                continue;

            // A bare .nro is best: nothing to unpack, so nothing to go wrong
            // between the download and the file we check. A zip is taken when
            // that is what the release publishes. A name that says nx-plaza
            // beats one that does not, so a release carrying several NROs
            // still picks ours.
            // Lower-cased first, so the name match is as case-blind as the
            // extension match beside it. A release asset called NX-Plaza.nro
            // should not lose to one called build.nro on a capital letter.
            std::string lower = name;
            for (char& c : lower)
                c = char(tolower(static_cast<unsigned char>(c)));
            bool named = lower.find("nx-plaza") != std::string::npos;
            int score = 0;
            if (endsWithNoCase(name, ".nro"))
                score = named ? 4 : 3;
            else if (endsWithNoCase(name, ".zip"))
                score = named ? 2 : 1;

            if (score > best) {
                best = score;
                url = link;
                assetName = name;
            }
        }
    }
    json_decref(root);

    std::string latest = tag;
    if (!latest.empty() && (latest[0] == 'v' || latest[0] == 'V'))
        latest.erase(0, 1);

    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_version = latest;
        g_assetUrl = url;
        g_assetName = assetName;
    }

    if (compareVersions(latest, APP_VERSION) <= 0) {
        LOG("update: %s is current (latest %s)", APP_VERSION, latest.c_str());
        setState(UpdateState::UpToDate, format("nx-plaza %s is the latest version.", APP_VERSION));
        return;
    }
    if (url.empty()) {
        setState(UpdateState::Failed,
            format("Release %s has no .nro to install.", latest.c_str()));
        return;
    }

    LOG("update: %s is available (running %s)", latest.c_str(), APP_VERSION);
    setState(UpdateState::Available, format("Version %s is available.", latest.c_str()));
}

void Update::installNow()
{
    std::string url, wanted, assetName;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        url = g_assetUrl;
        wanted = g_version;
        assetName = g_assetName;
    }

    const std::string exe = executablePath();
    if (exe.empty()) {
        setState(UpdateState::Failed,
            "This build does not know its own path, so it cannot replace itself.");
        return;
    }

    // From the asset's own name, which is what the choice above was made on.
    // A URL can carry a query or a redirect suffix and stop looking like the
    // file it serves; the name the release gave it does not.
    const bool zipped = endsWithNoCase(assetName, ".zip");
    const std::string download = dataPath(zipped ? kDownloadFile : kStagedFile);
    const std::string staged = dataPath(kStagedFile);
    const std::string backup = dataPath(kBackupFile);
    remove(download.c_str());
    remove(staged.c_str());
    remove(backup.c_str());

    // 1. Download to a staging file. The running NRO is never the target of a
    // transfer that might fail halfway.
    std::string error;
    bool ok = Http::download(url, download, &error, [](uint64_t got, uint64_t total) {
        if (g_cancel.load())
            return false;
        if (total > kMaxAssetBytes || got > kMaxAssetBytes)
            return false;
        g_progress.store(total > 0 ? float(double(got) / double(total)) : 0.0f);
        return true;
    });
    if (!ok) {
        remove(download.c_str());
        LOG("update: download failed: %s", error.c_str());
        setState(UpdateState::Failed, "The download did not finish: " + error);
        return;
    }

    // 2. A release published as an archive: take the one file we came for and
    // throw the archive away. Nothing else in it is unpacked.
    if (zipped) {
        setState(UpdateState::Downloading, "Unpacking");
        bool unpacked = extractNro(download, staged);
        remove(download.c_str());
        if (!unpacked) {
            remove(staged.c_str());
            setState(UpdateState::Failed, "The downloaded archive held no nx-plaza build.");
            return;
        }
    }

    // 3. Check what arrived before letting it anywhere near the running file.
    std::string stagedVersion = nroDisplayVersion(staged);
    if (stagedVersion.empty()) {
        remove(staged.c_str());
        setState(UpdateState::Failed, "The downloaded file is not a valid nx-plaza build.");
        return;
    }
    if (compareVersions(stagedVersion, wanted) != 0) {
        LOG("update: staged build says %s, release said %s", stagedVersion.c_str(),
            wanted.c_str());
        remove(staged.c_str());
        setState(UpdateState::Failed,
            format("The download is version %s, not %s.", stagedVersion.c_str(),
                wanted.c_str()));
        return;
    }

    // 4. Let go of our own RomFS before touching the file it lives in.
    //
    // This is what 0xE02 / FsError_TargetLocked was: romfsInit() mounts the
    // RomFS out of this very NRO and holds it open for the whole session, so
    // the app was locking the file against itself. Nothing reads it after
    // startup - the Mii parts and both shaders are loaded once and live in
    // memory and on the GPU from then on - so releasing it costs nothing, and
    // the guard puts it back on the way out however this ends.
    struct RomfsRelease {
        RomfsRelease() { romfsExit(); }
        ~RomfsRelease() { romfsInit(); }
    } releaseRomfs;

    // 5. Keep a copy of what works before overwriting it.
    setState(UpdateState::Downloading, "Installing");
    if (!copyFile(exe, backup)) {
        remove(staged.c_str());
        setState(UpdateState::Failed, "The current version could not be backed up.");
        return;
    }

    auto restore = [&](const char* why) {
        LOG("update: %s; restoring the backup", why);
        std::string restoreWhy;
        if (!overwriteFile(backup, exe, &restoreWhy))
            LOG("update: the backup could not be restored - it is still at %s", backup.c_str());
        else
            remove(backup.c_str());
        remove(staged.c_str());
    };

    std::string why;
    if (!overwriteFile(staged, exe, &why)) {
        restore("the new version could not be written");
        setState(UpdateState::Failed,
            "The update could not be written over the current one - " + why);
        return;
    }

    // 6. Read the installed file back off the card. A write that was truncated
    // or corrupted is still recoverable at this point, and not one step later.
    std::string installed = nroDisplayVersion(exe);
    if (compareVersions(installed, wanted) != 0) {
        restore("the installed file did not verify");
        setState(UpdateState::Failed, "The installed update did not verify, so it was rolled back.");
        return;
    }

    remove(backup.c_str());
    remove(staged.c_str());

    LOG("update: installed %s over %s at %s", wanted.c_str(), APP_VERSION, exe.c_str());
    g_progress.store(1.0f);
    g_restart.store(true);
    setState(UpdateState::Installed,
        format("Version %s is installed. Restart to run it.", wanted.c_str()));
}

void Update::restartIntoUpdate()
{
    const std::string exe = executablePath();
    if (exe.empty())
        return;
    // Hands the loader the file we just wrote; the caller leaves its main loop
    // and hbloader launches this instead of returning to the menu.
    envSetNextLoad(exe.c_str(), exe.c_str());
}

} // namespace nxp
