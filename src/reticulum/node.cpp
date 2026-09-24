/* src/reticulum/node.cpp — voidpalabra/reticulum.hpp over microReticulum.
 *
 * Everything microReticulum is touched through lives in this file and udp.cpp,
 * so the public header carries none of its types. microReticulum's callbacks
 * are plain function pointers and its transport is process-wide, so the state
 * here is one process-wide object, reached from those callbacks. That is the
 * measured shape of the implementation, and the OKF says so.
 *
 * THE LINK WATCHDOG IS OURS. microReticulum left Reticulum's link watchdog
 * unimplemented (Link::start_watchdog is empty; the Python original survives
 * only as a comment). Without it a link request that is lost is PENDING
 * forever, an idle link sends no keepalives (so the Python reference, which
 * does run its watchdog, closes it as stale), and a peer that vanishes leaves
 * its link "up" forever. Found 2026-09-23 behind a relay that drops 10% of
 * datagrams. `watchdog()` below re-implements the reference's rules from the
 * public Link API, driven by Node::loop: the same constants, the same states.
 * It is not a patch to the vendored source; see okf/concepts/reticulum.md. */
#include "voidpalabra/reticulum.hpp"

#include "rooted_fs.hpp"
#include "udp.hpp"

#include <microReticulum/Destination.h>
#include <microReticulum/Identity.h>
#include <microReticulum/Link.h>
#include <microReticulum/Log.h>
#include <microReticulum/Packet.h>
#include <microReticulum/Resource.h>
#include <microReticulum/Reticulum.h>
#include <microReticulum/Transport.h>
#include <microReticulum/Utilities/OS.h>
#include <microStore/Adapters/UniversalFileSystem.h>
#include <microStore/FileSystem.h>

#include <algorithm>
#include <exception>
#include <filesystem>
#include <map>
#include <memory>
#include <deque>
#include <set>

namespace voidpalabra::reticulum {

namespace {

namespace fs = std::filesystem;

RNS::Bytes bytes_of(const std::string& s) { return RNS::Bytes((const uint8_t*)s.data(), s.size()); }
std::string string_of(const RNS::Bytes& b) { return std::string((const char*)b.data(), b.size()); }

struct State {
    Options options;
    bool started = false;
    std::unique_ptr<microStore::FileSystem> filesystem;
    std::unique_ptr<RNS::Reticulum> reticulum;
    RNS::Identity identity{RNS::Type::NONE};
    RNS::Destination destination{RNS::Type::NONE};
    std::vector<RNS::Interface> interfaces;
    std::map<std::string, RNS::Link> links;       // link id hex -> link
    std::map<std::string, std::string> opened_to;  // link id hex -> destination hex (links WE opened)
    std::set<std::string> awaiting_path;           // destination hex
    std::map<std::string, RNS::Link> pending;      // link id hex -> a link WE asked for, not yet up
    struct Watch {
        double activated = 0;    // when the link came up (the reference counts from here)
        double keepalive_sent = 0;
        double stale_since = 0;  // 0 = not stale
    };
    std::map<std::string, Watch> watch;            // link id hex -> watchdog bookkeeping
    std::set<std::string> timed_out;               // links the watchdog is closing
    std::map<std::string, std::deque<std::string>> outbox;  // link id hex -> large messages waiting their turn
    struct Flight {
        RNS::Resource resource;
        double proof_wait_since = 0;  // 0 = not waiting on a proof while others wait
    };
    std::map<std::string, Flight> in_flight;  // link id hex -> its outgoing Resource
    double watchdog_last = 0;
    std::vector<Event> events;
};

/* Never destroyed: microReticulum's own statics may outlive anything we hold,
 * and a destructor running after theirs is the classic exit-time crash. */
State& S() {
    static State* s = new State();
    return *s;
}

void emit(Event e) { S().events.push_back(std::move(e)); }

std::string hex(const RNS::Bytes& b) { return b.toHex(); }

// ── link callbacks (plain function pointers, as microReticulum requires) ─────

void on_packet(const RNS::Bytes& plaintext, const RNS::Packet& packet) {
    Event e;
    e.type = Event::Type::data;
    e.link = hex(packet.destination_hash());  // a link packet is addressed to its link id
    e.bytes = string_of(plaintext);
    emit(std::move(e));
}

void on_resource(const RNS::Resource& resource) {
    if (resource.status() != RNS::Type::Resource::COMPLETE) {
        Event e;
        e.type = Event::Type::error;
        e.link = hex(resource.link().link_id());
        e.detail = "a large message did not arrive completely";
        emit(std::move(e));
        return;
    }
    Event e;
    e.type = Event::Type::data;
    e.link = hex(resource.link().link_id());
    e.bytes = string_of(resource.data());
    emit(std::move(e));
}

void on_closed(RNS::Link& link) {
    const std::string id = hex(link.link_id());
    Event e;
    e.type = Event::Type::link_closed;
    e.link = id;
    State& s = S();
    if (auto d = s.opened_to.find(id); d != s.opened_to.end()) e.destination = d->second;  // so a host can reopen
    if (s.timed_out.erase(id)) {
        e.detail = "timed out";
    } else switch (link.teardown_reason()) {
    // Reticulum names the SIDE that closed (initiator or destination), not "us"
    // or "them": which one we are decides whether it was the peer
    case RNS::Type::Link::TIMEOUT: e.detail = "timed out"; break;
    case RNS::Type::Link::DESTINATION_CLOSED:
        e.detail = link.initiator() ? "closed by the peer" : "closed here";
        break;
    case RNS::Type::Link::INITIATOR_CLOSED:
        e.detail = link.initiator() ? "closed here" : "closed by the peer";
        break;
    default: e.detail = "closed"; break;
    }
    s.links.erase(id);
    s.opened_to.erase(id);
    s.pending.erase(id);
    s.watch.erase(id);
    s.outbox.erase(id);
    s.in_flight.erase(id);
    emit(std::move(e));
}

void on_identified(const RNS::Link& link, const RNS::Identity& remote) {
    Event e;
    e.type = Event::Type::peer_identified;
    e.link = hex(link.link_id());
    e.identity = remote.hexhash();
    emit(std::move(e));
}

void wire_link(RNS::Link& link) {
    link.set_packet_callback(on_packet);
    link.set_link_closed_callback(on_closed);
    link.set_remote_identified_callback(on_identified);
    link.set_resource_strategy(RNS::Type::Link::ACCEPT_ALL);
    link.set_resource_concluded_callback(on_resource);
}

/* A peer opened a link to us. */
void on_inbound(RNS::Link& link) {
    wire_link(link);
    const std::string id = hex(link.link_id());
    S().links[id] = link;
    S().watch[id].activated = RNS::Utilities::OS::time();
    Event e;
    e.type = Event::Type::link_established;
    e.link = id;
    emit(std::move(e));
}

/* A link we opened is up: prove who we are on it, so the peer's Palabra host can
 * verify frames against a real identity rather than a claim. */
void on_outbound(RNS::Link& link) {
    wire_link(link);
    const std::string id = hex(link.link_id());
    S().links[id] = link;
    S().pending.erase(id);
    S().watch[id].activated = RNS::Utilities::OS::time();
    link.identify(S().identity);
    Event e;
    e.type = Event::Type::link_established;
    e.link = id;
    auto it = S().opened_to.find(id);
    if (it != S().opened_to.end()) e.destination = it->second;
    const RNS::Identity& remote = link.get_remote_identity();
    if (remote) e.identity = remote.hexhash();
    emit(std::move(e));
}

class Announces : public RNS::AnnounceHandler {
public:
    explicit Announces(const std::string& filter) : RNS::AnnounceHandler(filter.c_str()) {}
    void received_announce(const RNS::Bytes& destination_hash, const RNS::Identity& identity,
                           const RNS::Bytes& app_data) override {
        Event e;
        e.type = Event::Type::announce;
        e.destination = hex(destination_hash);
        if (identity) e.identity = identity.hexhash();
        e.bytes = string_of(app_data);
        emit(std::move(e));
    }
};

bool open_now(const std::string& dest_hex, std::string* error) {
    RNS::Bytes h;
    h.assignHex(dest_hex.c_str());
    RNS::Identity remote = RNS::Identity::recall(h);
    if (!remote) {
        if (error) *error = "no identity is known for " + dest_hex + " (no announce heard yet)";
        return false;
    }
    RNS::Destination out(remote, RNS::Type::Destination::OUT, RNS::Type::Destination::SINGLE,
                         S().options.app_name.c_str(), S().options.aspects.c_str());
    RNS::Link link(out, on_outbound, on_closed);
    const std::string id = hex(link.link_id());
    S().opened_to[id] = dest_hex;
    S().pending[id] = link;
    return true;
}

/* Reticulum's link watchdog (RNS/Link.py, __watchdog_job and __update_keepalive,
 * reference 1.5.4), which microReticulum does not run. Constants are the
 * reference's, not tuned: two implementations must agree on when a link is dead. */
constexpr double KEEPALIVE_MAX = 360, KEEPALIVE_MIN = 5, KEEPALIVE_MAX_RTT = 1.75;
constexpr double STALE_FACTOR = 2, KEEPALIVE_TIMEOUT_FACTOR = 4, STALE_GRACE = 5;

void time_out(RNS::Link& link) {
    S().timed_out.insert(hex(link.link_id()));
    link.teardown();  // -> on_closed, which reports "timed out"
}

void watchdog() {
    State& s = S();
    const double now = RNS::Utilities::OS::time();
    if (now - s.watchdog_last < 0.1) return;
    s.watchdog_last = now;

    // a link we asked for that never came up (its request or its proof was lost)
    std::vector<RNS::Link> expired;
    for (auto& [id, link] : s.pending) {
        const auto st = link.status();
        if ((st == RNS::Type::Link::PENDING || st == RNS::Type::Link::HANDSHAKE) &&
            now >= link.request_time() + link.establishment_timeout())
            expired.push_back(link);
    }
    for (auto& link : expired) time_out(link);  // outside the loop: on_closed edits `pending`

    std::vector<RNS::Link> dead;
    for (auto& [id, link] : s.links) {
        if (link.status() != RNS::Type::Link::ACTIVE) continue;
        State::Watch& w = s.watch[id];
        const double rtt = link.rtt();
        const double keepalive =
            std::max(std::min(rtt * (KEEPALIVE_MAX / KEEPALIVE_MAX_RTT), KEEPALIVE_MAX), KEEPALIVE_MIN);
        const double stale_time = keepalive * STALE_FACTOR;
        const double last_inbound = std::max(link.last_inbound(), w.activated);
        if (now >= last_inbound + keepalive && link.initiator() && now - w.keepalive_sent >= keepalive) {
            link.send_keepalive();  // the receiver answers; its answer is inbound traffic
            w.keepalive_sent = now;
        }
        if (now < last_inbound + stale_time) {
            w.stale_since = 0;  // traffic resumed (the reference's STALE -> ACTIVE)
        } else if (w.stale_since == 0) {
            w.stale_since = now;
        } else if (now >= w.stale_since + rtt * KEEPALIVE_TIMEOUT_FACTOR + STALE_GRACE) {
            dead.push_back(link);
        }
    }
    for (auto& link : dead) time_out(link);
}

/* ONE RESOURCE AT A TIME, AND NONE DROPPED. Reticulum lets a link carry one
 * outgoing Resource at a time. microReticulum, asked for a second while one is in
 * flight, marks it QUEUED "for the next watchdog tick" - but nothing holds it, so
 * it is silently dropped (found 2026-09-24: behind a lossy relay, every state
 * Palabra re-sent during a slow transfer vanished, for up to a minute). So large
 * messages wait here, in order, and start when the link is ready. */
constexpr std::size_t OUTBOX_LIMIT = 32;

bool start_resource(RNS::Link& link, const std::string& bytes) {
    // constructing a Resource sends nothing; start() encrypts, splits and advertises
    RNS::Resource r(bytes_of(bytes), link);
    r.start();
    if (r.status() == RNS::Type::Resource::QUEUED) return false;  // QUEUED = dropped (see above)
    S().in_flight.insert_or_assign(hex(link.link_id()), State::Flight{r, 0});
    return true;
}

/* A LOST PROOF MUST NOT HOLD THE QUEUE. When every part is sent, the sender
 * waits for the receiver's proof. If the proof is lost, the reference asks the
 * network cache for it again; microReticulum left that out, so it waits out
 * three proof timeouts (~50 s measured on loopback) before failing, and with one
 * Resource per link everything queued behind waits too (found 2026-09-24: a
 * Palabra state stuck 44 s behind a transfer that had in fact arrived). So when
 * something is waiting, a Resource that has sent everything and lacks only its
 * proof gets a grace of max(2 s, 4 x RTT) and is then abandoned. The message may
 * or may not have arrived. Node::send never promised more than "whole or not at
 * all", and a protocol that needs to know acknowledges at its own level (Palabra
 * does). With nothing waiting, the transfer keeps the reference's full patience. */
void unblock_lost_proofs() {
    State& s = S();
    const double now = RNS::Utilities::OS::time();
    for (auto it = s.in_flight.begin(); it != s.in_flight.end();) {
        const auto st = it->second.resource.status();
        auto l = s.links.find(it->first);
        if (l == s.links.end() || st >= RNS::Type::Resource::COMPLETE || st == RNS::Type::Resource::FAILED) {
            it = s.in_flight.erase(it);
            continue;
        }
        auto q = s.outbox.find(it->first);
        const bool waiting = q != s.outbox.end() && !q->second.empty();
        if (st != RNS::Type::Resource::AWAITING_PROOF || !waiting) {
            it->second.proof_wait_since = 0;
            ++it;
            continue;
        }
        if (it->second.proof_wait_since == 0) it->second.proof_wait_since = now;
        if (now - it->second.proof_wait_since >= std::max(2.0, 4 * l->second.rtt())) {
            it->second.resource.cancel();  // tells the receiver; frees the link
            it = s.in_flight.erase(it);
            continue;
        }
        ++it;
    }
}

void drain_outboxes() {
    State& s = S();
    unblock_lost_proofs();
    for (auto it = s.outbox.begin(); it != s.outbox.end();) {
        auto l = s.links.find(it->first);
        if (l == s.links.end()) {
            it = s.outbox.erase(it);
            continue;
        }
        auto& q = it->second;
        while (!q.empty() && l->second.status() == RNS::Type::Link::ACTIVE && l->second.ready_for_new_resource()) {
            if (!start_resource(l->second, q.front())) break;
            q.pop_front();
        }
        it = q.empty() ? s.outbox.erase(it) : std::next(it);
    }
}

RNS::LogLevel level_of(int n) {
    if (n <= 0) return RNS::LOG_NONE;
    if (n == 1) return RNS::LOG_ERROR;
    if (n == 2) return RNS::LOG_WARNING;
    if (n == 3) return RNS::LOG_NOTICE;
    if (n == 4) return RNS::LOG_DEBUG;
    return RNS::LOG_TRACE;
}

} // namespace

Node& Node::instance() {
    static Node n;
    return n;
}

bool Node::started() const { return S().started; }

bool Node::start(const Options& options, std::string* error) {
    State& s = S();
    if (s.started) {
        if (error) *error = "this process already runs a Reticulum node (one per process)";
        return false;
    }
    if (options.storage_dir.empty()) {
        if (error) *error = "no storage_dir: a node needs somewhere to keep its identity";
        return false;
    }
    try {
        RNS::loglevel(level_of(options.log_level));
        std::error_code ec;
        fs::create_directories(options.storage_dir, ec);
        if (ec) {
            if (error) *error = "could not create " + options.storage_dir + ": " + ec.message();
            return false;
        }
        s.options = options;
        // forward slashes everywhere: the filesystem adapter joins paths with '/'
        const std::string dir = fs::path(options.storage_dir).generic_string();
        // rooted: microReticulum's relative paths ("./known_store/"...) land in `dir`
        s.filesystem = std::make_unique<microStore::FileSystem>(new detail::RootedFileSystem(dir));
        s.filesystem->init(false);
        RNS::Utilities::OS::register_filesystem(*s.filesystem);

        // the identity: this device's, reused across runs
        const std::string idfile = dir + "/identity";
        if (fs::exists(idfile)) {
            s.identity = RNS::Identity::from_file(idfile.c_str());
            if (!s.identity) {
                if (error) *error = "the identity file " + idfile + " could not be read";
                return false;
            }
        } else {
            s.identity = RNS::Identity();
            if (!s.identity.to_file(idfile.c_str())) {
                if (error) *error = "could not write the identity to " + idfile;
                return false;
            }
        }

        for (const UdpInterface& u : options.udp) {
            auto impl = std::make_shared<detail::UdpInterfaceImpl>(u.name, u.listen_host, u.listen_port,
                                                                   u.forward_host, u.forward_port, u.broadcast, u.learn_peers);
            std::shared_ptr<RNS::InterfaceImpl> base = impl;
            RNS::Interface iface(base);
            iface.mode(RNS::Type::Interface::MODE_GATEWAY);
            RNS::Transport::register_interface(iface);
            if (!iface.start()) {
                if (error) *error = "interface " + u.name + ": " + impl->error();
                return false;
            }
            s.interfaces.push_back(iface);
        }

        s.reticulum = std::make_unique<RNS::Reticulum>();
        /* AFTER construction, not before: the Reticulum constructor resets the
         * storage path to "." (Reticulum.cpp), so a path set earlier is silently
         * lost and every Reticulum file - transport_identity, the destination
         * table - lands in the process's working directory. Found 2026-09-23 when
         * a transport_identity appeared in this repository's root. */
        RNS::Reticulum::storagepath(dir.c_str());
        RNS::Reticulum::transport_enabled(options.transport);
        s.reticulum->start();

        s.destination = RNS::Destination(s.identity, RNS::Type::Destination::IN, RNS::Type::Destination::SINGLE,
                                         options.app_name.c_str(), options.aspects.c_str());
        s.destination.set_link_established_callback(on_inbound);
        RNS::Transport::register_announce_handler(
            std::make_shared<Announces>(options.app_name + "." + options.aspects));
        s.started = true;
        return true;
    } catch (const std::exception& e) {
        if (error) *error = std::string("Reticulum did not start: ") + e.what();
        return false;
    }
}

void Node::loop() {
    State& s = S();
    if (!s.started) return;
    try {
        s.reticulum->loop();
        watchdog();
        drain_outboxes();
        // links waiting for a path: open the moment one arrives
        for (auto it = s.awaiting_path.begin(); it != s.awaiting_path.end();) {
            RNS::Bytes h;
            h.assignHex(it->c_str());
            if (RNS::Transport::has_path(h)) {
                std::string why;
                if (!open_now(*it, &why)) emit({Event::Type::error, "", *it, "", "", why});
                it = s.awaiting_path.erase(it);
            } else {
                ++it;
            }
        }
    } catch (const std::exception& e) {
        emit({Event::Type::error, "", "", "", "", std::string("loop: ") + e.what()});
    }
}

void Node::announce() {
    State& s = S();
    if (!s.started) return;
    s.destination.announce(bytes_of(s.options.announce_data));
}

void Node::set_announce_data(const std::string& data) { S().options.announce_data = data; }

std::string Node::identity() const { return S().started ? S().identity.hexhash() : std::string(); }
std::string Node::public_key() const { return S().started ? S().identity.get_public_key().toHex() : std::string(); }
std::string Node::destination() const { return S().started ? hex(S().destination.hash()) : std::string(); }

bool Node::open(const std::string& destination_hex, std::string* error) {
    State& s = S();
    if (!s.started) {
        if (error) *error = "the node has not started";
        return false;
    }
    if (destination_hex.size() != 32) {
        if (error) *error = "a destination hash is 32 hex characters";
        return false;
    }
    RNS::Bytes h;
    h.assignHex(destination_hex.c_str());
    if (!RNS::Transport::has_path(h)) {
        RNS::Transport::request_path(h);
        s.awaiting_path.insert(destination_hex);
        return true;
    }
    return open_now(destination_hex, error);
}

bool Node::send(const std::string& link_hex, const std::string& bytes, std::string* error) {
    State& s = S();
    auto it = s.links.find(link_hex);
    if (it == s.links.end() || it->second.status() != RNS::Type::Link::ACTIVE) {
        if (error) *error = "the link is not up";
        return false;
    }
    try {
        if (bytes.size() <= packet_limit()) {
            RNS::Packet(it->second, bytes_of(bytes)).send();
            return true;
        }
        auto& q = s.outbox[link_hex];
        if (q.empty() && it->second.ready_for_new_resource() && start_resource(it->second, bytes)) return true;
        // waits its turn (drain_outboxes). The same bytes already waiting are not
        // queued twice: a protocol that re-sends (Palabra does, every Timing::resend)
        // would otherwise fill the queue with copies behind one slow transfer.
        for (const std::string& waiting : q)
            if (waiting == bytes) return true;
        if (q.size() >= OUTBOX_LIMIT) {
            if (error) *error = "the link is busy: " + std::to_string(q.size()) + " large messages already waiting";
            return false;
        }
        q.push_back(bytes);
        return true;
    } catch (const std::exception& e) {
        if (error) *error = std::string("send: ") + e.what();
        return false;
    }
}

void Node::close(const std::string& link_hex) {
    auto it = S().links.find(link_hex);
    if (it != S().links.end()) it->second.teardown();
}

bool Node::link_up(const std::string& link_hex) const {
    auto it = S().links.find(link_hex);
    return it != S().links.end() && it->second.status() == RNS::Type::Link::ACTIVE;
}

void Node::poll(std::vector<Event>& out) {
    auto& ev = S().events;
    for (auto& e : ev) out.push_back(std::move(e));
    ev.clear();
}

std::size_t Node::packet_limit() { return RNS::Type::Link::MDU; }

std::string Node::destination_hash(const std::string& public_key_hex, const std::string& app_name,
                                   const std::string& aspects) {
    RNS::Identity id(false);
    RNS::Bytes pub;
    pub.assignHex(public_key_hex.c_str());
    id.load_public_key(pub);
    return RNS::Destination::hash(id, app_name.c_str(), aspects.c_str()).toHex();
}

} // namespace voidpalabra::reticulum
