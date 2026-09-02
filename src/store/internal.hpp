/* internal.hpp — shared inside the store layer, not public.
 *
 * A `Digest` is a fixed 32-byte array and a block key is the same bytes as a
 * string, because that is what a `std::map` wants. These two conversions are the
 * whole of it, and they live here so the block store and the container agree on
 * exactly one representation.
 */
#pragma once

#include <cstring>
#include <string>

#include "voidpalabra/store.hpp"

namespace voidpalabra {
namespace blk {

inline std::string key_bytes(const Digest& d) {
    return std::string(reinterpret_cast<const char*>(d.data()), d.size());
}

inline Digest key_of(const std::string& s) {
    Digest d{};
    std::memcpy(d.data(), s.data(), d.size());
    return d;
}

}  // namespace blk
}  // namespace voidpalabra
