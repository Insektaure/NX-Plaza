#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace nxp {

// A title from the console's own play history.
struct PlayedTitle {
    uint64_t applicationId = 0;
    std::string name;    // as the console shows it, in the console's language
    uint32_t hours = 0;  // total time on this console, rounded down
};

// The last few titles this console played, newest first.
//
// It is deliberately "last played" rather than "playing now" - there is no
// such thing while this app is in the foreground, because homebrew launched
// from hbmenu has either taken the game slot or is sitting over the album. No
// other game is running to ask about.
//
// Starts reading the history, on a thread of its own, once.
//
// It has to be off the drawing thread. Naming five titles means asking the
// console for each one's control data, which arrives with a 128 KB icon
// attached whether or not anybody wants it, and doing that where the passport
// is built stalled the screen visibly on the way in.
//
// Safe to call repeatedly and from anywhere; only the first call does anything.
void beginPlayHistoryLoad();

// True once the read has finished, successfully or not.
bool playHistoryReady();

// What has been read so far. Empty until the load finishes, and empty
// afterwards when the console has played nothing or would not say - callers
// must handle both, and `playHistoryReady` is what tells them apart.
//
// Copies rather than returning a reference: the list is written by another
// thread, and a reference into it would be a reference into something being
// rewritten.
std::vector<PlayedTitle> recentlyPlayed();

// How many titles are installed on this console, played or not.
//
// Costs nothing: the record enumeration behind recentlyPlayed() already walks
// every one of them, and this is the only thing wanted from the ones that were
// never opened. Zero until the scan reaches that point, which is early - it is
// published before the naming pass that takes the time.
size_t installedTitles();

// Stops the loader and forgets what it read. Called on the way out.
void endPlayHistory();

} // namespace nxp
