/* tests/reticulum/peer.cpp — one Reticulum node, driven by tests/reticulum/interop.py.
 *
 * Reticulum's transport is process-wide (okf/concepts/reticulum.md), so every
 * test of two peers is two processes, and the harness is a Python script that
 * can also run the official Python Reticulum as the other peer. This program is
 * the C++ side of every one of those tests. It prints lines the harness reads:
 *
 *   PUB <hex> / IDENT <hex> / DEST <hex>   who this node is
 *   ANNOUNCE <dest> <identity> <app data>  heard a peer
 *   LINK <link> <dest>                     a link came up
 *   IDENTIFIED <link> <identity>           the peer proved who it is
 *   VERSION <name>                         (sync roles) what this device holds
 *   RESULT ok | RESULT fail <why>          the verdict
 *
 * Roles:
 *   vector        start, print PUB/IDENT/DEST/ENTROPY, exit. The harness checks DEST
 *                 against the Python reference's hash of the same public key.
 *   client        wait for an announce, open a link, send a small message (one
 *                 packet) and a large one (a Resource), and require both echoed
 *                 back byte for byte.
 *   echo          announce every second and echo every message on every link.
 *   sync-founder  a Palabra replica holding two runes; serves sync on its links.
 *   sync-joiner   an empty replica; links to the founder, converges, then adds a
 *                 rune of its own and waits for it to be acknowledged.
 *
 *   peer <role> --dir D --listen P --forward Q [--seconds N] [--large BYTES] [--lossy 1] [--log 0-5]
 *
 * A PIPE instead of UDP (2026-10-05): `--pipe-listen PORT` or `--pipe-connect
 * PORT` carries the node's packets over a local TCP stream, HDLC-framed, as a
 * host carries them over Bluetooth; `--bitrate BPS` throttles what is written,
 * so a test sees a Bluetooth-slow link; the client then prints TRANSFER lines
 * (Node::transfers) while its large message moves.
 */
#include "voidpalabra/reticulum.hpp"
#include "voidpalabra/canonical.hpp"
#include "voidpalabra/replica.hpp"

#include "cJSON.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <thread>
#include <vector>

using namespace voidpalabra;
using namespace voidpalabra::reticulum;

namespace {

using Clock = std::chrono::steady_clock;
long long ms_since(Clock::time_point t0) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t0).count();
}

void say(const std::string& line) {
    std::printf("%s\n", line.c_str());
    std::fflush(stdout);
}

int fail(const std::string& why) {
    say("RESULT fail " + why);
    return 1;
}

std::string arg(int argc, char** argv, const char* name, const char* fallback = "") {
    for (int i = 2; i + 1 < argc; ++i)
        if (std::strcmp(argv[i], name) == 0) return argv[i + 1];
    return fallback;
}

/* The large message: bigger than any packet, with content that makes a
 * truncated or reordered transfer visible (every byte depends on its offset). */
std::string large_message(std::size_t n) {
    std::string s(n, '\0');
    for (std::size_t i = 0; i < n; ++i) s[i] = (char)('a' + (i * 7 + i / 251) % 26);
    return s;
}

std::string random_id() {
    std::random_device rd;
    std::mt19937_64 g(((std::uint64_t)rd() << 32) ^ rd());
    const char* a = "abcdefghijklmnopqrstuvwxyz0123456789";
    std::string s;
    for (int i = 0; i < 24; ++i) s += a[g() % 36];
    return s;
}

std::string doc_with(const std::vector<std::pair<std::string, std::string>>& runes) {
    std::string rs;
    for (const auto& [id, name] : runes)
        rs += (rs.empty() ? "" : ",") + std::string("{\"spirit\":{\"id\":\"") + id + "\",\"name\":\"" + name +
              "\"},\"glyph\":\"text\",\"content\":{\"body\":\"" + name + "\"}}";
    return "{\"mantles\":[{\"id\":\"m\",\"name\":\"notes\",\"runes\":[" + rs + "],\"layout\":{\"edges\":[]}}]}";
}

bool observe(Replica& r, const std::string& text) {
    cJSON* s = cJSON_Parse(text.c_str());
    if (!s) return false;
    Replica::Observed o = r.observe(s);
    cJSON_Delete(s);
    return o.ok;
}

std::string version_of(const Replica& r) {
    Doc f = r.flatten();
    return version_name(f.root);
}

std::size_t rune_count(const Replica& r) {
    Doc f = r.flatten();
    cJSON* mantles = cJSON_GetObjectItem(f.root, "mantles");
    std::size_t n = 0;
    for (cJSON* m = mantles ? mantles->child : nullptr; m; m = m->next) {
        cJSON* runes = cJSON_GetObjectItem(m, "runes");
        n += runes ? (std::size_t)cJSON_GetArraySize(runes) : 0;
    }
    return n;
}

} // namespace

/* A pipe carried over local TCP: the stand-in for a radio (see the header). */
struct TcpPipe {
    std::intptr_t sock = -1, listener = -1;
    Hdlc::Decoder decoder;
    std::string out;          // framed bytes waiting to be written
    double bitrate = 0;       // bits per second; 0 = unthrottled
    double allowance = 0;     // bytes we may write now
    Clock::time_point last = Clock::now();

    static void nonblock(std::intptr_t s) {
#ifdef _WIN32
        u_long on = 1;
        ioctlsocket((SOCKET)s, FIONBIO, &on);
#else
        fcntl((int)s, F_SETFL, fcntl((int)s, F_GETFL) | O_NONBLOCK);
#endif
    }
    bool listen_on(int port) {
        listener = (std::intptr_t)::socket(AF_INET, SOCK_STREAM, 0);
        int yes = 1;
        setsockopt((decltype(::socket(0, 0, 0)))listener, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof yes);
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        a.sin_port = htons((unsigned short)port);
        if (::bind((decltype(::socket(0, 0, 0)))listener, (sockaddr*)&a, sizeof a) != 0) return false;
        ::listen((decltype(::socket(0, 0, 0)))listener, 1);
        nonblock(listener);
        return true;
    }
    bool connect_to(int port) {
        sock = (std::intptr_t)::socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        a.sin_port = htons((unsigned short)port);
        for (int i = 0; i < 100; ++i) { // the other side may not be listening yet
            if (::connect((decltype(::socket(0, 0, 0)))sock, (sockaddr*)&a, sizeof a) == 0) {
                nonblock(sock);
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        return false;
    }
    void pump(Node& node, const std::string& name) {
        if (sock < 0 && listener >= 0) {
            const auto s = ::accept((decltype(::socket(0, 0, 0)))listener, nullptr, nullptr);
#ifdef _WIN32
            if (s != INVALID_SOCKET) { sock = (std::intptr_t)s; nonblock(sock); }
#else
            if (s >= 0) { sock = (std::intptr_t)s; nonblock(sock); }
#endif
        }
        for (auto& p : node.pipe_out(name)) out += Hdlc::frame(p);
        if (sock < 0) return;
        const auto now = Clock::now();
        const double dt = std::chrono::duration<double>(now - last).count();
        last = now;
        std::size_t can = out.size();
        if (bitrate > 0) {
            allowance = std::min(allowance + dt * bitrate / 8.0, bitrate / 8.0); // at most a second banked
            can = std::min(can, (std::size_t)allowance);
        }
        if (can > 0) {
            const int n = (int)::send((decltype(::socket(0, 0, 0)))sock, out.data(), (int)can, 0);
            if (n > 0) {
                out.erase(0, (std::size_t)n);
                allowance -= n;
            }
        }
        char buf[4096];
        for (;;) {
            const int n = (int)::recv((decltype(::socket(0, 0, 0)))sock, buf, sizeof buf, 0);
            if (n <= 0) break;
            for (auto& packet : decoder.feed(std::string(buf, (std::size_t)n))) node.pipe_in(name, packet);
        }
    }
};

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: peer <vector|client|echo|sync-founder|sync-joiner> --dir D --listen P --forward Q [--seconds N]\n");
        return 2;
    }
    const std::string role = argv[1];
    Options o;
    o.storage_dir = arg(argc, argv, "--dir");
    o.announce_data = role;
    o.log_level = std::atoi(arg(argc, argv, "--log", "1").c_str());
    UdpInterface u;
    u.listen_host = "127.0.0.1";
    u.listen_port = (std::uint16_t)std::atoi(arg(argc, argv, "--listen", "0").c_str());
    u.forward_host = "127.0.0.1";
    u.forward_port = (std::uint16_t)std::atoi(arg(argc, argv, "--forward", "0").c_str());
    const int pipe_listen = std::atoi(arg(argc, argv, "--pipe-listen", "0").c_str());
    const int pipe_connect = std::atoi(arg(argc, argv, "--pipe-connect", "0").c_str());
    const bool piped = pipe_listen || pipe_connect;
    if (role != "vector" && !piped) o.udp.push_back(u);
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
    const long long budget_ms = 1000LL * std::atoi(arg(argc, argv, "--seconds", "30").c_str());

    Node& node = Node::instance();
    std::string why;
    if (!node.start(o, &why)) return fail("start: " + why);
    say("PUB " + node.public_key());
    say("IDENT " + node.identity());
    say("DEST " + node.destination());
    // which OS generator seeded Reticulum's; empty means start() should have refused
    if (Node::entropy_source().empty()) return fail("no operating-system entropy source");
    say("ENTROPY " + Node::entropy_source());
    if (node.destination() != Node::destination_hash(node.public_key(), o.app_name, o.aspects))
        return fail("our own destination hash disagrees with destination_hash()");
    if (role == "vector") {
        say("RESULT ok");
        return 0;
    }
    TcpPipe pipe;
    pipe.bitrate = std::atof(arg(argc, argv, "--bitrate", "0").c_str());
    if (piped) {
        PipeInterface pi;
        pi.name = "radio";
        if (pipe.bitrate > 0) pi.bitrate = (std::uint32_t)pipe.bitrate;
        if (!node.add_pipe(pi, &why)) return fail("add_pipe: " + why);
        if (pipe_listen && !pipe.listen_on(pipe_listen)) return fail("pipe: could not listen");
        if (pipe_connect && !pipe.connect_to(pipe_connect)) return fail("pipe: could not connect");
    }
    long long next_transfer_say = 0;
    auto loop_all = [&] {
        node.loop();
        if (piped) pipe.pump(node, "radio");
    };

    // ── sync roles: a replica, sessions per link ─────────────────────────────
    Replica replica;
    std::unique_ptr<SyncLinks> sync;
    const bool founder = role == "sync-founder", joiner = role == "sync-joiner";
    if (founder || joiner) {
        if (!Replica::create(random_id(), replica, &why)) return fail("replica: " + why);
        if (founder && !observe(replica, doc_with({{"rune-a", "from-founder-a"}, {"rune-b", "from-founder-b"}})))
            return fail("founder observe");
        if (joiner && !observe(replica, doc_with({}))) return fail("joiner observe");
        sync = std::make_unique<SyncLinks>(replica, sync::Host{});
    }

    const auto t0 = Clock::now();
    long long next_announce = 0;
    std::string link;          // the client's / joiner's link
    bool asked = false, got_small = false, got_large = false, added = false;
    long long added_at = 0;
    const std::string small = "small message: one packet";
    // --lossy: a lone packet is fire-and-forget in Reticulum (no retransmission),
    // so over a lossy path only the Resource, which is reliable, is required back
    const bool lossy = arg(argc, argv, "--lossy", "0") == std::string("1");
    const std::string large = large_message((std::size_t)std::atoll(arg(argc, argv, "--large", "9000").c_str()));
    int echoed = 0, reopened = 0;

    while (ms_since(t0) < budget_ms) {
        loop_all();
        const long long now = ms_since(t0);
        if (role == "client" && now >= next_transfer_say) {
            for (const auto& t : node.transfers())
                say(std::string("TRANSFER ") + (t.incoming ? "in " : "out ") + std::to_string(t.done) + "/" + std::to_string(t.total));
            next_transfer_say = now + 500;
        }
        if (now >= next_announce && (role == "echo" || founder)) {
            node.announce();
            next_announce = now + 1000;
        }
        std::vector<Event> events;
        node.poll(events);
        for (const Event& e : events) {
            switch (e.type) {
            case Event::Type::announce:
                say("ANNOUNCE " + e.destination + " " + e.identity + " " + e.bytes);
                if ((role == "client" || joiner) && !asked) {
                    if (!node.open(e.destination, &why)) return fail("open: " + why);
                    asked = true;
                }
                break;
            case Event::Type::link_established:
                say("LINK " + e.link + " " + e.destination);
                if (role == "client" || joiner) link = e.link;
                if (role == "client") {
                    if (!node.send(e.link, small, &why)) return fail("send small: " + why);
                    if (!node.send(e.link, large, &why)) return fail("send large: " + why);
                }
                break;
            case Event::Type::peer_identified:
                say("IDENTIFIED " + e.link + " " + e.identity);
                break;
            case Event::Type::link_closed:
                say("CLOSED " + e.link + " " + e.detail);
                // what a host does: a link it wanted is gone (lost request, dead peer),
                // so the next announce opens a new one
                if ((role == "client" || joiner) && (link.empty() || link == e.link)) {
                    asked = false;
                    link.clear();
                    ++reopened;
                }
                break;
            case Event::Type::data:
                if (role == "echo") {
                    node.send(e.link, e.bytes);
                    say("ECHOED " + std::to_string(e.bytes.size()));
                    ++echoed;
                }
                if (role == "client") {
                    if (e.bytes == small) got_small = true;
                    else if (e.bytes == large) got_large = true;
                    else return fail("an echo came back different (" + std::to_string(e.bytes.size()) + " bytes)");
                }
                break;
            case Event::Type::error:
                say("ERROR " + e.detail);
                break;
            }
            if (sync) sync->handle(e, now);
        }
        if (sync) sync->tick(now);

        if (role == "client" && (got_small || lossy) && got_large) {
            if (reopened) say("REOPENED " + std::to_string(reopened));
            say("RESULT ok");
            return 0;
        }
        if (joiner && !added && rune_count(replica) == 2 && sync->in_sync()) {
            // converged on the founder's two runes: now a change of our own travels back
            if (!observe(replica, doc_with({{"rune-a", "from-founder-a"},
                                            {"rune-b", "from-founder-b"},
                                            {"rune-j", "from-joiner"}})))
                return fail("joiner observe (add)");
            added = true;
            added_at = now;
        }
        if (joiner && added && now - added_at > 300 && sync->in_sync()) {
            say("VERSION " + version_of(replica));
            say("RUNES " + std::to_string(rune_count(replica)));
            say("RESULT ok");
            // stay a little so the founder's acknowledgement is not cut short
            const auto t1 = Clock::now();
            while (ms_since(t1) < 1500) {
                loop_all();
                std::vector<Event> ev;
                node.poll(ev);
                for (const Event& e : ev) sync->handle(e, ms_since(t0));
                sync->tick(ms_since(t0));
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            node.close(link);  // goodbye: the founder stops serving when it hears it
            node.loop();
            return 0;
        }
        if (founder && rune_count(replica) == 3 && sync->in_sync()) {
            say("VERSION " + version_of(replica));
            say("RUNES " + std::to_string(rune_count(replica)));
            say("RESULT ok");
            // keep serving (and announcing) until the joiner says goodbye or 15 s
            // pass: our acknowledgement of its rune may still be on its way, and a
            // founder that vanishes the moment IT is satisfied strands the joiner
            const auto t1 = Clock::now();
            bool left = false;
            long long next = 0;
            while (!left && ms_since(t1) < 15000) {
                loop_all();
                if (ms_since(t1) >= next) {
                    node.announce();
                    next = ms_since(t1) + 1000;
                }
                std::vector<Event> ev;
                node.poll(ev);
                for (const Event& e : ev) {
                    sync->handle(e, ms_since(t0));
                    if (e.type == Event::Type::link_closed && e.detail == "closed by the peer") left = true;
                }
                sync->tick(ms_since(t0));
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            return 0;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    if (role == "echo") {
        say(echoed > 0 ? "RESULT ok" : "RESULT fail nothing arrived to echo");
        return echoed > 0 ? 0 : 1;
    }
    return fail(std::string("timed out (") + (asked ? "link asked" : "no announce heard") +
                (link.empty() ? ", no link" : ", link up") + (got_small ? ", small back" : "") +
                (got_large ? ", large back" : "") + ")");
}
