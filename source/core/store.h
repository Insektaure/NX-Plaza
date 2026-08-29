#pragma once

#include "core/crossing_file.h"
#include "core/model.h"
#include "core/util.h"

#include <mutex>
#include <string>
#include <vector>

namespace nxp {

// Everything the user can change on the Settings screen.
struct Settings {
    // The plaza to talk to. Compiled in; only a DEBUG=1 build can change it.
    std::string serverUrl = kPlazaServer;

    bool autoExchange = true;       // trade in the background while the app runs
    // Anywhere by default. The narrower settings are closer to what StreetPass
    // actually was, but a new console on a young plaza that only looks at its
    // own Wi-Fi meets nobody, and an app that does nothing on first run reads as
    // an app that does not work. Settings -> How far a crossing reaches narrows
    // it whenever the local plaza is busy enough to be worth it.
    Reach reach = Reach_World;      // how far a crossing may reach
    PlaceSharing placeSharing = Place_District;
    std::string districtLabel;      // "Namba Station" -- typed by the user
    std::string cityLabel;          // "Osaka"
    bool sharePlaying = true;       // advertise the title in the pass
    bool notify = true;             // toast when new passes land
    // Write plaza.log to the SD card. Off by default: a released build should
    // not be writing every twenty seconds for the life of a session, and the
    // log names the server, the handles it sees and this console's own id.
    bool logToFile = false;
    // Ask GitHub for the latest release once per launch. On by default.
    bool checkUpdates = true;
    int dailyLimit = 12;            // crossings accepted per day
    bool firstRunDone = false;

    // theme::Mode as an int, so core does not have to know about the UI layer.
    // 0 = Light (the default), 1 = Dark, 2 = match the console's own setting.
    int themeMode = 0;

    std::vector<std::string> blocked; // console ids we never accept again

    // The place label that actually leaves the console, honouring placeSharing.
    std::string sharedPlaceLabel() const;
};

// Numbers the plaza and passport screens quote.
struct Stats {
    uint32_t totalCrossings = 0; // sum of every crossing count
    uint32_t uniquePeople = 0;   // distinct consoles
    uint32_t unopened = 0;
    uint32_t today = 0;
    uint32_t places = 0;      // distinct place labels seen
    uint32_t week[7] = {};    // index 0 = six days ago, 6 = today
    uint64_t oldestUnopened = 0;
};

// The on-SD-card database, shared between the UI thread and the sync thread.
//
// All public methods are safe to call from any thread. Mutations mark the
// store dirty; App::update() calls flush() a couple of times a second so a
// burst of incoming passes costs one SD write instead of twenty.
class Store {
public:
    static Store& get();

    // Reads profile.json and crossings.json, filling in defaults for a first
    // launch. Always succeeds - a missing or broken file just means defaults.
    void load();

    // Writes anything dirty. Cheap when nothing changed.
    bool flush();

    // ------------------------------------------------------------- settings

    Settings settings() const;
    void setSettings(const Settings& s);

    Pass myPass() const;
    void setMyPass(const Pass& p);

    // The pass as it should go on the wire right now: our pass with the place
    // label and the played title filtered by the privacy settings, and `met`
    // filled in from the local database.
    Pass outgoingPass() const;

    // ------------------------------------------------------------ crossings

    std::vector<Crossing> crossings() const; //< newest first

    // Bumped whenever the collection changes in any way a screen would notice.
    //
    // crossings() hands back a copy of the whole vector, and the plaza and the
    // collection both used to ask for one every frame - eight heap strings per
    // card, copied sixty times a second, for a list that changes when a pass
    // arrives. A screen compares this instead and only copies when it moves.
    uint64_t crossingsGeneration() const;
    bool findCrossing(const std::string& id, Crossing& out) const;

    // Folds an incoming pass into the database. Returns true when this is a
    // console we had never met, false when it updated an existing crossing.
    // Ignores blocked ids and passes from ourselves.
    bool recordCrossing(const std::string& id, const Pass& pass,
        const std::string& place, uint64_t when);

    void markOpened(const std::string& id);
    void markTradedBack(const std::string& id);
    void markAllOpened();

    void block(const std::string& id);
    bool isBlocked(const std::string& id) const;

    // Settings > "Delete every pass". Keeps our own identity and pass.
    void deleteAllCrossings();

    Stats stats() const;

    // How many crossings we have accepted since local midnight, for the
    // daily-limit cap.
    int acceptedToday() const;

private:
    Store() = default;

    void saveProfileLocked();
    void saveCrossingsLocked();
    void pruneLocked();

    mutable std::recursive_mutex m_mutex;

    Settings m_settings;
    Pass m_pass;
    std::vector<Crossing> m_crossings; // kept sorted, newest lastSeen first

    bool m_profileDirty = false;
    bool m_crossingsDirty = false;
    uint64_t m_crossingsGeneration = 1;
    // Something structural happened - a card removed, the lot cleared - and
    // the files have to be rebuilt rather than patched.
    bool m_crossingsNeedRewrite = false;
    CrossingFile m_crossingFile;
    bool m_loaded = false;
};

} // namespace nxp
