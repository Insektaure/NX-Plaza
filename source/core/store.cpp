#include "core/store.h"

#include "core/identity.h"
#include "core/json.h"
#include "core/log.h"
#include "core/pieces.h"
#include "core/play_history.h"
#include "core/place.h"
#include "core/util.h"

#include <algorithm>
#include <ctime>
#include <set>

namespace nxp {

namespace {
    const char* kProfileFile = "profile.json";
    const char* kCrossingsFile = "crossings.json";

    // The 3DS kept 10 StreetPass slots per title.
    // crossings.idx/.dat write one 96-byte record instead, so the ceiling is
    // now about what is reasonable to hold in memory and draw, not what is
    // affordable to rewrite. 5000 is roughly 600 KB of index.
    constexpr size_t kMaxCrossings = 5000;

    uint64_t startOfLocalDay(uint64_t when)
    {
        time_t t = static_cast<time_t>(when);
        struct tm tmv {};
        localtime_r(&t, &tmv);
        tmv.tm_hour = 0;
        tmv.tm_min = 0;
        tmv.tm_sec = 0;
        return static_cast<uint64_t>(mktime(&tmv));
    }
}

std::string Settings::sharedPlaceLabel() const
{
    switch (placeSharing) {
    case Place_District:
        return districtLabel.empty() ? cityLabel : districtLabel;
    case Place_City:
        return cityLabel;
    case Place_Off:
    default:
        return std::string();
    }
}

Store& Store::get()
{
    static Store instance;
    return instance;
}

void Store::load()
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (m_loaded)
        return;
    m_loaded = true;

    ensureDataDir();

    // ---- profile.json: settings + our own pass
    json_t* profile = js::readFile(dataPath(kProfileFile));
    if (profile) {
        // Not a setting: a remembered figure from the server. Missing on any
        // profile written before this, which reads as zero and is corrected by
        // the first check-in.
        m_passesSent = static_cast<uint32_t>(js::getInt(profile, "passes_sent", 0));

        if (json_t* p = js::getObj(profile, "pieces")) {
            m_pieces.fromHex(js::getStrArray(p, "owned", 64),
                static_cast<int>(js::getInt(p, "active", 0)));

            // Read after the masks, because normalise() drops any source whose
            // piece is not held and fromHex is what decides that.
            if (json_t* from = js::getArr(p, "from")) {
                size_t i = 0;
                json_t* e = nullptr;
                json_array_foreach(from, i, e) {
                    if (!json_is_object(e))
                        continue;
                    uint8_t piece = static_cast<uint8_t>(js::getInt(e, "piece", 255));
                    std::string who = js::getStr(e, "who");
                    uint64_t when = static_cast<uint64_t>(js::getInt(e, "when", 0));

                    // Written by index before the key existed. Reading it that
                    // way once is what migrates it: the next save writes the
                    // key, and this build's table is still the one it was
                    // written against.
                    std::string picture = js::getStr(e, "picture");
                    if (picture.empty())
                        m_pieces.noteSource(static_cast<int>(js::getInt(e, "set", -1)),
                            piece, who, when);
                    else
                        m_pieces.noteSourceFor(picture, piece, who, when);
                }
            }
            m_pieces.normalise();
        }

        json_t* s = js::getObj(profile, "settings");
        if (s) {
            // A released build ignores whatever is on the card. There is one
            // plaza, and a profile carried over from a development build would otherwise leave
            // the console pointed at a server that is not there, with no way to
            // put it right.
            if (kServerIsEditable)
                m_settings.serverUrl = js::getStr(s, "server_url", m_settings.serverUrl);
            else
                m_settings.serverUrl = kPlazaServer;
            m_settings.autoExchange = js::getBool(s, "auto_exchange", true);
            m_settings.checkUpdates = js::getBool(s, "check_updates", true);
            m_settings.reach = static_cast<Reach>(
                std::min<int64_t>(std::max<int64_t>(js::getInt(s, "reach", Reach_World), 0),
                    Reach_Count - 1));
            m_settings.placeSharing = static_cast<PlaceSharing>(
                std::min<int64_t>(std::max<int64_t>(js::getInt(s, "place_sharing", Place_District), 0),
                    Place_Count - 1));
            m_settings.districtLabel = clampUtf8(js::getStr(s, "district"), 24);
            m_settings.cityLabel = clampUtf8(js::getStr(s, "city"), 24);
            m_settings.sharePlaying = js::getBool(s, "share_playing", true);
            m_settings.notify = js::getBool(s, "notify", true);
            m_settings.logToFile = js::getBool(s, "logToFile", false);
            m_settings.dailyLimit = static_cast<int>(js::getInt(s, "daily_limit", 12));
            m_settings.firstRunDone = js::getBool(s, "first_run_done", false);
            m_settings.themeMode = static_cast<int>(js::getInt(s, "theme", 0));
            m_settings.blocked = js::getStrArray(s, "blocked", 128);
        }

        json_t* p = json_object_get(profile, "pass");
        if (json_is_object(p))
            m_pass = Pass::fromJson(p);

        json_decref(profile);
    }

    // Runs whether or not the key was there: a first launch, or a build that
    // added a set, both need `owned` sized to the current list.
    m_pieces.normalise();

    if (m_settings.dailyLimit < 1 || m_settings.dailyLimit > 99)
        m_settings.dailyLimit = 12;
    if (m_settings.themeMode < 0 || m_settings.themeMode > 2)
        m_settings.themeMode = 0;

    if (m_pass.isBlank())
        m_pass = Pass::makeDefault(suggestedHandle());

    // ---- the collection: crossings.idx + crossings.dat
    if (CrossingFile::absent()) {
        // A collection written by an older build. Read it once, write it out in
        // the new shape, and leave the JSON where it is - if this goes wrong
        // the old file is still the collection.
        json_t* db = js::readFile(dataPath(kCrossingsFile));
        if (db) {
            json_t* list = js::getArr(db, "crossings");
            if (list) {
                size_t index;
                json_t* value;
                json_array_foreach(list, index, value) {
                    Crossing c = Crossing::fromJson(value);
                    if (!c.id.empty())
                        m_crossings.push_back(std::move(c));
                }
            }
            json_decref(db);

            if (!m_crossings.empty()) {
                LOG("store: migrating %zu crossings out of JSON", m_crossings.size());
                m_crossingFile.compact(m_crossings);
            }
        }
    } else {
        m_crossings = m_crossingFile.load();
        m_crossingsNeedRewrite = m_crossingFile.needsCompaction();
    }

    std::stable_sort(m_crossings.begin(), m_crossings.end(),
        [](const Crossing& a, const Crossing& b) { return a.lastSeen > b.lastSeen; });

    // The extras file, after the collection and entirely separately. A failure
    // here is logged and dropped: extras are extras, and a console with an
    // unreadable one still has every card it collected.
    m_extras.load();

    LOG("store: %zu crossings, handle '%s'", m_crossings.size(), m_pass.handle.c_str());
}

bool Store::flush()
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    bool wrote = false;
    if (m_profileDirty) {
        saveProfileLocked();
        wrote = true;
    }
    if (m_crossingsDirty) {
        saveCrossingsLocked();
        wrote = true;
    }
    // Its own file, its own write. Deliberately not folded into the collection's
    // success: the collection is what matters, and an extras file that would
    // not write must never look like a collection that would not write.
    if (m_extras.dirty() || m_extras.needsCompaction()) {
        m_extras.flush();
        wrote = true;
    }
    return wrote;
}

void Store::saveProfileLocked()
{
    json_t* s = json_object();
    // Only a build that can change the address has any reason to remember one.
    // A released build would write the compiled-in constant and then ignore it
    // on the next load, leaving a key on the card that looks like a setting and
    // is not one.
    if (kServerIsEditable)
        json_object_set_new(s, "server_url", json_string(m_settings.serverUrl.c_str()));
    json_object_set_new(s, "auto_exchange", json_boolean(m_settings.autoExchange));
    json_object_set_new(s, "reach", json_integer(m_settings.reach));
    json_object_set_new(s, "place_sharing", json_integer(m_settings.placeSharing));
    json_object_set_new(s, "district", json_string(m_settings.districtLabel.c_str()));
    json_object_set_new(s, "city", json_string(m_settings.cityLabel.c_str()));
    json_object_set_new(s, "share_playing", json_boolean(m_settings.sharePlaying));
    json_object_set_new(s, "notify", json_boolean(m_settings.notify));
    json_object_set_new(s, "logToFile", json_boolean(m_settings.logToFile));
    json_object_set_new(s, "check_updates", json_boolean(m_settings.checkUpdates));
    json_object_set_new(s, "daily_limit", json_integer(m_settings.dailyLimit));
    json_object_set_new(s, "first_run_done", json_boolean(m_settings.firstRunDone));
    json_object_set_new(s, "theme", json_integer(m_settings.themeMode));
    json_object_set_new(s, "blocked", js::strArray(m_settings.blocked));

    json_t* root = json_object();
    json_object_set_new(root, "version", json_integer(1));
    json_object_set_new(root, "passes_sent", json_integer(m_passesSent));

    json_t* pieces = json_object();
    json_object_set_new(pieces, "active", json_integer(m_pieces.active));
    json_object_set_new(pieces, "owned", js::strArray(m_pieces.toHex()));

    // Who brought each piece. Sparse - one entry per held piece, in no
    // particular order - so a build that changes the set table drops what no
    // longer fits instead of shifting everything along by one.
    json_t* from = json_array();
    for (const PieceSource& src : m_pieces.sources) {
        json_t* e = json_object();
        json_object_set_new(e, "picture", json_string(src.picture.c_str()));
        json_object_set_new(e, "piece", json_integer(src.piece));
        json_object_set_new(e, "who", json_string(src.who.c_str()));
        json_object_set_new(e, "when", json_integer(json_int_t(src.when)));
        json_array_append_new(from, e);
    }
    json_object_set_new(pieces, "from", from);
    json_object_set_new(root, "pieces", pieces);
    json_object_set_new(root, "settings", s);
    json_object_set_new(root, "pass", m_pass.toJson());

    m_profileDirty = !js::writeFile(dataPath(kProfileFile), root);
    json_decref(root);
}

void Store::saveCrossingsLocked()
{
    // A card removed, the collection cleared, or enough superseded text piled up
    // in the blob: rebuild both files. This is the only path that rewrites
    // everything, and the only one that is atomic.
    if (m_crossingsNeedRewrite || m_crossingFile.needsCompaction()) {
        if (m_crossingFile.compact(m_crossings)) {
            for (Crossing& c : m_crossings) {
                c.recordDirty = false;
                c.textDirty = false;
            }
            m_crossingsNeedRewrite = false;
            m_crossingsDirty = false;
        }
        return;
    }

    // Otherwise only what changed. Marking a card as read is a 96-byte write,
    // whatever the collection is holding.
    bool ok = true;
    for (Crossing& c : m_crossings) {
        if (c.slot == Crossing::kNoSlot) {
            ok = m_crossingFile.append(c) && ok;
        } else if (c.textDirty) {
            ok = m_crossingFile.writeOne(c, true) && ok;
        } else if (c.recordDirty) {
            ok = m_crossingFile.writeOne(c, false) && ok;
        } else {
            continue;
        }
        c.recordDirty = false;
        c.textDirty = false;
    }

    // Something would not write. Fall back to a full rebuild next time rather
    // than leaving the files disagreeing with what is on screen.
    m_crossingsNeedRewrite = !ok;
    m_crossingsDirty = !ok;
}

// Drops extras for people no longer in the collection. Called from every path
// that shrinks it: otherwise the file grows forever, holding notes about
// consoles that were pruned at the cap years ago.
CrossingExtras Store::extrasFor(const std::string& id) const
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (const CrossingExtras* found = m_extras.find(id))
        return *found;
    return CrossingExtras {};
}

void Store::extraSetU32(const std::string& id, uint16_t tag, uint32_t value)
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_extras.setU32(id, tag, value);
}

void Store::extraSetU64(const std::string& id, uint16_t tag, uint64_t value)
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_extras.setU64(id, tag, value);
}

void Store::extraSetText(const std::string& id, uint16_t tag, const std::string& value)
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_extras.setText(id, tag, value);
}

void Store::extraClear(const std::string& id, uint16_t tag)
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_extras.clearField(id, tag);
}

bool Store::isFavouriteLocked(const std::string& id) const
{
    const CrossingExtras* row = m_extras.find(id);
    return row != nullptr && row->has(extras::Favourite);
}

bool Store::isFavourite(const std::string& id) const
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    return isFavouriteLocked(id);
}

void Store::setFavourite(const std::string& id, bool on)
{
    if (!CrossingExtraFile::validId(id))
        return;

    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (isFavouriteLocked(id) == on)
        return;

    // One field, appended. Nothing else in the row is touched, so a tag this
    // build has never heard of cannot be lost here.
    if (on)
        m_extras.setU64(id, extras::Favourite, nowUnix());
    else
        m_extras.clearField(id, extras::Favourite);

    // Deliberately no generation bump. The generation means "the list of
    // crossings changed", and this does not change it : the star is read live
    // per card by isFavourite(). Bumping it made the collection and the plaza
    // re-copy the whole list for nothing, and made The Square reshuffle its
    // entire cast the next time it was opened - starring somebody is not a
    // reason for twenty other people to move.
}

size_t Store::favouriteCount() const
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    size_t n = 0;
    for (const Crossing& c : m_crossings) {
        if (isFavouriteLocked(c.id))
            n++;
    }
    return n;
}

void Store::dropExtraOrphansLocked()
{
    if (m_extras.rows() == 0)
        return;
    std::vector<std::string> live;
    live.reserve(m_crossings.size());
    for (const Crossing& c : m_crossings)
        live.push_back(c.id);
    size_t dropped = m_extras.dropOrphans(live);
    if (dropped > 0)
        LOG("extras: dropped %zu rows with no crossing left", dropped);
}

void Store::pruneLocked()
{
    if (m_crossings.size() <= kMaxCrossings)
        return;

    // The list is sorted newest first, so walking backwards visits the oldest
    // passes. Drop ones the user has already read before touching unread news.
    auto dropFromBack = [&](bool openedOnly, bool sparingFavourites) {
        size_t i = m_crossings.size();
        while (i > 0 && m_crossings.size() > kMaxCrossings) {
            --i;
            if (sparingFavourites && isFavouriteLocked(m_crossings[i].id))
                continue;
            if (!openedOnly || m_crossings[i].opened)
                m_crossings.erase(m_crossings.begin() + static_cast<ptrdiff_t>(i));
        }
    };

    // Read ones first, then unread, both sparing anything starred.
    dropFromBack(true, true);
    dropFromBack(false, true);

    // The cap is absolute, and the protection is best effort: a collection
    // entirely of favourites still has to come down to size, or it grows until
    // the console runs out of card. Starred people are simply last to go.
    if (m_crossings.size() > kMaxCrossings) {
        LOG("store: %zu crossings over the cap are all favourites; dropping the oldest",
            m_crossings.size() - kMaxCrossings);
        dropFromBack(false, false);
    }
    m_crossingsNeedRewrite = true;
    dropExtraOrphansLocked();
}

Settings Store::settings() const
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    return m_settings;
}

void Store::setSettings(const Settings& s)
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_settings = s;
    m_profileDirty = true;
}

Pass Store::myPass() const
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    return m_pass;
}

void Store::setMyPass(const Pass& p)
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_pass = p;
    m_pass.sanitize();
    m_profileDirty = true;
}

Pass Store::outgoingPass() const
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);

    Pass out = m_pass;
    out.district = m_settings.sharedPlaceLabel();
    // Hours go with the title. On their own they say how much of your life a
    // game has had, which is more than the title does, and hiding the title
    // while still sending "412h" would be a strange kind of private.
    // How many titles are on this console goes with them, under the same
    // switch. It is a smaller thing to say than which game and for how long,
    // but it is the same kind of thing - what is on this console rather than
    // what its owner typed - and somebody who turned that off did not mean
    // "except for the total".
    out.titles = m_settings.sharePlaying
        ? static_cast<uint32_t>(installedTitles())
        : 0u;
    if (!m_settings.sharePlaying) {
        out.playing = std::string();
        out.hours = 0;
    }

    out.met = static_cast<uint32_t>(m_crossings.size());

    out.sanitize();
    return out;
}

std::vector<Crossing> Store::crossings() const
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    return m_crossings;
}

uint64_t Store::crossingsGeneration() const
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    return m_crossingsGeneration;
}

bool Store::findCrossing(const std::string& id, Crossing& out) const
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    for (const Crossing& c : m_crossings) {
        if (c.id == id) {
            out = c;
            return true;
        }
    }
    return false;
}

bool Store::recordCrossing(const std::string& id, const Pass& pass,
    const std::string& place, uint64_t when)
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);

    if (id.empty() || id == identity().id || isBlocked(id))
        return false;

    if (when == 0)
        when = nowUnix();

    for (Crossing& c : m_crossings) {
        if (c.id != id)
            continue;

        // A repeat crossing: keep the freshest pass, bump the counter, and put
        // it back at the top of the inbox as unopened news.
        c.pass = pass;
        c.pass.sanitize();
        c.lastSeen = when;
        c.count++;
        if (!place.empty())
            c.place = place;
        c.opened = false;
        c.textDirty = true; // the pass itself was replaced
        m_crossingsDirty = true;
        m_crossingsGeneration++;

        std::stable_sort(m_crossings.begin(), m_crossings.end(),
            [](const Crossing& a, const Crossing& b) { return a.lastSeen > b.lastSeen; });

        // A repeat still counts. Crossing the same person again on another day
        // is another piece; twice in one afternoon is the same piece, because
        // the day is part of what decides it.
        grantPieceFor(id, when);
        noteTitleCount(id, pass);
        return false;
    }

    Crossing c;
    c.id = id;
    c.pass = pass;
    c.pass.sanitize();
    c.firstSeen = when;
    c.lastSeen = when;
    c.count = 1;
    c.place = place;
    c.opened = false;
    m_crossings.insert(m_crossings.begin(), std::move(c));

    pruneLocked();
    m_crossingsDirty = true;
    m_crossingsGeneration++;

    // After the prune, which is where the extras sweep runs: granting first
    // would add a row and then walk a sweep that has no reason to keep it in
    // mind. The collection is in its final shape by here.
    grantPieceFor(id, when);
    noteTitleCount(id, pass);
    return true;
}

void Store::markOpened(const std::string& id)
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    for (Crossing& c : m_crossings) {
        if (c.id == id && !c.opened) {
            c.opened = true;
            c.recordDirty = true;
            m_crossingsDirty = true;
            m_crossingsGeneration++;
        }
    }
}

void Store::markTradedBack(const std::string& id)
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    for (Crossing& c : m_crossings) {
        if (c.id == id && !c.tradedBack) {
            c.tradedBack = true;
            c.opened = true;
            c.recordDirty = true;
            m_crossingsDirty = true;
            m_crossingsGeneration++;
        }
    }
}

void Store::markAllOpened()
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    for (Crossing& c : m_crossings) {
        if (!c.opened) {
            c.opened = true;
            c.recordDirty = true;
            m_crossingsDirty = true;
            m_crossingsGeneration++;
        }
    }
}

void Store::block(const std::string& id)
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (id.empty() || isBlocked(id))
        return;

    m_settings.blocked.push_back(id);
    m_crossings.erase(std::remove_if(m_crossings.begin(), m_crossings.end(),
                          [&](const Crossing& c) { return c.id == id; }),
        m_crossings.end());
    m_profileDirty = true;
    m_crossingsDirty = true;
    m_crossingsGeneration++;
    m_crossingsNeedRewrite = true;
    dropExtraOrphansLocked();
}

bool Store::isBlocked(const std::string& id) const
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    return std::find(m_settings.blocked.begin(), m_settings.blocked.end(), id)
        != m_settings.blocked.end();
}

void Store::deleteAllCrossings()
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_crossings.clear();
    m_crossingsDirty = true;
    m_crossingsGeneration++;
    m_crossingsNeedRewrite = true;
    saveCrossingsLocked();
    dropExtraOrphansLocked();
    if (m_extras.dirty())
        m_extras.flush();
}

Stats Store::stats() const
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);

    Stats s;
    uint64_t now = nowUnix();
    uint64_t today = startOfLocalDay(now);
    std::set<std::string> places;

    s.uniquePeople = static_cast<uint32_t>(m_crossings.size());
    for (const Crossing& c : m_crossings) {
        s.totalCrossings += c.count;
        if (!c.opened) {
            s.unopened++;
            if (s.oldestUnopened == 0 || c.lastSeen < s.oldestUnopened)
                s.oldestUnopened = c.lastSeen;
        }
        if (!c.place.empty())
            places.insert(c.place);

        if (c.lastSeen >= today)
            s.today++;

        // Bucket the last seven local days; index 6 is today.
        for (int d = 0; d < 7; d++) {
            uint64_t dayStart = today - static_cast<uint64_t>(6 - d) * 24 * 3600;
            uint64_t dayEnd = dayStart + 24 * 3600;
            if (c.lastSeen >= dayStart && c.lastSeen < dayEnd) {
                s.week[d]++;
                break;
            }
        }
    }
    s.places = static_cast<uint32_t>(places.size());
    return s;
}

PieceInventory Store::pieces() const
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    return m_pieces;
}

void Store::setActivePieceSet(int set)
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (set < 0 || size_t(set) >= pieceSets().size() || set == m_pieces.active)
        return;
    m_pieces.active = set;
    m_profileDirty = true;
}

void Store::noteTitleCount(const std::string& crossingId, const Pass& pass)
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);

    // Nothing said means nothing written: every pass from before this existed
    // says nothing, and so does every pass whose owner shares no title. Writing
    // a zero would make "they did not say" indistinguishable from "an empty
    // console", and would put a row in the sidecar for every crossing that has
    // nothing to record.
    if (pass.titles == 0) {
        // Except when we had a number before and they have since turned
        // sharing off: leaving the old one standing would be showing a figure
        // its owner withdrew.
        const CrossingExtras* row = m_extras.find(crossingId);
        uint32_t had = 0;
        if (row != nullptr && row->getU32(extras::TitleCount, had))
            m_extras.clearField(crossingId, extras::TitleCount);
        return;
    }

    // Skipped when unchanged, for the same reason the piece write is: an
    // identical value appended is a dead entry, and dead entries are what
    // decide when the whole log gets rewritten.
    const CrossingExtras* row = m_extras.find(crossingId);
    uint32_t already = 0;
    if (row != nullptr && row->getU32(extras::TitleCount, already) && already == pass.titles)
        return;

    m_extras.setU32(crossingId, extras::TitleCount, pass.titles);
}

bool Store::grantPieceFor(const std::string& crossingId, uint64_t when)
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (crossingId.empty() || pieceSets().empty())
        return false;

    // A finished puzzle takes nothing more, so every crossing after the last
    // piece was thrown away in silence: the toast said nothing, the board
    // showed nothing, and the pieces were simply gone. Moving on to the first
    // unfinished one costs the owner nothing they chose - they had already got
    // what they picked this puzzle for.
    //
    // When they are all finished it stays put. There is nowhere better to go,
    // and switching for the sake of it would lose the record of which one was
    // being collected.
    if (m_pieces.complete(m_pieces.active)) {
        for (size_t i = 0; i < pieceSets().size(); i++) {
            if (m_pieces.complete(static_cast<int>(i)))
                continue;
            LOG("pieces: %s is finished; filling %s now",
                pieceSets()[size_t(m_pieces.active)].name, pieceSets()[i].name);
            m_pieces.active = static_cast<int>(i);
            m_profileDirty = true;
            break;
        }
    }

    int set = m_pieces.active;
    uint8_t piece = pieceFor(identity().id, crossingId, when, set);
    bool isNew = m_pieces.take(set, piece);

    // Recorded either way, with the flag saying which it was. A duplicate is
    // still what this person brought; it is simply not worth announcing.
    uint32_t packed
        = (isNew ? extras::PieceWasNew : 0u) | (uint32_t(set) << 16) | uint32_t(piece);

    // Crossing the same person twice in one day yields the same piece - pieceFor
    // is seeded on the day - so what we are about to write is very often exactly
    // what is already there. Writing it regardless appends an entry and
    // supersedes an identical one, and dead weight is what decides when the
    // whole log gets rewritten. So skip only a byte-for-byte repeat.
    //
    // Nothing else is skipped. A value differing by nothing but the new-piece
    // flag still has to be recorded, because the arrival toast tests that flag
    // to decide whether to announce a piece: leaving yesterday's flag standing
    // would announce one that was not granted.
    const CrossingExtras* row = m_extras.find(crossingId);
    uint32_t already = 0;
    if (row == nullptr || !row->getU32(extras::LastPiece, already) || already != packed)
        m_extras.setU32(crossingId, extras::LastPiece, packed);

    if (!isNew)
        return false;

    // Who it came from, taken now rather than looked up later: both callers put
    // the card into m_crossings and sanitize it before calling, and the handle
    // has to outlive the card anyway.
    std::string who;
    for (const Crossing& c : m_crossings) {
        if (c.id == crossingId) {
            who = c.pass.handle;
            break;
        }
    }
    m_pieces.noteSource(set, piece, who, when);

    m_profileDirty = true;
    LOG("pieces: %s brought piece %u of %s", crossingId.substr(0, 8).c_str(),
        unsigned(piece), pieceSets()[size_t(set)].name);
    return true;
}

uint32_t Store::passesSent() const
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    return m_passesSent;
}

void Store::rememberPassesSent(uint32_t count)
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    // Only when it moves. The sync worker offers this on every check-in, and
    // dirtying the profile twenty seconds apart forever would mean an SD write
    // twenty seconds apart forever for a number that had not changed.
    if (count == m_passesSent)
        return;
    m_passesSent = count;
    m_profileDirty = true;
}

int Store::acceptedToday() const
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    uint64_t today = startOfLocalDay(nowUnix());
    int n = 0;
    for (const Crossing& c : m_crossings) {
        if (c.lastSeen >= today)
            n++;
    }
    return n;
}

} // namespace nxp
