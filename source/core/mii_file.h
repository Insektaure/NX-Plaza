#pragma once

#include "core/mii.h"

#include <string>
#include <vector>

namespace nxp {

// Faces on the SD card, in dataDir()/export/.
//
// A face is already a short string - that is the whole point of the format -
// so a saved one is a small JSON file naming who it came from and carrying the
// same hex the wire carries. It is a sidecar, not a second source of truth:
// nothing in the app reads this folder unless the user asks it to.
struct MiiFile {
    std::string name;   // the filename, less its suffix - what the user typed
    std::string path;
    std::string handle; // the pass this face came off, when it came off one
    Mii face;           // parsed on listing, so the picker can draw it
    // False when the file is not a face this version can read - a face saved
    // by a newer build, or a JSON file that happens to live in the folder. It
    // is still listed, because a file the user can see and cannot open is less
    // confusing than one that silently is not there.
    bool readable = false;
};

// Path of the folder, with its trailing slash. Created on demand.
const std::string& miiExportDir();
bool ensureMiiExportDir();

// What the user typed, made safe to put on a FAT32 card.
//
// Strips the separators and wildcards FAT rejects, the control bytes, and the
// leading and trailing dots and spaces that make a file hard to delete from a
// PC. Returns an empty string when nothing usable is left, which the caller
// should treat as "ask again" rather than inventing a name.
std::string sanitizeMiiName(const std::string& typed);

// Where `name` would be written. `name` is sanitised first.
std::string miiExportPath(const std::string& name);

// Writes one face. Overwrites whatever is at that name, so callers that did
// not just ask the user about a collision should check `fileExists` first.
bool saveMii(const std::string& name, const Mii& face, const std::string& handle,
    std::string* errOut = nullptr);

// Every face in the folder, by name. Unreadable files are included with
// `readable` false. Never more than kMaxSavedMiis, so a folder someone has
// filled with junk cannot stall the picker.
std::vector<MiiFile> listSavedMiis();
constexpr size_t kMaxSavedMiis = 200;

// Reads one back. `errOut` says why when it fails, in words worth showing.
bool loadMii(const std::string& path, Mii& out, std::string* errOut = nullptr);

// Removes one, permanently. Only ever called behind a confirmation: there is
// no undo and nothing keeps a copy.
bool deleteSavedMii(const std::string& path, std::string* errOut = nullptr);

} // namespace nxp
