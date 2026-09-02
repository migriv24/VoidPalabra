---
type: Reference
title: Academic foundations
description: The mathematics Void Palabra rests on, translated into Palabra's own terms — why history is a partial order, where convergence comes from, and what confluence does and does not buy.
tags: [status:planned, audience:dev, confidence:asserted, reference]
timestamp: 2026-07-24T00:00:00Z
---

# Academic Foundations

> **Why this page is long and explicit.** Void Palabra departs from the version
> control model every reader already has. An agent reading this bundle will
> silently reconstruct git — snapshots, a main branch, a linear log — unless the
> alternative is spelled out *and justified*. Each section below states the
> mathematics, then states **what it means for Palabra specifically**. The second
> half is the part that matters; the citation is only there so the claim can be
> checked.

**Verification status.** Every source in §1–§6 was fetched or searched on
2026-07-24 and the claims below reflect what was actually read. Where the
literature disagrees, that is said out loud (see §4). Nothing here has been
implemented, so all "Palabra does X" statements are design intent.

**§3.1, §3.2, §9 and §10 were added 2026-07-27** and are of a different kind: they
are **standard, settled order theory and topology** (Birkhoff, Dilworth,
Dushnik–Miller, Alexandrov) recalled from knowledge rather than freshly fetched, and
stated only where the textbook result is unambiguous. The *applications* to Palabra
in those sections are new and are design intent like everything else. §10 records a
**defect** in the existing design rather than a foundation.

---

## 1. History is a partial order — four independent formalisms agree

The author's instinct — *"why does version control need to be sequential? treating
things as existing in a 1-dimensional axis of time seems like it won't be the best
architecture"* — is not a preference. Four separate research traditions arrived at
it, from different directions.

### 1.1 Mazurkiewicz traces / the trace monoid (1977)

Given an **independence relation** on actions, a history is an *equivalence class
of sequences modulo commuting adjacent independent actions*. Two actions are
independent iff performing them in either order yields the same state. The
resulting algebra is the **free partially commutative monoid** (trace monoid); a
trace is equivalently a **labelled partial order**.

**For Palabra:** this is the exact object Palabra stores. A
[history graph](/concepts/history-graph.md) is a trace, not a word. Note the
pleasing fit: `VoidCore:/concepts/rune.md` already declares a rune to be a
**monoid**. A trace monoid is a monoid with a commutation relation bolted on —
Palabra is not importing a foreign algebra, it is adding *independence* to one
Core already has.

> The practical payoff is subtractive: recording a partial order **discards
> arbitrary information** (which of two independent edits "happened first") while
> **keeping every real dependency**. A linear log stores a lie with confidence.

### 1.2 Categorical patch theory — Mimram & Di Giusto (2013), and Pijul

A repository state is not a sequence of patches but a **set** of them closed under
dependency. Patches are morphisms; merge is a **pushout**. The crucial technical
move: the category of files and patches **does not have all pushouts**, so one
takes its **free finite conservative cocompletion** — and the objects this adds
*are the conflicts*.

**For Palabra:** this is where [conflict](/concepts/conflict.md) as a first-class
value comes from, and it is not an ergonomic choice — it is what makes the algebra
**total**. If conflicts are errors, merge is a partial function and the whole
structure breaks. If conflicts are objects, merge is total and the graph is
closed. This also happens to be exactly Void Core's honesty principle (*never a
silent wrong answer*) falling out of a category-theoretic requirement.

### 1.3 Merkle-clocks — Sanjuán, Pöyhtäri, Teixeira & Psaras (2020)

*Merkle-CRDTs: Merkle-DAGs meet CRDTs.* The result Palabra leans on hardest: **a
Merkle-DAG can act as a logical clock.** Because each node names its parents by
hash, causality is *structural* — it is read off the DAG, not tracked alongside
it. This replaces vector clocks, whose size is O(peers) and which therefore die in
a mesh.

**For Palabra:** [utterances](/concepts/utterance.md) name their parents by hash,
and that *is* the clock. No wall-clock timestamps in the causal structure, no
version vectors, no peer registry. It also means causality survives a transport
that "may drop, reorder, or duplicate messages" — which is the only kind of
transport a mesh of devices actually has.

### 1.4 Multiway systems — Wolfram model / Gorard

A **multiway graph** contains *every* state reachable under *every* rule-application
ordering — branches that diverge and rejoin. This is, almost literally, the
author's picture: "a graph that stores multiple different states and aspects of
some given project."

**For Palabra:** useful as the *picture*, and as vocabulary for reasoning about
branches rejoining. **Not** load-bearing — the Wolfram model carries physics
commitments Palabra has no use for. Cited because it is the clearest published
visualization of "version history as a branching graph of states," and because it
is where the confluence caveat in §4 comes from.

---

## 2. Where convergence actually comes from — the join-semilattice

**This is the single most important section.** It is also the one that decides the
Δ-state CRDT question.

A **state-based CRDT** is a value in a **join-semilattice**: a partial order with a
least-upper-bound operation (`join`, `⊔`) that is

- **commutative** — `a ⊔ b = b ⊔ a` (order of peers doesn't matter)
- **associative** — `(a ⊔ b) ⊔ c = a ⊔ (b ⊔ c)` (grouping doesn't matter)
- **idempotent** — `a ⊔ a = a` (receiving the same thing twice is free)

Those three laws *are* order-independence. Not "usually converges" — converges, by
construction. A **Δ-state CRDT** (Almeida, Shoker & Baquero, 2014/2018) keeps the
laws but ships small *deltas* instead of whole states.

### 2.1 The decision (asked for explicitly, 2026-07-24)

**Adopted:**
- **The join-semilattice as Palabra's merge law.** Every versioned thing declares a
  join satisfying the three laws above. See [join](/concepts/join.md).
- **Δ-state, not full-state, on the wire.** Same laws, bounded message size.
- **Merkle-clocks instead of vector clocks** (§1.3) — O(1) per utterance, and
  self-verifying.

**Rejected:**
- **Last-writer-wins as the default resolution.** LWW is a silent wrong answer —
  it discards one side's work and reports success. It also smuggles **wall-clock
  time back in as the arbiter**, which is precisely the thing this design is
  trying to escape. LWW may be opted into per field; it is never the default.
- **Operation-based CRDTs as the foundation.** They require exactly-once,
  causally-ordered delivery — a guarantee a mesh cannot give cheaply.

**The consequence that unlocks the constrained-device problem:**

> A join needs only **the two current states**. It does not need history.
> Therefore **history is optional for correctness** — a device that stores no
> utterances at all still converges.

This is the direct answer to the research agent's objection (*"you cannot store a
month of patch algebra history on an ESP32"*). Correct — and under a state-based
join it does not have to. History buys time-travel, blame, and selective sync; it
does not buy convergence. That separation is what makes
[tiered peers](/concepts/peer-and-tier.md) possible **inside one protocol** rather
than requiring a second protocol for small devices.

### 2.2 The honest cost

CRDT metadata does not shrink for free. Tombstones and causal context accumulate,
and pruning them safely requires knowing what every peer has seen — which in an
open mesh you do not know. **This is an unsolved problem in Palabra, not a solved
one.** See [open questions](/design/open-questions.md) §4.

---

## 3. What "a version" is, and how to get a timeline back

The author's requirement: *"we have to give a concrete answer"* to "what version is
this application on?", while *"still being able to translate to time-based norms."*
Both are satisfiable, and the definitions come straight from §1.

- **A version is a downward-closed subset** of the history partial order — a
  *configuration* in event-structure terms, a *dependency-closed set of patches* in
  Pijul's, an *element of the lattice* in the CRDT's. A **cut**, not a point in
  time.
- **Its name is the Merkle hash of that set.** Deterministic, order-independent,
  and comparable: two peers who received the same utterances in opposite orders
  compute the *same name*. See [version as cut](/concepts/version-as-cut.md).
- **A timeline is a linear extension** (topological sort) of the partial order,
  made deterministic by a canonical tiebreak (hash order) among independent
  utterances.

> **The translation, in one line:** the partial order is the truth; a timeline is
> one of many valid *readings* of it, computed for a human. Same data, different
> projection — which is what `VoidCore:/concepts/scry.md` already is.

This is also why the three faces in the README are not three systems: an agent's
"recent relevant changes" is a *filtered downward-closed set*, a human's timeline
is a *linear extension*, and the system's history is the *whole order*. Three
scry projections of one object.

### 3.1 Birkhoff — the space of versions is a distributive lattice

Added 2026-07-27. This was the largest gap in this page: §3 defines a *version* but
says nothing about the structure of the set of *all* versions. It has one, and it is
a strong one.

The downward-closed subsets of a poset `P`, ordered by inclusion, form a
**distributive lattice** `J(P)` — meet is intersection, join is union.
**Birkhoff's representation theorem** (1937) says the converse: every finite
distributive lattice is `J(P)` for a unique poset `P`, recoverable as its
join-irreducible elements.

**For Palabra**, three consequences, in increasing order of usefulness:

1. **The set of all versions of a repo is a distributive lattice, completely
   determined by the history graph.** Meet is the greatest common cut (what git
   gropes for with "merge base"); join is merge. Nothing else needs storing — the
   version space *is* the history poset, viewed differently.
2. **Distributivity is why three-way-merge intuitions partly survive here.**
   `a ⊓ (b ⊔ c) = (a ⊓ b) ⊔ (a ⊓ c)` is a real law about cuts, and it is what makes
   "find the common ancestor, then reconcile" a coherent operation rather than a
   heuristic — the difference being that Palabra does not *need* it, since
   [join](/concepts/join.md) is total.
3. **An [utterance](/concepts/utterance.md) is precisely a join-irreducible
   version** — one that cannot be written as the merge of two strictly smaller
   versions. This is the sharpest available definition of "atomic change," and it is
   a **theorem rather than a design choice**. It also independently confirms the
   granularity ruling: a collapsed `batch` is join-*reducible*, so it is not an atom,
   so recording it as one is a category error and not merely a lossy convenience.

### 3.2 Dimension and width — the exact sense in which this is not one-dimensional

The author's framing — *"treating things as existing in a 1-dimensional axis of
time"* — is not loose. There are two precise notions, and they say different useful
things.

**Order dimension** (Dushnik–Miller, 1941): the fewest linear extensions whose
intersection reconstructs the partial order. A chain has dimension **1**. Any
antichain of size ≥ 2 forces dimension ≥ **2**.

> **Git is exactly dimension 1.** Palabra's histories are not. That is the entire
> claim, stated as a number rather than as an intuition — and "how many timelines
> would you need to lay side by side to lose nothing" is a genuinely clarifying way
> to ask it.

Computing order dimension is NP-hard for `k ≥ 3`, so it is **vocabulary, not a
metric**. Do not build a feature on it.

**Width** (Dilworth, 1950): the size of the largest antichain. **Dilworth's theorem**
says a poset of width `w` decomposes into exactly `w` chains, and unlike dimension
this is **computable in polynomial time** (via bipartite matching / König's theorem).

**For Palabra**, width is the one to implement, because it is directly operational:

- It is the honest measure of **how much genuine concurrency** a history contains.
- The **heads** ([history graph](/concepts/history-graph.md)) form an antichain, so
  head count is a lower bound on width — and "several heads is a normal resting
  state" becomes "width > 1 is a normal resting state."
- **Dilworth's decomposition is the rendering algorithm for Void Maiz**: a width-`w`
  history displays as `w` swim lanes, with no arbitrary interleaving anywhere. That
  is strictly more honest than a single timeline and no harder to draw.

So the projection story in §3 has a middle term that was missing: the human face can
be given a **linear extension** (one lane, arbitrary tiebreak) *or* a **chain
decomposition** (`w` lanes, nothing invented). The second is better and is now
cheap to compute.

---

## 4. Confluence — what it buys, and the caveat

Requested directly by the author. Confluence is the **dynamic** counterpart to §1's
commutation: commutation says independent actions may be reordered; confluence says
divergent *reduction paths* rejoin.

- **Church–Rosser / confluence:** for all `b`, `c` reachable from `a`, there is a
  `d` reachable from both.
- **Newman's lemma (1942):** *local* confluence + termination ⇒ *global*
  confluence. This is the practical lever — you check a local property and get a
  global guarantee.
- **Critical pairs (Knuth–Bendix; Huet 1980):** for terminating systems, local
  confluence reduces to joinability of critical pairs, obtained by unifying rule
  left-hand sides. This is the **decision procedure** for "will my rewrite rules
  merge cleanly?"
- **Plump — critical pairs in *term graph* rewriting (1994):** the right citation
  for Core, because a mantle is a **graph with sharing**, not a term. Two caveats
  that matter: in term *graph* rewriting, joinability of critical pairs is **not
  sufficient** for local confluence — one needs *strong* joinability — and
  confluence of general graph rewriting is **undecidable**.

**For Palabra:**
1. Where Core's `VoidCore:/concepts/reduce.md` stays in its
   **restricted confluent subset** (≤1 rule per glyph pair), merged reactive state
   has a **canonical normal form** — merge is a theorem.
2. Void Core's roadmap explicitly plans to **leave that subset** (general
   sub-pattern / tag-expression LHS *without* the confluence guarantee). So this
   guarantee is **conditional and must be flag-gated**, not assumed. Palabra must
   record, per mantle, whether its rule set is in the confluent fragment — and
   degrade to explicit conflict when it is not.
3. Critical-pair analysis is therefore a **Palabra-side lint**, not a Core feature.

**The caveat, stated because the literature is not unanimous:** confluence and the
Wolfram model's **causal invariance** are *different properties*. SetReplace's
research notes give explicit counterexamples in **both** directions (a confluent
system that is not causally invariant, and vice versa), while other Wolfram-model
writing describes confluence as necessary-but-not-sufficient for causal invariance.
**Palabra needs confluence** (same final state), **not** causal invariance (same
causal graph). Do not import the latter by association.

---

## 5. Storage — content addressing that makes diff cheap

**Prolly trees** ("probabilistic B-trees", coined by the Noms team; used by Dolt):
a content-addressed B-tree whose nodes are split **by content rather than by
size**. This buys three things at once — B-tree read/write performance, **O(changes)
diff** between two versions, and **structural sharing** (any block common to two
versions is stored once).

**For Palabra:** this is the concrete answer to "content-address a mantle so that
diff and sync cost scale with what changed, not with how big the mantle is." It is
more immediately useful than any of the four pillars in §1 and requires no new
theory — it is engineering to copy. It also answers Hormiga's asset-dedup problem
(`.miga` currently inlines assets as base64 because there is no dedup).

Content-defined chunking is what makes the hash of a version stable under
insertions — the property a size-split B-tree does not have.

---

## 6. What the sync protocol should be

**Range-based set reconciliation** (Meyer, 2023; used by Willow and Iroh): to
reconcile two sets, exchange a **fingerprint** over a range; if the fingerprints
match, that range is done. If not, split the range and recurse. Small sets are sent
outright.

**For Palabra:** this is the [reconciliation](/concepts/reconciliation.md)
primitive. It matters because it is **stateless with respect to peers** — no
knowledge of what the other side has seen, no session state, no peer registry.
Which is exactly the constraint Latin-OS's standing note imposes on Void Core:
*"no design may assume a single device, a single client, or a central scheduler."*

---

## 7. The PROP question — its honest home

Hormiga's proposal invoked **PROPs** (products-and-permutations categories,
Lafont's diagrammatic algebra) as foundation. The author's note is correct: PROPs
are the algebra of systems with **many inputs and outputs**.

Stated precisely, so it is neither over- nor under-claimed:

- Void Core's `reduce/` **is** genuinely a PROP presented by generators and
  relations. That is real.
- But a PROP is the algebra of the **reduction layer**. Palabra's *store* versions
  runes, tags, and content — a monoid plus a graph, not a PROP. **PROPs do not
  constrain the v1 store**, and should not be cited to justify decisions about it.
- **Where PROPs do belong in Palabra:** the multi-in/multi-out objects here are
  **devices and sync sessions**. A peer has ports; a sync session composes two
  peers' states; the *permutation* part is the statement that **which peer you
  sync with first does not matter** — which is the same commutativity as §2's
  join, seen one level up. Operads of wiring diagrams (Spivak) and network models
  (Baez–Foley–Moeller–Pollard) are the grammar for composing sync topologies.

So: PROPs are the algebra of **communication topology**, not of the database. Both
are real; conflating them produces confident nonsense.

---

## 8. Homotopical patch theory — read it, do not build on it

*Homotopical Patch Theory* (Angiuli, Morehouse, Licata & Harper, ICFP 2014):
patches as **paths** in a space of states, patch laws as higher paths, in homotopy
type theory. It is the rigorous version of "a history is a path, not a pile."

**For Palabra:** assurance about **shape**, not a design input. Its known
limitation is that it is a proof-of-concept over a toy repository model and has not
scaled to a practical VCS. Pijul's shipping implementation is grounded in §1.2's
patch category, not in HoTT. Read it to know the shape is real; do not wait for it,
and do not cite it to justify an implementation choice.

---

## 9. Topology — real, and modest

Added 2026-07-27, in answer to the author's *"I think topology is also important?"*
It is, in a limited and specific way, and the limit is worth stating so the
vocabulary is not over-drawn.

**The Alexandrov topology** on a poset takes the downward-closed sets as the **open
sets**. So:

| order-theoretic | topological |
|---|---|
| a [cut](/concepts/version-as-cut.md) | an **open set** |
| merge (`⊔`) | **union** |
| greatest common cut | **intersection** |
| a single [utterance](/concepts/utterance.md) with its ancestry | a **basic open** |

This is a genuine correspondence, not an analogy — and it is exactly why §3.1's
lattice is distributive (a topology always is). But note what it costs and buys:
**it is the same information, re-labelled.** It supplies vocabulary — locality,
neighbourhood, gluing — and **no new theorems for v1**. Use it to think; do not cite
it to decide.

**Sheaves are the one place it could pay.** A **sheaf** assigns data to open sets
with restriction maps, and asks whether local sections that agree on overlaps
**glue** into a global section. That is the sync problem stated precisely: each peer
holds a local section over the cut it can see, and convergence is the existence of a
global section. The obstruction to gluing is measured by **H¹** — so
"[conflict](/concepts/conflict.md)" and "nonzero first cohomology" are the same
phenomenon under two names, and §1.2's cocompletion result is the algebraic version
of the same repair.

**Why this is a check and not a tool:** in a join-semilattice, **associativity** means
pairwise agreement already implies global agreement, so once conflicts are objects
the obstruction vanishes by construction. The sheaf view therefore *confirms* the
design in [join](/concepts/join.md) and [conflict](/concepts/conflict.md) rather than
extending it. Its value is diagnostic — if a future feature ever makes merge
order-sensitive or partial again, H¹ is where the damage would show up, and that is
worth being able to name.

**Where topology genuinely bites is elsewhere: interaction nets.** A net's identity
is its **wiring up to deformation**, not the order its agents were listed — which is
why the `swap` flag is load-bearing (index-straight δδ and index-swapped γγ are
different pictures, and with one flavour they are indistinguishable;
`VoidCore:okf/concepts/reduce.md`, 2026-07-14). That has a hard consequence for
Palabra: **the content address of a mantle must be invariant under exactly the
deformations that do not change meaning, and under nothing else.** See §10.

## 10. Canonicalization — the hard problem Palabra does not have

Hashing a graph so that isomorphic graphs agree is **graph canonicalization**, for
which no polynomial algorithm is known. Discovering this late would be serious: it
sits underneath version names, reconciliation fingerprints, and conflict
determinism.

**Palabra avoids it by accident of Core's design.** Runes carry a `spirit.id` — a
frozen, unique, randomly minted identifier — so a mantle is a **vertex-labeled**
graph, and canonicalizing a labeled graph is a sort: `O(n log n)`, no isomorphism
testing. This is the second time Core's random IDs pay for themselves (the first is
in §2, where they make concurrent creation conflict-free for free), and it is a
constraint Palabra now places back on Core.

**The place the luck runs out** — agents minted *during reduction* are not authored,
so no utterance records their identity, and two peers reducing the same net
independently produce different labels and therefore different hashes for identical
state. This invalidates merge-by-normal-form as written in §4/[conflict](/concepts/conflict.md)
until reduction mints identities deterministically from the redex. Full statement and
the proposed fix: [canonical form](/concepts/canonical-form.md).

---

# Sources

- [A Categorical Theory of Patches — Mimram & Di Giusto](https://arxiv.org/abs/1311.3903) ([PDF](https://www.lix.polytechnique.fr/~smimram/docs/mimram_ctp.pdf))
- [Merkle-CRDTs: Merkle-DAGs meet CRDTs — Sanjuán, Pöyhtäri, Teixeira & Psaras](https://arxiv.org/abs/2004.00107)
- [Efficient State-based CRDTs by Delta-Mutation — Almeida, Shoker & Baquero](https://arxiv.org/abs/1410.2803)
- [Newman's lemma](https://en.wikipedia.org/wiki/Newman's_lemma) · [Critical Pairs in Term Graph Rewriting — Plump](https://eprints.whiterose.ac.uk/id/eprint/148076/1/Plump.MFCS.94.pdf)
- [Confluence and Causal Invariance — SetReplace research notes](https://github.com/maxitg/SetReplace/blob/master/Research/ConfluenceAndCausalInvariance/ConfluenceAndCausalInvariance.md)
- [Prolly Trees — Dolt documentation](https://docs.dolthub.com/architecture/storage-engine/prolly-tree) · [DoltHub blog](https://www.dolthub.com/blog/2024-03-03-prolly-trees/)
- [Range-Based Set Reconciliation — Meyer](https://arxiv.org/pdf/2212.13567) · [Willow's RBSR spec](https://willowprotocol.org/specs/rbsr/index.html)
- [Concurrency Traces (Mazurkiewicz trace theory)](https://link.springer.com/content/pdf/10.1007/978-3-662-64821-6_4)
- [Some Quantum Mechanical Properties of the Wolfram Model — Gorard](https://content.wolfram.com/sites/13/2020/07/29-2-2.pdf)
- [Birkhoff's representation theorem](https://en.wikipedia.org/wiki/Birkhoff%27s_representation_theorem) — §3.1; the version space as a distributive lattice
- [Dilworth's theorem](https://en.wikipedia.org/wiki/Dilworth%27s_theorem) · [Order dimension (Dushnik–Miller)](https://en.wikipedia.org/wiki/Order_dimension) — §3.2; width is computable, dimension is vocabulary
- [Alexandrov topology](https://en.wikipedia.org/wiki/Alexandrov_topology) — §9; cuts as open sets
- [Elementary Applied Topology — Ghrist](https://www2.math.upenn.edu/~ghrist/notes.html) — §9; the readable entry point for sheaves as data fusion
- [Graph canonization](https://en.wikipedia.org/wiki/Graph_canonization) — §10; the problem Core's `spirit.id` removes
