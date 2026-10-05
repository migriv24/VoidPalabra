/* src/reticulum/entropy.cpp — see entropy.hpp. One function per platform, each
 * the operating system's own cryptographic generator; nothing here is a
 * generator of ours. */
#include "entropy.hpp"

#if defined(_WIN32)
#include <windows.h>
#include <bcrypt.h>
#elif defined(__APPLE__) || defined(__ANDROID__)
#include <stdlib.h> // arc4random_buf
#elif defined(__linux__)
#include <cerrno>
#include <cstdio>
#include <sys/random.h>
#endif

namespace voidpalabra::reticulum::detail {

#if defined(_WIN32)

bool os_entropy(std::uint8_t* out, std::size_t len) {
    while (len > 0) {
        const ULONG n = len > 0x10000000u ? 0x10000000u : static_cast<ULONG>(len);
        if (BCryptGenRandom(nullptr, out, n, BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0) return false;
        out += n;
        len -= n;
    }
    return true;
}
const char* os_entropy_source() { return "BCryptGenRandom"; }

#elif defined(__APPLE__) || defined(__ANDROID__)

/* Android too (2026-10-04): bionic's getrandom() is declared only from API 28,
 * and the applications that link this build for API 26, so the Linux branch
 * below did not compile there. bionic's arc4random_buf is the platform's own
 * kernel-seeded generator and exists at every API level. */
bool os_entropy(std::uint8_t* out, std::size_t len) {
    arc4random_buf(out, len); // cannot fail
    return true;
}
const char* os_entropy_source() { return "arc4random_buf"; }

#elif defined(__linux__)

bool os_entropy(std::uint8_t* out, std::size_t len) {
    while (len > 0) {
        const ssize_t n = getrandom(out, len, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            break; // ENOSYS on a very old kernel: try the device below
        }
        out += n;
        len -= static_cast<std::size_t>(n);
    }
    if (len == 0) return true;
    std::FILE* f = std::fopen("/dev/urandom", "rb");
    if (!f) return false;
    const std::size_t got = std::fread(out, 1, len, f);
    std::fclose(f);
    return got == len;
}
const char* os_entropy_source() { return "getrandom"; }

#else

bool os_entropy(std::uint8_t*, std::size_t) { return false; }
const char* os_entropy_source() { return ""; }

#endif

} // namespace voidpalabra::reticulum::detail
