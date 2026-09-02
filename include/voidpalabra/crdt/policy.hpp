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

    /* One value wins, chosen deterministically from the tags - so every peer
     * picks the SAME one - but arbitrarily with respect to meaning.
     *
     * NOT "last writer wins". You cannot implement last-writer-wins without a
     * wall clock, and okf/concepts/history-graph.md forbids consulting wall clock
     * to resolve anything. There is no "last" here, so the name does not claim
     * one. join.md says LWW is "acceptable" for view state; this is what that
     * actually means once the clock is removed.
     *
     * Right for: placement, a cursor, a cached thumbnail. Wrong for anything a
     * user would be upset to lose. */
    Pick,

    /* The numeric maximum. Genuinely commutative, associative and idempotent, so
     * it discards nothing meaningful - useful for a high-water mark or a counter
     * that only climbs. Non-numeric values fall back to Conflict. */
    Max,
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
     * should converge rather than argue. Everything else conflicts. */
    static JoinPolicy core_defaults();
};


}  // namespace voidpalabra
