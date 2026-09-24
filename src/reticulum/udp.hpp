/* src/reticulum/udp.hpp — a UDP interface for microReticulum, on Windows and POSIX.
 *
 * microReticulum ships a UDP interface only as an EXAMPLE, and a POSIX-only one
 * (it includes netinet/in.h and uses MSG_DONTWAIT). This is ours: Winsock or
 * BSD sockets, non-blocking, polled from Reticulum's loop. One datagram carries
 * one Reticulum packet, which is what the Python reference's UDPInterface sends,
 * so the two interoperate with mirrored ports.
 *
 * LEARN_PEERS: "beacon out, unicast back". Android drops incoming broadcast but
 * never unicast (Void Maiz's LAN measurements), so a phone on a broadcast-only
 * interface hears nobody. With learn_peers on, every datagram also goes, by
 * unicast, to each address that sent us one in the last minute (at most 64). The
 * phone's own broadcast reaches the desktop, which then answers it directly.
 * Reticulum packets carry no addresses and a duplicate is dropped by its hash,
 * so the extra copies are harmless.
 *
 * On a desktop or phone, Void Maiz will supply the sockets (okf/concepts/reticulum.md,
 * step 3). This interface is for the tests, and for any host without Void Maiz. */
#pragma once

#include <microReticulum/Interface.h>
#include <microReticulum/Bytes.h>

#include <cstdint>
#include <string>
#include <vector>

namespace voidpalabra::reticulum::detail {

class UdpInterfaceImpl : public RNS::InterfaceImpl {
public:
    UdpInterfaceImpl(const std::string& name, const std::string& listen_host, std::uint16_t listen_port,
                     const std::string& forward_host, std::uint16_t forward_port, bool broadcast,
                     bool learn_peers);
    ~UdpInterfaceImpl() override;

    bool start() override;
    void stop() override;
    void loop() override;
    void detach() override { stop(); }
    std::string toString() const override;

    const std::string& error() const { return error_; }
    void learn(std::uint32_t addr, std::uint16_t port);

protected:
    bool send_outgoing(const RNS::Bytes& data) override;

private:
    std::string listen_host_, forward_host_;
    std::uint16_t listen_port_ = 0, forward_port_ = 0;
    bool broadcast_ = false;
    bool learn_peers_ = false;
    struct Heard {
        std::uint32_t addr;  // network byte order
        std::uint16_t port;  // network byte order
        double seen;
    };
    std::vector<Heard> heard_;  // who sent to us lately (learn_peers)
    std::intptr_t socket_ = -1;  // SOCKET on Windows, int elsewhere
    std::uint32_t forward_addr_ = 0;  // network byte order
    RNS::Bytes buffer_;
    std::string error_;
};

} // namespace voidpalabra::reticulum::detail
