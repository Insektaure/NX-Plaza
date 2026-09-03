#pragma once

#include "core/crossing_extras.h"
#include "core/crossing_file.h"
#include "core/model.h"
#include "core/pieces.h"
#include "core/util.h"

#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace nxp {

struct TrophyFacts;

// A console that will never be accepted again.
//
// The handle is kept alongside the id because blocking deletes their pass:
// after that there is nothing left to look the name up in, and a list of bare
// 32-character ids tells nobody who they blocked or whether it was a mistake.
struct BlockedConsole {
    std::string id;
    std::string name;  // their handle when they were blocked, if it was known
    uint64_t when = 0; // unix seconds
};

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

    std::vector<BlockedConsole> blocked; // consoles we never accept again

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

    // `name` is their handle, remembered for the list; blocking deletes the
    // pass it would otherwise have been read from.
    void block(const std::string& id, const std::string& name = std::string());
    bool isBlocked(const std::string& id) const;

    // Lets one console through again, or all of them. Neither is told.
    void unblock(const std::string& id);
    void unblockAll();

    // Settings > "Delete every pass". Keeps our own identity and pass.
    void deleteAllCrossings();

    Stats stats() const;

    // How many times this console's pass has gone to somebody, as the server
    // last reported it.
    //
    // Kept here rather than read straight off Sync::Status because that is
    // memory only: it is zero until the first check-in answers, and stays zero
    // with no network. The pass screen would then tell somebody who has traded
    // for weeks that their pass has never been sent. Remembered in profile.json
    // so the last known figure survives a launch, with the server still the
    // authority whenever it answers.
    uint32_t passesSent() const;
    void rememberPassesSent(uint32_t count);

    // Collectible pieces. The inventory is per-user state, so it lives in
    // profile.json with everything else the owner accumulated; which crossing
    // gave what is per-crossing and lives in the extras sidecar.
    PieceInventory pieces() const;
    void setActivePieceSet(int set);

    // What a purchase produced. `set` is -1 when there was nothing to sell.
    struct PiecePurchase {
        int set = -1;
        int piece = -1;
    };

    // Grants a piece the owner does not already hold and records the shop as
    // where it came from. With `activeOnly` it comes from the puzzle being
    // filled and nothing else, and fails when that one is finished; without,
    // it is drawn from every unfinished puzzle at once.
    //
    // Missing rather than random: a crossing can hand you a piece twice
    // because meeting somebody twice is its own reward, but paying coins for
    // one you already had would just be a fine.
    PiecePurchase buyPiece(bool activeOnly);

    // Grants the piece this crossing is worth, into the active set. Returns
    // true only when it was one the owner did not already hold, so a caller can
    // tell "you got something" from "you got another one of those".
    bool grantPieceFor(const std::string& crossingId, uint64_t when);

    // How many crossings we have accepted since local midnight, for the
    // daily-limit cap.
    int acceptedToday() const;

    // Extra per-crossing data, in its own file. Features add tags here rather
    // than growing crossings.idx or crossings.dat, neither of which can change
    // shape without costing a live user their collection.
    //
    // Reading is a copy, like everything else the store hands out: the file's
    // own flush runs on the sync worker, and a reference into it would be a
    // race waiting for its first caller.
    CrossingExtras extrasFor(const std::string& id) const;

    // Writing is one field at a time, which is what makes a change a short
    // append rather than a rewrite of the whole file. It also means there is no
    // way to overwrite a row wholesale, and so no way to drop the tags this
    // build does not understand.
    void extraSetU32(const std::string& id, uint16_t tag, uint32_t value);
    void extraSetU64(const std::string& id, uint16_t tag, uint64_t value);
    void extraSetText(const std::string& id, uint16_t tag, const std::string& value);
    void extraClear(const std::string& id, uint16_t tag);

    // ------------------------------------------------------------ trophies

    // Everything the trophy conditions ask about, in one walk of the
    // collection. The answers are derived every time rather than counted as
    // they happen: a figure kept alongside the crossings is a figure that can
    // disagree with them.
    TrophyFacts trophyFacts() const;

    // When a trophy was first seen to be earned, or 0. Only the date is
    // stored - whether it is earned at all is re-derived - so losing this
    // costs a date and nothing else.
    uint64_t trophyDate(const std::string& id) const;
    // Records `when` against `id` if there is nothing there yet. True when it
    // was new, which is what the toast waits for.
    bool noteTrophyDate(const std::string& id, uint64_t when);

    // -------------------------------------------------------------- scores

    // Best score for a game, by name. Nothing hangs off these - no coins, no
    // unlocks - so they live in profile.json with everything else readable,
    // and an edited number costs the owner their own record and nothing more.
    uint32_t bestScore(const std::string& game) const;
    // True when this beat what was there, which is what a screen wants to
    // know to say "a new best".
    bool noteBestScore(const std::string& game, uint32_t score);

    // Favourites. A favourite is kept when the collection prunes at its cap,
    // which is the point of it: the people worth keeping are the ones you said
    // were worth keeping.
    bool isFavourite(const std::string& id) const;
    void setFavourite(const std::string& id, bool on);
    size_t favouriteCount() const;

private:
    Store() = default;

    // Records how many titles the sender said they had, when they said.
    void noteTitleCount(const std::string& crossingId, const Pass& pass);

    void dropExtraOrphansLocked();
    // Without copying the row, for the prune loop.
    bool isFavouriteLocked(const std::string& id) const;
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
    CrossingExtraFile m_extras;
    PieceInventory m_pieces;
    uint32_t m_passesSent = 0;
    // id -> when it was first seen earned. Keyed by id so the table can grow,
    // shrink or be reordered without a date landing on the wrong trophy.
    std::map<std::string, uint64_t> m_trophyDates;
    std::map<std::string, uint32_t> m_bestScores;
    bool m_loaded = false;
};

} // namespace nxp
