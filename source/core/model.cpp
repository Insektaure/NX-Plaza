#include "core/model.h"

#include "core/json.h"
#include "core/util.h"

#include <algorithm>

namespace nxp {

namespace {
    constexpr size_t kMaxHandle = 16;
    constexpr size_t kMaxGreeting = 60;
    constexpr size_t kMaxActivity = 28;
    constexpr size_t kMaxTitle = 32;
    constexpr size_t kMaxDistrict = 24;
    constexpr size_t kMaxCarrying = 4;
    constexpr size_t kMaxGames = 8;

    void clampList(std::vector<std::string>& list, size_t maxItems, size_t maxLen)
    {
        if (list.size() > maxItems)
            list.resize(maxItems);
        for (std::string& s : list)
            s = clampUtf8(trim(s), maxLen);
        list.erase(std::remove_if(list.begin(), list.end(),
                       [](const std::string& s) { return s.empty(); }),
            list.end());
    }
}

Mii Pass::face() const
{
    Mii parsed;
    if (!mii.empty() && Mii::fromHex(mii, parsed))
        return parsed;
    return Mii::fromSeed(portrait);
}

void Pass::setFace(const Mii& face)
{
    mii = face.toHex();
}

Mii Peer::face() const
{
    Mii parsed;
    if (!mii.empty() && Mii::fromHex(mii, parsed))
        return parsed;
    return Mii::fromSeed(portrait);
}

void Pass::sanitize()
{
    handle = clampUtf8(trim(handle), kMaxHandle);
    greeting = clampUtf8(trim(greeting), kMaxGreeting);
    activity = clampUtf8(trim(activity), kMaxActivity);
    playing = clampUtf8(trim(playing), kMaxTitle);
    district = clampUtf8(trim(district), kMaxDistrict);

    clampList(carrying, kMaxCarrying, kMaxTitle);
    clampList(games, kMaxGames, kMaxTitle);

    // A face that does not parse is dropped rather than drawn wrong; the seed
    // then stands in for it.
    Mii probe;
    if (!mii.empty() && !Mii::fromHex(mii, probe))
        mii.clear();

    if (theme >= 6)
        theme = 0;
    hours = std::min<uint32_t>(hours, 99999);
    met = std::min<uint32_t>(met, 9999999);
}

json_t* Pass::toJson() const
{
    json_t* o = json_object();
    json_object_set_new(o, "handle", json_string(handle.c_str()));
    json_object_set_new(o, "greeting", json_string(greeting.c_str()));
    json_object_set_new(o, "activity", json_string(activity.c_str()));
    json_object_set_new(o, "playing", json_string(playing.c_str()));
    json_object_set_new(o, "district", json_string(district.c_str()));
    json_object_set_new(o, "carrying", js::strArray(carrying));
    json_object_set_new(o, "games", js::strArray(games));
    json_object_set_new(o, "mii", json_string(mii.c_str()));
    json_object_set_new(o, "portrait", json_integer(portrait));
    json_object_set_new(o, "theme", json_integer(theme));
    json_object_set_new(o, "hours", json_integer(hours));
    json_object_set_new(o, "met", json_integer(met));
    return o;
}

Pass Pass::fromJson(json_t* obj)
{
    Pass p;
    if (!json_is_object(obj))
        return p;

    p.handle = js::getStr(obj, "handle");
    p.greeting = js::getStr(obj, "greeting");
    p.activity = js::getStr(obj, "activity");
    p.playing = js::getStr(obj, "playing");
    p.district = js::getStr(obj, "district");
    p.carrying = js::getStrArray(obj, "carrying", kMaxCarrying);
    p.games = js::getStrArray(obj, "games", kMaxGames);
    p.mii = js::getStr(obj, "mii");
    p.portrait = static_cast<uint32_t>(js::getInt(obj, "portrait"));
    p.theme = static_cast<uint32_t>(js::getInt(obj, "theme"));
    p.hours = static_cast<uint32_t>(js::getInt(obj, "hours"));
    p.met = static_cast<uint32_t>(js::getInt(obj, "met"));
    p.sanitize();
    return p;
}

Pass Pass::makeDefault(const std::string& suggestedHandle)
{
    Pass p;
    p.handle = suggestedHandle.empty() ? std::string("Traveller") : suggestedHandle;
    p.greeting = "Just passing through.";
    p.activity = "";
    p.playing = "";
    p.district = "";
    p.portrait = makePortraitSeed();
    p.setFace(Mii::fromSeed(p.portrait));
    p.theme = 0;
    p.sanitize();
    return p;
}

json_t* Crossing::toJson() const
{
    json_t* o = json_object();
    json_object_set_new(o, "id", json_string(id.c_str()));
    json_object_set_new(o, "pass", pass.toJson());
    json_object_set_new(o, "first_seen", json_integer(static_cast<json_int_t>(firstSeen)));
    json_object_set_new(o, "last_seen", json_integer(static_cast<json_int_t>(lastSeen)));
    json_object_set_new(o, "count", json_integer(count));
    json_object_set_new(o, "place", json_string(place.c_str()));
    json_object_set_new(o, "opened", json_boolean(opened));
    json_object_set_new(o, "traded_back", json_boolean(tradedBack));
    return o;
}

Crossing Crossing::fromJson(json_t* obj)
{
    Crossing c;
    if (!json_is_object(obj))
        return c;

    c.id = js::getStr(obj, "id");
    c.pass = Pass::fromJson(json_object_get(obj, "pass"));
    c.firstSeen = static_cast<uint64_t>(js::getInt(obj, "first_seen"));
    c.lastSeen = static_cast<uint64_t>(js::getInt(obj, "last_seen"));
    c.count = static_cast<uint32_t>(js::getInt(obj, "count", 1));
    c.place = clampUtf8(js::getStr(obj, "place"), 32);
    c.opened = js::getBool(obj, "opened");
    c.tradedBack = js::getBool(obj, "traded_back");
    if (c.count == 0)
        c.count = 1;
    return c;
}

std::string Peer::proximityLabel() const
{
    switch (closeness) {
    case 0:
        return "same network";
    case 1:
        return "nearby network";
    default:
        return "somewhere else";
    }
}

uint32_t portraitStyle(uint32_t seed)
{
    return seed & 0x7u;
}

uint32_t portraitHueDegrees(uint32_t seed)
{
    return (seed >> 8) % 360u;
}

uint32_t makePortraitSeed()
{
    uint32_t v = 0;
    randomBytes(&v, sizeof(v));
    return v;
}

} // namespace nxp
