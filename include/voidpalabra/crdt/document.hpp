/* document.hpp — the enriched document: building it, merging it, reading it back.
 */
#pragma once

#include <map>
#include <set>
#include <string>
#include <vector>

#include "voidpalabra/canonical.hpp"

#include "voidpalabra/crdt/policy.hpp"

struct cJSON;


namespace voidpalabra {

/* --- documents ----------------------------------------------------------- */

/* The enriched form of a Void Core slice: the versioned slice (`mantles`, and
 * nothing else — SPEC.md §4.4) with per-element CRDT metadata attached.
 *
 * Held as JSON so it is storable, canonicalizable, and inspectable with the tools
 * that already exist. Palabra invents no binary object model. */
struct Doc {
    cJSON* root = nullptr;  // owned

    Doc() = default;
    ~Doc();
    Doc(const Doc&) = delete;
    Doc& operator=(const Doc&) = delete;
    Doc(Doc&& o) noexcept;
    Doc& operator=(Doc&& o) noexcept;
};

/* A source of unique tags. `Mint` exists so tests can be deterministic without
 * the library reaching for a CSPRNG it cannot then control. */
struct Mint {
    virtual ~Mint() = default;
    virtual Tag next() = 0;
};
/* Deterministic, for tests and for replay: "<prefix>_0001", "<prefix>_0002", … */
struct CounterMint : Mint {
    std::string prefix;
    unsigned long n = 0;
    explicit CounterMint(std::string p) : prefix(std::move(p)) {}
    Tag next() override;
};

/* Core state document -> enriched document. Every present element gets one tag. */
Doc enrich(const cJSON* state, Mint& mint);

/* Enriched document -> Core state document.
 *
 * Takes the policy because resolving a field is a READ decision (see FieldJoin).
 * With the default policy a conflicted field yields its lowest-ordered value -
 * so ALWAYS check `conflicts()` alongside; `flatten` cannot represent two values
 * in a single-valued Core document, and choosing silently is only safe when the
 * caller knows a conflict is there.
 *
 * ROUND-TRIP LAW: flatten(enrich(x)) == x, for any state x with no conflicts.
 * This is deliberately the same law `VoidCore:scry/roundtrip.py` holds a Lens to
 * (`unscry(scry(x)) == x`), and for the same reason: a mapping written separately
 * for each direction drifts into silent data loss. Property-tested. */
Doc flatten(const Doc& doc, const JoinPolicy& policy = {});

/* The merge. Commutative, associative, idempotent — property-tested. */
Doc join(const Doc& a, const Doc& b);

/* Canonical bytes of an enriched document — including its metadata, so two peers
 * agree on the merged state AND on how they got there.
 *
 * Normalizes every OrSet before encoding, so the result depends on the VALUE and
 * not on how a producer wrote it down. Without that, `a ⊔ a = a` holds as a value
 * and fails as a name — which the property test caught. */
std::string canon_doc(const Doc& doc);

/* --- editing ------------------------------------------------------------- */
/* So that nothing outside the library hand-writes CRDT metadata. Each returns
 * false if the target does not exist. */

/* Multi-value write: retires the values this peer can see and adds one. Two peers
 * doing this concurrently produce a conflict rather than a winner. */
bool set_field(Doc& doc, const std::string& mantle, const std::string& rune_id,
               const std::string& field, const cJSON* value, Mint& mint);

bool add_tag(Doc& doc, const std::string& mantle, const std::string& rune_id,
             const std::string& tag, Mint& mint);
/* Observed-remove: retires only the tags visible here, so a concurrent re-add
 * survives. */
bool remove_tag(Doc& doc, const std::string& mantle, const std::string& rune_id,
                const std::string& tag);


}  // namespace voidpalabra
