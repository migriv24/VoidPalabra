/* src/reticulum/entropy.hpp — the operating system's random generator, for
 * seeding Reticulum's.
 *
 * WHY THIS EXISTS (found 2026-10-01 by Void Verguenza, reading the vendored
 * code). The Crypto library's RNG (vendor/reticulum/Crypto/RNG.cpp) knows how to
 * read hardware generators on Arduino, ESP32 and nRF52, and nothing else. On
 * Windows, Linux, macOS and the Android NDK it starts from fixed constants and
 * the compile time, and the only thing that varies is a 32-bit microsecond clock
 * value mixed in at each rekey. microReticulum's hook for adding a noise source
 * is commented out. Every identity key and every link's ephemeral key comes from
 * that generator. So Node::start reads the operating system's generator here and
 * stirs it in through the Crypto library's public `RNG.stir`, before any key
 * exists. Not a patch: the public API was enough. See okf/concepts/reticulum.md,
 * defect 11. */
#pragma once

#include <cstddef>
#include <cstdint>

namespace voidpalabra::reticulum::detail {

/* Fill `out` with `len` bytes from the operating system's cryptographic random
 * generator. False if the platform has none or it failed; the caller must then
 * refuse to make keys rather than fall back to anything weaker. */
bool os_entropy(std::uint8_t* out, std::size_t len);

/* The name of the facility os_entropy reads ("BCryptGenRandom", "getrandom",
 * "arc4random_buf"), or "" on a platform with none. */
const char* os_entropy_source();

/* Reticulum's generator, seeded from the operating system (src/reticulum/rng.cpp).
 *
 * ensure_rng_seeded: on the FIRST call, RNG.begin then a 64-byte stir from the OS.
 *   The order is the fix: begin() overwrites the state (a stir before it is lost)
 *   and returns early once run (so Reticulum's own later begin() is harmless).
 *   Later calls do nothing and return true. Called by Node::start before the
 *   identity exists, and by every crypto.hpp call that needs randomness, so the
 *   generator is seeded whichever comes first.
 * restir_rng: stir 64 fresh bytes in (Node::loop, every few minutes). */
bool ensure_rng_seeded();
bool restir_rng();

} // namespace voidpalabra::reticulum::detail
