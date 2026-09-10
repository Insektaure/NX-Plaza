#include "core/quest_record.h"

#include "core/identity.h"
#include "core/log.h"
#include "core/util.h"

#include <algorithm>
#include <cstring>

namespace nxp {

namespace {
    const char* kFile = "quest.dat";
    constexpr char kMagic[4] = { 'N', 'X', 'P', 'Q' };
    constexpr uint16_t kVersion = 2;

    constexpr size_t kHeadV1 = 4 + 2 + 2 + 4 + 4;         // through climbs
    constexpr size_t kHeadV2 = kHeadV1 + 2 + 2 + 2 + 2;   // + nextId, counts
    constexpr size_t kItemBytes = 8;
    constexpr size_t kHash = 32;

    // A file claiming more than this is not one this build wrote, and
    // reserving what it asks for is how a corrupt length becomes an
    // allocation nobody meant to make.
    constexpr size_t kMaxEquips = 512;
    constexpr size_t kMaxOwner = 64;
    constexpr uint32_t kSaneFloor = 999;

    void put16(std::string& out, uint16_t v)
    {
        out.push_back(char(uint8_t(v)));
        out.push_back(char(uint8_t(v >> 8)));
    }

    void put32(std::string& out, uint32_t v)
    {
        for (int i = 0; i < 4; i++)
            out.push_back(char(uint8_t(v >> (i * 8))));
    }

    uint16_t get16(const uint8_t* p) { return uint16_t(p[0] | (p[1] << 8)); }

    uint32_t get32(const uint8_t* p)
    {
        return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16)
            | (uint32_t(p[3]) << 24);
    }
}

QuestRecord& QuestRecord::get()
{
    static QuestRecord instance;
    return instance;
}

// Packed exactly as the file of that version is, because a hash over a
// different byte layout is a hash over a different file.
std::string QuestRecord::body(uint16_t version) const
{
    std::string out;
    out.append(kMagic, sizeof(kMagic));
    put16(out, version);
    put16(out, 0);
    put32(out, m_deepest);
    put32(out, m_climbs);
    if (version == 1)
        return out;

    put16(out, m_nextId);
    put16(out, uint16_t(m_items.size()));
    put16(out, uint16_t(m_worn.size()));
    put16(out, 0);

    for (const Item& item : m_items) {
        put16(out, item.id);
        out.push_back(char(item.quality));
        out.push_back(char(item.slot));
        put16(out, item.seed);
        put16(out, item.floor);
    }
    for (const Wearing& row : m_worn) {
        out.push_back(char(uint8_t(row.owner.size())));
        out.append(row.owner);
        for (int i = 0; i < Slot_Count; i++)
            put16(out, row.gear.worn[i]);
    }
    return out;
}

std::string QuestRecord::signature(uint16_t version) const
{
    // The token is the key. It is 256 secret bits that live in identity.json
    // and never leave this console except as a bearer header, so the hash
    // cannot be reproduced from the source alone.
    uint8_t digest[kHash];
    sha256Over({ body(version), identity().token }, digest);
    return std::string(reinterpret_cast<const char*>(digest), kHash);
}

void QuestRecord::load()
{
    if (m_loaded)
        return;
    m_loaded = true;

    std::string blob;
    if (!readWholeFile(dataPath(kFile), blob))
        return; // nobody has climbed yet

    const uint8_t* p = reinterpret_cast<const uint8_t*>(blob.data());
    size_t size = blob.size();
    uint16_t version = size >= 6 ? get16(p + 4) : 0;
    if (size < kHeadV1 + kHash || memcmp(p, kMagic, sizeof(kMagic)) != 0
        || (version != 1 && version != kVersion)) {
        LOG("quest: %s is not a record this build reads; starting empty", kFile);
        return;
    }

    uint32_t deepest = get32(p + 8);
    uint32_t climbs = get32(p + 12);
    uint16_t nextId = 1;
    std::vector<Item> items;
    std::vector<Wearing> worn;

    if (version == 1) {
        if (size != kHeadV1 + kHash) {
            LOG("quest: %s is the wrong size for a version 1 record", kFile);
            return;
        }
    } else {
        if (size < kHeadV2 + kHash) {
            LOG("quest: %s is too short for its own header", kFile);
            return;
        }
        nextId = get16(p + 16);
        size_t itemCount = get16(p + 18);
        size_t equipCount = get16(p + 20);
        if (itemCount > kBagLimit || equipCount > kMaxEquips) {
            LOG("quest: %s claims %zu items and %zu wearers; refusing it", kFile,
                itemCount, equipCount);
            return;
        }

        size_t at = kHeadV2;
        if (size < at + itemCount * kItemBytes + kHash) {
            LOG("quest: %s is shorter than the items it lists", kFile);
            return;
        }
        items.reserve(itemCount);
        for (size_t i = 0; i < itemCount; i++) {
            const uint8_t* q = p + at;
            Item item;
            item.id = get16(q);
            item.quality = q[2];
            item.slot = q[3];
            item.seed = get16(q + 4);
            item.floor = get16(q + 6);
            // A tag from a build that grew a quality or a slot this one does
            // not have would index past the end of a table, so it is refused
            // rather than clamped into meaning something it never meant.
            if (item.id == 0 || item.quality >= Quality_Count
                || item.slot >= Slot_Count) {
                LOG("quest: %s holds an item this build cannot read", kFile);
                return;
            }
            items.push_back(item);
            at += kItemBytes;
        }

        worn.reserve(equipCount);
        for (size_t i = 0; i < equipCount; i++) {
            if (size < at + 1 + kHash) {
                LOG("quest: %s ends inside its wearers", kFile);
                return;
            }
            size_t len = p[at++];
            if (len > kMaxOwner || size < at + len + Slot_Count * 2 + kHash) {
                LOG("quest: %s ends inside a wearer", kFile);
                return;
            }
            Wearing row;
            row.owner.assign(reinterpret_cast<const char*>(p + at), len);
            at += len;
            for (int slot = 0; slot < Slot_Count; slot++) {
                row.gear.worn[slot] = get16(p + at);
                at += 2;
            }
            worn.push_back(std::move(row));
        }

        if (size != at + kHash) {
            LOG("quest: %s has %zu bytes nobody claimed", kFile, size - at - kHash);
            return;
        }
    }

    // Verified against what the file claims, not against what is in memory.
    uint32_t wasDeepest = m_deepest;
    uint32_t wasClimbs = m_climbs;
    m_deepest = deepest;
    m_climbs = climbs;
    m_nextId = nextId;
    m_items = std::move(items);
    m_worn = std::move(worn);

    std::string expected = signature(version);
    bool ok = expected.size() == kHash
        && memcmp(expected.data(), p + (size - kHash), kHash) == 0;
    if (!ok) {
        LOG("quest: %s did not verify; starting empty", kFile);
        m_deepest = wasDeepest;
        m_climbs = wasClimbs;
        m_nextId = 1;
        m_items.clear();
        m_worn.clear();
        return;
    }

    if (version == 1) {
        LOG("quest: upgrading %s from version 1", kFile);
        m_dirty = true;
    }

    if (m_deepest > kSaneFloor) {
        LOG("quest: %s claims floor %u; starting empty", kFile, unsigned(m_deepest));
        m_deepest = 0;
        m_climbs = 0;
        m_dirty = true;
    }

    // An id counter behind the gear it is supposed to be ahead of would hand
    // out an id already in use, and a loadout would then point at two things.
    for (const Item& item : m_items)
        m_nextId = std::max<uint16_t>(m_nextId, uint16_t(item.id + 1));
}

bool QuestRecord::flush()
{
    if (!m_dirty)
        return true;

    std::string file = body(kVersion);
    file.append(signature(kVersion));

    if (!writeWholeFileAtomic(dataPath(kFile), file)) {
        LOG("quest: could not write %s", kFile);
        return false;
    }
    m_dirty = false;
    return true;
}

bool QuestRecord::noteFloor(uint32_t floor)
{
    if (floor == 0 || floor > kSaneFloor || floor <= m_deepest)
        return false;
    m_deepest = floor;
    m_dirty = true;
    LOG("quest: floor %u is a new best", unsigned(floor));
    return true;
}

void QuestRecord::noteClimb()
{
    m_climbs++;
    m_dirty = true;
}

// ---------------------------------------------------------------- the bag

bool QuestRecord::add(const Item& item)
{
    if (!item.valid() || bagFull())
        return false;
    m_items.push_back(item);
    m_nextId = std::max<uint16_t>(m_nextId, uint16_t(item.id + 1));
    m_dirty = true;
    return true;
}

void QuestRecord::discard(uint16_t itemId)
{
    if (itemId == 0)
        return;
    auto it = std::find_if(m_items.begin(), m_items.end(),
        [itemId](const Item& i) { return i.id == itemId; });
    if (it == m_items.end())
        return;
    m_items.erase(it);

    for (Wearing& row : m_worn) {
        for (int slot = 0; slot < Slot_Count; slot++) {
            if (row.gear.worn[slot] == itemId)
                row.gear.worn[slot] = 0;
        }
    }
    m_dirty = true;
}

const Item* QuestRecord::find(uint16_t itemId) const
{
    if (itemId == 0)
        return nullptr;
    for (const Item& item : m_items) {
        if (item.id == itemId)
            return &item;
    }
    return nullptr;
}

// --------------------------------------------------------------- the party

QuestRecord::Wearing* QuestRecord::rowFor(const std::string& owner)
{
    for (Wearing& row : m_worn) {
        if (row.owner == owner)
            return &row;
    }
    return nullptr;
}

const QuestRecord::Wearing* QuestRecord::rowFor(const std::string& owner) const
{
    for (const Wearing& row : m_worn) {
        if (row.owner == owner)
            return &row;
    }
    return nullptr;
}

QuestRecord::Loadout QuestRecord::loadout(const std::string& owner) const
{
    const Wearing* row = rowFor(owner);
    return row ? row->gear : Loadout {};
}

void QuestRecord::equip(const std::string& owner, uint8_t slot, uint16_t itemId)
{
    if (slot >= Slot_Count || owner.size() > kMaxOwner)
        return;
    if (itemId != 0) {
        const Item* item = find(itemId);
        if (!item || item->slot != slot)
            return; // a ring does not go on a weapon peg
        // One item, one wearer. Taken off whoever had it rather than
        // duplicated, which is the bug this loop exists to make impossible.
        for (Wearing& row : m_worn) {
            for (int s = 0; s < Slot_Count; s++) {
                if (row.gear.worn[s] == itemId)
                    row.gear.worn[s] = 0;
            }
        }
    }

    Wearing* row = rowFor(owner);
    if (!row) {
        if (itemId == 0)
            return;
        if (m_worn.size() >= kMaxEquips)
            return;
        m_worn.push_back(Wearing { owner, Loadout {} });
        row = &m_worn.back();
    }
    row->gear.worn[slot] = itemId;
    m_dirty = true;
}

bool QuestRecord::wearer(uint16_t itemId, std::string& owner) const
{
    if (itemId == 0)
        return false;
    for (const Wearing& row : m_worn) {
        for (int slot = 0; slot < Slot_Count; slot++) {
            if (row.gear.worn[slot] == itemId) {
                owner = row.owner;
                return true;
            }
        }
    }
    return false;
}

uint16_t QuestRecord::worstSpare() const
{
    uint16_t worst = 0;
    uint32_t rating = 0;
    for (const Item& item : m_items) {
        std::string ignored;
        if (wearer(item.id, ignored))
            continue;
        uint32_t score = itemRating(item);
        if (worst == 0 || score < rating) {
            worst = item.id;
            rating = score;
        }
    }
    return worst;
}

size_t QuestRecord::clearOut(const Loadout& gear, bool apply)
{
    std::vector<uint16_t> doomed;
    for (const Item& item : m_items) {
        if (item.slot >= Slot_Count)
            continue;
        const Item* against = find(gear.worn[item.slot]);
        if (!against)
            continue; // nothing on that peg, so nothing to be worse than
        std::string ignored;
        if (wearer(item.id, ignored))
            continue; // never anything somebody is standing in
        if (itemRating(item) < itemRating(*against))
            doomed.push_back(item.id);
    }
    if (apply) {
        for (uint16_t id : doomed)
            discard(id);
        if (!doomed.empty())
            LOG("quest: cleared out %zu pieces", doomed.size());
    }
    return doomed.size();
}

size_t QuestRecord::release(const std::vector<std::string>& liveOwners)
{
    size_t freed = 0;
    for (auto it = m_worn.begin(); it != m_worn.end();) {
        // The empty string is your own Mii, which is never gone.
        bool alive = it->owner.empty()
            || std::find(liveOwners.begin(), liveOwners.end(), it->owner)
                != liveOwners.end();
        if (alive) {
            ++it;
            continue;
        }
        for (int slot = 0; slot < Slot_Count; slot++) {
            if (it->gear.worn[slot] != 0)
                freed++;
        }
        it = m_worn.erase(it);
        m_dirty = true;
    }
    if (freed > 0)
        LOG("quest: %zu pieces came back from people who are gone", freed);
    return freed;
}

} // namespace nxp
