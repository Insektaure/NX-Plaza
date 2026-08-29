#include "core/identity.h"

#include "core/json.h"
#include "core/log.h"
#include "core/util.h"

namespace nxp {

namespace {
    Identity g_identity;
    bool g_persisted = false;

    const char* kFile = "identity.json";

    Identity generate()
    {
        Identity id;
        id.version = 1;
        id.id = randomHex(16);    // 128 bits
        id.token = randomHex(32); // 256 bits
        id.created = nowUnix();
        return id;
    }

    bool save(const Identity& id)
    {
        json_t* root = json_object();
        json_object_set_new(root, "version", json_integer(id.version));
        json_object_set_new(root, "id", json_string(id.id.c_str()));
        json_object_set_new(root, "token", json_string(id.token.c_str()));
        json_object_set_new(root, "created", json_integer(static_cast<json_int_t>(id.created)));
        json_object_set_new(root, "note",
            json_string("Public id and secret token for nx-plaza. "
                        "Copy this file to move your plaza identity to another console; "
                        "delete it to become someone new."));

        bool ok = js::writeFile(dataPath(kFile), root);
        json_decref(root);
        return ok;
    }

    // Keeps a corrupt file around instead of silently overwriting it - if a
    // user's identity file got truncated we do not want to be the reason their
    // 1,284 crossings became unattributable.
    void quarantine(const std::string& path)
    {
        std::string backup = format("%s.bad-%llu", path.c_str(),
            (unsigned long long)nowUnix());
        rename(path.c_str(), backup.c_str());
        LOG("identity: kept unreadable file as %s", backup.c_str());
    }
}

bool Identity::valid() const
{
    return id.size() == 32 && token.size() == 64;
}

std::string Identity::shortCode() const
{
    // Crockford-ish base32 without the letters that get misread out loud.
    static const char* alphabet = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";

    uint8_t raw[16] = {};
    if (!fromHex(id, raw, sizeof(raw)))
        return "--------";

    uint64_t v = 0;
    for (int i = 0; i < 5; i++)
        v = (v << 8) | raw[i];

    char out[10];
    int o = 0;
    for (int i = 6; i >= 0; i--) {
        if (o == 3)
            out[o++] = '-';
        out[o++] = alphabet[(v >> (i * 5)) & 0x1F];
    }
    out[o] = '\0';
    return std::string(out);
}

bool identityInit()
{
    ensureDataDir();
    std::string path = dataPath(kFile);

    if (fileExists(path)) {
        json_t* root = js::readFile(path);
        if (root) {
            Identity loaded;
            loaded.version = static_cast<uint32_t>(js::getInt(root, "version", 1));
            loaded.id = js::getStr(root, "id");
            loaded.token = js::getStr(root, "token");
            loaded.created = static_cast<uint64_t>(js::getInt(root, "created"));
            json_decref(root);

            if (loaded.valid()) {
                g_identity = loaded;
                g_persisted = true;
                LOG("identity: loaded %s (created %llu)", g_identity.id.c_str(),
                    (unsigned long long)g_identity.created);
                return true;
            }
        }
        quarantine(path);
    }

    g_identity = generate();
    g_persisted = save(g_identity);
    LOG("identity: generated %s%s", g_identity.id.c_str(),
        g_persisted ? "" : " (NOT PERSISTED -- sd card unwritable)");
    return g_persisted;
}

const Identity& identity()
{
    // A caller reaching us before identityInit() would otherwise get an empty
    // id and quietly talk to the server as nobody; generate on demand instead.
    if (!g_identity.valid())
        g_identity = generate();
    return g_identity;
}

bool identityRotate()
{
    g_identity = generate();
    g_persisted = save(g_identity);
    LOG("identity: rotated to %s", g_identity.id.c_str());
    return g_persisted;
}

} // namespace nxp
