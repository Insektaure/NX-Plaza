#pragma once

#include <string>

namespace nxp {

// What the console can honestly say about where it is.
//
// The Switch has no GPS and homebrew cannot read the Wi-Fi scan list, so
// "proximity" is approximated by the network you are attached to. The SSID
// itself never leaves the console: it is hashed, and only 64 bits of the hash
// travel, which is enough for two consoles on the same network to land in the
// same bucket and nothing else.
struct PlaceInfo {
    bool online = false;      // the console currently has internet
    bool wireless = false;    // attached over Wi-Fi rather than LAN
    std::string token;        // 16 hex chars, or empty when unknown
    std::string networkName;  // SSID / profile name -- LOCAL DISPLAY ONLY
    std::string regionCode;   // "JPN", "USA", ... from system settings
};

// Reads the current network profile. Cheap enough to call once a second.
PlaceInfo currentPlace();

// The salted, truncated hash of an SSID. Exposed for tests and for the
// docs; `currentPlace()` already applies it.
std::string placeTokenFromSsid(const std::string& ssid);

// The account nickname, used only as the suggested handle on first run. It is
// never sent anywhere unless the user keeps it as their pass handle.
std::string suggestedHandle();

} // namespace nxp
