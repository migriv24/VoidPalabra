/* orset.hpp — the one primitive the whole CRDT layer is built from.
 *
 * An observed-remove set of tag -> value. `join` is union on both members, so the
 * three laws hold BY CONSTRUCTION — set union is commutative, associative and
 * idempotent, and nothing else in the structure can break them.
 *
 * The same primitive serves three roles, and there is deliberately not a family:
 * a SET (a rune's tags), a MULTI-VALUE REGISTER (a field, where a write retires
 * what it observed), and a KEYED MAP (presence of runes and mantles).
 */
#pragma once

#include <map>
#include <set>
#include <string>
#include <vector>

#include "voidpalabra/canonical.hpp"

struct cJSON;

namespace voidpalabra {

/* A unique tag minted per add. Opaque; only equality and ordering matter. */
using Tag = std::string;

/* The one primitive. An observed-remove set of tag→value, from which the OR-Set,
 * the multi-value register, and the keyed OR-Map are all built.
 *
 * `join` is union on both members, so the three laws hold by construction —
 * set union is commutative, associative and idempotent, and nothing else in the
 * structure can break them. That is the entire correctness argument, and it is
 * why this primitive is worth having exactly one of. */
struct OrSet {
    std::map<Tag, std::string> adds;  // tag -> canonical value bytes
    std::set<Tag> removes;            // tags observed at the time of a remove

    /* Values currently present: those with at least one live tag. Sorted and
     * deduplicated, so the result is a set rather than a bag. */
    std::vector<std::string> values() const;
    bool empty() const;

    /* Add a value under a fresh tag. The caller supplies the tag so that minting
     * stays outside this file — a test wants determinism, a peer wants a CSPRNG,
     * and both are legitimate. */
    void add(const Tag& tag, const std::string& value);

    /* Observed-remove: retire exactly the tags visible now. A concurrent add
     * carries a tag this peer never saw, so it survives — which is the whole
     * point, and the reason "remove" cannot simply drop a value. */
    void remove_value(const std::string& value);
    void remove_all();
};

OrSet join(const OrSet& a, const OrSet& b);

/* A multi-value register: a write retires what it saw and adds one value.
 * Concurrent writes therefore both survive — and two surviving values IS the
 * conflict (okf/concepts/conflict.md), not an error signalling one. */
using Register = OrSet;
void write(Register& r, const Tag& tag, const std::string& value);


}  // namespace voidpalabra
