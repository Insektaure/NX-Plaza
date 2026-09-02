#pragma once

#include <cstdint>
#include <string>

namespace nxp {

// Coins, and what has been done with them.
//
// Ten a day for opening the app, which is the whole point of them: a plaza
// only works if people come back to it. The day that decides a grant comes
// from the plaza's own clock, which every reply already carries, so winding
// this console's clock forward earns nothing.
//
// Two running totals rather than a balance, because totals only ever go up and
// a balance goes both ways. That makes the file idempotent to read, and it is
// what would let a server ratchet the spend later without having to agree with
// anything already on the card.
//
// The file is checked with a hash over its own contents and this console's
// secret token. That token is in identity.json and nowhere else, so the check
// is not a constant somebody can lift out of an open-source binary - a tool to
// forge it would have to read the owner's own identity file. It is tamper
// resistant, not tamper proof, and it does not pretend otherwise.
class Wallet {
public:
    static Wallet& get();

    // Reads wallet.dat. A missing file is a new wallet; one that does not
    // verify is treated as a new wallet too, which errs towards no coins.
    void load();

    // Writes it, if anything changed.
    bool flush();

    uint32_t balance() const;
    uint32_t granted() const { return m_granted; }
    uint32_t spent() const { return m_spent; }

    // Unix seconds from the plaza. Grants the day's coins if this is a day the
    // wallet has not seen. Cheap and safe to call on every check-in.
    void notePlazaTime(uint64_t serverTime);

    // Takes `amount` if it is there. Nothing partial: a purchase either
    // happens or does not.
    bool spend(uint32_t amount);

    // How many coins a day is worth.
    static constexpr uint32_t kDaily = 10;
    // What the shop charges. A piece of the puzzle you are filling costs
    // double one drawn from all of them: what is being paid for is the choice,
    // not the piece.
    static constexpr uint32_t kChosenPiecePrice = 100;
    static constexpr uint32_t kAnyPiecePrice = 50;

private:
    Wallet() = default;

    std::string signature() const;

    uint32_t m_granted = 0;
    uint32_t m_spent = 0;
    // The plaza day the last grant was for. Days since the epoch by the
    // server's clock, not this console's.
    uint32_t m_lastDay = 0;
    bool m_dirty = false;
    bool m_loaded = false;
};

} // namespace nxp
