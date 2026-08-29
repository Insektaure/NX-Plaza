#include "app.h"
#include "core/identity.h"
#include "core/play_history.h"
#include "core/log.h"
#include "core/store.h"
#include "core/util.h"
#include "net/http.h"

#include <switch.h>

namespace {

// Services this app needs, in the order they have to come up.
// Anything optional is allowed to fail: a console with no account service
// still gets a working plaza, just without a suggested handle.
struct Services {
    bool romfs = false;
    bool pl = false;
    bool nifm = false;
    bool setsys = false;
    bool set = false;
    bool account = false;
    bool csrng = false;

    bool init()
    {
        romfs = R_SUCCEEDED(romfsInit());
        if (!romfs)
            return false; // shaders live in romfs; nothing works without it

        nxp::ensureDataDir();
        nxp::logInit();

        csrng = R_SUCCEEDED(csrngInitialize());
        pl = R_SUCCEEDED(plInitialize(PlServiceType_User));
        if (!pl) {
            LOG("main: pl (shared fonts) unavailable");
            return false;
        }

        nifm = R_SUCCEEDED(nifmInitialize(NifmServiceType_User));
        set = R_SUCCEEDED(setInitialize());
        setsys = R_SUCCEEDED(setsysInitialize());
        account = R_SUCCEEDED(accountInitialize(AccountServiceType_Application));

        return true;
    }

    void exit()
    {
        if (account)
            accountExit();
        if (setsys)
            setsysExit();
        if (set)
            setExit();
        if (nifm)
            nifmExit();
        if (pl)
            plExit();
        if (csrng)
            csrngExit();

        nxp::logExit();

        if (romfs)
            romfsExit();
    }
};

} // namespace

int main(int argc, char* argv[])
{
    // argv[0] is the NRO the loader ran, and the only thing the updater is ever
    // allowed to write over. Taken before anything else can fail.
    if (argc > 0 && argv && argv[0])
        nxp::setExecutablePath(argv[0]);

    Services services;
    if (!services.init()) {
        services.exit();
        return 1;
    }

    LOG("nx-plaza " APP_VERSION " starting");

    // The unique id is created here, before anything can need it: the store
    // stamps it into crossings.json and the sync worker authenticates with it.
    nxp::identityInit();
    nxp::Store::get().load();

    // Now that the settings exist, the log file can be opened - or not. Lines
    // from before this point were buffered and are written out if it opens.
    nxp::logSetFileEnabled(nxp::Store::get().settings().logToFile);

    nxp::Http::globalInit();

    // Off the drawing thread and started early, so the passport has it by the
    // time anybody opens that tab.
    nxp::beginPlayHistoryLoad();

    {
        nxp::App app;
        if (app.init())
            app.run();
        app.exit();
    }

    nxp::Store::get().flush();

    // Joins the loader if it is still going. It holds ns and pdm sessions, and
    // letting the process tear down around a thread that is mid-IPC is how a
    // clean exit becomes a hang.
    nxp::endPlayHistory();

    nxp::Http::globalExit();
    services.exit();
    return 0;
}
