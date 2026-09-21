---
type: Concept
title: Concurrent structure
description: What happens when two people rewrite one structure at once — a node graph, a tree, a booking sheet — and why every case has an answer that needs no coordination, provided the application writes its changes as effects whose footprints are disjoint. Equivalence, capacity and acyclicity as declared link rules; the Lamport stamp behind "last one wins"; provenance from tags.
resource: src/canonical/links.cpp
tags: [status:current, audience:dev, audience:library, confidence:measured]
timestamp: 2026-09-20T00:00:00Z
---

The question arrived from Void Maiz on 2026-09-20, about interaction nets edited by
several people at once, with the author's framing: *"the order of interaction should
not affect the final result … nodes that were selected at the same time can be gone,
missing, duplicated, split, transformed … for each of those scenarios, isn't there an
answer?"* and *"think of the rune/mantle/holiday structure as our greatest asset in
building these systems that often run into conflicts with linear based
architectures."*

There is an answer to each, and it is not specific to interaction nets. It is the
same answer for a folder tree two people reorganize, a seat two people book, two
contacts two people merge, and a wire two rewrites share.

# The one principle

> **Concurrent changes commute exactly when neither deletes or overwrites the part the
> other depends on.**

That is *parallel independence* from double-pushout graph rewriting (Ehrig et al.;
the Local Church–Rosser theorem), stated for replicas. It says where every difficulty
comes from, and so where every fix must go: **not in the join** — the
[join](/concepts/join.md) composes what it is given, correctly — but in **what each
device writes**. A change that writes only NEW things, and ADDS facts relating them to
old ones, has a footprint no concurrent change can overlap. A change that edits a
shared thing in place has a footprint every neighbour's change overlaps.

Maiz measured the failure precisely: two disjoint redexes `(a,b)` and `(c,d)`, an
auxiliary wire between `a` and `c`. Written as edits over links, peer 1 replaces the
link `a–c` with `a'–c`, peer 2 replaces it with `a–c'`, and the merge holds two broken
halves and never the `a'–c'` the mathematics requires. Each peer overwrote the shared
link: overlapping footprints. `tests/links_test.cpp` keeps that failure measured.

# The fix, as three declared rules

Palabra does not know what a wire, a seat or a folder is, and must not (the Allomone
lesson). What it can provide is three kinds of rule an application **declares per
relation label**, each a pure function of one state document, so every peer computes
the same answer with no coordination. Normative as `SPEC.md` §5.11.

| rule | what it answers | why it needs no coordination |
|---|---|---|
| **equivalence** | "these two are one thing" — a wire fused by a rewrite, two contacts merged, an alias | equivalence relations form the **partition lattice**; the join of two is the closure of their union, which is commutative, associative and idempotent. Two peers fusing different pairs never conflict: A says x≡y, B says y≡z, the merge says x≡y≡z |
| **capacity** | "at most N may occupy this slot" — one wire per port, one booking per seat, one parent per node, one consumer per resource | each device kept the rule; the merge is CHECKED, and a violation is a value with a hash, identical on every peer, listing every competing link |
| **acyclic** | "these links never close a loop" — folder containment, prerequisites, boxes in boxes | the Kleppmann et al. move problem (two moves each valid, a cycle after the merge), detected deterministically instead of undone |

## The shared wire, solved without a chase

HVM2 (Taelin, §5, *Substitution Map & Atomic Linker*) meets the shared-wire case in
shared memory and answers it with **wires as variables**: a rewrite never writes "A is
joined to B", only a substitution for its own end, and a reader chases the
substitutions. Maiz proposed the replicated version: end-slots as registers, each
written only by the rewrite that consumes its agent.

The partition-lattice form (Maiz's second formulation) is strictly simpler, and it is
the one adopted:

- **A wire is a rune** of a glyph the application declares (option (a) of Maiz's §3.5.1).
  Its identity is its `spirit.id`, immutable and random, surviving `enrich` and
  `flatten` without any format change.
- **An agent's port attaches to a wire** with a link `"i:0"`.
- **A rewrite writes only new things**: the agents it mints, a new wire segment for
  each boundary it inherits, and a **fusion link** (`=`) from each new segment to the
  old wire. It removes the agents it consumed. It never touches a wire another rewrite
  could also touch.
- **A reader takes the quotient**: every rune maps to its class representative, and a
  wire's ends are the live attachments of every segment in its class.

§3.1 again: peer 1 writes `a'.1 → w1`, `w1 = w`; peer 2 writes `c'.1 → w2`, `w2 = w`.
The merge holds `{w, w1, w2}` as one class whose live ends are `a'.1` and `c'.1`.
Nobody wrote that wire; it is read from what both wrote. The capacity rule *a wire has
two ends* (counted through equivalence) is satisfied, *one wire per port* is
satisfied, and there is no broken link to report, because nothing was ever
overwritten.

Why not the chase: end-slot registers are overwritten, so they need the
one-writer-per-slot argument, which holds only in systems with one principal port.
Fusion links are only ever added, so the partition form works for any rewriting
system, needs no chain-termination argument, and a cycle of fusions is harmless (it is
just a class).

# The author's scenarios, and the general cases behind them

| scenario | in an interaction net | the general case | answer |
|---|---|---|---|
| **overlap**: two rewrites want one node | impossible: one principal port per agent means no critical pairs (Lafont) | two rules match overlapping parts; two bookings of one seat | **capacity**, reported: a rule with critical pairs must CONSUME BY LINKING (`consumed-by`, capacity 1 at the consumed thing) rather than by deleting — deleting destroys the evidence the merge needs. Avoidance before the fact is the application's (claims) |
| **duplicated**: the same rewrite fired twice | idempotent when minted agents take ids derived from the redex | the same change made on two devices | the OR-set holds one value under two tags; provenance (below) lists both writers. Random ids break this — conflict.md's "compare previews, never commits" |
| **gone**: my input was consumed elsewhere | the rewrite that ate it came first in no meaningful sense | an edit to something deleted | `deleted_while_edited` ([conflict](/concepts/conflict.md)) when the removal did not see the edit; a capacity violation when two consumptions were both recorded |
| **split / transformed**: disjoint rewrites sharing a boundary | the shared wire | two refactors touching one interface | **equivalence**: write the boundary as a class and fuse, never overwrite |
| **missing**: an edit arrives before the thing it edits | a delta naming `a'` before `a'` | any delta | already handled: SPEC §5.3, an edit may land before its creation |

# Decisions on Maiz's §3.5

1. **Wire identity: (a), wires as runes, is the normative answer**, not a prototype.
   (b), an `id` on Core edges, would be a migration of every document for something
   runes already provide; and Core edges are values in an OR-set, identified by their
   whole content, which is exactly what makes a link idempotent to add twice. (c), an
   identity minted by this library, would be a library learning its host's domain.
2. **The chase lives here, the meaning lives there.** The quotient and the checks are
   generic (`links.hpp`: `quotient`, `check_links`); which labels fuse, and what
   occupies a port, is declared by the application — the same split as a `JoinPolicy`.
3. **Effects, not intentions.** Replicating firings and replaying them would make the
   document a derivation that every reader must recompute, and it would make history
   mandatory — against this library's founding rule that *history is optional;
   convergence is not* (an ESP32 holding zero utterances must still converge). The
   intention belongs in the [utterance](/concepts/utterance.md) — *why* the state
   changed, for history and blame — and the state holds the effects.
4. **Merge-by-reduction and the quotient are one principle at two times.** Before a
   commit, comparing pure previews (conflict.md) and claims (Maiz's presence) make
   footprints disjoint in advance: *avoidance*. After a commit, fusion makes a shared
   boundary disjoint by construction, and the checks find what avoidance missed:
   *repair*, in the sense of Balegas et al. (*Explicit Consistency*, EuroSys 2015;
   *IPA*, VLDB 2019). The deterministic half of repair is here; the human half — which
   booking stands — is a decision, and is never made automatically.

# "Last one wins", honestly

A replica's counter is now a **Lamport clock** (SPEC §5.7): every merge moves it past
every counter it has seen, capped at 2^40 so a hostile peer cannot exhaust it. A tag
minted after seeing a write therefore outranks that write, and `FieldJoin::Latest`
shows the value under the greatest stamp — by counter, then writer, then tag. No wall
clock, no coordination, and the same answer on every peer. Without the Lamport rule a
new device would lose every concurrent move to a busy one forever; the test that says
so is `a_new_device_is_not_outranked_forever_by_a_busy_one`.

`core_defaults()` now declares `placement` Latest, and a mantle's `id` Pick: three
devices each running `mantle new team` left a standing conflict asking which random
string to keep — a question with no answer.

# Who wrote this

Every tag names its writer, so `writers(place)` answers "who wrote each live value"
with no new record: a canvas flashes a changed node in its author's colour, a conflict
row says whose each side is, an audit view lists who created a rune. It names
**replicas**, not people, and it is a claim until frames are signed.

# What is not solved

- **Automatic repair.** A violation is reported, never fixed. An application with a
  deterministic repair (re-deriving a rewrite) applies it as an ordinary edit.
- **Cost.** Each check is O(links) over a flattened state, run after a merge. Fine at
  canvas scale; unmeasured at a million links.
- **Growth.** A fused segment is never removed — removing one could split a class
  another peer is still fusing onto — so a net that reduces for a long time accumulates
  wire runes. Compacting them is safe only at a point every peer has passed, which is
  the same open problem as tag growth ([open questions](/design/open-questions.md) §5).
- **Names.** Links resolve by name, as Core writes them. A link to a name two runes
  share is skipped here and reported as `duplicate_name`.

# Status

`current` as of 2026-09-20. `tests/links_test.cpp` (the shared wire, its failing
single-link encoding, a port, a seat, sameness, a move cycle, a 20,000-link chain, the
Lamport stamp, provenance) and `conformance/cases/23-links.json`,
`25-lamport-and-latest.json`.
