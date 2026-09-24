/* src/reticulum/udp.cpp — see udp.hpp. */
#include "udp.hpp"

#include <microReticulum/Log.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using socklen_t = int;
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <microReticulum/Utilities/OS.h>

#include <algorithm>
#include <cstring>

namespace voidpalabra::reticulum::detail {

namespace {

constexpr std::intptr_t kNoSocket = -1;
constexpr double kHeardFor = 60;       // seconds a peer stays learned without a word
constexpr std::size_t kHeardMax = 64;  // a LAN, not the internet

#ifdef _WIN32
bool winsock_up() {
    static bool ok = [] {
        WSADATA w;
        return WSAStartup(MAKEWORD(2, 2), &w) == 0;
    }();
    return ok;
}
void close_socket(std::intptr_t s) { ::closesocket((SOCKET)s); }
bool set_nonblocking(std::intptr_t s) {
    u_long on = 1;
    return ::ioctlsocket((SOCKET)s, FIONBIO, &on) == 0;
}
#else
bool winsock_up() { return true; }
void close_socket(std::intptr_t s) { ::close((int)s); }
bool set_nonblocking(std::intptr_t s) {
    int flags = ::fcntl((int)s, F_GETFL, 0);
    return flags >= 0 && ::fcntl((int)s, F_SETFL, flags | O_NONBLOCK) == 0;
}
#endif

bool parse_ipv4(const std::string& host, std::uint32_t& out) {
    in_addr a{};
    if (::inet_pton(AF_INET, host.c_str(), &a) != 1) return false;
    out = a.s_addr;
    return true;
}

} // namespace

UdpInterfaceImpl::UdpInterfaceImpl(const std::string& name, const std::string& listen_host,
                                   std::uint16_t listen_port, const std::string& forward_host,
                                   std::uint16_t forward_port, bool broadcast, bool learn_peers)
    : RNS::InterfaceImpl(name.c_str()), listen_host_(listen_host), forward_host_(forward_host),
      listen_port_(listen_port), forward_port_(forward_port), broadcast_(broadcast),
      learn_peers_(learn_peers) {
    _IN = true;
    _OUT = true;
    _bitrate = 10 * 1000 * 1000;  // a guess, as microReticulum's example makes; LAN-class
    _HW_MTU = 1064;
}

UdpInterfaceImpl::~UdpInterfaceImpl() { stop(); }

std::string UdpInterfaceImpl::toString() const {
    return "UdpInterface[" + _name + " " + listen_host_ + ":" + std::to_string(listen_port_) + " -> " +
           forward_host_ + ":" + std::to_string(forward_port_) + "]";
}

bool UdpInterfaceImpl::start() {
    _online = false;
    if (!winsock_up()) {
        error_ = "Winsock would not start";
        return false;
    }
    std::uint32_t listen_addr = 0;
    if (!parse_ipv4(listen_host_, listen_addr) || !parse_ipv4(forward_host_, forward_addr_)) {
        error_ = "not an IPv4 address: " + listen_host_ + " or " + forward_host_;
        return false;
    }
    std::intptr_t s = (std::intptr_t)::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == kNoSocket
#ifdef _WIN32
        || (SOCKET)s == INVALID_SOCKET
#endif
    ) {
        error_ = "could not open a UDP socket";
        return false;
    }
    int yes = 1;
    ::setsockopt((decltype(::socket(0, 0, 0)))s, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof yes);
    if (broadcast_)
        ::setsockopt((decltype(::socket(0, 0, 0)))s, SOL_SOCKET, SO_BROADCAST, (const char*)&yes, sizeof yes);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = listen_addr;
    addr.sin_port = htons(listen_port_);
    if (::bind((decltype(::socket(0, 0, 0)))s, (const sockaddr*)&addr, sizeof addr) != 0) {
        error_ = "could not listen on UDP port " + std::to_string(listen_port_) + " (in use?)";
        close_socket(s);
        return false;
    }
    if (!set_nonblocking(s)) {
        error_ = "could not make the UDP socket non-blocking";
        close_socket(s);
        return false;
    }
    socket_ = s;
    _online = true;
    return true;
}

void UdpInterfaceImpl::stop() {
    if (socket_ != kNoSocket) close_socket(socket_);
    socket_ = kNoSocket;
    _online = false;
}

void UdpInterfaceImpl::loop() {
    if (!_online) return;
    // drain everything waiting: a loop call that reads one datagram falls behind a burst
    for (int budget = 0; budget < 256; ++budget) {
        sockaddr_in from{};
        socklen_t from_len = sizeof from;
        char* at = (char*)buffer_.writable(_HW_MTU);
        const int len = (int)::recvfrom((decltype(::socket(0, 0, 0)))socket_, at, _HW_MTU, 0,
                                        (sockaddr*)&from, &from_len);
        if (len <= 0) break;  // would block, or an error: nothing more this loop
        buffer_.resize((size_t)len);
        if (learn_peers_) learn(from.sin_addr.s_addr, from.sin_port);
        RNS::InterfaceImpl::handle_incoming(buffer_);
    }
}

bool UdpInterfaceImpl::send_outgoing(const RNS::Bytes& data) {
    if (!_online) return false;
    sockaddr_in to{};
    to.sin_family = AF_INET;
    to.sin_addr.s_addr = forward_addr_;
    to.sin_port = htons(forward_port_);
    const int sent = (int)::sendto((decltype(::socket(0, 0, 0)))socket_, (const char*)data.data(),
                                   (int)data.size(), 0, (const sockaddr*)&to, sizeof to);
    // learn_peers: the same datagram to everyone heard lately, by unicast (udp.hpp)
    const double now = RNS::Utilities::OS::time();
    for (const Heard& h : heard_) {
        if (now - h.seen > kHeardFor) continue;
        if (h.addr == forward_addr_ && h.port == htons(forward_port_)) continue;
        sockaddr_in u{};
        u.sin_family = AF_INET;
        u.sin_addr.s_addr = h.addr;
        u.sin_port = h.port;
        ::sendto((decltype(::socket(0, 0, 0)))socket_, (const char*)data.data(), (int)data.size(), 0,
                 (const sockaddr*)&u, sizeof u);
    }
    RNS::InterfaceImpl::handle_outgoing(data);
    return sent == (int)data.size();
}

void UdpInterfaceImpl::learn(std::uint32_t addr, std::uint16_t port) {
    const double now = RNS::Utilities::OS::time();
    for (Heard& h : heard_)
        if (h.addr == addr && h.port == port) {
            h.seen = now;
            return;
        }
    // forget the stale first, then the oldest, so the list stays small
    heard_.erase(std::remove_if(heard_.begin(), heard_.end(),
                                [&](const Heard& h) { return now - h.seen > kHeardFor; }),
                 heard_.end());
    if (heard_.size() >= kHeardMax)
        heard_.erase(std::min_element(heard_.begin(), heard_.end(),
                                      [](const Heard& a, const Heard& b) { return a.seen < b.seen; }));
    heard_.push_back({addr, port, now});
}

} // namespace voidpalabra::reticulum::detail
