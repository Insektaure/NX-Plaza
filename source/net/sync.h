#pragma once

#include "core/model.h"
#include "net/http.h"

#include <switch.h>

#include <atomic>
#include <mutex>
#include <string>
#include <vector>

namespace nxp {

// The exchange engine: one background thread that announces this console to
// the plaza server and pulls in whatever passes are waiting.
//
// StreetPass on the 3DS was a radio that traded records with anything that
// walked past. There is no equivalent radio on the Switch that homebrew can
// drive, so the "walking past" is inferred server-side from a hashed network
// token plus a coarse network area, and the trade itself happens over HTTP.
// Everything else about the model is kept: passes are tiny, they arrive while
// you are doing something else, and you find them later.
class Sync {
public:
    enum class State {
        Offline,  // no internet, or networking failed to start
        Idle,     // connected, nothing to do right now
        Working,  // a request is in flight
        Error,    // last request failed; will retry with backoff
    };

    struct Status {
        State state = State::Offline;
        std::string message;      // one line, shown on the Nearby screen
        std::string networkName;  // local display only, never sent
        // The match token, published here so no screen has to ask the system
        // for it. The worker looks the network up anyway, once per check-in.
        std::string placeToken;
        bool placeKnown = false;  // we have a place token to match on
        uint64_t lastSuccess = 0; // unix seconds
        uint64_t lastAttempt = 0;
        int awake = 0;            // consoles announced in the same place
        int pending = 0;          // passes the server is holding for us
        uint32_t handedOut = 0;   // how many consoles carry our pass
    };

    bool start();
    void stop();

    // Wake the worker immediately (the inbox's manual refresh).
    void kick();

    // Our pass or privacy settings changed: republish on the next tick.
    void publishPass();

    // Ask the server to forget this console entirely.
    void forgetMe();

    // Tell the server never to pair us with `id` again.
    void blockPeer(const std::string& id);

    // Lifts the block this console put in place. The plaza lifts only ours: if
    // they blocked us too, the two stay apart and the server does not say so.
    void unblockPeer(const std::string& id);

    // Asks the plaza to drop every block this console put in place, without
    // naming them. The list on the card can be incomplete - it is a file on an
    // SD card, and restoring an old one loses entries the plaza still holds -
    // and those are reachable by owner when they are not reachable by id.
    void unblockAllPeers();

    Status status() const;
    std::vector<Peer> peers() const;

    // Console ids recorded since the last call. Drives the arrival toast.
    std::vector<std::string> takeArrivals();

private:
    static void threadEntry(void* self);
    void run();

    // Reports a failed reply, and re-registers when the server has

    // forgotten this console. Always returns false.

    bool failed(const HttpResponse& response, const char* what);

    bool doHello();
    bool doCheckin();
    bool doExchange();
    bool doSimple(const char* path, const std::string& extraKey,
        const std::string& extraValue);

    std::string endpoint(const char* path) const;
    void setError(const std::string& message);
    void setState(State state, const std::string& message);

    Thread m_thread {};
    UEvent m_wakeEvent {};
    std::atomic<bool> m_running { false };
    std::atomic<bool> m_threadStarted { false };
    std::atomic<bool> m_publishWanted { true };
    std::atomic<bool> m_exchangeWanted { true };
    std::atomic<bool> m_forgetWanted { false };

    mutable std::mutex m_mutex;
    Status m_status;
    std::vector<Peer> m_peers;
    std::vector<std::string> m_arrivals;
    // Ids waiting to go to the plaza, with the attempts each has cost.
    struct Pending {
        std::string id;
        int tries = 0;
    };
    std::vector<Pending> m_blockQueue;
    std::vector<Pending> m_unblockQueue;
    bool m_unblockAllWanted = false;

    // Six attempts span about two minutes: the worker's backoff doubles from
    // two seconds, so they land at 0, 4, 12, 28, 60 and 124 seconds. That
    // covers a server restart or a lift being taken. Past it the request is
    // dropped - one the plaza will never accept, a malformed id or a console it
    // has no row for, should not be retried for the life of the session.
    //
    // The backoff is the worker's, not this item's, so a session where other
    // requests are also failing spends these six sooner.
    static constexpr int kMaxSendTries = 6;

    // Puts a failed send back on its queue, counting the attempt, and gives up
    // once it has cost kMaxSendTries. Declared here rather than with the other
    // methods because it names Pending, which is declared just above.
    void requeue(std::vector<Pending>& queue, Pending item, const char* what);

    // The queues, on the card, so closing the app does not lose them.
    //
    // Only the worker touches the file. The queues are also read by the drawing
    // thread - status() runs every frame - and an SD write while holding the
    // mutex would stall a frame, which is how opening Settings used to freeze
    // the app. So a caller marks the queues dirty and the worker writes.
    void loadPending();
    void savePendingIfDirty();
    void markPendingDirty();

    bool m_pendingDirty = false;

    uint64_t m_nextCheckinMs = 0;
    uint64_t m_nextExchangeMs = 0;
    unsigned m_failures = 0;
};

} // namespace nxp
