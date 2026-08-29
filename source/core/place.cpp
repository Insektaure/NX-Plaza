#include "core/place.h"

#include "core/log.h"
#include "core/util.h"

#include <switch.h>

#include <cstring>

namespace nxp {

namespace {
    // Fixed across all consoles on purpose: two Switches at the same station
    // have to derive the same token, so this cannot be a per-console salt.
    const char* kPlaceSalt = "nx-plaza/place/v1|";

    std::string regionString()
    {
        SetRegion region;
        if (R_FAILED(setGetRegionCode(&region)))
            return "";
        switch (region) {
        case SetRegion_JPN:
            return "JPN";
        case SetRegion_USA:
            return "USA";
        case SetRegion_EUR:
            return "EUR";
        case SetRegion_AUS:
            return "AUS";
        case SetRegion_HTK:
            return "HTK";
        case SetRegion_CHN:
            return "CHN";
        default:
            return "";
        }
    }
}

std::string placeTokenFromSsid(const std::string& ssid)
{
    if (ssid.empty())
        return std::string();

    uint8_t digest[32];
    sha256Over({ kPlaceSalt, ssid }, digest);
    return toHex(digest, 8); // 64 bits is plenty to collide venues, not people
}

PlaceInfo currentPlace()
{
    PlaceInfo info;
    info.regionCode = regionString();

    NifmInternetConnectionType type {};
    NifmInternetConnectionStatus status {};
    u32 strength = 0;
    if (R_SUCCEEDED(nifmGetInternetConnectionStatus(&type, &strength, &status))) {
        info.online = status == NifmInternetConnectionStatus_Connected;
        info.wireless = type == NifmInternetConnectionType_WiFi;
    }

    NifmNetworkProfileData profile {};
    if (R_SUCCEEDED(nifmGetCurrentNetworkProfile(&profile))) {
        const NifmWirelessSettingData& w = profile.wireless_setting_data;
        size_t len = w.ssid_len;
        if (len > sizeof(w.ssid) - 1)
            len = sizeof(w.ssid) - 1;

        std::string ssid(w.ssid, len);
        if (!ssid.empty()) {
            info.token = placeTokenFromSsid(ssid);
            info.networkName = ssid;
        } else {
            profile.network_name[sizeof(profile.network_name) - 1] = '\0';
            info.networkName = profile.network_name;
        }
    }

    return info;
}

std::string suggestedHandle()
{
    std::string name;

    AccountUid uid {};
    if (R_FAILED(accountGetPreselectedUser(&uid)) || !accountUidIsValid(&uid)) {
        s32 total = 0;
        if (R_FAILED(accountListAllUsers(&uid, 1, &total)) || total < 1)
            uid = AccountUid {};
    }

    if (accountUidIsValid(&uid)) {
        AccountProfile profile;
        if (R_SUCCEEDED(accountGetProfile(&profile, uid))) {
            AccountProfileBase base {};
            if (R_SUCCEEDED(accountProfileGet(&profile, nullptr, &base))) {
                base.nickname[sizeof(base.nickname) - 1] = '\0';
                name = base.nickname;
            }
            accountProfileClose(&profile);
        }
    }

    if (name.empty()) {
        SetSysDeviceNickName nickname {};
        if (R_SUCCEEDED(setsysGetDeviceNickname(&nickname))) {
            nickname.nickname[sizeof(nickname.nickname) - 1] = '\0';
            name = nickname.nickname;
        }
    }

    return clampUtf8(trim(name), 16);
}

} // namespace nxp
