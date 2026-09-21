/* session.cpp — see include/voidpalabra/sync.hpp.
 *
 * The protocol, in full:
 *
 *   hello   — both sides say who they are, and repeat it as a keepalive. A hello
 *             with a new session nonce means the peer restarted: forget what it had
 *             acknowledged and send again.
 *   doc     — whole shareable state, sent when it differs from what the peer last
 *             acknowledged, and re-sent on a timer until acknowledged.
 *   ack / refuse — the peer merged that digest, or will not, and why.
 *   want / content / absent — files by address; only what was asked for is accepted,
 *             and only after its bytes match its address.
 *   presence — opaque bytes, newest by sender sequence, expiring on the receiver's
 *             clock. It never touches the replica.
 *   bye     — done.
 *
 * Why whole-state anti-entropy rather than deltas, for now: it is the version whose
 * correctness under a hostile transport needs no argument. A lost, duplicated or
 * reordered `doc` is harmless — the join is idempotent, commutative and associative,
 * and the timer re-sends until an ack arrives. Deltas and range reconciliation cut the
 * BYTES; they add nothing to what converges, and they are an optimization to measure
 * against this rather than a place to start.
 */
#include "voidpalabra/sync.hpp"

#include "voidpalabra/canonical.hpp"
#include "cJSON.h"

#include <algorithm>
#include <string>

namespace voidpalabra {
namespace sync {

Session::Session(Replica& replica, Host host, Timing timing, Limits limits)
    : replica_(replica), host_(std::move(host)), timing_(timing), limits_(limits) {}

/* ── sending ─────────────────────────────────────────────────────────────── */

void Session::send(Step& s, Message m) {
    m.from = replica_.id();
    m.session = nonce_;
    if (m.kind == Kind::hello) m.seq = peer_.empty() ? 0 : 1;
    if (host_.sign) {
        m.auth.clear();
        m.auth = host_.sign(encode_frame(m));
    }
    s.send.push_back(encode_frame(m));
}

void Session::finish(Step& s, const std::string& why, bool say_bye) {
    if (state_ == State::closed) return;
    if (say_bye) {
        Message m;
        m.kind = Kind::bye;
        m.reason = why;
        send(s, m);
    }
    state_ = State::closed;
    s.events.push_back(Event(Event::Type::closed, peer_, why));
}

/* ── derived state, recomputed only when the replica moves ───────────────── */

void Session::refresh_export() {
    if (seen_revision_ == replica_.revision()) return;
    seen_revision_ = replica_.revision();
    Doc exp = exportable(replica_.doc(), host_.share, replica_.policy());
    char* text = cJSON_PrintUnformatted(exp.root);
    export_json_ = text ? text : "";
    if (text) cJSON_free(text);
    export_digest_ = to_hex(sha256(canon_doc(exp)));

    /* What may be served is exactly what the SHAREABLE version names. A file only a
     * private rune names is not served — its address alone would answer "does this
     * device hold that picture?", which is a leak the export set exists to prevent. */
    serveable_.clear();
    if (!host_.references.fields.empty()) {
        Doc flat = flatten(exp, replica_.policy());
        for (const Reference& r : references(flat.root, host_.references))
            serveable_.insert(r.address);
    }
}

bool Session::serveable(const std::string& address) {
    refresh_export();
    return serveable_.count(address) > 0;
}

void Session::refresh_wants(Step& s, Millis now) {
    if (host_.references.fields.empty()) return;
    Doc flat = replica_.flatten();
    std::set<std::string> named;
    for (const Reference& r : references(flat.root, host_.references)) named.insert(r.address);

    for (auto it = wants_.begin(); it != wants_.end();) {
        bool gone = !named.count(it->first);
        bool arrived = host_.have && host_.have(it->first);
        it = (gone || arrived) ? wants_.erase(it) : std::next(it);
    }
    for (const auto& a : named) {
        if (wants_.count(a) || (host_.have && host_.have(a))) continue;
        Want w;
        w.state = host_.fetch == FetchPolicy::automatic ? ContentState::wanted : ContentState::deferred;
        wants_[a] = w;
    }
    ask(s, now);
}

void Session::ask(Step& s, Millis now) {
    if (state_ != State::open) return;
    Message m;
    m.kind = Kind::want;
    for (auto& [address, w] : wants_) {
        if (w.state != ContentState::wanted) continue;
        if (m.addresses.size() == limits_.max_addresses) {
            send(s, m);
            m.addresses.clear();
        }
        m.addresses.push_back(address);
        w.state = ContentState::requested;
        w.asked_at = now;
        ++w.attempts;
    }
    if (!m.addresses.empty()) send(s, m);
}

/* ── the three ways time and bytes arrive ────────────────────────────────── */

Step Session::start(Millis now) {
    Step s;
    nonce_ = replica_.id() + "@" + std::to_string(now);
    started_ = last_heard_ = now;
    state_ = State::connecting;
    Message m;
    m.kind = Kind::hello;
    send(s, m);
    last_said_ = now;
    return s;
}

Step Session::tick(Millis now) {
    Step s;
    if (state_ == State::closed) return s;

    if (state_ == State::connecting) {
        if (now - started_ >= timing_.hello_timeout) {
            finish(s, "the peer never said hello", false);
            return s;
        }
        if (now - last_said_ >= timing_.resend) {
            Message m;
            m.kind = Kind::hello;
            send(s, m);
            last_said_ = now;
        }
        return s;
    }

    if (now - last_heard_ >= timing_.silence_timeout) {
        finish(s, "the peer went silent", false);
        return s;
    }

    /* Inside the coalescing window the export is not even recomputed: computing it
     * is O(document), and a host changing its document every frame would otherwise
     * pay that every frame for states that will never be sent. */
    bool settling = timing_.coalesce > 0 && doc_sent_at_ >= 0 &&
                    now - doc_sent_at_ < timing_.coalesce;
    if (!settling) {
        std::uint64_t before = seen_revision_;
        refresh_export();
        if (seen_revision_ != before) refresh_wants(s, now);
    }

    /* A NEW state goes out at once (or at the end of the coalescing window); an
     * unacknowledged one is repeated on the timer. Waiting out the timer for a
     * change the peer has never seen would make every edit feel two seconds late
     * for no gain. */
    /* A state identical to the one the peer just sent us is one the peer holds: it
     * sent it. Echoing it back would double the traffic of every one-sided burst —
     * the burst test measured exactly that, 42 states for 21 changes. */
    if (!settling && !export_digest_.empty() && export_digest_ == last_merged_)
        acked_ = export_digest_;
    bool changed = export_digest_ != sent_;
    if (!settling && export_digest_ != acked_ && export_digest_ != refused_ &&
        (changed || now - doc_sent_at_ >= timing_.resend)) {
        Message m;
        m.kind = Kind::doc;
        m.digest = export_digest_;
        m.payload = export_json_;
        send(s, m);
        sent_ = export_digest_;
        doc_sent_at_ = now;
        last_said_ = now;
    }

    for (auto& [address, w] : wants_) {
        if (w.state != ContentState::requested || now - w.asked_at < timing_.content_timeout) continue;
        if (w.attempts >= timing_.content_attempts) {
            w.state = ContentState::unavailable;
            s.events.push_back(Event(Event::Type::content_unavailable, peer_, "no answer", address));
        } else {
            w.state = ContentState::wanted;
        }
    }
    ask(s, now);

    if (presence_live_ && now - presence_seen_ >= timing_.presence_ttl) {
        presence_live_ = false;
        s.events.push_back(Event(Event::Type::presence_expired, peer_));
    }

    if (!s.send.empty()) last_said_ = now;
    if (now - last_said_ >= timing_.keepalive) {
        Message m;
        m.kind = Kind::hello;
        send(s, m);
        last_said_ = now;
    }
    return s;
}

Step Session::receive(const std::string& frame, Millis now) {
    Step s;
    if (state_ == State::closed) return s;
    Message m;
    std::string why, blank;
    if (!decode_frame(frame, m, limits_, &why, host_.verify ? &blank : nullptr)) {
        s.events.push_back(Event(Event::Type::message_refused, peer_, why));
        return s;
    }
    if (host_.verify && !host_.verify(blank, m.auth)) {
        s.events.push_back(Event(Event::Type::message_refused, peer_, "signature did not verify"));
        return s;
    }
    on_message(s, m, now);
    if (!s.send.empty()) last_said_ = now;
    return s;
}

void Session::on_message(Step& s, const Message& m, Millis now) {
    if (m.kind == Kind::hello) {
        if (m.from == replica_.id()) {
            /* Not a transport error: a second device holds this replica's identity —
             * copied, cloned or restored — and every tag either mints may already
             * mean two things. Nothing is exchanged. */
            finish(s, "the peer is using this replica's id; one of the two must fork", true);
            return;
        }
        if (!peer_.empty() && m.from != peer_) {
            s.events.push_back(Event(Event::Type::message_refused, peer_, "a different replica on this session"));
            return;
        }
        last_heard_ = now;
        bool first = peer_.empty();
        bool restarted = m.session != peer_session_;
        if (first) {
            peer_ = m.from;
            s.events.push_back(Event(Event::Type::peer_identified, peer_));
        }
        if (restarted) {
            peer_session_ = m.session;
            acked_.clear();
            refused_.clear();
            sent_.clear();
            doc_sent_at_ = -1;
            last_merged_.clear();
        }
        bool opening = state_ == State::connecting || restarted;
        if (opening) state_ = State::open;
        /* Answer a peer that has not heard us yet — every time it asks, not only the
         * first time, because the first answer may be the one the network lost. */
        if (m.seq == 0 || opening) {
            Message back;
            back.kind = Kind::hello;
            send(s, back);
        }
        if (opening) {
            Step more = tick(now);  // send our state now rather than on the next tick
            s.send.insert(s.send.end(), more.send.begin(), more.send.end());
            s.events.insert(s.events.end(), more.events.begin(), more.events.end());
        }
        return;
    }

    /* Nothing but hello is heard from a peer that has not said who it is. */
    if (peer_.empty() || m.from != peer_) return;
    last_heard_ = now;

    switch (m.kind) {
        case Kind::doc: {
            if (!m.digest.empty() && m.digest == last_merged_) {
                Message a;
                a.kind = Kind::ack;
                a.digest = m.digest;
                send(s, a);
                return;
            }
            Doc incoming;
            incoming.root = cJSON_ParseWithLength(m.payload.data(), m.payload.size());
            std::string why;
            MergeResult r = MergeResult::invalid;
            std::string digest;
            if (!incoming.root) {
                why = "the state is not JSON";
            } else if (!validate(incoming, &why)) {
                why = "the state failed validation: " + why;
            } else {
                digest = to_hex(sha256(canon_doc(incoming)));
                if (digest != m.digest) why = "the state does not match its digest";
                else r = replica_.merge(incoming, &why);
            }
            if (r == MergeResult::ok) {
                last_merged_ = digest;
                Message a;
                a.kind = Kind::ack;
                a.digest = m.digest;
                send(s, a);
                Event e(Event::Type::merged, peer_);
                e.conflicts = replica_.conflicts().size();
                e.anomalies = replica_.anomalies().size();
                s.events.push_back(e);
                refresh_wants(s, now);
            } else {
                Message f;
                f.kind = Kind::refuse;
                f.digest = m.digest;
                f.reason = why.substr(0, 1000);
                send(s, f);
                s.events.push_back(Event(Event::Type::merge_refused, peer_, why));
                if (r == MergeResult::identity_collision)
                    finish(s, "identity collision: " + why, true);
            }
            return;
        }
        case Kind::ack:
            refresh_export();
            if (m.digest == export_digest_) acked_ = m.digest;
            return;
        case Kind::refuse:
            refused_ = m.digest;
            s.events.push_back(Event(Event::Type::peer_refused_state, peer_, m.reason));
            return;
        case Kind::want: {
            Message none;
            none.kind = Kind::absent;
            for (const auto& a : m.addresses) {
                std::string bytes;
                if (serveable(a) && host_.read && host_.read(a, bytes)) {
                    Message c;
                    c.kind = Kind::content;
                    c.address = a;
                    c.payload = std::move(bytes);
                    send(s, c);
                } else {
                    none.addresses.push_back(a);
                }
            }
            if (!none.addresses.empty()) send(s, none);
            return;
        }
        case Kind::content: {
            auto it = wants_.find(m.address);
            /* Only what was asked for. A peer pushing files nobody requested is using
             * this device's storage without asking; it is refused, not stored. */
            if (it == wants_.end() || it->second.state != ContentState::requested) {
                s.events.push_back(Event(Event::Type::message_refused, peer_, "a file nobody asked for", m.address));
                return;
            }
            if (!content_matches(m.address, m.payload)) {
                it->second.state = ContentState::wanted;
                s.events.push_back(Event(Event::Type::content_refused, peer_,
                                    "bytes did not match their address", m.address));
                return;
            }
            wants_.erase(it);
            s.events.push_back(Event(Event::Type::content_arrived, peer_, "", m.address, m.payload));
            return;
        }
        case Kind::absent:
            for (const auto& a : m.addresses) {
                auto it = wants_.find(a);
                if (it == wants_.end() || it->second.state != ContentState::requested) continue;
                it->second.state = ContentState::unavailable;
                s.events.push_back(Event(Event::Type::content_unavailable, peer_, "the peer does not serve it", a));
            }
            return;
        case Kind::presence:
            /* Newest by the sender's own sequence — not by arrival, not by a clock.
             * A duplicate or a late, older one is dropped. */
            if (m.seq <= presence_in_) return;
            presence_in_ = m.seq;
            presence_seen_ = now;
            presence_live_ = true;
            s.events.push_back(Event(Event::Type::presence, peer_, "", "", m.payload));
            return;
        case Kind::bye:
            finish(s, m.reason.empty() ? "the peer said goodbye" : "the peer said goodbye: " + m.reason, false);
            return;
        case Kind::hello:
            return;
    }
}

/* ── host-initiated ──────────────────────────────────────────────────────── */

Step Session::publish_presence(const std::string& payload, Millis now) {
    Step s;
    if (state_ != State::open) return s;
    if (payload.size() > limits_.max_presence) {
        s.events.push_back(Event(Event::Type::message_refused, peer_, "presence larger than max_presence"));
        return s;
    }
    Message m;
    m.kind = Kind::presence;
    m.seq = ++presence_out_;
    m.payload = payload;
    send(s, m);
    last_said_ = now;
    return s;
}

Step Session::fetch(const std::string& address, Millis now) {
    Step s;
    auto it = wants_.find(address);
    if (it == wants_.end()) return s;
    if (it->second.state == ContentState::deferred || it->second.state == ContentState::unavailable) {
        it->second.state = ContentState::wanted;
        it->second.attempts = 0;
    }
    ask(s, now);
    if (!s.send.empty()) last_said_ = now;
    return s;
}

Step Session::set_fetch(FetchPolicy policy, Millis now) {
    Step s;
    host_.fetch = policy;
    for (auto& [address, w] : wants_) {
        if (policy == FetchPolicy::automatic && w.state == ContentState::deferred) {
            w.state = ContentState::wanted;
            w.attempts = 0;
        } else if (policy == FetchPolicy::cautious && w.state == ContentState::wanted) {
            w.state = ContentState::deferred;
        }
    }
    ask(s, now);
    if (!s.send.empty()) last_said_ = now;
    return s;
}

Step Session::close(const std::string& reason, Millis now) {
    Step s;
    (void)now;
    finish(s, reason, true);
    return s;
}

ContentState Session::content_state(const std::string& address) const {
    auto it = wants_.find(address);
    if (it != wants_.end()) return it->second.state;
    if (host_.have && host_.have(address)) return ContentState::held;
    return ContentState::unknown;
}

bool Session::in_sync() const {
    return state_ == State::open && !export_digest_.empty() && acked_ == export_digest_ &&
           seen_revision_ == replica_.revision();
}

}  // namespace sync
}  // namespace voidpalabra
