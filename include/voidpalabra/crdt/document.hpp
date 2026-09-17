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

/* The enriched form of a Void Core slice: the versioned slice — `mantles` and
 * `glyphs`, SPEC.md §4.4 — with per-element CRDT metadata attached.
 *
 * `glyphs` joined on 2026-09-03, when Void Core 0.2.14 introduced declarations.
 * Without them a merge delivers a peer's runes and not the schemas that say what
 * those runes mean, so the content arrives present in the document and unreachable
 * through the projection.
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
 * ROUND-TRIP LAW: flatten(enrich(x)) == x FOR THE VERSIONED SLICE, for any state
 * x with no conflicts — compared through the canonical form, which is defined over
 * that slice. Deliberately the same law `VoidCore:scry/roundtrip.py` holds a Lens
 * to (`unscry(scry(x)) == x`), and for the same reason: a mapping written
 * separately for each direction drifts into silent data loss. Property-tested.
 *
 * READ THE SCOPE, because the short form of that sentence is a trap. `flatten`
 * returns THE SLICE, not your document. `config`, `domains`, `bindings` and
 * `active` are not in it — they are peer-local resolution and were never
 * versioned. A caller who writes this result back as their whole state document
 * loses all four.
 *
 * The correct use is to SPLICE: lift `mantles` and `glyphs` out of the result and
 * put them into the document you already hold, which keeps this device's domains
 * and config its own. Void Hormiga does exactly that, and it is why nothing broke
 * there when `glyphs` was added — but a host that rebuilds its document key by key
 * from its own model would have dropped the new key silently, which is Void Core
 * 0.2.14 §2(c)'s warning. The document will grow keys again. */
Doc flatten(const Doc& doc, const JoinPolicy& policy = {});

/* The merge. Commutative, associative, idempotent — property-tested.
 *
 * The laws hold for ANY input, including a malformed document: where two peers
 * disagree about what kind of node sits at a path, the choice is a function of the
 * nodes rather than of argument order, so the mesh still converges. That is a
 * guarantee about convergence, not about correctness — a malformed node can still
 * win. Anything that arrived from another peer goes through `validate` first. */
Doc join(const Doc& a, const Doc& b);

/* Is this enriched document — or a delta, which has the same shape with parts
 * missing — safe to merge? SPEC.md §5.6.
 *
 * Everything a peer sends is checked before it touches local state, and the answer
 * is all-or-nothing: a document with one bad register is refused whole, because
 * merging "the good parts" would make what a peer holds depend on which parts this
 * implementation happened to like.
 *
 * What it checks:
 *   - the root is `{"palabra": 1, "mantles"?: {...}, "glyphs"?: {...}}` and nothing
 *     else. A newer document shape bumps `palabra`; an older peer refusing it stops
 *     sync VISIBLY, which is better than merging structure it cannot check.
 *   - every node is where the shape (§5.2) says it can be, and is the kind of node
 *     that position holds. A kind mismatch is what used to split a mesh for good.
 *   - every OrSet is exactly `{"a": {tag: lowercase-hex}, "r": [tag]}`.
 *   - every register, tag-set and edge value is CANONICAL (`is_canonical`), so two
 *     spellings of one value cannot be read as a conflict.
 *   - names and tags are non-empty.
 *
 * What it does not check: who is allowed to have sent this. That is trust, and it
 * is not in this library yet (okf/design/open-questions.md §6).
 *
 * `why`, when given, receives a path and a reason for the first failure. */
bool validate(const cJSON* root, std::string* why = nullptr);
bool validate(const Doc& doc, std::string* why = nullptr);

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
