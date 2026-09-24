---
okf_version: "0.1"
---

# Void Palabra — Knowledge Bundle (dev)

An Open Knowledge Format bundle describing **Void Palabra**: the system layer for
Void Core — how state is **remembered** (history), **named** (versions),
**merged** (convergence), **stored** (persistence), and **spoken** between
devices (sync). A library, not an application.

This is the **dev** bundle. **Eleven concepts are built** — [canonical
form](/concepts/canonical-form.md), [join](/concepts/join.md),
[persistence](/concepts/persistence.md) and [archive](/concepts/archive.md) as of
2026-07-27, then [utterance](/concepts/utterance.md), [history
graph](/concepts/history-graph.md) and [version as cut](/concepts/version-as-cut.md)
on 2026-08-27, once Void Core reified commands and closed the pure/effectful
question, then [replica](/concepts/replica.md) on 2026-09-16, when a client's
automatic sync needed deletions to stay deleted, and [anomaly](/concepts/anomaly.md)
and [content reference](/concepts/content-reference.md) on 2026-09-18 — what a merge
breaks that no device broke, and what a document names that it does not hold —
and the [sync session](/concepts/sync-session.md) on 2026-09-19, the pure state machine two
peers run to converge, and [concurrent structure](/concepts/concurrent-structure.md) on
2026-09-20 — why two people rewriting one graph, tree or booking sheet need no
coordination if their changes have disjoint footprints. The first four name **state**; the
next three name **change**; the replica is what keeps a device's state between
exchanges.

Each is `status:current` with a `resource:` link. Everything else is
`status:planned`, per the honesty convention inherited from the Void Core bundle: a
concept may not claim `status:current` without a `resource:` link to the code that
backs it, and `tools/check_okf.py` runs as a ctest suite because a convention is
only a guarantee if something checks it.

**Palabra is C++20** behind a C ABI, CMake, zero dependencies — the same shape as
Void Core's C core, because the consumers are C++ (Hormiga, Maiz) and because a
`State`-tier peer will never run a scripting runtime.

**Origin.** Founded 2026-07-24 out of Void Hormiga's message
`MESSAGE_FOR_VOIDCORE_hormiga-versioning-and-sync-2026-07-23.md` (Part 2), and
the author's ruling that the versioning + sync layer is **its own library**, not
a Void Core subsystem — because Void Core does no file or network I/O by
definition (`VoidCore:SPEC.md §9`), and a store plus a transport are both, in
Core's own vocabulary, holidays.

## Read this first

The two departures below are **load-bearing and counter-intuitive**. An LLM
reading this bundle will default to git's model unless told not to. They are
stated here, argued in [why not linear](/design/why-not-linear.md), and grounded
in [academic foundations](/references/academic-foundations.md).

1. **History is a partial order, not a sequence.** Independent changes are
   genuinely unordered. A timeline is a *linear extension* computed for display,
   not the underlying truth.
2. **History is optional; convergence is not.** Two peers converge because of the
   **[join](/concepts/join.md)** law on state, not because they share a history.
   A device carrying zero history still merges correctly.

# The three pillars

Named by the author 2026-07-27. The bundle is organized by concept rather than by
pillar, so the map is here:

| pillar | what it is | pages |
|---|---|---|
| **Remembering** | non-linear version control | [utterance](/concepts/utterance.md), [history graph](/concepts/history-graph.md), [version as cut](/concepts/version-as-cut.md), [join](/concepts/join.md), [conflict](/concepts/conflict.md), [canonical form](/concepts/canonical-form.md), [persistence](/concepts/persistence.md) |
| **Speaking** | device-to-device communication (LAN first, transport-agnostic by construction) | [peer and tier](/concepts/peer-and-tier.md), [reconciliation](/concepts/reconciliation.md), [transport shape](/design/transport-shape.md) |
| **Computing** | system management: naming, routing and recording compute, including models | [compute](/concepts/compute.md), [world models](/design/world-models.md) |

The third pillar is the newest and the one most likely to swallow the project, so it
opens with a boundary: **Palabra owns the naming, routing and history of compute; it
does not own the computing.** The other half of that sentence is
**Void Bicho** (`VoidBicho:/README.md`), founded 2026-07-27 out of this ruling —
a sibling library that loads and runs models. **Neither imports the other**; they
meet at two data shapes and nothing else.

# Concepts — the vocabulary

* [Canonical form](/concepts/canonical-form.md) - the deterministic bytes and the hash; the rung underneath every other name here
* [Utterance](/concepts/utterance.md) - the atomic unit: a content-addressed change naming its parents (*not* a commit)
* [History graph](/concepts/history-graph.md) - the partial order of utterances; a Merkle-DAG that is its own clock
* [Version as cut](/concepts/version-as-cut.md) - a version is a downward-closed *set*, named by hash — the answer to "what version is this?"
* [Join](/concepts/join.md) - the merge law every versioned thing must declare; where convergence actually comes from
* [Conflict](/concepts/conflict.md) - a first-class state in the graph, never an error and never a silent winner
* [Peer and tier](/concepts/peer-and-tier.md) - who participates and how much they carry (ESP32 → data server, one protocol)
* [Reconciliation](/concepts/reconciliation.md) - the have/want protocol; range-based set reconciliation
* [Persistence](/concepts/persistence.md) - mantles/runes/holidays onto a real filesystem; formats, codecs, mobile
* [Archive](/concepts/archive.md) - save, load and local version tracking; the surface an application actually calls
* [Replica](/concepts/replica.md) - one device's state kept between exchanges, so removals propagate and a sync loop is correct rather than merely frequent
* [Anomaly](/concepts/anomaly.md) - a rule every device kept that a merge broke — duplicate names, broken links, a type removed under its runes
* [Content reference](/concepts/content-reference.md) - what a document names but does not hold; who says what is missing, who fetches, who checks
* [Sync session](/concepts/sync-session.md) - the protocol as a pure state machine: frames and time in, frames and events out; measured under a hostile network
* [Concurrent structure](/concepts/concurrent-structure.md) - concurrent rewrites of one structure: equivalence, capacity and acyclicity as declared link rules; the Lamport stamp behind "last one wins"; who wrote what
* [Compute](/concepts/compute.md) - naming, routing and recording compute across peers; prompts as addressable projections

# Design — the rationale

* [Why not linear](/design/why-not-linear.md) - **the central argument**; the translation table from git vocabulary to Palabra's
* [Transport shape](/design/transport-shape.md) - the protocol is a pure state machine; transports are dumb byte pipes
* [World models](/design/world-models.md) - where a learned model earns its keep, and the three places it does not
* [Integration](/design/integration.md) - the hooks an application implements, the holiday shapes worth generalizing, and the CLI apps will extend
* [Forcing clients](/design/forcing-clients.md) - Hormiga's save system and a LAN chatroom; what each proves and what each blocks on
* [Open questions](/design/open-questions.md) - what is genuinely undecided, and what blocks what

# References

* [Academic foundations](/references/academic-foundations.md) - the mathematics, **translated to Palabra's needs** rather than cited at it
* [Prior art](/references/prior-art.md) - systems that already did parts of this, and what each one settles

# Planned work

* [Roadmap](/roadmap.md) - the rungs, cheapest-useful-first
* [Log](/log.md) - what has changed in this bundle

# Naming

Concept names below are **descriptive and provisional**. A Void-flavored set was
proposed at founding (utterance → *dicho*, peer → *voz*, the graph → *coro*) and
is **not adopted** — the author's call, deferred. See
[open questions](/design/open-questions.md) §1.

# Sibling bundles

Void Core's ontology (rune, mantle, domain, holiday, glyph, dispatcher,
Voidscript, reduce/temper/scry) is defined authoritatively in the Void Core
bundle at `../VoidCore/okf`. Void Maiz's projection concepts live at
`../VoidMaiz/okf`; Void Buzz's at `../Void Buzz/okf`; **Void Bicho's compute
vocabulary at `../VoidBicho/okf`**.

Sibling bundles are named by **relative path**, because that is how the family
actually finds each other — every project in it sits beside the others in one
folder, and an absolute path would be true on exactly one machine. Cross-bundle
*references* are written `VoidCore:/concepts/mantle.md` (plain-text convention —
OKF links are bundle-root-relative, so foreign bundles cannot be linked
directly).

**Palabra owns none of Core's vocabulary.** It does not redefine rune, mantle, or
holiday; it defines only what it takes to remember, name, merge, store, and
transmit them.

## Networking: Reticulum (decided 2026-09-23)

All device-to-device networking in the Void family runs over **Reticulum**, and
Palabra is its Void translation: session frames ride Reticulum links from an
optional companion target, `voidpalabra_reticulum`, while the core stays
zero-dependency and pure. See [Reticulum](/concepts/reticulum.md)
(`status:current` since 2026-09-24) for what was decided, what is built and
proven against the Python reference, the ten microReticulum defects and their
workarounds, the lossy-network tests, what Android and iOS allow, and the order
of work.
