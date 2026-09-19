/* sync.hpp — what networking IS, with none of what networking DOES.
 *
 * ── The line this file sits on ──
 *
 * The author ruled it on 2026-09-18: Void Maiz owns what networking LOOKS like,
 * Void Palabra owns what networking IS, the host owns what is SHARED. This file is
 * the middle one, and it is sans-IO (okf/design/transport-shape.md): a `Session` is
 * a pure state machine that is handed frames and the time, and hands back frames to
 * send and events to show. It never opens a socket, never reads a clock, never
 * sleeps, never blocks. Whatever owns the network — a view library's module, an
 * application, a test harness — moves the bytes and passes the time in.
 *
 * That is also why nothing here needs the trust model to exist before it can be
 * built and measured: nothing here reaches the world. Trust gates the TRANSPORT,
 * which is the caller's. What this file does carry, as the conditions on building it
 * require, is a slot for a signature on every message and a hook to check one
 * (`Host::sign`, `Host::verify`) — present from day one, unused until trust lands.
 *
 * ── What one session is ──
 *
 * The relationship between this device and ONE peer, over whatever carries frames
 * between them. A device talking to three peers holds three sessions over one
 * replica, driven from one thread (the replica is not thread-safe).
 *
 * ── What it guarantees, as tests rather than prose ──
 *
 *   - two peers converge on each other's shareable state under a transport that
 *     drops, duplicates, reorders and partitions (tests/sync_test.cpp simulates all
 *     four, many schedules, n peers);
 *   - a rune the export set refuses never leaves this device — not its content, not
 *     its name inside a link, and not a file only it names;
 *   - every state that waits has a transition on elapsed time;
 *   - no frame, however malformed or large, is trusted with more than `Limits` allows;
 *   - presence never touches the replica: it is a separate kind of message with an
 *     opaque payload, and it cannot carry a document.
 */
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "voidpalabra/references.hpp"
#include "voidpalabra/replica.hpp"

namespace voidpalabra {
namespace sync {

using Millis = std::int64_t;

/* ── what may leave this device ───────────────────────────────────────────── */

/* Asked about every mantle (`rune_id` empty) and every rune, every time anything is
 * about to be sent. The SAME function the host's own interface asks, so that "what
 * is private" has one answer on a device rather than two that drift.
 *
 * Returning false withholds the thing whole: its content, its name inside any link,
 * and any file only it names. Two consequences that follow from the algebra, stated
 * because a user will meet them:
 *
 *   - PRIVATE IS NOT RETRACTION. Something already shared stays on the peers that
 *     received it. Marking it private stops FUTURE changes from leaving.
 *   - A REMOVAL IS NEVER PRIVATE. If something withheld is DELETED, the deletion
 *     still travels — only its presence record, never its fields — because a thing
 *     shared before it was made private must not live forever on everyone else's
 *     device. What that reveals is a random id and the fact that it was removed. */
using ExportSet = std::function<bool(const std::string& mantle, const std::string& rune_id)>;
ExportSet share_everything();

/* The shareable projection of a replica document. Withheld runes and mantles are
 * absent (or, if removed, present only as their removal); every edge whose endpoint
 * names a withheld rune is withheld too, and so is any edge naming a name that a
 * withheld rune shares with a shared one — ambiguity resolves toward not leaking. */
Doc exportable(const Doc& doc, const ExportSet& share, const JoinPolicy& policy = {});

/* ── the wire ─────────────────────────────────────────────────────────────── */

enum class Kind {
    hello,     // who I am (replica id) and which session this is
    doc,       // my shareable state, whole — payload is its JSON
    ack,       // I merged the state with this digest
    refuse,    // I will not merge the state with this digest, and why
    want,      // send me these files, by address
    content,   // one file — payload is its bytes
    absent,    // I do not have, or will not serve, these files
    presence,  // ephemeral: an opaque payload about who is here. Never versioned
    bye,       // I am leaving this session
};

struct Message {
    Kind kind = Kind::hello;
    std::string from;                    // sender's replica id
    std::string session;                 // sender's session nonce
    /* Sender-local, per kind where it matters. On a HELLO it says whether the
     * sender has heard the receiver yet: 0 = not yet, 1 = yes. A hello saying "not
     * yet" is always answered; one saying "yes" never is. Without that, losing the
     * one reply to the first hello deadlocked the handshake — one side waited for a
     * hello, the other had already answered and would not answer again, and both
     * gave up. Found by the lossy-mesh test on its first run. */
    std::uint64_t seq = 0;
    std::string digest;                  // doc / ack / refuse
    std::string reason;                  // refuse / bye
    std::vector<std::string> addresses;  // want / absent
    std::string address;                 // content
    std::string payload;                 // doc JSON, content bytes, presence bytes
    /* The signature slot. Carried on every message; produced by `Host::sign` and
     * checked by `Host::verify` when the host supplies them. Empty otherwise. */
    std::string auth;
};

struct Limits {
    std::size_t max_frame = 64u * 1024 * 1024;  // any single frame
    std::size_t max_presence = 16u * 1024;      // presence payload (Void Maiz's default)
    std::size_t max_addresses = 256;            // per want / absent
};

/* A frame is "VPS1", a 4-byte little-endian header length, the header as JSON, and
 * the payload as raw bytes. Raw because a picture carried as hex or base64 costs a
 * third to double its size for nothing a frame needs. */
std::string encode_frame(const Message& m);

/* Everything about the frame is checked against `limits` BEFORE it is believed: the
 * magic, the header length against the frame, the frame against max_frame, every
 * field's type, the kind, and the payload against what that kind may carry.
 * `with_auth_blank`, if given, receives the frame re-encoded with an empty `auth` —
 * the bytes a signature is over. */
bool decode_frame(const std::string& frame, Message& out, const Limits& limits,
            std::string* why = nullptr, std::string* with_auth_blank = nullptr);

/* ── what the host supplies ───────────────────────────────────────────────── */

enum class FetchPolicy {
    automatic,  // ask peers for every missing file as soon as it is known
    /* Ask for nothing until the host says so (`Session::fetch`). A file another
     * member added is KNOWN — the rune naming it is visible, with a placeholder — but
     * not fetched. The author asked for this setting; the host sets it, this enforces
     * it by not asking, and the view draws the placeholder from `content_state`. */
    cautious,
};

struct Host {
    ExportSet share = share_everything();
    ReferencePolicy references;  // which fields name files; empty = no file sync
    std::function<bool(const std::string& address)> have;  // do I hold it
    std::function<bool(const std::string& address, std::string& bytes)> read;  // to serve
    FetchPolicy fetch = FetchPolicy::automatic;

    /* The trust seams — hooks, not cryptography. This library stays dependency-free;
     * an application or a networking module that links libsodium (or anything else)
     * plugs it in here. `sign` receives the frame with an empty auth slot and returns
     * the auth bytes. `verify` receives the same bytes and the auth; a message it
     * rejects is dropped and reported, never acted on. */
    std::function<std::string(const std::string& frame_without_auth)> sign;
    std::function<bool(const std::string& frame_without_auth, const std::string& auth)> verify;
};

struct Timing {
    Millis resend = 2000;           // a doc not acknowledged, a hello not answered
    Millis hello_timeout = 20000;   // give up on a peer that never says hello
    Millis silence_timeout = 60000; // a peer heard from, then not at all
    Millis content_timeout = 15000; // a requested file that did not arrive
    int content_attempts = 4;       // before a file is reported unavailable from this peer
    Millis presence_ttl = 30000;    // a peer's presence, unrefreshed, expires
    Millis keepalive = 20000;       // say something at least this often, so silence means gone
};

/* ── what comes out ───────────────────────────────────────────────────────── */

struct Event {
    enum class Type {
        peer_identified,     // hello received; `peer` is its replica id
        merged,              // a peer's state was merged; see `conflicts`, `anomalies`
        merge_refused,       // a peer's state failed validation; `detail` says why
        peer_refused_state,  // the peer refused ours; `detail` is its reason
        content_arrived,     // `address`, `bytes`: verified — store them
        content_refused,     // `address`: bytes that did not match their address
        content_unavailable, // `address`: this peer does not have it or would not serve it
        presence,            // `bytes`: the peer's latest presence payload
        presence_expired,    // the peer's presence went stale
        message_refused,     // a frame failed decoding or verification; `detail`
        closed,              // the session ended; `detail` says why
    };
    Type type = Type::closed;
    std::string peer;
    std::string detail;
    std::string address;
    std::string bytes;
    std::size_t conflicts = 0;
    std::size_t anomalies = 0;

    Event() = default;
    Event(Type t, std::string p, std::string d = "", std::string a = "", std::string b = "")
        : type(t), peer(std::move(p)), detail(std::move(d)), address(std::move(a)),
          bytes(std::move(b)) {}
};

struct Step {
    std::vector<std::string> send;  // frames, in order, for this session's peer
    std::vector<Event> events;
};

enum class ContentState {
    unknown,      // not named by anything this device can see
    held,         // named, and the host has it
    deferred,     // named, missing, and cautious mode is waiting for the host
    wanted,       // named, missing, to be asked for
    requested,    // asked for; waiting
    unavailable,  // asked for, and this peer does not have it (or gave up)
};

/* ── the session ──────────────────────────────────────────────────────────── */

class Session {
public:
    Session(Replica& replica, Host host, Timing timing = {}, Limits limits = {});

    Step start(Millis now);
    Step receive(const std::string& frame, Millis now);
    /* Time passes. Also where LOCAL changes are noticed: observe the replica, then
     * tick, and a changed shareable state goes out. */
    Step tick(Millis now);
    Step publish_presence(const std::string& payload, Millis now);
    /* Cautious mode: the host decided to fetch this one. Also retries an unavailable
     * one. */
    Step fetch(const std::string& address, Millis now);
    Step close(const std::string& reason, Millis now);

    bool is_open() const { return state_ == State::open; }
    bool is_closed() const { return state_ == State::closed; }
    const std::string& peer() const { return peer_; }
    ContentState content_state(const std::string& address) const;
    /* Whether the peer has acknowledged this device's current shareable state. */
    bool in_sync() const;

private:
    enum class State { connecting, open, closed };
    struct Want {
        ContentState state = ContentState::wanted;
        Millis asked_at = 0;
        int attempts = 0;
    };

    void send(Step& s, Message m);
    void refresh_export();
    void refresh_wants(Step& s, Millis now);
    void ask(Step& s, Millis now);
    void on_message(Step& s, const Message& m, Millis now);
    void finish(Step& s, const std::string& why, bool say_bye);
    bool serveable(const std::string& address);

    Replica& replica_;
    Host host_;
    Timing timing_;
    Limits limits_;

    State state_ = State::connecting;
    std::string nonce_;
    std::string peer_;
    std::string peer_session_;
    Millis started_ = 0, last_heard_ = 0, last_said_ = 0;

    std::uint64_t seen_revision_ = UINT64_MAX;
    std::string export_json_, export_digest_;
    std::set<std::string> serveable_;

    std::string acked_;           // digest the peer acknowledged
    std::string refused_;         // digest the peer refused; not resent until it changes
    std::string sent_;            // digest most recently sent
    Millis doc_sent_at_ = -1;
    std::string last_merged_;     // digest of the peer's state last merged

    std::map<std::string, Want> wants_;
    std::uint64_t presence_out_ = 0, presence_in_ = 0;
    Millis presence_seen_ = -1;
    bool presence_live_ = false;
};

}  // namespace sync
}  // namespace voidpalabra
