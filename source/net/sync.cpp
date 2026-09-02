#include "net/sync.h"

#include "core/identity.h"
#include "core/json.h"
#include "core/log.h"
#include "core/place.h"
#include "core/store.h"
#include "core/util.h"
#include "net/http.h"

#include <algorithm>
#include <cstdio>   // remove(), for the pending file

namespace nxp {

namespace {
    constexpr uint64_t kCheckinIntervalMs = 20 * 1000;
    constexpr uint64_t kExchangeIntervalMs = 60 * 1000;
    constexpr uint64_t kTickMs = 2 * 1000;
    constexpr uint64_t kMaxBackoffMs = 5 * 60 * 1000;
    constexpr int kMaxPerExchange = 8;

    Peer::State peerStateFromString(const std::string& s)
    {
        if (s == "exchanging")
            return Peer::State_Exchanging;
        if (s == "passed")
            return Peer::State_Passed;
        if (s == "far")
            return Peer::State_OutOfRange;
        return Peer::State_Waiting;
    }
}

bool Sync::start()
{
    if (m_threadStarted)
        return true;

    // After the guard, not before it: loadPending() appends, so a second
    // start() would read the file twice and owe the plaza everything twice.
    // Before the thread, so the worker finds last session's arrears already
    // in its queues.
    loadPending();

    ueventCreate(&m_wakeEvent, false);
    m_running = true;

    // 128 KiB of stack: curl plus an mbedTLS handshake needs considerably more
    // than the default a plain thread would get.
    Result rc = threadCreate(&m_thread, threadEntry, this, nullptr, 128 * 1024, 0x2C, -2);
    if (R_FAILED(rc)) {
        LOG("sync: threadCreate failed (0x%x)", rc);
        m_running = false;
        return false;
    }

    rc = threadStart(&m_thread);
    if (R_FAILED(rc)) {
        LOG("sync: threadStart failed (0x%x)", rc);
        threadClose(&m_thread);
        m_running = false;
        return false;
    }

    m_threadStarted = true;
    return true;
}

void Sync::stop()
{
    if (!m_threadStarted)
        return;

    m_running = false;
    ueventSignal(&m_wakeEvent);
    threadWaitForExit(&m_thread);
    threadClose(&m_thread);
    m_threadStarted = false;
}

void Sync::threadEntry(void* self)
{
    static_cast<Sync*>(self)->run();
}

void Sync::kick()
{
    m_exchangeWanted = true;
    m_nextCheckinMs = 0;
    ueventSignal(&m_wakeEvent);
}

void Sync::publishPass()
{
    m_publishWanted = true;
    ueventSignal(&m_wakeEvent);
}

void Sync::forgetMe()
{
    m_forgetWanted = true;
    ueventSignal(&m_wakeEvent);
}

void Sync::blockPeer(const std::string& id)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_blockQueue.push_back(Pending { id, 0 });
        m_pendingDirty = true;
    }
    ueventSignal(&m_wakeEvent);
}

void Sync::unblockPeer(const std::string& id)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_unblockQueue.push_back(Pending { id, 0 });
        m_pendingDirty = true;
    }
    ueventSignal(&m_wakeEvent);
}

namespace {
    const char* kPendingFile = "pending.json";
}

void Sync::markPendingDirty()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_pendingDirty = true;
}

void Sync::loadPending()
{
    json_t* root = js::readFile(dataPath(kPendingFile));
    if (!root)
        return;

    // Read straight into the queues rather than through a helper: Pending is
    // private to this class, and a free function cannot name it without either
    // making it public or dragging jansson into the header.
    auto read = [&](const char* key, std::vector<Pending>& out) {
        json_t* list = js::getArr(root, key);
        if (!list)
            return;
        size_t i = 0;
        json_t* entry = nullptr;
        json_array_foreach(list, i, entry) {
            if (!json_is_object(entry))
                continue;
            std::string id = js::getStr(entry, "id");
            // A bounded read of a file anybody could have edited: a wrong-length
            // id is not a console, and sixty-four is far past anything a person
            // does by hand.
            if (id.size() != 32 || out.size() >= 64)
                continue;
            out.push_back(Pending { id, static_cast<int>(js::getInt(entry, "tries", 0)) });
        }
    };

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        read("block", m_blockQueue);
        read("unblock", m_unblockQueue);
        m_unblockAllWanted = js::getBool(root, "unblock_all", false);

        if (!m_blockQueue.empty() || !m_unblockQueue.empty() || m_unblockAllWanted) {
            LOG("sync: %zu blocks and %zu unblocks still owed to the plaza%s",
                m_blockQueue.size(), m_unblockQueue.size(),
                m_unblockAllWanted ? ", plus a clear-all" : "");
        }
    }
    json_decref(root);
}

void Sync::savePendingIfDirty()
{
    json_t* root = nullptr;
    bool empty = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_pendingDirty)
            return;
        m_pendingDirty = false;

        empty = m_blockQueue.empty() && m_unblockQueue.empty() && !m_unblockAllWanted;
        if (!empty) {
            auto write = [](const std::vector<Pending>& queue) {
                json_t* list = json_array();
                for (const Pending& item : queue) {
                    json_t* e = json_object();
                    json_object_set_new(e, "id", json_string(item.id.c_str()));
                    json_object_set_new(e, "tries", json_integer(item.tries));
                    json_array_append_new(list, e);
                }
                return list;
            };
            root = json_object();
            json_object_set_new(root, "version", json_integer(1));
            json_object_set_new(root, "block", write(m_blockQueue));
            json_object_set_new(root, "unblock", write(m_unblockQueue));
            json_object_set_new(root, "unblock_all", json_boolean(m_unblockAllWanted));
        }
    }

    // Outside the lock: this is an SD write, and the drawing thread takes the
    // same mutex every frame for the status pip.
    const std::string path = dataPath(kPendingFile);
    if (empty) {
        // Nothing owed, so nothing on the card. An absent file is the clean
        // state rather than an empty object somebody has to interpret.
        remove(path.c_str());
        return;
    }
    if (!js::writeFile(path, root))
        LOG("sync: could not write %s", kPendingFile);
    json_decref(root);
}

void Sync::requeue(std::vector<Pending>& queue, Pending item, const char* what)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    // Either way the file changes: the entry comes back with one more attempt
    // against it, or it is gone for good and should not be read again on the
    // next launch.
    m_pendingDirty = true;
    if (++item.tries >= kMaxSendTries) {
        LOG("sync: giving up on %s for %s after %d tries", what, item.id.c_str(),
            item.tries);
        return;
    }
    queue.push_back(std::move(item));
}

void Sync::unblockAllPeers()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_unblockAllWanted = true;
        m_pendingDirty = true;
    }
    ueventSignal(&m_wakeEvent);
}

Sync::Status Sync::status() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_status;
}

std::vector<Peer> Sync::peers() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_peers;
}

std::vector<std::string> Sync::takeArrivals()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<std::string> out;
    out.swap(m_arrivals);
    return out;
}

std::string Sync::endpoint(const char* path) const
{
    std::string base = trim(Store::get().settings().serverUrl);
    while (!base.empty() && base.back() == '/')
        base.pop_back();
    if (base.empty())
        return std::string();
    return base + path;
}

void Sync::setState(State state, const std::string& message)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_status.state = state;
    m_status.message = message;
}

void Sync::setError(const std::string& message)
{
    LOG("sync: %s", message.c_str());
    std::lock_guard<std::mutex> lock(m_mutex);
    m_status.state = State::Error;
    m_status.message = message;
    m_status.lastAttempt = nowUnix();
}

// A reply that failed, and what to do about it.
//
// A 404 from anything but hello means one thing: the server has no row for
// this console. Either the operator wiped the database, or the console is
// pointed at a different plaza than the one it registered with. Both are
// recovered the same way - say hello again, which re-registers the id under
// the token this console already holds.
//
// Without this a wiped server left every existing console reporting an error
// forever: nothing re-sends hello on its own, so the only ways back were
// editing your pass or pressing "Check in now" by hand, neither of which the
// error message suggests.
bool Sync::failed(const HttpResponse& response, const char* what)
{
    if (response.status == 404 && response.error.empty()) {
        LOG("sync: server does not know us (404 on %s); saying hello again", what);
        m_publishWanted = true;
    }
    setError(response.error.empty()
            ? format("Server said %ld on %s", response.status, what)
            : response.error);
    return false;
}

bool Sync::doHello()
{
    std::string url = endpoint("/v1/hello");
    if (url.empty()) {
        setError("No plaza server set yet. Settings > Plaza server.");
        return false;
    }

    PlaceInfo place = currentPlace();
    Pass pass = Store::get().outgoingPass();

    json_t* root = json_object();
    json_object_set_new(root, "id", json_string(identity().id.c_str()));
    json_object_set_new(root, "client", json_string("nx-plaza/" APP_VERSION));
    json_object_set_new(root, "region", json_string(place.regionCode.c_str()));
    json_object_set_new(root, "pass", pass.toJson());
    std::string body = js::dump(root);
    json_decref(root);

    HttpResponse response = Http::postJson(url, body, identity().id, identity().token);
    if (!response.ok()) {
        setError(response.error.empty()
                ? format("Server said %ld on hello", response.status)
                : response.error);
        return false;
    }

    json_t* reply = js::parse(response.body);
    if (reply) {
        uint32_t handedOut = static_cast<uint32_t>(js::getInt(reply, "met"));
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_status.handedOut = handedOut;
        }
        // Remembered on the card too, so the pass screen has a figure before
        // the first check-in of the next session and while offline. Outside our
        // own lock: the store takes its own, and holding two is how an ordering
        // bug gets written.
        //
        // Zero is not written. A reply that omits the field parses as zero, and
        // storing that would erase a figure the card already holds.
        if (handedOut > 0)
            Store::get().rememberPassesSent(handedOut);
        json_decref(reply);
    }

    m_publishWanted = false;
    return true;
}

bool Sync::doCheckin()
{
    std::string url = endpoint("/v1/checkin");
    if (url.empty())
        return false;

    Settings settings = Store::get().settings();
    PlaceInfo place = currentPlace();

    json_t* root = json_object();
    json_object_set_new(root, "id", json_string(identity().id.c_str()));
    json_object_set_new(root, "place", json_string(place.token.c_str()));
    json_object_set_new(root, "reach", json_integer(settings.reach));
    json_object_set_new(root, "district", json_string(settings.sharedPlaceLabel().c_str()));
    json_object_set_new(root, "awake", json_boolean(true));
    std::string body = js::dump(root);
    json_decref(root);

    HttpResponse response = Http::postJson(url, body, identity().id, identity().token, 6000);
    if (!response.ok())
        return failed(response, "check-in");

    json_t* reply = js::parse(response.body);
    if (!reply) {
        setError("Server sent something that was not JSON");
        return false;
    }

    // How many consoles the radar will plot. Nothing to do with the daily
    // crossing limit, which happens to default to the same number: that one is
    // how many passes you accept in a day and the user can set it to anything
    // from 1 to 48. This is how many faces fit on the rings legibly.
    constexpr size_t kMaxPeers = 12;

    std::vector<Peer> peers;
    json_t* list = js::getArr(reply, "peers");
    if (list) {
        size_t index;
        json_t* value;
        json_array_foreach(list, index, value) {
            if (!json_is_object(value) || peers.size() >= kMaxPeers)
                continue;
            Peer peer;
            peer.handle = clampUtf8(js::getStr(value, "handle"), 16);
            peer.playing = clampUtf8(js::getStr(value, "playing"), 32);
            peer.mii = clampUtf8(js::getStr(value, "mii"), 40);
            peer.portrait = static_cast<uint32_t>(js::getInt(value, "portrait"));
            peer.closeness = static_cast<int>(js::getInt(value, "closeness"));
            peer.state = peerStateFromString(js::getStr(value, "state"));
            peers.push_back(std::move(peer));
        }
    }

    int pending = static_cast<int>(js::getInt(reply, "pending"));
    json_decref(reply);

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_peers = std::move(peers);
        m_status.awake = static_cast<int>(m_peers.size());
        m_status.pending = pending;
        m_status.placeKnown = !place.token.empty();
        m_status.networkName = place.networkName;
        m_status.placeToken = place.token;
        m_status.lastSuccess = nowUnix();
    }

    if (pending > 0)
        m_exchangeWanted = true;

    return true;
}

bool Sync::doExchange()
{
    Store& store = Store::get();
    Settings settings = store.settings();

    int budget = settings.dailyLimit - store.acceptedToday();
    if (budget <= 0) {
        setState(State::Idle, "Daily crossing limit reached. Back tomorrow.");
        m_exchangeWanted = false;
        return true;
    }

    std::string url = endpoint("/v1/exchange");
    if (url.empty())
        return false;

    PlaceInfo place = currentPlace();

    json_t* root = json_object();
    json_object_set_new(root, "id", json_string(identity().id.c_str()));
    json_object_set_new(root, "place", json_string(place.token.c_str()));
    json_object_set_new(root, "reach", json_integer(settings.reach));
    json_object_set_new(root, "district", json_string(settings.sharedPlaceLabel().c_str()));
    json_object_set_new(root, "max", json_integer(std::min(budget, kMaxPerExchange)));
    json_object_set_new(root, "pass", store.outgoingPass().toJson());
    std::string body = js::dump(root);
    json_decref(root);

    HttpResponse response = Http::postJson(url, body, identity().id, identity().token, 10000);
    if (!response.ok())
        return failed(response, "exchange");

    json_t* reply = js::parse(response.body);
    if (!reply) {
        setError("Server sent something that was not JSON");
        return false;
    }

    std::vector<std::string> arrivals;
    json_t* list = js::getArr(reply, "crossings");
    if (list) {
        size_t index;
        json_t* value;
        json_array_foreach(list, index, value) {
            if (!json_is_object(value))
                continue;

            std::string id = js::getStr(value, "id");
            Pass pass = Pass::fromJson(json_object_get(value, "pass"));
            std::string label = clampUtf8(js::getStr(value, "place"), 32);
            uint64_t when = static_cast<uint64_t>(js::getInt(value, "at", static_cast<int64_t>(nowUnix())));

            if (id.empty() || pass.handle.empty())
                continue;

            store.recordCrossing(id, pass, label, when);
            arrivals.push_back(id);
        }
    }

    int remaining = static_cast<int>(js::getInt(reply, "remaining"));
    uint32_t met = static_cast<uint32_t>(js::getInt(reply, "met"));
    json_decref(reply);

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_status.pending = remaining;
        m_status.lastSuccess = nowUnix();
        if (met > 0)
            m_status.handedOut = met;
        for (const std::string& id : arrivals)
            m_arrivals.push_back(id);
    }

    // Outside the lock, as above. Zero is not written: a reply that omits the
    // field would otherwise erase a figure the card already holds.
    if (met > 0)
        Store::get().rememberPassesSent(met);

    if (!arrivals.empty())
        LOG("sync: %zu passes arrived", arrivals.size());

    m_exchangeWanted = remaining > 0;
    return true;
}

bool Sync::doSimple(const char* path, const std::string& extraKey,
    const std::string& extraValue)
{
    std::string url = endpoint(path);
    if (url.empty())
        return false;

    json_t* root = json_object();
    json_object_set_new(root, "id", json_string(identity().id.c_str()));
    if (!extraKey.empty())
        json_object_set_new(root, extraKey.c_str(), json_string(extraValue.c_str()));
    std::string body = js::dump(root);
    json_decref(root);

    HttpResponse response = Http::postJson(url, body, identity().id, identity().token);
    if (!response.ok())
        return failed(response, path);
    return true;
}

void Sync::run()
{
    LOG("sync: worker started");

    while (m_running) {
        uint64_t now = monotonicMs();
        Settings settings = Store::get().settings();
        PlaceInfo place = currentPlace();

        bool didWork = false;
        bool failed = false;

        if (!Http::available()) {
            setState(State::Offline, "Networking is unavailable.");
        } else if (!place.online) {
            setState(State::Offline, "No internet connection.");
            std::lock_guard<std::mutex> lock(m_mutex);
            m_status.networkName = place.networkName;
            m_status.placeToken = place.token;
            m_status.placeKnown = false;
        } else {
            std::vector<Pending> blocks;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                blocks.swap(m_blockQueue);
            }
            for (Pending& item : blocks) {
                setState(State::Working, "Blocking a console...");
                if (!doSimple("/v1/block", "target", item.id)) {
                    // Put back rather than dropped. The local half of a block
                    // has already been applied, so losing this one leaves this
                    // console filtering their passes while ours keep reaching
                    // them - the one failure that points the wrong way.
                    requeue(m_blockQueue, item, "block");
                    failed = true;
                }
                didWork = true;
                markPendingDirty();
            }

            std::vector<Pending> unblocks;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                unblocks.swap(m_unblockQueue);
            }
            for (Pending& item : unblocks) {
                setState(State::Working, "Unblocking a console...");
                if (!doSimple("/v1/unblock", "target", item.id)) {
                    // Same reasoning inverted: the entry is already gone from
                    // profile.json, so dropping this leaves the plaza holding a
                    // block with nothing left on the card able to name it.
                    requeue(m_unblockQueue, item, "unblock");
                    failed = true;
                }
                didWork = true;
                markPendingDirty();
            }

            bool clearBlocks = false;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                std::swap(clearBlocks, m_unblockAllWanted);
            }
            if (clearBlocks) {
                setState(State::Working, "Clearing the block list...");
                if (!doSimple("/v1/unblock-all", std::string(), std::string())) {
                    // Put back, unlike a single unblock: this is the recovery
                    // for a card whose list is already incomplete, so losing it
                    // to one bad request would leave nothing able to ask again.
                    std::lock_guard<std::mutex> lock(m_mutex);
                    m_unblockAllWanted = true;
                    failed = true;
                }
                didWork = true;
                markPendingDirty();
            }

            if (m_forgetWanted) {
                setState(State::Working, "Asking the server to forget us...");
                if (doSimple("/v1/forget", std::string(), std::string()))
                    m_forgetWanted = false;
                else
                    failed = true;
                didWork = true;
            }

            if (m_running && m_publishWanted) {
                setState(State::Working, "Publishing your pass...");
                if (!doHello())
                    failed = true;
                didWork = true;
            }

            if (m_running && !failed && now >= m_nextCheckinMs) {
                setState(State::Working, "Looking around...");
                if (doCheckin())
                    m_nextCheckinMs = monotonicMs() + kCheckinIntervalMs;
                else
                    failed = true;
                didWork = true;
            }

            bool exchangeDue = settings.autoExchange && now >= m_nextExchangeMs;
            if (m_running && !failed && (exchangeDue || m_exchangeWanted)) {
                setState(State::Working, "Trading passes...");
                if (doExchange())
                    m_nextExchangeMs = monotonicMs() + kExchangeIntervalMs;
                else
                    failed = true;
                didWork = true;
            }

            if (!failed && didWork) {
                m_failures = 0;
                Status snapshot = status();
                std::string message;
                if (!snapshot.placeKnown) {
                    message = "Connected over LAN: matching by network area only.";
                } else if (snapshot.awake > 0) {
                    message = format("%d console%s awake near you", snapshot.awake,
                        snapshot.awake == 1 ? "" : "s");
                } else {
                    message = "Nobody else is awake here yet.";
                }
                setState(State::Idle, message);
            }
        }

        // One write per pass rather than one per change: a burst of sends
        // settles first, and the file is only touched when something moved.
        savePendingIfDirty();

        if (failed) {
            m_failures = std::min(m_failures + 1, 8u);
            uint64_t backoff = std::min<uint64_t>(kTickMs << m_failures, kMaxBackoffMs);
            m_nextCheckinMs = monotonicMs() + backoff;
            m_nextExchangeMs = m_nextCheckinMs;
            m_exchangeWanted = false;
        }

        if (!m_running)
            break;

        // Sleep until the next due task, or until someone signals us.
        uint64_t nowMs = monotonicMs();
        uint64_t nextDue = std::min(m_nextCheckinMs, m_nextExchangeMs);
        uint64_t waitMs = nextDue > nowMs ? nextDue - nowMs : kTickMs;
        waitMs = std::min<uint64_t>(std::max<uint64_t>(waitMs, 250), 30 * 1000);
        waitSingle(waiterForUEvent(&m_wakeEvent), waitMs * 1000000ULL);
    }

    LOG("sync: worker stopped");
}

} // namespace nxp
