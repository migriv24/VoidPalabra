---
type: Concept
title: Join
description: The merge law every versioned thing must declare — commutative, associative, idempotent. Where Palabra's convergence actually comes from, and why it needs no history.
resource: src/crdt/document.cpp
tags: [status:current, audience:dev, audience:library, confidence:asserted, foundation]
timestamp: 2026-07-24T00:00:00Z
---

The **join** (`⊔`) is Palabra's merge law. Every versioned thing — a rune's
content field, a tag set, a mantle's rune collection — **must declare a join**, and
that join **must** satisfy three laws:

| law | statement | what it buys |
|---|---|---|
| **commutative** | `a ⊔ b = b ⊔ a` | the order you sync with peers does not matter |
| **associative** | `(a ⊔ b) ⊔ c = a ⊔ (b ⊔ c)` | how you group merges does not matter |
| **idempotent** | `a ⊔ a = a` | receiving the same thing twice is free |

Together these make the state a **join-semilattice**, and they are not a
convention — they *are* order-independence, as a theorem. This is the formal
content of the author's "don't think linearly": with these three laws, there is
nothing left for an order to change.

Two structural notes that follow, added 2026-07-27:

- **The version space is a distributive lattice** (Birkhoff), determined entirely by
  the history poset — so joins have meets, and *"the greatest common cut"* is a real
  operation rather than git's "merge base" heuristic. And **an
  [utterance](/concepts/utterance.md) is precisely a join-irreducible version**, which
  is the sharpest definition of "atomic change" available and is a theorem rather
  than a choice ([academic foundations](/references/academic-foundations.md) §3.1).
- **Idempotence and commutativity are only true up to bytes.** Two states that *mean*
  the same thing but serialize differently are, to a join, two different states. So
  the three laws are not testable — and not true — without
  [canonical form](/concepts/canonical-form.md) underneath them. That is why it is
  Rung 0 and this is Phase 1.

# This is where convergence comes from

**Not** from the [history graph](/concepts/history-graph.md). The history graph
carries causality, audit and time travel. Convergence is entirely a property of the
join.

The consequence is the most important structural fact in this bundle:

> A join needs only **the two current states**. Therefore **two peers converge
> without sharing any history at all.**

That is the answer to *"you cannot store a month of patch history on an ESP32."*
Correct — and it does not need to. See [peer and tier](/concepts/peer-and-tier.md).

# The decision: Δ-state, and explicitly not LWW

Decided 2026-07-24 at the author's request. Full reasoning in
[academic foundations](/references/academic-foundations.md) §2.

**Adopted** — **Δ-state CRDTs**: the three laws hold, but peers ship small
*deltas* rather than whole states, so message size is bounded by what changed.

**Rejected** — **last-writer-wins as a default.** Two reasons, either sufficient:
1. LWW **discards one side's work and reports success** — a silent wrong answer,
   which Void Core's honesty principle forbids outright.
2. LWW decides by **wall-clock timestamp**, smuggling linear time back in as the
   arbiter — the exact thing this architecture exists to escape.

LWW may be **opted into per field** where a field genuinely has no meaningful
merge (a cursor position, a cached thumbnail). It is never the default, and
choosing it is a declaration, not an accident.

# Joins per Core type

Sketches, not settled. Each needs its own page once built.

| Core thing | proposed join | note |
|---|---|---|
| **tag set** | OR-Set (add/remove with causal context) | tags are a set; this is the textbook case |
| **content field** (scalar) | **conflict** unless a join is declared | the honest default — see [conflict](/concepts/conflict.md) |
| **rune collection** in a mantle | OR-Set keyed by `spirit.id` | random IDs make concurrent creation conflict-free *for free* |
| **`layout.edges`** | OR-Set of edges | edges may dangle already (`VoidCore:SPEC.md §3.7`); endpoints are **mutable names**, so resolve to `spirit.id` before comparing |
| **an ordered content field** | a sequence CRDT (Fugue) — **per-glyph opt-in** | replaces "rune order"; see below |
| **`placement`** (the view slice) | **`Pick`** — see below; "LWW" is the wrong name | it is view state; Core already carves it out of undo |

# Declared per-field joins — and why "LWW" cannot exist here

**Built 2026-07-27.** A field's join is *declared* by the host through a
`JoinPolicy`, and two design points are load-bearing.

**The policy is a read-time projection, not a merge rule.** It is applied by
`flatten` and `conflicts`, never by `join`. So two peers running **different**
policies still merge to byte-identical documents — they may *display* different
resolutions, which is a UI difference rather than a data divergence. Nothing is
destroyed either: both values stay in the document and a policy chooses which to
show, so changing the policy later re-resolves old data correctly. Had the policy
touched the merge, convergence would silently depend on configuration, which would
undo the claim this page exists to make.

**And the honest naming.** This page used to say LWW is "acceptable" for
`placement`. Implementing it revealed that **last-writer-wins cannot be built
here at all**: there is no "last" without a wall clock, and
[history graph](/concepts/history-graph.md) forbids consulting wall clock to
resolve anything. What is actually available is a **deterministic but arbitrary
pick** — every peer chooses the same value, for reasons having nothing to do with
meaning or recency. The enum says `Pick`, not `LWW`, because a name that promised
recency would be a small lie told at every call site.

| policy | resolves by | right for |
|---|---|---|
| `Conflict` *(default)* | nothing — both values stand | anything a user would be upset to lose |
| `Pick` | deterministic order over the values | `placement`, a cursor, a cached thumbnail |
| `Max` | numeric maximum — genuinely commutative | a high-water mark or a counter that only climbs |

`Max` over a non-numeric value **falls back to `Conflict`** rather than guessing: a
declaration that does not match the data is a misconfiguration, and picking
silently would hide it behind plausible output. The fallback for an undeclared
field stays `Conflict`, permanently — a field nobody thought about silently losing
data is the exact failure this design exists to prevent.

# Ordered content — built, and non-interleaving is the point

**Settled 2026-07-27** by Void Core: rune order is **not semantic**
(`VoidCore:SPEC.md §4`), and *"an application that genuinely needs an ordering
MUST make it explicit in a content field"*. So a mantle is an unordered keyed set
— the easy case — and the hard case moved into a **content field** rather than
disappearing. Void Hormiga's **newsletter block order** is that case, and Hormiga
is going multi-user.

**Built the same day.** The property that matters is not convergence:

> Convergence is the easy half. If one editor types `abc` at a spot while another
> types `xyz` at the same spot, a naive list CRDT (RGA, Logoot) can converge on
> **`axbycz`** — every peer agrees, and the result is nonsense nobody wrote.

**Fugue** (Weidner & Kleppmann, 2023) prevents it with a **tree** rather than a
total order over identifiers. Inserting after `left` makes the new element
`left`'s *right child* when it has none; otherwise it becomes the *left child* of
whatever currently follows. The list is the in-order traversal. A run typed by one
person is therefore a **chain** — each element the right child of the previous —
so it is one contiguous subtree, and subtrees are emitted whole. The traversal has
no way to enter one run and leave it mid-way.

**It needed no new merge code.** A sequence field is two OrSets — `nodes` (tag =
the element's own id) and `dead` — so it merges by union and inherits the three
laws from the primitive. `join` did not have to learn anything. Deletion is an
**add** to `dead` rather than a removal from `nodes`, because a removed element
may still be another element's parent: a tombstone is load-bearing structure here,
not litter.

**A sequence is a representation, not a `FieldJoin`.** `FieldJoin` decides how to
*resolve* two concurrent writes to one scalar; a sequence never has that problem,
because concurrent edits merge structurally instead of competing. Orthogonal axes,
and the storage is self-describing so no declaration is needed at read time.

**The test that proves it, and the guard on that test.** Two peers typing runs
concurrently must produce `abcxyz` or `xyzabc`, never an interleaving. But the
obvious fixture gives peer A ids like `Ae_0001` and peer B `Be_0001`, which sort
into two clean groups — so **interleaving would be impossible however broken the
algorithm was**, and the test would pass for the wrong reason. The suite instead
mints ids that *already alternate when sorted* (`00000000`, `00000001`, …), so
sorting alone yields exactly `axbycz`, and asserts the fixture is adversarial
before trusting what it proves. Measured result: **ids that sort to `axbycz`
merge to `abcxyz`**.

# What building it settled

`current` — **built 2026-07-27** (`src/join.cpp`, `include/voidpalabra/join.hpp`);
15 property tests / 789 checks in `tests/join_test.cpp`, laws checked on **random**
documents and compared through the [canonical form](/concepts/canonical-form.md), so
"equal" means byte-identical — the only definition that survives two machines.

**The structural consequence, which the design had not stated:** *a join over plain
Void Core state is impossible.* Two peers holding `{a}` and `{}` cannot tell "I never
had `a`" from "I removed `a`", so any join of bare state is union and **removes never
propagate**. Distinguishing them needs per-element metadata beside Core's document.

So Palabra merges an **enriched document**, and `enrich`/`flatten` are the seam. This
is the concrete vindication of the roadmap reordering: the container format must store
this metadata, and it could not have been designed before this phase.

**Unique tags, not version vectors.** The metadata is one unique tag per add
(Shapiro's original OR-Set). Version vectors are O(peers) and
[history graph](/concepts/history-graph.md) rejects them for that reason; tags are
O(adds) — a growth problem rather than a scaling wall, and it is exactly the growth
already recorded as [open questions](/design/open-questions.md) §5. **Core's random
ids pay for themselves a third time here**: conflict-free concurrent creation,
`O(n log n)` canonicalization, and now observed-remove with no peer registry.

**One primitive, three uses.** An observed-remove set of `tag → value` is the OR-Set
(tags), the multi-value register (a content field), and the keyed OR-Map (runes by
`spirit.id`). `join` is union on both members, so the three laws hold *by
construction* — set union is commutative, associative and idempotent, and nothing in
the structure can break them. That is the whole correctness argument.

**Add-wins is a consequence, not a preference.** A remove can only retire tags it
*observed*; a concurrent add carries a tag the remover never saw, so it survives.

**What the property test caught.** Document-level idempotence failed while the
primitive's held: `canon_doc` was encoding the document *as written*, so two
byte-different representations of the same CRDT value got different names — `a ⊔ a =
a` holding as a **value** and failing as a **name**. `canon_doc` now normalizes every
OrSet first. It is the same failure Rung 0 exists to prevent, one level up, and it was
only visible because the fixture hand-wrote metadata — which is why `set_field` /
`add_tag` / `remove_tag` now exist, so nothing outside the library does that again.

**Still planned:** the sequence CRDT (Fugue) for an ordered content field — a
per-glyph opt-in since Core's SPEC §4 answer, not a foundation — and **resolution as
a new utterance**. That last one is no longer blocked: utterances exist as of
2026-08-27 ([utterance](/concepts/utterance.md)), so recording a resolution as one is
now ordinary work rather than a dependency. The enriched document's shape is in
[SPEC.md](../../SPEC.md) §5.
