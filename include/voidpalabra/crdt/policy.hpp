/* policy.hpp — declared per-field joins, applied at READ time.
 */
#pragma once

#include <map>
#include <set>
#include <string>
#include <vector>

#include "voidpalabra/canonical.hpp"

struct cJSON;

namespace voidpalabra {

/* --- declared per-field joins -------------------------------------------- */
/*
 * By default every scalar field is a multi-value register that CONFLICTS when two
 * peers write it concurrently. That is the honest default (okf/concepts/join.md:
 * a content field conflicts "unless a join is declared") but it is the wrong
 * answer for some fields, and wrong in a way users feel immediately - two editors
 * nudging blocks around a canvas should not conflict on every move.
 *
 * -- The policy is a READ-TIME projection, not a merge rule --
 *
 * This is the load-bearing design decision. The policy is applied by `flatten`
 * and `conflicts`, NOT by `join`. So:
 *
 *   - Two peers running DIFFERENT policies still merge to byte-identical
 *     documents. They may display different resolutions, which is a UI
 *     difference, not a data divergence.
 *   - Nothing is destroyed. Both values stay in the document; a policy chooses
 *     which to show. Changing the policy later re-resolves old data correctly.
 *   - Convergence stays a property of the join alone, exactly as
 *     okf/concepts/join.md claims. A policy that could change the merge would
 *     quietly make convergence depend on configuration.
 */
enum class FieldJoin {
    /* Both values survive; the reader gets a Conflict. The default, and the only
     * option that never discards anything. */
    Conflict,

    /* One value wins, chosen deterministically from the values - so every peer
     * picks the SAME one - but arbitrarily with respect to meaning.
     *
     * NOT "last writer wins" — that is `Latest`, below, which ranks by a Lamport
     * stamp rather than a wall clock (okf/concepts/history-graph.md forbids
     * consulting one). Pick ranks by nothing but the bytes, so it claims nothing.
     *
     * Right for: a value with no meaning to rank by — a random id, a cached
     * thumbnail. Wrong for anything a user would be upset to lose. */
    Pick,

    /* The numeric maximum. Genuinely commutative, associative and idempotent, so
     * it discards nothing meaningful - useful for a high-water mark or a counter
     * that only climbs. Non-numeric values fall back to Conflict. */
    Max,

    /* The latest write in the only sense a distributed system can mean it, with no
     * clock: the value under the highest LAMPORT stamp.
     *
     * A replica's counter is a Lamport counter (SPEC §5.7): every merge moves it past
     * every counter it has seen, so a tag `<id>_<n>` minted after SEEING a write
     * always carries a larger `n` than that write's. The value shown is the one whose
     * live tag has the largest `n`, then the larger replica id, then the larger
     * tag - a total order every peer computes identically from the document alone.
     *
     * What that order promises, and what it does not:
     *   - a write made after seeing another write outranks it. That is "last" as a
     *     person can perceive it: I saw your move, then I moved.
     *   - between two writes neither saw, the order is arbitrary but agreed: the
     *     device that had seen more writes wins, then the id. Nobody's clock, however
     *     wrong, can move it.
     *   - it is not a security property. A peer that mints enormous counters wins
     *     every Latest field it writes. Use it for view state - placement, sizes,
     *     collapsed flags - never for anything a member could be harmed by losing.
     *
     * Replaces nothing: `Pick` stays, for fields where even this much meaning is
     * more than the data has. */
    Latest,
};

struct JoinPolicy {
    /* Field name -> how to resolve it. Names are the enriched document's field
     * keys: "placement", "spirit.name", "content.<key>", "domain", "id".
     * A trailing "*" matches a prefix, so "content.*" covers every content key. */
    std::map<std::string, FieldJoin> fields;

    /* What an undeclared field gets. Conflict, and it should stay Conflict -
     * anything else means a field nobody thought about silently loses data. */
    FieldJoin fallback = FieldJoin::Conflict;

    FieldJoin lookup(const std::string& field) const;

    /* The policy a Void Core host almost certainly wants: `placement` is view
     * state that Core already carves out of undo, so two peers moving a rune
     * converge on the later move (`Latest`) rather than argue; a mantle's `id` is
     * a random identity with nothing to choose between (`Pick`). Everything else
     * conflicts. SPEC §5.10. */
    static JoinPolicy core_defaults();
};


}  // namespace voidpalabra
