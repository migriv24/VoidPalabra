/* voidpalabra/crypto.hpp — a Reticulum identity's operations, without a node.
 *
 * Part of the OPTIONAL companion `voidpalabra_reticulum` (okf/concepts/reticulum.md);
 * the `voidpalabra` core links none of it.
 *
 * WHY THIS EXISTS (2026-10-01, asked by Void Verguenza and written under the
 * author's grant). `Node` keeps its identity private, which was right until an
 * application needed the identity's own operations: Verguenza seals each secret's
 * key to the devices allowed to open it (`encrypt_to`, Reticulum's own "encrypt to
 * a destination") and signs everything it records (`sign`). The family's rule is
 * one identity system, Reticulum's, so these are Reticulum's primitives with no
 * second cryptographic library.
 *
 * STATELESS ON PURPOSE. Every call takes the keys it uses. Nothing here needs a
 * running `Node` or a network, so a CLI and an application on one device can both
 * sign while only one of them runs the node (one node per process, and two nodes
 * may not share a storage folder).
 *
 * PLAINTEXT GOES INTO CALLER-OWNED BUFFERS. `decrypt` and `token_decrypt` write into
 * memory the caller allocated (and can lock and wipe). Private keys are read from
 * caller memory. microReticulum's own `Bytes` copies are not wiped by
 * microReticulum; this file wipes the copies it can reach before releasing them,
 * and the OKF says plainly that intermediate copies inside microReticulum are not
 * reachable from here.
 *
 * RANDOMNESS. Anything that needs random bytes (a new identity, an ephemeral key,
 * a token's IV) first makes sure the generator is seeded from the operating
 * system (defect 11), whether or not a `Node` was started.
 *
 * No microReticulum type appears in this header. */
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace voidpalabra::reticulum::crypto {

constexpr std::size_t kPrivateKeySize = 64;   // X25519 private ‖ Ed25519 private, as Reticulum writes it
constexpr std::size_t kPublicKeySize = 64;    // X25519 public ‖ Ed25519 public
constexpr std::size_t kSignatureSize = 64;    // Ed25519
constexpr std::size_t kIdentityHashSize = 16; // truncated SHA-256 of the public key
constexpr std::size_t kTokenKeySize = 64;     // AES-256-CBC + HMAC-SHA256 halves
constexpr std::size_t kHashSize = 32;         // SHA-256

/* Random bytes from Reticulum's generator, seeded from the operating system first.
 * False only if the operating system gave no entropy. */
bool random(std::uint8_t* out, std::size_t len);

void sha256(const std::uint8_t* data, std::size_t len, std::uint8_t out[kHashSize]);

/* A new identity. The private key is written into caller memory. */
bool generate_identity(std::uint8_t private_key[kPrivateKeySize], std::uint8_t public_key[kPublicKeySize]);

/* The public key and identity hash that belong to a private key, or to a public key. */
bool public_key_of(const std::uint8_t private_key[kPrivateKeySize], std::uint8_t public_key[kPublicKeySize]);
void identity_hash(const std::uint8_t public_key[kPublicKeySize], std::uint8_t out[kIdentityHashSize]);

bool sign(const std::uint8_t private_key[kPrivateKeySize], const std::uint8_t* message, std::size_t len,
          std::uint8_t signature[kSignatureSize]);
bool verify(const std::uint8_t public_key[kPublicKeySize], const std::uint8_t* message, std::size_t len,
            const std::uint8_t signature[kSignatureSize]);

/* Encrypt to an identity: an ephemeral X25519 key, HKDF, then a token
 * (Reticulum's Identity.encrypt). Only the holder of the private key opens it.
 * Output is ciphertext, so it is returned as a string. */
bool encrypt_to(const std::uint8_t public_key[kPublicKeySize], const std::uint8_t* plaintext, std::size_t len,
                std::string& ciphertext);

/* Open what encrypt_to made. Writes at most `capacity` bytes into `out` and sets
 * `*written`. False if the ciphertext is not for this key, was changed, or the
 * plaintext would not fit (nothing is written then). */
bool decrypt(const std::uint8_t private_key[kPrivateKeySize], const std::uint8_t* ciphertext, std::size_t len,
             std::uint8_t* out, std::size_t capacity, std::size_t* written);

/* A token under a given 64-byte key: AES-256-CBC with a random IV, then HMAC-SHA256
 * over it (encrypt-then-MAC). No associated data, which is Reticulum's choice; a
 * caller that needs to bind the plaintext to a context puts the context inside it. */
bool token_encrypt(const std::uint8_t key[kTokenKeySize], const std::uint8_t* plaintext, std::size_t len,
                   std::string& token);
bool token_decrypt(const std::uint8_t key[kTokenKeySize], const std::uint8_t* token, std::size_t len,
                   std::uint8_t* out, std::size_t capacity, std::size_t* written);

/* Overwrite memory in a way the compiler will not remove. */
void wipe(void* data, std::size_t len);

} // namespace voidpalabra::reticulum::crypto
