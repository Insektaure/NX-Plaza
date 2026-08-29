#pragma once

#include <cstdint>
#include <string>

namespace nxp {

// The console's identity on the plaza network.
//
// Generated once, on the very first launch, and then kept on the SD card at
// sdmc:/switch/nx-plaza/identity.json. Nothing about it is derived from the
// console: no serial, no MAC, no account id. It is 128 bits of CSPRNG output,
// which means it cannot be used to link a pass back to a physical Switch, and
// the user can throw it away and become someone new at any time.
struct Identity {
    // Public. 32 lowercase hex characters (128 bits). Travels with every
    // pass so the other side can recognise a repeat crossing.
    std::string id;

    // Secret. 64 lowercase hex characters (256 bits). Sent as a bearer token
    // so nobody else can publish or delete passes as us. Never leaves the
    // console except in the Authorization header.
    std::string token;

    uint64_t created = 0; // unix seconds
    uint32_t version = 1;

    bool valid() const;

    // Human-readable form of the first 35 bits, e.g. "K4M-92X7". Shown on the
    // first-run screen for pairing a companion device, and in Settings so two
    // people can confirm they actually crossed.
    std::string shortCode() const;
};

// Loads the identity, generating and persisting one if this is a first launch.
// Returns false only if the SD card is unwritable, in which case the app runs
// with a session-only identity so it still starts.
bool identityInit();

// The identity for this process. Valid after identityInit().
const Identity& identity();

// Throws away this console's identity and generates a new one. Used by
// Settings > "Start over": every pass we handed out becomes unlinkable to us.
bool identityRotate();

} // namespace nxp
