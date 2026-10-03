/* src/reticulum/crypto.cpp — voidpalabra/crypto.hpp over microReticulum.
 *
 * Every call builds a short-lived RNS::Identity or Token from the caller's keys,
 * does one thing, and wipes the copies of key and plaintext bytes this file made
 * before they are released. What microReticulum copies internally (its key
 * objects, HKDF output, the token's split keys) is not reachable from here; the
 * header and okf/concepts/reticulum.md say so rather than claim otherwise. */
#include "voidpalabra/crypto.hpp"

#include "entropy.hpp"

#include <microReticulum/Bytes.h>
#include <microReticulum/Cryptography/Token.h>
#include <microReticulum/Identity.h>

#include <RNG.h>

#include <cstring>
#include <exception>

namespace voidpalabra::reticulum::crypto {

namespace {

RNS::Bytes bytes(const std::uint8_t* p, std::size_t n) { return RNS::Bytes(p, n); }

/* Wipe a Bytes this file owns. Only for buffers made here and not handed on:
 * a Bytes shares its storage with its copies, so this is never used on one that
 * microReticulum kept. */
void wipe_bytes(RNS::Bytes& b) {
    if (b.size() > 0) wipe(const_cast<std::uint8_t*>(b.data()), b.size());
}

/* An identity holding the caller's private key. The key's copies inside the
 * identity are wiped by `release`. */
bool load_private(const std::uint8_t* private_key, RNS::Identity& id) {
    RNS::Bytes prv = bytes(private_key, kPrivateKeySize);
    bool ok = false;
    try {
        id = RNS::Identity(false);
        ok = id.load_private_key(prv);
    } catch (const std::exception&) {
        ok = false;
    }
    wipe_bytes(prv);
    return ok;
}

void release(RNS::Identity& id) {
    if (!id) return;
    wipe(const_cast<std::uint8_t*>(id.encryptionPrivateKey().data()), id.encryptionPrivateKey().size());
    wipe(const_cast<std::uint8_t*>(id.signingPrivateKey().data()), id.signingPrivateKey().size());
}

/* Copy plaintext into caller memory and wipe the Bytes it came in. */
bool deliver(RNS::Bytes& plain, std::uint8_t* out, std::size_t capacity, std::size_t* written) {
    if (written) *written = 0;
    const bool fits = plain.size() <= capacity;
    if (fits && plain.size() > 0) std::memcpy(out, plain.data(), plain.size());
    if (fits && written) *written = plain.size();
    wipe_bytes(plain);
    return fits;
}

} // namespace

void wipe(void* data, std::size_t len) {
    volatile std::uint8_t* p = static_cast<volatile std::uint8_t*>(data);
    for (std::size_t i = 0; i < len; ++i) p[i] = 0;
}

bool random(std::uint8_t* out, std::size_t len) {
    if (!detail::ensure_rng_seeded()) return false;
    RNG.rand(out, len);
    return true;
}

void sha256(const std::uint8_t* data, std::size_t len, std::uint8_t out[kHashSize]) {
    const RNS::Bytes h = RNS::Identity::full_hash(bytes(data, len));
    std::memcpy(out, h.data(), kHashSize);
}

bool generate_identity(std::uint8_t private_key[kPrivateKeySize], std::uint8_t public_key[kPublicKeySize]) {
    if (!detail::ensure_rng_seeded()) return false;
    try {
        RNS::Identity id(true);
        RNS::Bytes prv = id.get_private_key();
        const RNS::Bytes pub = id.get_public_key();
        const bool ok = prv.size() == kPrivateKeySize && pub.size() == kPublicKeySize;
        if (ok) {
            std::memcpy(private_key, prv.data(), kPrivateKeySize);
            std::memcpy(public_key, pub.data(), kPublicKeySize);
        }
        wipe_bytes(prv);
        release(id);
        return ok;
    } catch (const std::exception&) {
        return false;
    }
}

bool public_key_of(const std::uint8_t private_key[kPrivateKeySize], std::uint8_t public_key[kPublicKeySize]) {
    RNS::Identity id(RNS::Type::NONE);
    if (!load_private(private_key, id)) return false;
    const RNS::Bytes pub = id.get_public_key();
    const bool ok = pub.size() == kPublicKeySize;
    if (ok) std::memcpy(public_key, pub.data(), kPublicKeySize);
    release(id);
    return ok;
}

void identity_hash(const std::uint8_t public_key[kPublicKeySize], std::uint8_t out[kIdentityHashSize]) {
    const RNS::Bytes h = RNS::Identity::truncated_hash(bytes(public_key, kPublicKeySize));
    std::memcpy(out, h.data(), kIdentityHashSize);
}

bool sign(const std::uint8_t private_key[kPrivateKeySize], const std::uint8_t* message, std::size_t len,
          std::uint8_t signature[kSignatureSize]) {
    RNS::Identity id(RNS::Type::NONE);
    if (!load_private(private_key, id)) return false;
    bool ok = false;
    try {
        const RNS::Bytes sig = id.sign(bytes(message, len));
        ok = sig.size() == kSignatureSize;
        if (ok) std::memcpy(signature, sig.data(), kSignatureSize);
    } catch (const std::exception&) {
        ok = false;
    }
    release(id);
    return ok;
}

bool verify(const std::uint8_t public_key[kPublicKeySize], const std::uint8_t* message, std::size_t len,
            const std::uint8_t signature[kSignatureSize]) {
    try {
        RNS::Identity id(false);
        id.load_public_key(bytes(public_key, kPublicKeySize));
        return id.validate(bytes(signature, kSignatureSize), bytes(message, len));
    } catch (const std::exception&) {
        return false;
    }
}

bool encrypt_to(const std::uint8_t public_key[kPublicKeySize], const std::uint8_t* plaintext, std::size_t len,
                std::string& ciphertext) {
    if (!detail::ensure_rng_seeded()) return false;
    RNS::Bytes plain = bytes(plaintext, len);
    bool ok = false;
    try {
        RNS::Identity id(false);
        id.load_public_key(bytes(public_key, kPublicKeySize));
        const RNS::Bytes ct = id.encrypt(plain);
        ciphertext.assign(reinterpret_cast<const char*>(ct.data()), ct.size());
        ok = ct.size() > 0;
    } catch (const std::exception&) {
        ok = false;
    }
    wipe_bytes(plain);
    return ok;
}

bool decrypt(const std::uint8_t private_key[kPrivateKeySize], const std::uint8_t* ciphertext, std::size_t len,
             std::uint8_t* out, std::size_t capacity, std::size_t* written) {
    if (written) *written = 0;
    RNS::Identity id(RNS::Type::NONE);
    if (!load_private(private_key, id)) return false;
    bool ok = false;
    try {
        // microReticulum returns empty bytes on any failure (wrong key, changed bytes)
        RNS::Bytes plain = id.decrypt(bytes(ciphertext, len));
        ok = plain.size() > 0 && deliver(plain, out, capacity, written);
    } catch (const std::exception&) {
        ok = false;
    }
    release(id);
    return ok;
}

bool token_encrypt(const std::uint8_t key[kTokenKeySize], const std::uint8_t* plaintext, std::size_t len,
                   std::string& token) {
    if (!detail::ensure_rng_seeded()) return false;
    RNS::Bytes k = bytes(key, kTokenKeySize);
    RNS::Bytes plain = bytes(plaintext, len);
    bool ok = false;
    try {
        RNS::Cryptography::Token t(k);
        const RNS::Bytes ct = t.encrypt(plain);
        token.assign(reinterpret_cast<const char*>(ct.data()), ct.size());
        ok = ct.size() > 0;
    } catch (const std::exception&) {
        ok = false;
    }
    wipe_bytes(k);
    wipe_bytes(plain);
    return ok;
}

bool token_decrypt(const std::uint8_t key[kTokenKeySize], const std::uint8_t* token, std::size_t len,
                   std::uint8_t* out, std::size_t capacity, std::size_t* written) {
    if (written) *written = 0;
    RNS::Bytes k = bytes(key, kTokenKeySize);
    bool ok = false;
    try {
        RNS::Cryptography::Token t(k);
        RNS::Bytes plain = t.decrypt(bytes(token, len)); // throws on a bad HMAC
        ok = deliver(plain, out, capacity, written);
    } catch (const std::exception&) {
        ok = false;
    }
    wipe_bytes(k);
    return ok;
}

} // namespace voidpalabra::reticulum::crypto
