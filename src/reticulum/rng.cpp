/* src/reticulum/rng.cpp — Reticulum's generator, seeded from the operating system.
 * See entropy.hpp for the order and why it is the whole fix (defect 11,
 * okf/concepts/reticulum.md). */
#include "entropy.hpp"

#include <RNG.h>

#include <atomic>

namespace voidpalabra::reticulum::detail {

namespace {
std::atomic<bool> g_seeded{false};

bool stir_from_os() {
    std::uint8_t buf[64];
    if (!os_entropy(buf, sizeof buf)) return false;
    RNG.stir(buf, sizeof buf, sizeof buf * 8); // full credit: these are the OS generator's bytes
    volatile std::uint8_t* p = buf;
    for (std::size_t i = 0; i < sizeof buf; ++i) p[i] = 0;
    return true;
}
} // namespace

bool ensure_rng_seeded() {
    if (g_seeded.load()) return true;
    RNG.begin("Reticulum"); // before the stir: begin() overwrites the state
    if (!stir_from_os()) return false;
    g_seeded.store(true);
    return true;
}

bool restir_rng() { return ensure_rng_seeded() && stir_from_os(); }

} // namespace voidpalabra::reticulum::detail
