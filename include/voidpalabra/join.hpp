/* join.hpp — the merge law. Phase 1.
 *
 * Convergence comes from HERE, not from history (okf/concepts/join.md). Every
 * versioned thing declares a join satisfying three laws:
 *
 *     commutative   a ⊔ b = b ⊔ a          the order you sync in does not matter
 *     associative   (a⊔b)⊔c = a⊔(b⊔c)      how you group merges does not matter
 *     idempotent    a ⊔ a = a              receiving twice is free
 *
 * Those three ARE order-independence, as a theorem. And a join needs only the two
 * current states — which is why history is optional and an ESP32 storing zero
 * utterances still converges.
 *
 * ── The one structural consequence, stated up front ──
 *
 * A join over plain Void Core state is impossible. Two peers holding {a} and {}
 * cannot tell "I never had a" from "I removed a", so any join of bare state is
 * union, and removes never propagate. Distinguishing them requires per-element
 * metadata that lives BESIDE Core's state document.
 *
 * So Palabra merges an ENRICHED document, and `enrich`/`flatten` are the seam.
 * This is why the roadmap puts the join before persistence: the container format
 * has to store this metadata, and you cannot design a format for data whose shape
 * you have not decided.
 *
 * ── Why unique tags rather than version vectors ──
 *
 * The metadata here is a unique tag per add (Shapiro's original OR-Set), not a
 * per-replica counter. Version vectors are O(peers) and a mesh has unbounded
 * peers — okf/concepts/history-graph.md rejects them for exactly this reason.
 * Tags are O(adds), which is a growth problem rather than a scaling wall, and it
 * is the growth problem already recorded as open-questions.md §5.
 *
 * Void Core's randomly-minted ids are the tag source. That "accident of Core's
 * design" now pays for itself a third time — conflict-free concurrent creation,
 * O(n log n) graph canonicalization, and now observed-remove without a peer
 * registry.
 */
#pragma once

/* The umbrella header for the CRDT layer. Include this to get everything, or one
 * of the pieces below when a translation unit only needs part of it:
 *
 *   crdt/orset.hpp      the primitive, and the source of the three laws
 *   crdt/policy.hpp     declared per-field joins (read-time resolution)
 *   crdt/document.hpp   enrich / join / flatten, and editing
 *   crdt/conflict.hpp   a conflict as an addressed value
 *   crdt/sequence.hpp   ordered content (Fugue)
 */

#include "voidpalabra/crdt/orset.hpp"
#include "voidpalabra/crdt/policy.hpp"
#include "voidpalabra/crdt/document.hpp"
#include "voidpalabra/crdt/conflict.hpp"
#include "voidpalabra/crdt/sequence.hpp"
