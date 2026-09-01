#pragma once

#include <string>

namespace nxp {

// Copying the console's files into a backup folder on the same card.
//
// The thing worth protecting is identity.json.
//
// Plain copies in a plain folder, deliberately. No archive and no restore
// button: the point is that the files are sitting there in the open, so putting
// one back is dragging it in a file manager rather than trusting this app to
// still run on the day it is needed.
//
// It is the same SD card, so this is protection against the app, a bad write or
// a mistake - not against losing the card.

// backup/<date>/ inside the app's folder, with a trailing slash.
const std::string& backupDir();

// Copies everything worth keeping into a new dated folder. `whereOut` gets the
// folder, `errorOut` the reason when it fails.
bool createBackup(std::string& whereOut, std::string& errorOut);

} // namespace nxp
