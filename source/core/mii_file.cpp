#include "core/mii_file.h"

#include "core/json.h"
#include "core/util.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>

namespace nxp {

namespace {

constexpr const char* kSuffix = ".json";
constexpr const char* kFormat = "nx-plaza/mii";
constexpr size_t kMaxNameChars = 48;

// FAT32 rejects these outright, and a stray '/' would write outside the
// folder, which is the one mistake here that could touch somebody else's file.
bool forbidden(char c)
{
    return c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<'
        || c == '>' || c == '|';
}

} // namespace

const std::string& miiExportDir()
{
    static const std::string dir = std::string(dataDir()) + "export/";
    return dir;
}

bool ensureMiiExportDir()
{
    if (!ensureDataDir())
        return false;
    // No trailing slash: mkdir wants the directory, not a path inside it.
    std::string dir = miiExportDir();
    dir.pop_back();
    int rc = mkdir(dir.c_str(), 0777);
    return rc == 0 || errno == EEXIST;
}

std::string sanitizeMiiName(const std::string& typed)
{
    std::string out;
    out.reserve(typed.size());
    for (unsigned char c : typed) {
        if (c < 0x20 || c == 0x7F || forbidden(static_cast<char>(c)))
            continue;
        out += static_cast<char>(c);
        if (out.size() >= kMaxNameChars)
            break;
    }

    // Leading and trailing dots and spaces are legal to write and a nuisance to
    // delete from a PC, so they come off here rather than being someone's
    // problem later.
    size_t begin = out.find_first_not_of(" .");
    if (begin == std::string::npos)
        return {};
    size_t end = out.find_last_not_of(" .");
    return out.substr(begin, end - begin + 1);
}

std::string miiExportPath(const std::string& name)
{
    return miiExportDir() + sanitizeMiiName(name) + kSuffix;
}

bool saveMii(const std::string& name, const Mii& face, const std::string& handle,
    std::string* errOut)
{
    std::string clean = sanitizeMiiName(name);
    if (clean.empty()) {
        if (errOut)
            *errOut = "That name has nothing in it a file can be called.";
        return false;
    }
    if (!ensureMiiExportDir()) {
        if (errOut)
            *errOut = "Could not make the export folder on the SD card.";
        return false;
    }

    json_t* root = json_object();
    // `format` and `version` first so the file says what it is before it says
    // anything else, and so a face from a newer build can be told apart from a
    // JSON file that merely landed in the folder.
    json_object_set_new(root, "format", json_string(kFormat));
    json_object_set_new(root, "version", json_integer(Mii::kVersion));
    json_object_set_new(root, "name", json_string(clean.c_str()));
    if (!handle.empty())
        json_object_set_new(root, "handle", json_string(handle.c_str()));
    json_object_set_new(root, "mii", json_string(face.toHex().c_str()));
    json_object_set_new(root, "saved", json_integer(static_cast<json_int_t>(nowUnix())));

    bool ok = js::writeFile(miiExportDir() + clean + kSuffix, root);
    json_decref(root);

    if (!ok && errOut)
        *errOut = "Could not write to the SD card.";
    return ok;
}

bool loadMii(const std::string& path, Mii& out, std::string* errOut)
{
    json_t* root = js::readFile(path);
    if (!root) {
        if (errOut)
            *errOut = "That file could not be read.";
        return false;
    }

    std::string format = js::getStr(root, "format");
    std::string hex = js::getStr(root, "mii");
    json_decref(root);

    if (format != kFormat) {
        if (errOut)
            *errOut = "That is not a face this app saved.";
        return false;
    }
    if (!Mii::fromHex(hex, out)) {
        // fromHex declines anything whose version byte is not this one, which is
        // the same rule the wire uses - an older face cannot be read into this
        // layout without turning into somebody else.
        if (errOut)
            *errOut = "That face was saved by a different version of the app.";
        return false;
    }
    return true;
}

bool deleteSavedMii(const std::string& path, std::string* errOut)
{
    // Only ever a file this folder listed, so there is nothing to resolve and
    // nowhere for a name to point that the picker did not already show.
    if (remove(path.c_str()) == 0)
        return true;
    if (errOut)
        *errOut = "Could not delete that file from the SD card.";
    return false;
}

std::vector<MiiFile> listSavedMiis()
{
    std::vector<MiiFile> out;

    std::string dir = miiExportDir();
    std::string open = dir;
    open.pop_back();
    DIR* d = opendir(open.c_str());
    if (!d)
        return out; // no folder yet is not an error: it means nothing is saved

    const size_t suffixLen = std::strlen(kSuffix);
    while (dirent* entry = readdir(d)) {
        if (out.size() >= kMaxSavedMiis)
            break;

        std::string file = entry->d_name;
        if (file.size() <= suffixLen
            || file.compare(file.size() - suffixLen, suffixLen, kSuffix) != 0)
            continue;

        MiiFile item;
        item.name = file.substr(0, file.size() - suffixLen);
        item.path = dir + file;

        json_t* root = js::readFile(item.path);
        if (root) {
            if (js::getStr(root, "format") == kFormat) {
                item.handle = js::getStr(root, "handle");
                item.readable = Mii::fromHex(js::getStr(root, "mii"), item.face);
            }
            json_decref(root);
        }
        out.push_back(item);
    }
    closedir(d);

    // readdir gives whatever order the filesystem feels like, which reads as
    // random to someone looking for a name they typed.
    std::sort(out.begin(), out.end(),
        [](const MiiFile& a, const MiiFile& b) { return a.name < b.name; });
    return out;
}

} // namespace nxp
