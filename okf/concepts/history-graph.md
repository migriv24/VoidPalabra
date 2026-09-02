---
type: Concept
title: History graph
description: The partial order of utterances — a Merkle-DAG that is its own logical clock. Not a log, not a timeline, and not required for convergence.
resource: src/utterance/history.cpp
tags: [status:current, audience:dev, audience:library, confidence:measured, foundation]
timestamp: 2026-08-27T00:00:00Z
---

The **history graph** is the set of all [utterances](/concepts/utterance.md) a peer
holds, ordered by the parent relation. It is a **Merkle-DAG**, and it is a
**partial order** — not a log, not a timeline, and not a chain.

# What "partial order" buys

Two utterances are **concurrent** when neither is reachable from the other. In a
linear log this situation cannot be represented: one of them must be written down
first, and the reader cannot tell whether that ordering was *real* (a dependency)
or *arbitrary* (an accident of arrival).

The history graph keeps them apart. Concurrency is recorded as concurrency.

> This is the entire argument in one sentence: **a linear history stores an
> arbitrary choice as though it were a fact, and merge is hard precisely because
> it has to guess which orderings were real.**

Formally this is a Mazurkiewicz trace — an equivalence class of sequences modulo
commutation of independent actions ([academic
foundations](/references/academic-foundations.md) §1.1).

# The graph is the clock

Because each utterance names its parents by hash, the graph *is* the logical clock.
No timestamps participate in the causal structure. Consequences:

- **No vector clocks.** Their size is O(peers); a mesh has unbounded peers.
- **No session state.** A peer needs no memory of what any other peer has seen.
- **Transport may be terrible.** Drops, reordering and duplication are all
  survivable, because causality is carried *in* the data, not in the channel.
  **Measured 2026-08-27**, which is the point at which that stopped being a
  claim: a twelve-utterance graph delivered in forty randomized orders, and in
  every one the receiver reached the same cut name *and* the same rendered
  timeline. Delivering everything twice costs nothing.
- **Self-verifying.** A hash mismatch is detectable without trusting the sender.

Wall-clock time may still be *recorded* on an utterance as metadata, for human
display. It must never be *consulted* to decide causality or to resolve a
[conflict](/concepts/conflict.md).

# There is no main branch

A branch, in git, is a named mutable pointer that makes one line of history
privileged. Palabra has no such thing, by the author's ruling (2026-07-24): these
applications are meant to share information *with as little barrier as possible*,
and a privileged branch is a barrier.

What replaces it:
- **Heads** — the maximal elements of the partial order. A peer may have several,
  and having several is a normal resting state, not a problem to be fixed. Heads form
  an **antichain**, so their count is a lower bound on the graph's **width**
  (Dilworth) — and *"several heads is normal"* is exactly *"width > 1 is normal"*.
  Width is cheap to compute and its **chain decomposition is the honest rendering**:
  a width-`w` history draws as `w` lanes with nothing interleaved arbitrarily, which
  is better than a linear extension and no harder
  ([academic foundations](/references/academic-foundations.md) §3.2).

  Built: `heads()`, and the **linear extension** with a canonical tiebreak. **Not
  built: the width and the chain decomposition.** `heads().size()` is offered as
  the lower bound and is documented as exactly that, because reporting a lower
  bound as a width would be the sort of quiet wrong answer this bundle refuses.
  The maximum antichain needs a bipartite matching and is worth doing when
  something renders a graph.
- **Tags** — names attached to a [cut](/concepts/version-as-cut.md). Meaningful,
  optional, and non-privileged. Whether a tag can carry authority (`admin`) is
  open — see [open questions](/design/open-questions.md) §5.

Matrix's room-state DAG is the production precedent: it merges concurrent state
across servers with **no consensus algorithm and no central authority**, and its
event graph has been independently analyzed as a CRDT.

# What a node must have before it is held

Built with one refusal that is worth stating as a rule, because it is what makes
the graph a *clock* rather than a bag: **an utterance whose parents are not all
present is not admitted.** A node whose ancestry is absent has no position in the
partial order, so holding it would mean carrying something unplaceable while
reporting a complete history — which is exactly the property a Merkle clock exists
to provide.

The unplaceable node is **handed back to the caller** rather than discarded. That
is what a peer does with a message it cannot yet place: hold it, and ask for the
region it is missing. Discarding it would turn a recoverable ordering problem into
a permanent gap, and silently keeping it would turn one into a lie.

A second refusal, from the same instinct: an utterance whose recomputed digest
does not match the name it arrived under is refused outright. A content address
that is not checked is not a content address — everything downstream trusts the
name, so one bad pairing admitted is every later verification agreeing with it.

# History is optional

**The load-bearing architectural claim.** Convergence comes from the
[join](/concepts/join.md) on *state*, not from the history graph. Two peers who
both hold the current state can merge with **no utterances at all**.

The history graph therefore buys:
- time travel (recover any [cut](/concepts/version-as-cut.md)),
- blame and audit (who said what, and after what),
- efficient selective sync (fetch only the missing region).

It does **not** buy convergence. That separation is what lets an ESP32 and a data
server be peers in the same protocol — see [peer and tier](/concepts/peer-and-tier.md).

# Status

**`current` as of 2026-08-27**, unblocked by `VoidCore:SPEC.md §6.2`.
`src/utterance/history.cpp`, normative in [SPEC.md](../../SPEC.md) §8.3–§8.4.

Built: the graph, heads, ancestry and concurrency, cut names, the linear extension
with its canonical tiebreak, out-of-order receive with retry, and a verifying JSON
round trip. Not built: **width / chain decomposition**, **selective sync** (that is
[reconciliation](/concepts/reconciliation.md), Phase 4), and the **system-level
inverse-of-undo**, which is still unnamed
([open questions](/design/open-questions.md) §1).
