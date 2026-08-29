#pragma once

#include <string>

namespace nxp {

// Where the app is in the update cycle. Only one of these runs at a time.
enum class UpdateState {
    Idle,        // nothing asked for yet
    Checking,    // talking to the release API
    UpToDate,    // checked, and this build is current
    Available,   // a newer release exists and can be installed
    Downloading, // fetching and installing it
    Installed,   // on disk and verified; the app has to relaunch to run it
    Failed,      // see message()
};

// Self-update from the project's GitHub releases.
//
// The rule the whole thing is built around: never destroy the only working
// copy. The download goes to a staging file, the staged file is checked before
// it is allowed near the running one, the running one is copied to a backup
// before it is replaced, and the replacement is read back off the card and
// checked again before the backup is thrown away. A console that loses power
// in the middle of this still has an app to boot.
//
// Everything slow happens on a worker thread. The scenes only ever read state.
class Update {
public:
    static Update& get();

    // Asks the release API what the latest version is. `announce` marks a check
    // the user started, which is the difference between saying "you are up to
    // date" and saying nothing at all.
    void beginCheck(bool announce);

    // Downloads and installs whatever the last check found. Does nothing
    // unless the state is Available.
    void beginInstall();

    // Joins the worker. Call before the app tears the network down.
    void shutdown();

    UpdateState state() const;

    // The tag of the release that was found, without its leading "v".
    std::string version() const;

    // What to show the user: an error when Failed, a summary otherwise.
    std::string message() const;

    // 0..1 while Downloading, and only meaningful then. A server that does not
    // send a length leaves this at 0 for the whole transfer.
    float progress() const;

    // True when the check was started by the user rather than by the app.
    bool announce() const;

    // True once an update is installed and the app should relaunch into it.
    bool wantsRestart() const;

    // Points the loader at the newly installed file. The caller still has to
    // leave its main loop; this only decides what runs next.
    static void restartIntoUpdate();

private:
    Update() = default;
    void run();
    static void threadEntry(void* arg);

    bool spawn();
    void checkNow();
    void installNow();
};

// Compares dotted versions ("1.2.3", "v0.4"). Positive when `a` is newer than
// `b`, negative when older, zero when they are the same release. Missing parts
// count as zero, so "1.2" and "1.2.0" are the same version.
int compareVersions(const std::string& a, const std::string& b);

// Reads the DisplayVersion out of an NRO's embedded NACP. Empty when the file
// is not an NRO, carries no asset section, or has no readable version - all of
// which are reasons not to install it over anything.
std::string nroDisplayVersion(const std::string& path);

} // namespace nxp
