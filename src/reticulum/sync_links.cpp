/* src/reticulum/sync_links.cpp — Palabra sessions over Reticulum links
 * (voidpalabra/reticulum.hpp, SyncLinks). No microReticulum type appears here:
 * this file speaks only to Node's public API, which is the point of it. */
#include "voidpalabra/reticulum.hpp"

namespace voidpalabra::reticulum {

SyncLinks::SyncLinks(Replica& replica, sync::Host host, sync::Timing timing)
    : replica_(replica), host_(std::move(host)), timing_(timing) {}

void SyncLinks::route(const std::string& link, const sync::Step& step) {
    for (const sync::Event& e : step.events) events_.push_back(e);
    for (const std::string& frame : step.send)
        if (!Node::instance().send(link, frame)) ++send_failures_;
}

void SyncLinks::handle(const Event& event, sync::Millis now) {
    switch (event.type) {
    case Event::Type::link_established: {
        auto& slot = sessions_[event.link];
        slot = std::make_unique<sync::Session>(replica_, host_, timing_);
        route(event.link, slot->start(now));
        break;
    }
    case Event::Type::data: {
        auto it = sessions_.find(event.link);
        if (it != sessions_.end()) {
            route(event.link, it->second->receive(event.bytes, now));
            end_if_closed(event.link, *it->second);
        }
        break;
    }
    case Event::Type::link_closed:
        sessions_.erase(event.link);
        break;
    default:
        break;
    }
}

void SyncLinks::tick(sync::Millis now) {
    for (auto& [link, session] : sessions_) {
        route(link, session->tick(now));
        end_if_closed(link, *session);
    }
}

/* A session that ended (the peer never said hello, went silent, or closed)
 * leaves a link that carries nothing, so the link is closed too, and the host
 * sees `link_closed` and may open a new one. This is what un-sticks a HALF-OPEN
 * link: Reticulum's handshake ends with one RTT packet from the initiator, and
 * if it is lost the initiator's link is up while the other side's never
 * activates (and, by the reference's own rule, waits 6 minutes before giving
 * up). Keepalives still get answered, so no link watchdog can see it. The sync
 * session can: no hello within Timing::hello_timeout. Found 2026-09-24 behind a
 * lossy relay. The session erases itself when `link_closed` comes back. */
void SyncLinks::end_if_closed(const std::string& link, const sync::Session& session) {
    if (session.is_closed() && Node::instance().link_up(link)) Node::instance().close(link);
}

bool SyncLinks::in_sync() const {
    if (sessions_.empty()) return false;
    for (const auto& [link, session] : sessions_)
        if (!session->in_sync()) return false;
    return true;
}

std::vector<sync::Event> SyncLinks::take_events() {
    std::vector<sync::Event> out;
    out.swap(events_);
    return out;
}

} // namespace voidpalabra::reticulum
