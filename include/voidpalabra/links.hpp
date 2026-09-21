/* links.hpp — rules about LINKS that an application declares, checked after a merge.
 *
 * ── The general statement ──
 *
 * anomaly.hpp says it first: a join is a union of independent objects, so any rule
 * relating two objects can hold on every device and fail on the merge. The anomalies
 * there are the rules Void Core states. This file is the same idea for the rules an
 * APPLICATION states about its links, declared per relation label the way a
 * JoinPolicy declares per field — because which relations mean "one wire per port"
 * or "at most one parent" is the application's knowledge, and this library must not
 * learn it (the Allomone lesson).
 *
 * Three shapes of rule cover a surprising amount of ground, and each is a pure
 * function of one state document, so every peer holding the same document computes
 * the same answer with no coordination:
 *
 *   EQUIVALENCE  links under these labels mean "these two are one thing". Read as the
 *                smallest equivalence relation containing them — the join of two
 *                equivalence relations is the transitive closure of their union,
 *                which is commutative, associative and idempotent, so the PARTITION
 *                LATTICE is itself a join-semilattice and fusing never conflicts.
 *                A wire fused by two concurrent rewrites; two contacts merged as one
 *                person by two members at once (A says x≡y, B says y≡z: x≡y≡z); an
 *                alias; a duplicate a member marked as the same as another. The
 *                result is a QUOTIENT: every rune maps to its class's representative.
 *
 *   CAPACITY     at most N live links may occupy one SLOT. One wire per port; one
 *                booking per seat; an input consumed by at most one rewrite (a
 *                double spend); one assignee per task; one parent per node. Each
 *                device kept the rule; the merge of two devices' links breaks it,
 *                and the break is reported with every competing link, never
 *                resolved by picking one.
 *
 *   ACYCLIC      links under these labels must not form a directed cycle. Two
 *                members each move one folder into the other (Kleppmann et al., "A
 *                highly-available move operation for replicated trees"): valid on
 *                both devices, a cycle after the merge. Also prerequisites,
 *                "depends on", containment of boxes inside boxes.
 *
 * ── Why report, not repair ──
 *
 * The literature splits invariants into those you AVOID by coordinating first and
 * those you REPAIR after the merge (Balegas et al., "Putting Consistency Back into
 * Eventual Consistency"; IPA, VLDB 2019). Avoidance is the application's — claims,
 * locks, a turn — because it needs the network and a person's patience. Repair has a
 * deterministic half and a human half. The deterministic half is here: every peer
 * finds the same violations and names them with the same hash. The human half —
 * which booking stands, which parent wins — is a decision, and an automatic answer
 * would be the kind of silent loss conflict.md forbids. An application that DOES
 * have a deterministic repair (an interaction net re-deriving a rewrite) applies it
 * as an ordinary edit, and the violation disappears when the state no longer breaks
 * the rule — exactly like an anomaly.
 *
 * ── How links are read ──
 *
 * Over a Void Core state document — `Replica::flatten()`, any past version in an
 * archive, or a document that never saw a merge. An endpoint is a rune name in the
 * link's own mantle, or `{mantle, rune}` (VoidCore SPEC §3.7), resolved to the
 * rune's `spirit.id`. A link whose endpoint resolves to nothing, or to two runes
 * sharing a name, is SKIPPED: those are `link_broken` and `duplicate_name`, reported
 * by `anomalies`, and counting them here too would report one problem twice under two
 * names. Normative as SPEC §5.11; pinned by conformance/cases/23-links.json.
 */
#pragma once

#include <map>
#include <string>
#include <vector>

#include "voidpalabra/canonical.hpp"

struct cJSON;

namespace voidpalabra {

struct RuneRef {
    std::string mantle;
    std::string id;  // spirit.id
    bool operator<(const RuneRef& o) const {
        return mantle != o.mantle ? mantle < o.mantle : id < o.id;
    }
    bool operator==(const RuneRef& o) const { return mantle == o.mantle && id == o.id; }
};

/* One live link with both ends resolved. */
struct Link {
    std::string mantle;    // whose `layout.edges` holds it
    std::string relation;
    RuneRef from, to;
    std::string bytes;     // the link's canonical encoding: its identity
};

/* Every link in `state` whose two ends resolve, in canonical order. */
std::vector<Link> resolved_links(const cJSON* state);

/* Which part of a link occupies what. */
enum class Slot {
    to,     // the rune at the `to` end — "at most N things point here"
    from,   // the rune at the `from` end — "this points at at most N things"
    ends,   // the rune at either end, once per end
    /* (rune, port) at either end, the ports read from a relation "i:j" — the
     * convention Void Core's `reduce` uses for interaction nets (`i` at `from`, `j`
     * at `to`). A link whose relation is not "i:j" occupies no port. */
    ports,
    from_port,  // (rune, i) at the `from` end only — when only one side has ports
    to_port,    // (rune, j) at the `to` end only
};

/* A relation-label pattern: exact, or a trailing `*` for a prefix ("*" is every
 * label, "" included). */
bool label_matches(const std::string& pattern, const std::string& relation);

struct Capacity {
    std::string name;             // the host's word for the rule; named in a violation
    std::string relation = "*";   // which links count
    Slot slot = Slot::to;
    std::size_t max = 1;
    /* Count on each rune's EQUIVALENCE CLASS rather than the rune: "a fused wire has
     * at most two ends" counts the ends of every segment fused into it. */
    bool through_equivalence = false;
};

struct LinkRules {
    /* Label patterns. A link under an equivalence label FUSES and never OCCUPIES:
     * no capacity rule counts it, whatever the capacity's own pattern says, because
     * "these two are one" is not a use of either. */
    std::vector<std::string> equivalence;
    std::vector<Capacity> capacity;
    std::vector<std::string> acyclic;      // label patterns; direction is from -> to
};

/* ── equivalence ──────────────────────────────────────────────────────────── */

struct Quotient {
    /* Only runes in a class of two or more. The representative is the least
     * member by (mantle, spirit.id) — arbitrary, but the same on every peer. */
    std::map<RuneRef, RuneRef> rep;
    /* The classes of two or more, members sorted, classes sorted by representative. */
    std::vector<std::vector<RuneRef>> classes;

    /* `r` itself when it is in no class. */
    RuneRef representative(const RuneRef& r) const;
};

Quotient quotient(const cJSON* state, const LinkRules& rules);

/* ── violations ───────────────────────────────────────────────────────────── */

enum class ViolationKind {
    over_capacity,  // a slot holds more links than its rule allows
    cycle,          // links under an acyclic label close a loop
};

struct Violation {
    ViolationKind kind = ViolationKind::over_capacity;
    std::string rule;                // Capacity::name, or the acyclic pattern
    /* over_capacity: the slot — the rune (its class representative, when counted
     * through equivalence) and, for ports, ":<port>". cycle: empty. */
    std::string mantle;
    std::string slot;
    std::vector<std::string> runes;  // spirit.ids involved, sorted, distinct
    std::vector<std::string> links;  // the competing links' canonical bytes, sorted

    /* Named like a conflict or an anomaly: two peers seeing the same violation
     * compute the same hash, so it can be stored, dismissed and referred to. */
    Digest hash() const;
};

/* Every violation of `rules` in `state`, in a deterministic order. */
std::vector<Violation> check_links(const cJSON* state, const LinkRules& rules);

/* {kind, rule, mantle?, slot?, runes, links:[edge JSON]}. Caller owns the tree. */
cJSON* violation_to_json(const Violation& v);

}  // namespace voidpalabra
