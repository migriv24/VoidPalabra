/* src/reticulum/pipe.hpp — an interface whose bytes the host moves.
 *
 * reticulum.hpp, `PipeInterface`. Packets the node sends wait in `out` until the
 * host takes them; packets the host hands in wait in `in` until the node's loop
 * reads them. No socket, no thread: the host calls Node from one thread (the
 * header's rule), and so does this. */
#pragma once

#include <microReticulum/Interface.h>
#include <microReticulum/Bytes.h>

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace voidpalabra::reticulum::detail {

class PipeInterfaceImpl : public RNS::InterfaceImpl {
public:
    PipeInterfaceImpl(const std::string& name, std::uint32_t bitrate) : RNS::InterfaceImpl(name.c_str()) {
        _IN = true;
        _OUT = true;
        _bitrate = bitrate;
        _HW_MTU = 1064; // the same buffer the UDP interface reads into
    }

    bool start() override {
        _online = true;
        return true;
    }
    void stop() override { _online = false; }
    void detach() override { stop(); }
    std::string toString() const override { return "PipeInterface[" + _name + "]"; }

    void loop() override {
        if (!_online) return;
        while (!in_.empty()) {
            const std::string p = std::move(in_.front());
            in_.pop_front();
            RNS::Bytes b((const uint8_t*)p.data(), p.size());
            RNS::InterfaceImpl::handle_incoming(b);
        }
    }

    void push_in(std::string packet) {
        if (in_.size() < kLimit) in_.push_back(std::move(packet));
    }
    std::vector<std::string> take_out() {
        std::vector<std::string> v(std::make_move_iterator(out_.begin()), std::make_move_iterator(out_.end()));
        out_.clear();
        return v;
    }

protected:
    bool send_outgoing(const RNS::Bytes& data) override {
        if (!_online) return false;
        if (out_.size() >= kLimit) return false; // a host that stopped reading: drop, Reticulum retries
        out_.emplace_back((const char*)data.data(), data.size());
        RNS::InterfaceImpl::handle_outgoing(data);
        return true;
    }

private:
    static constexpr std::size_t kLimit = 4096;
    std::deque<std::string> in_, out_;
};

} // namespace voidpalabra::reticulum::detail
