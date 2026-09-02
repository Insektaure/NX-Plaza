#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/mii.h"

#include <jansson.h>

namespace nxp {

// How far a crossing is allowed to reach. Venue is the closest thing to real
// StreetPass: only consoles that shared a network with you.
enum Reach : int {
    Reach_Venue = 0, // same place token only (same Wi-Fi network)
    Reach_Area = 1,  // place token or the same coarse network area
    Reach_World = 2, // top up with strangers from anywhere ("world plaza")
    Reach_Count
};

// How much of where-we-met leaves the console.
enum PlaceSharing : int {
    Place_Off = 0,      // no label at all
    Place_District = 1, // the label the user typed, e.g. "Namba Station"
    Place_City = 2,     // city only, e.g. "Osaka"
    Place_Count
};

// Everything a console is willing to say about itself.
//
// This whole struct is the payload that travels between consoles - it is
// deliberately tiny (about 400 bytes of JSON), in the spirit of the 3DS's
// few-hundred-byte StreetPass records. It never contains the account name,
// friend code, IP, or a precise position.
struct Pass {
    std::string handle;   // chosen name, <= 16 characters
    std::string greeting; // <= 60 characters
    std::string activity; // "trading radishes"
    std::string playing;  // title the user chose to advertise, or empty
    std::string district; // coarse place label, or empty when sharing is off

    std::vector<std::string> carrying; // up to 4 small tradeable things
    std::vector<std::string> games;    // up to 8 titles, for "games you share"

    // How many titles are on the sender's console. Zero means "not said",
    // and what any pass says whose owner has turned off sharing what they are playing.
    uint32_t titles = 0;

    // The face, as 78 hex characters. Empty until the console makes one, which
    // is why `portrait` stays: it seeds a plausible face for a pass that has
    // never been through the editor, so nobody is ever a blank.
    std::string mii;
    uint32_t portrait = 0; // face seed, and the hue of the card's stage
    uint32_t theme = 0;    // card theme index, see ui/theme.h
    uint32_t hours = 0;    // hours in the advertised title
    uint32_t met = 0;      // how many people carry this pass

    // Clamps every field to its documented maximum. Always called on both
    // sides of the wire, so a hostile server cannot hand us a 4 MB greeting.
    void sanitize();

    bool isBlank() const { return handle.empty() && greeting.empty(); }

    // The face this pass wears: the stored one, or one derived from the seed.
    Mii face() const;
    void setFace(const Mii& mii);

    json_t* toJson() const;
    static Pass fromJson(json_t* obj);

    // A pass with sensible defaults for a console that has never run the app.
    static Pass makeDefault(const std::string& suggestedHandle);
};

// A pass that arrived from someone else, plus our local bookkeeping.
struct Crossing {
    std::string id; // the other console's public id
    Pass pass;

    uint64_t firstSeen = 0; // unix seconds, first time we ever crossed
    uint64_t lastSeen = 0;  // unix seconds, most recent crossing
    uint32_t count = 1;     // how many times we have crossed this console

    std::string place;       // where we crossed, as they described it
    bool opened = false;     // has the user looked at it yet
    bool tradedBack = false; // did the user send something back

    // Which record in crossings.idx this is, so one card can be written back
    // without rewriting the collection. Travels with the crossing through the
    // re-sorting the list does, and is not part of the pass or the wire.
    static constexpr uint32_t kNoSlot = 0xFFFFFFFFu;
    uint32_t slot = kNoSlot;

    // What still has to reach the disk. Transient: never serialised, and kept
    // on the crossing rather than in a list of slots so it survives the store
    // re-sorting itself.
    bool recordDirty = false; // a fixed-width field changed (read, traded back)
    bool textDirty = false;   // the pass itself changed, so the blob must grow

    json_t* toJson() const;
    static Crossing fromJson(json_t* obj);
};

// A console the server says is awake in the same place as us right now.
// Feeds the radar screen; never persisted.
struct Peer {
    enum State : int {
        State_Waiting = 0,
        State_Exchanging,
        State_Passed,
        State_OutOfRange,
    };

    std::string handle;
    std::string playing;
    std::string mii;       // their face, so the radar shows who is out there
    uint32_t portrait = 0;
    State state = State_Waiting;

    Mii face() const;
    int closeness = 0; // 0 = same network, higher = looser match

    // The radar's distance caption. The number is a closeness bucket, not a
    // real distance - we have no way to measure metres over the internet.
    std::string proximityLabel() const;
};

// Portrait seeds are unpacked into these knobs by the drawing code.
uint32_t portraitStyle(uint32_t seed);
uint32_t portraitHueDegrees(uint32_t seed);
uint32_t makePortraitSeed();

} // namespace nxp
