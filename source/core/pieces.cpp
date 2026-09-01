#include "core/pieces.h"

#include "core/util.h"

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
        { "Turnip Grove", 15 },
        { "Lantern Bay", 15 },
        { "Hollowreach", 15 },
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
