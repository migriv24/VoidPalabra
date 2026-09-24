/* voidpalabra/reticulum.hpp — Reticulum, the family's network, as Palabra speaks it.
 *
 * okf/concepts/reticulum.md. Decided by the author 2026-09-23: all
 * device-to-device networking in the Void family runs over Reticulum, and Void
 * Palabra is its Void-based translation. This is the OPTIONAL companion target
 * `voidpalabra_reticulum`. The `voidpalabra` core does not link it, and stays
 * zero-dependency and pure.
 *
 * WHAT IT GIVES A HOST
 *   - an IDENTITY (Reticulum's two keys: Ed25519 to sign, X25519 to agree),
 *     kept in `storage_dir` and reused across runs;
 *   - one DESTINATION (`<app>.<aspects>`, e.g. `voidpalabra.sync`) that it
 *     announces, and that peers open links to;
 *   - LINKS (encrypted, forward-secret) over which it moves opaque bytes: a
 *     message that fits one packet goes as a packet, anything larger as a
 *     Reticulum Resource. Either way the receiver gets the whole message, or
 *     nothing;
 *   - EVENTS, polled: announces heard, links up and down, a peer's identity
 *     proven, messages received.
 *
 * WHAT IT DOES NOT DO
 *   - Sync. Palabra's `sync::Session` stays a pure state machine; a host moves
 *     its frames through `send` and feeds `data` events to `receive`
 *     (`SyncLink` below does exactly that).
 *   - Decide who may join. That stays the application's.
 *
 * ONE NODE PER PROCESS. The Reticulum implementation underneath
 * (microReticulum) keeps its transport in process-wide state, and its callbacks
 * are plain function pointers. So `Node` is a process singleton, and a test of
 * two peers is two processes. The OKF records this as a measured limit, not a
 * design choice.
 *
 * THREADING: none. Call everything from one thread; `loop()` pumps the network.
 * No microReticulum type appears in this header.
 */
#pragma once

#include "voidpalabra/sync.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace voidpalabra::reticulum {

/* A UDP interface: this node listens on `listen_port` and sends to
 * `forward_host:forward_port`. Reticulum's own `UDPInterface` has the same
 * shape (listen and forward), so the Python reference and this node speak to
 * each other with mirrored ports. `forward_host` may be a broadcast address
 * (set `broadcast`), which is how a LAN of several nodes hears one announce. */
struct UdpInterface {
    std::string name = "udp";
    std::string listen_host = "0.0.0.0";
    std::uint16_t listen_port = 0;
    std::string forward_host = "127.0.0.1";
    std::uint16_t forward_port = 0;
    bool broadcast = false;
    /* Also send every datagram, by unicast, to each address heard from in the
     * last minute: how a phone that drops incoming broadcast still hears the
     * desktop that heard its broadcast. Also how one node serves many on a
     * single machine: each peer forwards to it, and it answers whoever spoke. */
    bool learn_peers = false;
};

struct Options {
    /* Where the identity and Reticulum's own state live. Created if missing.
     * One per device, never shared between two running nodes. */
    std::string storage_dir;
    std::string app_name = "voidpalabra";
    std::string aspects = "sync";         // dotted, as Reticulum writes them
    std::string announce_data;            // app data carried by this node's announces
    std::vector<UdpInterface> udp;
    bool transport = false;               // route for others (an always-on node)
    int log_level = 1;                    // 0 none, 1 errors, 2 warnings, 3 notices, 4 debug, 5 trace
};

struct Event {
    enum class Type {
        announce,         // a peer's destination was heard: `destination`, `identity`, `bytes` = its app data
        link_established, // `link` is up; `destination` is set when WE opened it
        peer_identified,  // the peer on `link` proved `identity`
        link_closed,      // `link` is gone; `detail` says why
        data,             // a whole message arrived on `link`: `bytes`
        error,            // `detail`
    };
    Type type = Type::error;
    std::string link;         // link id, hex
    std::string destination;  // destination hash, hex
    std::string identity;     // identity hash, hex
    std::string bytes;
    std::string detail;
};

class Node {
public:
    static Node& instance();  // one per process (see the header comment)

    /* Loads or creates the identity, opens the interfaces, starts Reticulum.
     * False with a reason on failure. Starting twice is refused. */
    bool start(const Options& options, std::string* error = nullptr);
    bool started() const;

    /* Pump the network. Call often (every frame, or every few ms). */
    void loop();

    void announce();
    /* What this node's announces carry from now on (Options::announce_data at start). */
    void set_announce_data(const std::string& data);

    std::string identity() const;        // this node's identity hash, hex
    std::string public_key() const;      // both public keys, hex (64 bytes)
    std::string destination() const;    // this node's destination hash, hex

    /* Open a link to a destination heard in an announce (or known from a path).
     * Asks the network for a path first when none is known, and opens the link
     * as soon as one arrives. The link reports itself as a `link_established`
     * event carrying this destination. */
    bool open(const std::string& destination_hex, std::string* error = nullptr);

    /* Send one whole message on a link: a packet when it fits, a Resource when
     * it does not. False if the link is not up.
     *
     * A packet is fire-and-forget (Reticulum does not retransmit it); a Resource
     * is reliable, but a link carries one at a time, so large messages queue per
     * link, in order, and start as the link frees up. The same bytes already
     * waiting are not queued twice. False, too, when 32 are already waiting. */
    bool send(const std::string& link_hex, const std::string& bytes, std::string* error = nullptr);

    void close(const std::string& link_hex);
    bool link_up(const std::string& link_hex) const;

    /* Everything that happened since the last poll, in order. */
    void poll(std::vector<Event>& out);

    /* The largest message that goes as one packet on a link. */
    static std::size_t packet_limit();

    /* Reticulum's destination hash for a public key (64 bytes, hex) and a name,
     * computed without a network: the vector the interop test checks against
     * the Python reference. */
    static std::string destination_hash(const std::string& public_key_hex, const std::string& app_name,
                                        const std::string& aspects);

private:
    Node() = default;
};

/* ── Palabra over Reticulum ──────────────────────────────────────────────────
 * One `sync::Session` per Reticulum link. Each Palabra frame goes as ONE whole
 * message (Node::send makes it a packet or a Resource), so no stream framing is
 * needed and a lost message is a lost frame, which the session already tolerates
 * (it was built and measured under a hostile network).
 *
 *     SyncLinks sync(replica, host);
 *     for (auto& e : events) sync.handle(e, now);   // after node.poll(events)
 *     sync.tick(now);                               // every loop
 */
class SyncLinks {
public:
    SyncLinks(Replica& replica, sync::Host host, sync::Timing timing = {});

    /* A Node event: a link coming up starts a session on it, data is fed to that
     * link's session, a link going down ends its session. Other events are ignored.
     * The other way round too: a session that ends (no hello, a silent peer)
     * closes its link, so the host sees `link_closed` and can open a new one. */
    void handle(const Event& event, sync::Millis now);
    void tick(sync::Millis now);

    std::size_t sessions() const { return sessions_.size(); }
    /* True when every link's peer has acknowledged this device's current state. */
    bool in_sync() const;
    /* What the sessions reported (content arrived, presence, conflicts…), in order. */
    std::vector<sync::Event> take_events();
    /* Messages that could not be sent (the link was down). */
    std::size_t send_failures() const { return send_failures_; }

private:
    void route(const std::string& link, const sync::Step& step);
    void end_if_closed(const std::string& link, const sync::Session& session);

    Replica& replica_;
    sync::Host host_;
    sync::Timing timing_;
    std::map<std::string, std::unique_ptr<sync::Session>> sessions_;
    std::vector<sync::Event> events_;
    std::size_t send_failures_ = 0;
};

} // namespace voidpalabra::reticulum
