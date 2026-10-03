/* tests/reticulum/crypto_test.cpp — voidpalabra/crypto.hpp, the identity's
 * operations without a node. Every key here is made at test time. */
#include "voidpalabra/crypto.hpp"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace voidpalabra::reticulum::crypto;

static int failures = 0;
#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            ++failures;                                                   \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                 \
    } while (0)

static const std::uint8_t* u8(const std::string& s) { return reinterpret_cast<const std::uint8_t*>(s.data()); }

int main() {
    std::uint8_t prv_a[kPrivateKeySize], pub_a[kPublicKeySize];
    std::uint8_t prv_b[kPrivateKeySize], pub_b[kPublicKeySize];
    CHECK(generate_identity(prv_a, pub_a));
    CHECK(generate_identity(prv_b, pub_b));
    CHECK(std::memcmp(pub_a, pub_b, kPublicKeySize) != 0);

    // the public key and hash come back from the private key
    std::uint8_t pub_again[kPublicKeySize];
    CHECK(public_key_of(prv_a, pub_again));
    CHECK(std::memcmp(pub_a, pub_again, kPublicKeySize) == 0);
    std::uint8_t h1[kIdentityHashSize], h2[kIdentityHashSize];
    identity_hash(pub_a, h1);
    identity_hash(pub_again, h2);
    CHECK(std::memcmp(h1, h2, kIdentityHashSize) == 0);

    // sign and verify; a changed message, a changed signature, another key all fail
    const std::string msg = "verguenza/test/v1 a message to sign";
    std::uint8_t sig[kSignatureSize];
    CHECK(sign(prv_a, u8(msg), msg.size(), sig));
    CHECK(verify(pub_a, u8(msg), msg.size(), sig));
    CHECK(!verify(pub_b, u8(msg), msg.size(), sig));
    std::string changed = msg;
    changed[3] ^= 1;
    CHECK(!verify(pub_a, u8(changed), changed.size(), sig));
    sig[10] ^= 1;
    CHECK(!verify(pub_a, u8(msg), msg.size(), sig));

    // encrypt to an identity; only its private key opens it
    std::uint8_t secret[64];
    CHECK(random(secret, sizeof secret));
    std::string ct;
    CHECK(encrypt_to(pub_a, secret, sizeof secret, ct));
    std::uint8_t out[128];
    std::size_t n = 0;
    CHECK(decrypt(prv_a, u8(ct), ct.size(), out, sizeof out, &n));
    CHECK(n == sizeof secret && std::memcmp(out, secret, n) == 0);
    CHECK(!decrypt(prv_b, u8(ct), ct.size(), out, sizeof out, &n));
    std::string bad = ct;
    bad[bad.size() / 2] ^= 1;
    CHECK(!decrypt(prv_a, u8(bad), bad.size(), out, sizeof out, &n));
    CHECK(!decrypt(prv_a, u8(ct), ct.size(), out, 8, &n)); // too small: refused, nothing written
    CHECK(n == 0);

    // two seals of the same bytes differ (ephemeral key, random IV)
    std::string ct2;
    CHECK(encrypt_to(pub_a, secret, sizeof secret, ct2));
    CHECK(ct != ct2);

    // a token under a given key
    std::uint8_t key[kTokenKeySize];
    CHECK(random(key, sizeof key));
    const std::string value = "vg-test-not-a-real-password";
    std::string token;
    CHECK(token_encrypt(key, u8(value), value.size(), token));
    std::vector<std::uint8_t> back(value.size() + 32);
    CHECK(token_decrypt(key, u8(token), token.size(), back.data(), back.size(), &n));
    CHECK(n == value.size() && std::memcmp(back.data(), value.data(), n) == 0);
    std::string tampered = token;
    tampered[20] ^= 1;
    CHECK(!token_decrypt(key, u8(tampered), tampered.size(), back.data(), back.size(), &n));
    key[0] ^= 1;
    CHECK(!token_decrypt(key, u8(token), token.size(), back.data(), back.size(), &n));

    // SHA-256 of "abc" (FIPS 180-4)
    std::uint8_t d[kHashSize];
    sha256(reinterpret_cast<const std::uint8_t*>("abc"), 3, d);
    CHECK(d[0] == 0xba && d[1] == 0x78 && d[31] == 0xad);

    wipe(prv_a, sizeof prv_a);
    CHECK(prv_a[0] == 0 && prv_a[63] == 0);

    if (failures) std::fprintf(stderr, "%d failure(s)\n", failures);
    else std::printf("crypto: all checks passed\n");
    return failures ? 1 : 0;
}
