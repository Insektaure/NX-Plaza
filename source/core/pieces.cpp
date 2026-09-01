#include "core/pieces.h"

#include "core/util.h"

#include <algorithm>
#include <cstdio>
#include <ctime>

namespace nxp {

namespace {
    // Fifteen to a puzzle.
    //
    // With pieces drawn at random, filling fifteen takes about fifty crossings
    // with people not already met today - the coupon collector's sum,
    // 15 * (1 + 1/2 + ... + 1/15). At the default cap of twelve crossings a day
    // that is several days on a busy plaza and a great deal longer on a quiet
    // one.
    //
    // Fifteen also cuts as 5x3, which is the shape a picture wants.
    const std::vector<PieceSet> kSets = {
        { "Forest in the Rain", "forest_in_the_rain", 15 },
        { "Mountain in the Fog", "mountain_in_the_fog", 15 },
        { "Mystical Swamp", "mystical_swamp", 15 },
    };

    // The day, in whole local days since the epoch. Local rather than UTC so
    // the set turns over when the owner's day does, not in the middle of an
    // evening.
    uint64_t localDayIndex(uint64_t when)
    {
        time_t t = time_t(when);
        struct tm parts {};
        localtime_r(&t, &parts);
        parts.tm_hour = 0;
        parts.tm_min = 0;
        parts.tm_sec = 0;
        time_t midnight = mktime(&parts);
        if (midnight == time_t(-1))
            return when / 86400;
        return uint64_t(midnight) / 86400;
    }
}

const std::vector<PieceSet>& pieceSets() { return kSets; }

int pieceSetIndex(const std::string& picture)
{
    if (picture.empty())
        return -1;
    for (size_t i = 0; i < kSets.size(); i++) {
        if (picture == kSets[i].image)
            return static_cast<int>(i);
    }
    return -1;
}

uint8_t pieceFor(const std::string& myId, const std::string& theirId, uint64_t when, int set)
{
    if (set < 0 || size_t(set) >= kSets.size())
        return 0;
    uint8_t count = kSets[size_t(set)].count;
    if (count == 0)
        return 0;

    // Order matters: my id first, so the two consoles in a crossing derive
    // different pieces. This is not a swap - each side is drawing its own.
    char day[24];
    snprintf(day, sizeof(day), "%llu", (unsigned long long) localDayIndex(when));

    std::string seed = myId;
    seed += '|';
    seed += theirId;
    seed += '|';
    seed += day;
    seed += '|';
    seed += std::to_string(set);

    return uint8_t(fnv1a(seed) % count);
}

// ------------------------------------------------------------------ holding

bool PieceInventory::has(int set, uint8_t piece) const
{
    if (set < 0 || size_t(set) >= owned.size() || piece >= 32)
        return false;
    return (owned[size_t(set)] & (1u << piece)) != 0;
}

bool PieceInventory::take(int set, uint8_t piece)
{
    if (set < 0 || size_t(set) >= owned.size() || piece >= 32)
        return false;
    uint32_t bit = 1u << piece;
    if (owned[size_t(set)] & bit)
        return false; // already held: a duplicate, and not news
    owned[size_t(set)] |= bit;
    return true;
}

int PieceInventory::countHeld(int set) const
{
    if (set < 0 || size_t(set) >= owned.size())
        return 0;
    uint32_t mask = owned[size_t(set)];
    int n = 0;
    while (mask) {
        n += int(mask & 1u);
        mask >>= 1;
    }
    return n;
}

bool PieceInventory::complete(int set) const
{
    if (set < 0 || size_t(set) >= kSets.size())
        return false;
    return countHeld(set) >= int(kSets[size_t(set)].count);
}

void PieceInventory::noteSource(int set, uint8_t piece, const std::string& who,
    uint64_t when)
{
    if (set < 0 || size_t(set) >= kSets.size())
        return;
    noteSourceFor(kSets[size_t(set)].image, piece, who, when);
}

void PieceInventory::noteSourceFor(const std::string& picture, uint8_t piece,
    const std::string& who, uint64_t when)
{
    int set = pieceSetIndex(picture);
    if (set < 0 || piece >= kSets[size_t(set)].count)
        return;

    // Replace rather than append if something is already here. Nothing should
    // call this twice for one piece, but an inventory that grew two entries for
    // the same tile would quietly disagree with itself about who gave it.
    for (PieceSource& src : sources) {
        if (src.picture == picture && src.piece == piece) {
            src.who = who;
            src.when = when;
            return;
        }
    }

    PieceSource src;
    src.picture = picture;
    src.piece = piece;
    src.who = who;
    src.when = when;
    sources.push_back(std::move(src));
}

const PieceSource* PieceInventory::sourceFor(int set, uint8_t piece) const
{
    if (set < 0 || size_t(set) >= kSets.size())
        return nullptr;
    for (const PieceSource& src : sources) {
        if (src.piece == piece && src.picture == kSets[size_t(set)].image)
            return &src;
    }
    return nullptr;
}

void PieceInventory::normalise()
{
    owned.resize(kSets.size(), 0u);

    // Bits past the end of a set cannot be held. A set that shrank between
    // builds would otherwise read as complete on pieces that no longer exist.
    for (size_t i = 0; i < owned.size(); i++) {
        uint8_t count = kSets[i].count;
        uint32_t valid = count >= 32 ? 0xFFFFFFFFu : ((1u << count) - 1u);
        owned[i] &= valid;
    }

    if (active < 0 || size_t(active) >= kSets.size())
        active = 0;

    // A source is only meaningful next to the piece it belongs to. Anything
    // naming a picture this build does not have, or a piece that is not held,
    // is dropped here rather than carried forward to confuse a screen.
    sources.erase(std::remove_if(sources.begin(), sources.end(),
                      [this](const PieceSource& src) {
                          return !has(pieceSetIndex(src.picture), src.piece);
                      }),
        sources.end());
}

std::vector<std::string> PieceInventory::toHex() const
{
    std::vector<std::string> out;
    out.reserve(owned.size());
    for (uint32_t mask : owned) {
        char b[16];
        snprintf(b, sizeof(b), "%08x", mask);
        out.emplace_back(b);
    }
    return out;
}

void PieceInventory::fromHex(const std::vector<std::string>& hex, int activeSet)
{
    owned.clear();
    owned.reserve(hex.size());
    for (const std::string& s : hex) {
        uint32_t mask = 0;
        // Read by hand rather than with strtoul: a stray character should mean
        // "no pieces", not whatever the parser stopped at.
        bool ok = !s.empty() && s.size() <= 8;
        for (char c : s) {
            int digit = -1;
            if (c >= '0' && c <= '9')
                digit = c - '0';
            else if (c >= 'a' && c <= 'f')
                digit = c - 'a' + 10;
            else if (c >= 'A' && c <= 'F')
                digit = c - 'A' + 10;
            if (digit < 0) {
                ok = false;
                break;
            }
            mask = (mask << 4) | uint32_t(digit);
        }
        owned.push_back(ok ? mask : 0u);
    }
    active = activeSet;
    normalise();
}

} // namespace nxp
