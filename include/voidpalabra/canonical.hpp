/* canonical.hpp — the deterministic bytes of a Void Core slice, and its hash.
 *
 * Rung 0 of the Void Palabra roadmap. Every other name in Palabra is built on
 * this: a version name, a reconciliation fingerprint, a prolly-tree key, the
 * canonical ordering of a conflict's sides. See okf/concepts/canonical-form.md.
 *
 * The requirement, stated exactly:
 *
 *     Two peers holding the same slice, reached by any route, must compute the
 *     same bytes.
 *
 * "Any route" is the load-bearing clause. One peer authored runes in one order;
 * another received them reversed over a lossy transport; a third rebuilt the
 * slice from storage. All three must agree.
 *
 * This is where Palabra's central argument becomes code. A serializer has to be
 * told, for every sequence, whether its order is REAL or ARBITRARY — which is
 * exactly the distinction okf/design/why-not-linear.md says a linear log throws
 * away. Hence two sequence encodings, SEQ and SET, and choosing between them is
 * a semantic act rather than a formatting one.
 *
 *     When in doubt, preserve order. Discarding an order that turns out to be
 *     real is a silent wrong answer, which Void Core's honesty principle
 *     forbids. Preserving an order that turns out to be arbitrary costs a false
 *     conflict, which is merely annoying. The failure modes are not symmetric,
 *     so neither is the default.
 *
 * Input is a cJSON tree, because that is what Void Core and Void Maiz already
 * hold. Palabra deliberately does not define a value type of its own.
 */
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>

struct cJSON;

namespace voidpalabra {

/* Bumped when the encoding changes in a way that moves bytes. Mixed into every
 * digest, so an old and a new implementation never quietly agree.
 *
 * v2 (2026-07-27): dropped the `rune_order` policy after Void Core ruled rune
 * order non-semantic (SPEC §4). Bumped within hours of v1, deliberately: the rule
 * is "if the bytes move, bump", and starting to reason case-by-case about whether
 * a change "really counts" is how this kind of guard rots. */
inline constexpr int kCanonVersion = 2;

/* A value that has no single honest byte form, or state that violates SPEC. */
class CanonicalError : public std::runtime_error {
public:
    explicit CanonicalError(const std::string& what) : std::runtime_error(what) {}
};

/* What this canonicalization treats as meaningful.
 *
 * This used to carry a `rune_order` parameter, because whether a mantle's rune
 * order is semantic was open and it changes these bytes. **Void Core answered it
 * on 2026-07-27 and the answer is no** — SPEC §4 is now normative:
 *
 *     Rune order is preserved but not semantic. [...] an order-sensitive
 *     consumer (a canonical form, a content hash, a sync join) MUST be
 *     order-insensitive at the Core level, because two peers who create the same
 *     runes in different orders hold EQUAL state.
 *
 * So the parameter is gone rather than defaulted. Keeping it would keep a
 * supported way to compute a name that Core says is wrong — and a policy knob
 * whose wrong setting is silently wrong is worse than no knob. An application
 * that genuinely needs an ordering puts it in a content field (SPEC §4, the same
 * ruling as `placement`), where it is opaque to Core and this encoder preserves
 * it exactly. */
struct Policy {
    /* Whether `placement` (Void Core's view slice, SPEC §3.2) is part of the
     * canonical form. It is mergeable state with a declared join
     * (okf/concepts/join.md), so it is included by default; excluding it gives
     * the "same content, rearranged" hash.
     *
     * This one is a genuine choice rather than an open question: both answers are
     * correct, for different questions. */
    bool include_view = true;

    std::string tag() const;
};

using Digest = std::array<std::uint8_t, 32>;

/* --- canonical bytes ---------------------------------------------------- */

std::string canon_rune(const cJSON* rune, const Policy& policy = {});
std::string canon_mantle(const cJSON* mantle, const Policy& policy = {});

/* Canonical bytes of the VERSIONED SLICE of a Void Core state document.
 *
 * That slice is `mantles` and nothing else, per okf/concepts/utterance.md
 * §"What an utterance may target". `domains`, `bindings` and `config` are
 * peer-local resolution — a domain carries real deploy commands (SPEC §3.5), so
 * syncing one would run one device's deploy on another. `active` is a cursor and
 * `_baseline` is dirty-tracking; neither is content.
 *
 * Mantles are a set keyed by their (unique) name: their order in the array is an
 * artifact of creation, not information. */
std::string canon_slice(const cJSON* state, const Policy& policy = {});

/* Encode an arbitrary JSON value. Sequences keep their order — the conservative
 * reading, and the right one for anything Palabra does not understand, which
 * includes every rune's `content` (Void Core does not interpret it either, per
 * SPEC §3.2, and Palabra must not invent an interpretation Core declines). */
std::string encode(const cJSON* value);

/* The inverse, for the storage path only.
 *
 * The canonical form is a one-way function BY DESIGN — it exists to name things,
 * and §2.1's integral fold and §2.4's set deduplication are deliberately lossy
 * with respect to how a value was written down. `decode` recovers a value that is
 * EQUAL under the encoding, not the original text: `encode(decode(b)) == b`, but
 * `decode(encode(x))` may differ from `x` in ways the encoding declares
 * meaningless. That is exactly what a store needs and is not a round-trip law.
 *
 * Returns nullptr on malformed input. Caller owns the tree. */
cJSON* decode(const std::string& bytes);

/* --- the NFC precondition ------------------------------------------------ */
/*
 * DECIDED 2026-08-21, on Void Hormiga's offer. Palabra does NOT normalize
 * Unicode: callers MUST supply NFC. See SPEC.md §6.1.
 *
 * The reasoning, briefly. Doing it properly needs the Unicode decomposition and
 * composition-exclusion tables, which is disproportionate here and heavy for an
 * ESP32 (`State` tier). The application has the keyboard; Palabra has a hash
 * function. Normalizing where the text is entered is both cheaper and more
 * correct, because that is the only place that knows the text is being entered.
 *
 * The risk this creates, stated plainly: a caller that violates the precondition
 * gets SILENT DIVERGENCE. `café` typed on a mac (NFD) and on Windows (NFC) are
 * different bytes, so two peers compute different names for what a human calls
 * the same rune, and it presents as "the merge did nothing" rather than as an
 * error. Hormiga's bilingual database is exactly this case and said so.
 *
 * So the precondition ships with a way to check it. */

/* Advisory: does this string contain combining marks, i.e. does it look like it
 * has NOT been composed?
 *
 * A DIAGNOSTIC, not a validator, and the distinction is deliberate. Some
 * sequences are legitimately decomposed and have no composed form, so a `true`
 * here is a reason to look rather than proof of a bug — which is why nothing in
 * this library rejects on it. It exists so a host can assert over its own corpus
 * in its own tests, where a false positive costs a glance instead of a refusal.
 *
 * Covers the Latin/general combining blocks, which is where the realistic failure
 * lives (`á é í ó ú ñ ü` in Spanish content typed on mixed platforms). */
bool has_combining_marks(const std::string& utf8);

/* --- names -------------------------------------------------------------- */

Digest rune_hash(const cJSON* rune, const Policy& policy = {});
Digest mantle_hash(const cJSON* mantle, const Policy& policy = {});
Digest slice_hash(const cJSON* state, const Policy& policy = {});

/* The sayable answer to "what version is this?".
 *
 * A cut is named by the hash of its canonical form
 * (okf/concepts/version-as-cut.md). At Rung 0 there is no history yet, so this
 * names the CURRENT slice — already the useful half: two peers who reached the
 * same state by different routes say the same word. */
std::string version_name(const cJSON* state, const Policy& policy = {});

std::string to_hex(const Digest& d);
/* A display abbreviation. Never use it to compare or to address. */
std::string short_hex(const Digest& d, std::size_t n = 8);

/* --- SHA-256 ------------------------------------------------------------ */
/* Chosen over BLAKE2 because it is universal and HARDWARE-ACCELERATED ON THE
 * ESP32, which okf/concepts/peer-and-tier.md requires to be a full peer.
 *
 * Implemented here rather than vendored only because Palabra is zero-dependency
 * at Rung 0. Verified against the FIPS 180-4 vectors in the test suite. Once
 * Phase 4 arrives Palabra needs signatures anyway, and Hormiga already vendors
 * libsodium built — at that point this should become crypto_hash_sha256. */
Digest sha256(const void* data, std::size_t len);
Digest sha256(const std::string& s);

}  // namespace voidpalabra
