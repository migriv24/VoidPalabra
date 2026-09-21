---
type: Roadmap
title: Roadmap
description: What is not yet done, in the order it should be done, and what blocks what. Shipped work lives in the log and in each concept's status.
tags: [status:planned, audience:dev, confidence:tentative, roadmap]
timestamp: 2026-07-27T00:00:00Z
---

**Convention, inherited from Void Core: this file lists only what is _not yet
done_.** When something ships it moves to [log](/log.md) and its concept page
earns `status:current` with a `resource:` link. The roadmap had drifted into
listing completed work with checkmarks; that is what a log is for, and a roadmap
that celebrates is a roadmap that stops being read.

The ordering rule, unchanged since founding:

> **Every rung must earn its keep with zero peers.**
>
> If a step is only valuable once sync works, it is scheduled too early. Sync is
> the furthest thing out and the most likely to slip; nothing else may depend on
> it to be worth having.

---

# Where things stand

| | status | remaining |
|---|---|---|
| **Rung 0** — [canonical form](/concepts/canonical-form.md) | built + [76 conformance vectors](../conformance/README.md) | Unicode normalization; the C ABI, when a non-C++ consumer exists |
| **Phase 1** — [join](/concepts/join.md) | built — per-field joins and the sequence CRDT | a text-shaped sequence API, if a character editor ever needs one |
| **Phase 2** — [persistence](/concepts/persistence.md) | store, container, structural chunking and file I/O built | codecs; platform mapping |
| **client surface** — [archive](/concepts/archive.md) | built, incl. atomic file I/O | — |
| **Phase 3** — [history graph](/concepts/history-graph.md) | utterances, the graph, cuts and the linear extension built — **unblocked 2026-08-27** | replay; inverses; width / chain decomposition |
| **Phase 4** — [reconciliation](/concepts/reconciliation.md) | the [replica](/concepts/replica.md), the door (2026-09-16), [anomalies](/concepts/anomaly.md), [content references](/concepts/content-reference.md) (2026-09-18) and the [sync session](/concepts/sync-session.md) (2026-09-19), [concurrent structure](/concepts/concurrent-structure.md) and the stream envelope (2026-09-20) built | protocol unblocked; transport blocked on trust, and the socket layer is platform code outside this library ([integration](/design/integration.md) §7) |
| **Phase 5** — the CLI surface | not started | deliberately last |

Language: **C++20, CMake, zero dependencies**, behind a C ABI when one is needed.

---

# The next things, in order

This is the plan. Everything below this section is detail or is blocked.

## 1. ~~File I/O and atomic writes~~ — **done 2026-07-27**

`Archive::write_file` / `read_file`, write-temp-then-rename with the temporary
beside the target so the rename is atomic, and distinguished errors. Details in
[archive](/concepts/archive.md). It was the gap between "built" and "usable".

Still open under it: a **path policy** — where an archive lives per platform.
Desktop works; Android scoped storage, iOS sandboxes and browser OPFS are their
own work and should not be guessed at from here.

## 2. ~~The sequence CRDT (Fugue)~~ — **done 2026-07-27**

Built as two OrSets, so it inherited the three laws and needed no new merge code.
Details in [join](/concepts/join.md). The measured property: ids that sort to
`axbycz` merge to `abcxyz`.

Still open under it: **a text-shaped API**. The current surface is
insert/erase-by-index over elements, which suits Hormiga's newsletter *blocks*. A
character-level editor would want run-length operations, and building that on
per-character nodes would be wasteful.

## 3. ~~The reconciliation state machine~~ — **built 2026-09-19; the risk is now measured**

The [sync session](/concepts/sync-session.md): whole-state anti-entropy, the export set,
files by hash, presence, and a signature slot, as a pure state machine. Thirty simulated
schedules of four peers under 30% loss, duplication, reordering and a partition — thirty
converged, after the first run found a handshake deadlock. What remains under it:
**range reconciliation** to cut the bytes, and **one file ledger per device** rather than
per session. Neither changes what converges.

### What it was, kept for the record

**Its two prerequisites landed 2026-09-16, pulled forward by a real client.** A
machine that exchanges state needs something persistent to exchange —
the [replica](/concepts/replica.md), which is also what makes a removal propagate —
and a door that refuses what a peer should not have sent ([SPEC.md](../SPEC.md) §5.6).
Both were built before the machine because the client's own sync loop needed them
immediately; the machine itself is still not started. What it must now also carry is
recorded in [transport shape](/design/transport-shape.md): bounded parsing, a
transition on elapsed time for every waiting state, and a second message class for
ephemeral presence.

Per [transport shape](/design/transport-shape.md) §3, the protocol's *algebra* is
not blocked by the trust model; only the *transport* is. So the pure state
machine — `step(local, incoming) -> (local', [outgoing])` — can be built and
tested now, against an in-process harness of *n* peers with adversarial drop,
duplicate, reorder, partition and heal.

**Why here rather than first:** every other claim in this bundle has been
measured, and this one has not. [History graph](/concepts/history-graph.md) asserts that Palabra
survives a transport that *"may drop, reorder and duplicate"*; the join suite
proves convergence under arbitrary **merge** order, which is not the same thing
as arbitrary **message** order. Building this converts the project's largest
remaining assumption into a test, with no socket and no security exposure.

Conditions, from [open questions](/design/open-questions.md) §6: signature and
capability slots present from day one, nothing leaves the process, hostile peer
assumed throughout.

## 4. The trust model — **the long pole, and it is not code**

Nothing in Phase 4 ships without it, and it is design work rather than
implementation: adopt **Meadowcap** or not, decide what a capability is scoped to,
settle whether a tag can carry authority (probably not — see
[open questions](/design/open-questions.md) §6), and answer key rotation with no
central authority.

**Why fourth rather than later:** it gates the chatroom, it is the one item where
thinking time cannot be compressed by writing code faster, and Hormiga's
libsodium is already vendored and built — so the implementation, when the design
lands, is unusually cheap.

---

# Was blocked on Void Core — **cleared 2026-08-27**

Both items shipped in Core 0.2.8 (`VoidCore:SPEC.md §6.2`), and Phase 3's first
rung was built the same day.

| need | Core's status | what it unblocked |
|---|---|---|
| **Reified commands + the pure/effectful split** | **shipped**, `SPEC.md §6.2`: a command journal beside undo, and a closed list of effectful verbs | [utterance](/concepts/utterance.md), [history graph](/concepts/history-graph.md), [version as cut](/concepts/version-as-cut.md) — all `status:current` |
| **Scope of the undoable slice** | **half-answered.** *Which* commands cross the holiday boundary is normative; what `undo` should *do* about one is not | nothing further — Palabra's answer (`mantles` only) was already recorded and is unaffected |

Core's stance — *resolve pure-vs-effectful before building it* — was right, and
waiting cost nothing: there was more than enough unblocked work, and the rung took
one day once the data existed.

**Still owed by Core, and neither blocks anything:** a `slice` value distinguishing
`mantles` from `active` (costs a redundant utterance today), and *n*-entries-per-
batch (costs false conflicts at device two). Both in
[open questions](/design/open-questions.md) §3.1.

---

# Remaining under built layers

Small, and none of it is on the critical path.

**Rung 0.** Unicode normalization ([open questions](/design/open-questions.md)
§8) — the one known way the built code can violate its own requirement; it bites
first when a mac and a Windows device sync. The **C ABI** when a non-C++ consumer
appears; writing one now would be speculative surface.

**Phase 1.** Nothing on the critical path. A **text-shaped sequence API** (run-length
operations rather than per-element insert/erase) if a character-level editor ever
becomes a client; Hormiga's newsletter blocks do not need it.

**Phase 2.** **Extract `BlockStore` to an interface** so an application can back
storage with its own — Hormiga's SQLite is the obvious second implementation, and
[integration](/design/integration.md) §3 argues content addressing is what makes
the substitution safe. Worth doing when that second implementation exists and not
before: an interface with one implementation is a guess about the second.
**Codecs** (glyph → bytes → glyph, for content that is not JSON) and
**platform mapping**. Also: the **container format belongs in
[SPEC.md](../SPEC.md)**, which currently specifies the canonical form and the
enriched document but not the file layout.

---

# Phase 3 — the history graph

**No longer gated.** Rung 1 shipped 2026-08-27; what remains is listed below it.

1. ~~[Utterances](/concepts/utterance.md): content-addressed, parents-by-hash.~~ **done**
2. ~~The [history graph](/concepts/history-graph.md) as a Merkle-clock.~~ **done**
3. ~~[Versions as cuts](/concepts/version-as-cut.md), named by Merkle hash.~~ **done**
4. Linear extension **done** with its canonical tiebreak; the **chain
   decomposition** (Dilworth; [academic foundations](/references/academic-foundations.md)
   §3.2) is **not** — it needs a bipartite matching and is worth doing when
   something renders a graph. `heads().size()` is offered meanwhile as the lower
   bound it actually is.
5. The system-level inverse-of-undo (append an inverting utterance). **Not
   started**, and still unnamed — [open questions](/design/open-questions.md) §1.
6. **Replay** — applying an utterance to a Void Core state. Deliberately *not*
   Palabra's: it needs Void Core, which this library does not link, and an
   utterance carries `command` plus `minted` precisely so the replayer can live in
   the application. The first application to want it will say what shape it wants.
7. **Persisting a history into an [archive](/concepts/archive.md).** The graph
   round-trips through JSON today; folding it into the container is the obvious
   next small thing, and it is what makes time travel survive a restart.

**Note what Phase 3 is *not* needed for.** Local version tracking already works
without it — [archive](/concepts/archive.md) — because on one device the history
genuinely is a sequence. Phase 3 buys the **partial** order, which starts
mattering at device two.

---

# Phase 4 — reconciliation

1. The **pure state machine** — see "the next three things" §2. **Not blocked.**
2. **Trust** — signed *deltas* (what travels), public-key identity, capabilities
   with regions decidable from the change alone. **The gate.** Six findings from the
   first real request, two of which rule out the obvious design, are in
   [open questions](/design/open-questions.md) §6.1. Revocation poisoning honest
   descendants is the one that blocks shipping.
3. [Reconciliation](/concepts/reconciliation.md): range-based set reconciliation.
4. [Tier](/concepts/peer-and-tier.md) and capability declaration; State / Recent /
   Full peers, plus [compute](/concepts/compute.md) capability on one statement.
5. One transport — LAN first, mDNS + QUIC, as a holiday. **Blocked on 2.**

---

# Phase 5 — the CLI surface

Agents must be able to drive all of the above, which means dispatcher verbs.
Deliberately **last**, and the reason is stronger than it looks: applications will
**extend Palabra's CLI into their own Voidscript frameworks**, so these verbs
appear inside every adopting app. The vocabulary is a compatibility surface, not a
convenience ([integration](/design/integration.md) §4).

**Settled early, because it costs nothing:** every capability is reachable through
Core's `effect` seam *before* it is a verb. Applications integrate today with no
naming commitment, and the verbs become sugar over calls that already work — the
correct order for a decision this expensive to reverse.

---

# Alongside, not in sequence — compute

[Compute](/concepts/compute.md) does not form a chain with the others.

- **Prompt-as-addressable-scry** needs only Phase 3's cut names — no transport, no
  trust model — and pays for itself on one device (reproducible agent runs). It
  could be pulled forward the moment cuts had names, and **as of 2026-08-27 they
  do** (`c:…`, [version as cut](/concepts/version-as-cut.md)). Nothing in it is
  blocked any more; it is simply not scheduled.
- **Capability declaration and routing** rides Phase 4 and inherits its gate.

Model loading, inference and agent scheduling are **not Palabra's**. Since
2026-07-27 the first two have an owner — **Void Bicho**, a sibling library with
its own bundle and roadmap. Palabra is built first, by the author's ruling, and
**neither imports the other**, so nothing here waits on anything there.

---

# Explicitly out of scope

- **Versioning the reduction itself** (morphisms in a PROP). Genuinely novel,
  genuinely far, needed by no client.
- **Void Buzz's constrained radio links.** Deferred by the author; the `State`
  tier is the seam where they will attach.
- **Becoming a server.** Palabra is peer-to-peer or nothing.
- **Learned models as load-bearing components** — answered rather than scheduled
  ([world models](/design/world-models.md)). If one is ever added it goes at
  conflict *resolution*, pruning policy, peer scheduling, or semantic search, and
  nowhere else.

---

# Research still worth doing

Two of the five items here were answered on 2026-07-27 and are struck.

1. **Prototype pruning against a real workload.** Metadata growth
   ([open questions](/design/open-questions.md) §5) is still the most likely thing
   to sink the design at scale, and the only open question that cannot be reasoned
   out. Now cheap to attempt: the join is built, so a long simulated edit history
   can be run against it and measured.
2. **Read Merkle-CRDTs properly** — the closest single match to Phase 3; likely
   shortens it.
3. **Read Pijul's actual model**, not its marketing — specifically whether patch
   commutation applies to keyed records or only to sequences.
4. ~~Confirm the unordered-set shortcut~~ — **answered** by Core, `SPEC.md §4`:
   rune order is not semantic.
5. ~~Critical-pair analysis as a Palabra-side lint~~ — **unblocked** rather than
   answered. It decides when merge-by-normal-form is legitimate, and was moot
   while reduction ids diverged; Core fixed that, so this is now worth doing —
   subject to comparing pure previews rather than committed mantles
   ([open questions](/design/open-questions.md) §3.2).
