#include "core/backup.h"

#include "core/log.h"
#include "core/util.h"

#include <cstdio>
#include <ctime>
#include <sys/stat.h>
#include <unistd.h>

namespace nxp {

namespace {
    // What a backup carries, and what it does not.
    //
    // The log is left out: it is a diagnostic, it names the server and this
    // console, and it is the largest file in the folder. cacert.pem is left out
    // too - it is not the owner's data, it is a bundle anybody can download, and
    // a stale copy put back months later would break https in a way that took
    // an afternoon to work out last time.
    const char* kContents[] = {
        "identity.json",
        "profile.json",
        "crossings.idx",
        "crossings.dat",
        "crossings.ext",
    };
    constexpr size_t kContentCount = sizeof(kContents) / sizeof(kContents[0]);

    std::string g_dir;

    std::string twoDigits(int v)
    {
        char b[8];
        snprintf(b, sizeof(b), "%02d", v);
        return b;
    }
}

const std::string& backupDir()
{
    if (g_dir.empty())
        g_dir = dataPath("backup") + "/";
    return g_dir;
}

bool createBackup(std::string& whereOut, std::string& errorOut)
{
    ensureDataDir();
    std::string root = backupDir();
    if (!root.empty() && root.back() == '/')
        root.pop_back();
    mkdir(root.c_str(), 0777);

    // A folder per backup, named by the day. Somebody scanning a list of these
    // wants to know when, and a folder of loose dated files would be a worse
    // thing to hand to a file manager.
    time_t now = time_t(nowUnix());
    struct tm parts {};
    localtime_r(&now, &parts);
    std::string stem = std::to_string(1900 + parts.tm_year) + "-"
        + twoDigits(parts.tm_mon + 1) + "-" + twoDigits(parts.tm_mday);

    std::string folder = root + "/" + stem;
    // Two backups on the same day both keep their place rather than the second
    // quietly writing over the first.
    struct stat st {};
    for (int n = 2; stat(folder.c_str(), &st) == 0 && n < 100; n++)
        folder = root + "/" + stem + "-" + std::to_string(n);

    if (mkdir(folder.c_str(), 0777) != 0) {
        errorOut = "could not make the backup folder";
        return false;
    }

    size_t copied = 0;
    for (size_t i = 0; i < kContentCount; i++) {
        std::string contents;
        if (!readWholeFile(dataPath(kContents[i]), contents))
            continue; // a file that does not exist yet is not a failure

        if (!writeWholeFileAtomic(folder + "/" + kContents[i], contents)) {
            // Whatever was copied stays: a partial backup is still a copy of
            // the files that made it, and deleting them would be throwing away
            // the identity we may have just saved.
            errorOut = std::string("could not copy ") + kContents[i];
            LOG("backup: failed on %s after %zu files", kContents[i], copied);
            return false;
        }
        copied++;
    }

    if (copied == 0) {
        rmdir(folder.c_str());
        errorOut = "there was nothing to back up";
        return false;
    }

    LOG("backup: copied %zu files to %s", copied, folder.c_str());
    whereOut = folder;
    return true;
}

} // namespace nxp
