---
type: Reference
title: Prior art
description: Systems that already built parts of Void Palabra, what each one settles, and what each one leaves open. The answer to "am I the only one who has built something like this?"
tags: [status:planned, audience:dev, confidence:asserted, reference]
timestamp: 2026-07-24T00:00:00Z
---

> *"I really hope I'm not the only one who's built something like this, and I hope
> you can find some references online for someone who built something similar."*
> — the author, 2026-07-24

**You are not.** Every individual piece of Void Palabra exists in production
somewhere. What does not exist is the *combination*, over *this* data model. That
is the honest position: the risk here is integration risk, not "nobody knows
whether this is possible."

Surveyed 2026-07-24.

---

# The closest single match: Merkle-CRDTs

**Merkle-CRDTs** (Protocol Labs, 2020) is the paper that combines the two halves
Palabra needs — a Merkle-DAG for causality and content addressing, a CRDT for
convergence — and shows the DAG can *serve as the logical clock*. This is
essentially Palabra's storage/sync core, already written down and analyzed.

Shipping implementations:
- **OrbitDB** — distributed P2P database on IPFS, Merkle-CRDT logs.
- **DefraDB** (Source Network) — an actual database built on Merkle-CRDTs.

**Settles:** the architecture is sound and has been built more than once.
**Leaves open:** it says little about pruning, and nothing about Core's data model.

---

# No main branch, in production: Matrix

**Matrix** room state is a **DAG of events with a partial order**, merged by *State
Resolution v2*, with **no consensus algorithm and no central authority**. Every
server computes the same state from the same events regardless of arrival order.
Its event graph has been independently analyzed as a CRDT (Jacob et al.).

This is the strongest evidence that the author's "no main branch" instinct is
practical and not merely elegant: Matrix runs it at scale, including **access
control over an eventually consistent partial order without finality** — which is
precisely Palabra's open question §6.

**Settles:** no-main-branch works in production, including for authority.
**Leaves open:** Matrix's state resolution is notoriously subtle; copy the shape,
not the algorithm.

---

# Patch-based version control: Pijul / Darcs

**Pijul** implements Mimram & Di Giusto's patch category: a repository state is a
*set* of patches, independent patches commute, merge is a pushout, and conflicts
are first-class objects arising from the free cocompletion.

**Settles:** patch-native VCS ships; conflicts-as-objects is implementable.
**Leaves open:** Pijul is built for *text files*, where ordered sequences are the
hard case. Palabra's mantles are largely **unordered keyed sets** — which is
strictly easier — with sequences only where order is semantic. Palabra should
expect to need *less* machinery than Pijul, not more.

---

# Version control over structured data: Dolt

**Dolt** — "git for SQL data": commits, branches, diff and merge over rows, built
on a **commit graph of prolly trees**. It scales to millions of versions and
branches.

**Settles:** versioning structured records (not files) is a solved engineering
problem, and prolly trees are the structure that makes it work.
**Leaves open:** Dolt is deliberately git-shaped — linear commits, a main branch,
a server. Take the **storage engine**, leave the **history model**.

---

# Local-first sync without a server: Willow / Iroh / Earthstar

**Willow** is a protocol family for synchronizable data stores with no third
party, offline operation, and **Meadowcap** — a capability system granting read or
write on a *region* of data with **no central authority**. **Iroh** (v1.0, June
2026) is the QUIC-based P2P layer, with a Willow implementation in progress. Both
use **range-based set reconciliation** (Meyer, 2023).

**Settles:** [reconciliation](/concepts/reconciliation.md) and the trust model.
Meadowcap is very likely what Palabra should adopt rather than invent
([open questions](/design/open-questions.md) §6).
**Leaves open:** Willow versions *documents*, not a graph model with rewrite rules.

---

# CRDTs for application data: Automerge / Yjs

**Automerge** and **Yjs** are the mature op-based CRDT libraries, with real work on
compressed column-oriented storage and efficient sync (Kleppmann). **Fugue**
(Weidner & Kleppmann, 2023) is the current best answer to the sequence problem —
maximal non-interleaving, better than RGA or Logoot for concurrent insertion.

**Settles:** the sequence-CRDT problem ([join](/concepts/join.md)'s hard case) has
a current best answer worth adopting directly.
**Leaves open:** both are op-based, requiring causally-ordered exactly-once
delivery — a guarantee Palabra deliberately declines to need
([academic foundations](/references/academic-foundations.md) §2.1).

Also worth reading: **"Local-first software"** (Kleppmann, Wiggins, van
Hardenberg, McGranaghan, 2019) — short, and effectively the mission statement for
what Hormiga asked for.

---

# The nearest thing to Palabra's actual data model: RDF/graph versioning

Versioning **graphs** rather than files is an active research area — OSTRICH
(delta-based RDF archiving), condensed quad representations annotated with the
versions they are valid in, invertible patches supporting revert and merge with
hash-secured integrity.

**Settles:** graph versioning is a real field with real results.
**Leaves open:** it is mostly *archival* (query the past) rather than
*collaborative* (merge concurrent edits). The two literatures have not really met,
and Palabra sits in the gap.

---

# What is genuinely unbuilt

Stated plainly so it is not mistaken for a solved problem:

1. **Version control over an interaction-net model** with rewrite rules, where
   confluence supplies a canonical merge for reactive state. Nobody has this.
   **Currently blocked, 2026-07-27:** confluence gives peers the same normal form
   *up to renaming*, and reduction mints fresh CSPRNG identities, so peers hash
   identical state differently. Until Core mints deterministically from the redex
   ([open questions](/design/open-questions.md) §3.2) this remains unbuilt for a
   reason that is fixable rather than deep — which is the good kind of blocker, and
   the reason it is the first thing to ask Core for.
2. **A tiered protocol** where a device storing *zero* history is a full
   participant in the same protocol as an archive server. The pieces exist
   (state-based CRDTs need no history; Willow has partial sync) but the explicit
   tier-declaration design is Palabra's own.
3. **The combination** — Merkle-CRDT storage + patch-theoretic conflicts +
   capability-based P2P + a rewrite-rule data model, driven by one CLI that agents
   and humans share.

(1) and (3) are where Palabra's risk lives. Everything else can be borrowed.

# Sources

- [Merkle-CRDTs](https://arxiv.org/abs/2004.00107) · [DefraDB on Merkle-CRDTs](https://open.source.network/blog/how-defradb-uses-merkle-crdts-to-maintain-data-consistency-and-conflict-free)
- [Matrix State Resolution v2](https://matrix.org/docs/older/stateres-v2/) · [Analysis of the Matrix Event Graph RDT](https://publikationen.bibliothek.kit.edu/1000129941/116598252)
- [A Categorical Theory of Patches](https://arxiv.org/abs/1311.3903) · [Pijul theory notes](https://github.com/bitemyapp/Pijul/blob/master/theory.md)
- [Dolt storage engine / prolly trees](https://docs.dolthub.com/architecture/storage-engine/prolly-tree) · [How Dolt scales to millions of versions](https://www.dolthub.com/blog/2025-05-16-millions-of-versions/)
- [Willow Protocol](https://willowprotocol.org/) · [Range-Based Set Reconciliation](https://willowprotocol.org/specs/rbsr/index.html) · [iroh-willow](https://github.com/n0-computer/iroh-willow)
- [Local-first software (Wikipedia overview)](https://en.wikipedia.org/wiki/Local-first_software)
- [Condensed Representation for Snapshot-Based RDF Graphs](https://arxiv.org/html/2506.21203)
