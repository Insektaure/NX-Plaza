#include "core/wallet.h"

#include "core/identity.h"
#include "core/log.h"
#include "core/util.h"

#include <cstring>

namespace nxp {

namespace {
    const char* kFile = "wallet.dat";
    constexpr char kMagic[4] = { 'N', 'X', 'P', 'W' };
    // 2 added `won`. A version 1 file still reads: it is parsed with no
    // winnings and rewritten as version 2 on the next flush, so nobody loses a
    // coin to the upgrade.
    constexpr uint16_t kVersion = 2;

    // magic, version, reserved, granted, spent, lastDay, won, then the hash.
    constexpr size_t kBodyV1 = 4 + 2 + 2 + 4 + 4 + 4;
    constexpr size_t kBodySize = kBodyV1 + 4;
    constexpr size_t kFileSize = kBodySize + 32;
    constexpr size_t kFileV1 = kBodyV1 + 32;

    size_t bodySizeFor(uint16_t version)
    {
        return version == 1 ? kBodyV1 : kBodySize;
    }

    void put16(uint8_t*& p, uint16_t v)
    {
        *p++ = uint8_t(v);
        *p++ = uint8_t(v >> 8);
    }

    void put32(uint8_t*& p, uint32_t v)
    {
        for (int i = 0; i < 4; i++)
            *p++ = uint8_t(v >> (i * 8));
    }

    uint16_t get16(const uint8_t* p) { return uint16_t(p[0] | (p[1] << 8)); }

    uint32_t get32(const uint8_t* p)
    {
        return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16)
            | (uint32_t(p[3]) << 24);
    }

    // Whole days since the epoch, by whatever clock produced `when`.
    uint32_t dayOf(uint64_t when) { return uint32_t(when / 86400); }
}

Wallet& Wallet::get()
{
    static Wallet instance;
    return instance;
}

std::string Wallet::signature(uint16_t version) const
{
    // Packed exactly as the file of that version is, because a hash over a
    // different byte layout is a hash over a different file.
    uint8_t body[kBodySize];
    uint8_t* p = body;
    memcpy(p, kMagic, sizeof(kMagic));
    p += sizeof(kMagic);
    put16(p, version);
    put16(p, 0);
    put32(p, m_granted);
    put32(p, m_spent);
    put32(p, m_lastDay);
    if (version != 1)
        put32(p, m_won);
    size_t size = bodySizeFor(version);

    // The token is the key. It is 256 secret bits that live in identity.json
    // and never leave this console except as a bearer header, so the hash
    // cannot be reproduced from the source alone.
    uint8_t digest[32];
    sha256Over({ std::string(reinterpret_cast<const char*>(body), size),
                   identity().token },
        digest);
    return std::string(reinterpret_cast<const char*>(digest), sizeof(digest));
}

void Wallet::load()
{
    if (m_loaded)
        return;
    m_loaded = true;

    std::string blob;
    if (!readWholeFile(dataPath(kFile), blob))
        return; // no wallet yet, which is a wallet with nothing in it

    const uint8_t* p = reinterpret_cast<const uint8_t*>(blob.data());
    uint16_t version = blob.size() >= 6 ? get16(p + 4) : 0;
    bool sizeOk = (version == 1 && blob.size() == kFileV1)
        || (version == kVersion && blob.size() == kFileSize);
    if (!sizeOk || memcmp(p, kMagic, sizeof(kMagic)) != 0
        || (version != 1 && version != kVersion)) {
        LOG("wallet: %s is not a wallet this build reads; starting empty", kFile);
        return;
    }

    uint32_t granted = get32(p + 8);
    uint32_t spent = get32(p + 12);
    uint32_t lastDay = get32(p + 16);
    uint32_t won = version == 1 ? 0u : get32(p + 20);

    // Verified against what the file claims, not against what is in memory.
    uint32_t wasGranted = m_granted;
    uint32_t wasSpent = m_spent;
    uint32_t wasDay = m_lastDay;
    uint32_t wasWon = m_won;
    m_granted = granted;
    m_spent = spent;
    m_lastDay = lastDay;
    m_won = won;
    std::string expected = signature(version);
    bool ok = expected.size() == 32
        && memcmp(expected.data(), p + bodySizeFor(version), 32) == 0;

    if (!ok) {
        // Erring towards no coins on purpose. A wallet that does not verify has
        // either been edited or been corrupted, and of the two ways to be
        // wrong, handing out coins is the worse one.
        LOG("wallet: %s did not verify; starting empty", kFile);
        m_granted = wasGranted;
        m_spent = wasSpent;
        m_lastDay = wasDay;
        m_won = wasWon;
        return;
    }

    // A version 1 file is now in memory as a version 2 wallet with nothing
    // won. Marking it dirty is what upgrades the file, on the next flush.
    if (version == 1) {
        LOG("wallet: upgrading %s from version 1", kFile);
        m_dirty = true;
    }

    if (m_spent > m_granted + m_won) {
        // Only reachable by editing, since spend() will not go past the
        // balance. Clamped rather than refused: the owner keeps their record
        // of having spent it.
        LOG("wallet: spent %u of %u earned; clamping", unsigned(m_spent),
            unsigned(m_granted + m_won));
        m_spent = m_granted + m_won;
        m_dirty = true;
    }
}

bool Wallet::flush()
{
    if (!m_dirty)
        return true;

    uint8_t file[kFileSize];
    uint8_t* p = file;
    memcpy(p, kMagic, sizeof(kMagic));
    p += sizeof(kMagic);
    put16(p, kVersion);
    put16(p, 0);
    put32(p, m_granted);
    put32(p, m_spent);
    put32(p, m_lastDay);
    put32(p, m_won);

    std::string sig = signature(kVersion);
    memcpy(file + kBodySize, sig.data(), 32);

    if (!writeWholeFileAtomic(dataPath(kFile),
            std::string(reinterpret_cast<const char*>(file), kFileSize))) {
        LOG("wallet: could not write %s", kFile);
        return false;
    }
    m_dirty = false;
    return true;
}

uint32_t Wallet::balance() const
{
    uint32_t earned = m_granted + m_won;
    return earned > m_spent ? earned - m_spent : 0u;
}

void Wallet::award(uint32_t amount)
{
    if (amount == 0)
        return;
    m_won += amount;
    m_dirty = true;
    LOG("wallet: won %u, %u to spend", unsigned(amount), unsigned(balance()));
}

void Wallet::notePlazaTime(uint64_t serverTime)
{
    if (serverTime == 0)
        return;
    uint32_t day = dayOf(serverTime);
    if (day <= m_lastDay)
        return;

    // Ten for the day, however many days have passed. Paying for the ones that
    // were missed would reward leaving the app closed for a fortnight, which
    // is the opposite of what this is for.
    m_granted += kDaily;
    m_lastDay = day;
    m_dirty = true;
    LOG("wallet: %u coins for a new day, %u to spend", unsigned(kDaily),
        unsigned(balance()));
}

bool Wallet::spend(uint32_t amount)
{
    if (amount == 0 || balance() < amount)
        return false;
    m_spent += amount;
    m_dirty = true;
    return true;
}

} // namespace nxp
