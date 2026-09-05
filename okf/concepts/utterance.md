---
type: Concept
title: Utterance
description: The atomic unit of Palabra — a content-addressed change to a Void Core slice that names its causal parents by hash. Deliberately not called a commit.
resource: src/utterance/utterance.cpp
tags: [status:current, audience:dev, audience:library, confidence:measured, foundation]
timestamp: 2026-08-27T00:00:00Z
---

An **utterance** is the atomic unit Void Palabra stores and transmits: a
**content-addressed change** to a Void Core slice, naming its **causal parents**
by hash.

**Built 2026-08-27**, and the shape it landed in differs from the 2026-07-24
sketch in two ways worth stating before the argument, because both are
consequences of what Void Core actually shipped:

```jsonc
{
  "hash":    "u:9fa3...",           // content address; SPEC.md §8.2
  "parents": ["u:2b1c...", ...],    // causal predecessors, by hash — this IS the clock
  "command": "rune new text title", // Core's CANONICAL line (VoidCore:SPEC.md §6.2)
  "verb":    "rune",
  "minted":  ["rune_9f2c..."],      // the identities Core minted running it
  "group":   "session-1" | null,    // a human-facing grouping label, NOT an atom
  "who":     "ada" | null,          // attribution, from VoidCore config.actor
  "seq":     1                      // local provenance; NOT hashed, NOT an order
}
```

1. **The change is carried as a `command` plus its `minted` ids, not as a
   `{target, delta}` pair.** The sketch assumed Palabra would have to *derive* a
   delta; Core's journal hands over the command that produced one, in canonical
   form, and `minted` closes the determinism gap that made a command string
   insufficient on its own. Deriving a delta on top of that would be re-deriving
   something the record already states.

2. **The prefix is `u:`, matching `v:`.** The sketch wrote `utt_9fa3…`, which is
   Core's *id* convention (`rune_…`) rather than Palabra's *name* convention. Cuts
   are `c:`. The three prefixes are distinct because the objects are, and comparing
   across two of them is always a mistake — see
   [version as cut](/concepts/version-as-cut.md).

# Why not "commit"

A commit, in every system a reader has used, means *a snapshot of everything at a
moment in time, with one parent*. An utterance is none of those three:

| commit | utterance |
|---|---|
| a snapshot of the whole tree | a **delta** to one slice |
| one parent (or two, at a merge) | **any number** of parents |
| ordered by time within a branch | ordered **only** by the parent relation |
| the unit of history *and* of intent | the unit of history; intent is `group` |

The name matters because the wrong name reimports the wrong model. See
[why not linear](/design/why-not-linear.md).

# Parents are the clock

An utterance names its parents **by hash**, which makes causality *structural* —
readable off the graph rather than tracked beside it. This is the Merkle-clock
result (see [academic foundations](/references/academic-foundations.md) §1.3), and
it is why Palabra needs **no vector clocks** (O(peers), fatal in a mesh), **no
peer registry**, and **no wall-clock time** in its causal structure.

Two utterances with no path between them are **concurrent** — genuinely unordered,
not "unordered because we lost the timestamps." That is information, and Palabra
keeps it.

# Granularity: n utterances, not one blob — **and Core answered differently**

Void Core's `batch` verb applies many commands atomically as **one undo frame**.
The tempting move is to record a batch as one utterance. **Do not.**

Collapsing a batch into an atom destroys the **independence relation between the
commands inside it** — which is precisely the information that lets utterances
commute and merge cleanly. Coarse units mean fewer legal commutations, which means
**more false conflicts**.

So: a `batch` becomes **n utterances plus a shared `group` label**. The label
preserves the human-facing "one action, one undo"; the n utterances preserve the
algebra. This is the resolution of the author's answer to question 6 (2026-07-24)
and of `VoidCore:okf/design/command-architecture.md` §3.

**What was built records one utterance per batch, because that is all the data
allows.** `VoidCore:SPEC.md §6.2` records a `batch` as a single journal entry,
matching its single undo frame, and Core stated the reasoning: the granularity
question *"needed no nestable-command machinery to answer."* From inside Core that
is right — the undo frame is the unit there.

The cost lands here rather than there, and it is the cost this section already
named: **coarse units mean fewer legal commutations, which means more false
conflicts.** A batch of five independent rune edits arrives as one atom that
cannot commute with anything, so two peers who each batched five unrelated edits
conflict where ten separate utterances would have merged.

That is not a disagreement about the argument, only about who pays it, and it is
the one open ask back to Core. **What would close it:** the journal entry for a
`batch` carrying its sub-commands — even as a nested array on the single entry,
leaving Core's undo frame and its entry count exactly as they are. That is
additive, and it is enough to expand a batch here into *n* utterances sharing a
`group`.

The honest statement of today is: **the label exists and is carried; the n does
not exist yet.**

# Purity — the line that must be drawn first

Void Core distinguishes pure model mutations from **effects** (`effect`, `save`,
`deploy` — the holiday boundary). An utterance may only ever record a **pure**
change. An effect is not replayable, not invertible, and not content-addressable
by its result; recording one as an utterance would produce a history that lies.

`VoidCore:okf/design/command-architecture.md` §2 calls this *"probably the single
most important distinction to get right."* Palabra inherited that judgment and
declined to build until it was settled.

**Core settled it 2026-08-27**, in `VoidCore:SPEC.md §6.2`:

> A command is **effectful** iff its verb can reach the host through the effect
> handler. The complete list is `save`, `deploy`, `build`, `preview`, `effect`.
> Every other verb is **pure**.

The list is **closed rather than maintained**, because `vc_set_effect_handler` is
the only way out of a core that does no I/O by definition — which is what makes
this a rule instead of a table someone has to keep current.

Two properties of that classification are Palabra-facing, and Core made both
deliberately because a second peer exists:

- **It is static.** `save` is effectful on a host that registered no effect
  handler at all, even though nothing left the process. The alternative —
  classifying by whether an effect *actually fired* — would make the same command
  a recordable change on one peer and not on another, so convergence would depend
  on host configuration. Palabra agrees, and the reason is the one
  [join](/concepts/join.md) exists to state: agreement across devices beats
  precision per device.
- **Observation may upgrade, never downgrade.** A `batch` containing a `deploy` is
  effectful. Monotone, so a peer that *cannot* fire the effect can never be the
  one that under-reports.

The rule at the top of this section is therefore now executable: **keep entries
where `pure` is true, drop the rest** — plus Palabra's own two exclusions, for the
`view` and `host` slices, which follow from the versioned slice below. All three
are normative in [SPEC.md](../../SPEC.md) §8.1.

One thing Palabra adds on its own authority: **a skip is reported, never
swallowed.** That is Core's argument for recording effectful entries at all,
applied one layer up — a consumer that cannot distinguish *"nothing was excluded"*
from *"something was excluded and not reported"* cannot tell a complete replay
from an incomplete one.

# What an utterance may target

**Ruled 2026-07-27, widened 2026-09-03.** An utterance targets **`mantles` and
`glyphs`**. `domains`, `bindings` and `config` are **peer-local resolution** — real
state, but not versioned content, and never synced.

The forcing case is `domain`. A Void Core domain is *the real hosting target*: a
repo path, build and deploy commands, a port. Syncing one as content means **device
A's deploy command runs on device B** — with A's paths, A's credentials, and A's
assumptions about what is installed. That is a correctness bug and a serious
security hole, and it arrives *by default* if Palabra naively versions "the state
document."

The right analogy is that a git remote's filesystem path is not cloned. So:

| slice | versioned? | why |
|---|---|---|
| `mantles` | **yes** | the content; what the user actually made |
| `glyphs` | **yes** | the schemas that say what the content means — added 2026-09-03 |
| `active` | no | a cursor — view state, like `placement` |
| `domains` | **no** | executable configuration; peer-local by name |
| `bindings` | **no** | a mantle↔domain wiring, resolved locally |
| `config` | **no** | setup, not content |

**Why `glyphs` is on the yes side when `domains` is not**, since both are "config"
in loose speech and the distinction decides the whole table. Void Core put it best
in the message that proposed it:

> A domain is *how this machine reaches the world*. A glyph declaration is *what
> the runes you are already syncing mean*.

The forcing argument against `domains` is that a domain carries real `build` and
`deploy` commands, so syncing one **executes** device A's command on device B. A
declaration executes nothing — Core stores `presentations` without reading it. And
the cost of leaving it out is not hypothetical: a peer receiving a mantle without
its declarations holds the content in its document and cannot reach it through the
projection, **with no error anywhere**. That is not a degraded sync, it is a sync
that looks like it worked. `mantles` and `glyphs` are a value and its type.

One key *inside* `glyphs` stays out: Core stamps each descriptor `source:
"document" | "host"` to say how **this peer** resolved it, and that is peer-local
in exactly the way `domains` is. It is excluded from the canonical form and from
the CRDT register alike — see [canonical form](/concepts/canonical-form.md).

A mantle therefore references a domain **by name**, and **each peer resolves that
name against its own domain table**. Two peers can hold the same mantle and deploy
it to entirely different places, which is correct — that is what "the same
newsletter, my staging site and your staging site" means.

This is Palabra's answer to `VoidCore:SPEC.md §12` and to
[open questions](/design/open-questions.md) §4. It is narrower than Core's undoable
slice (which includes `active`), and deliberately so: the undo slice answers *what a
local user can take back*, while the versioned slice answers *what may be sent to
another machine*. Those are different questions and should not be forced to share an
answer.

# Identity and determinism

Void Core mints rune IDs from the OS CSPRNG (`vc_mint_id`), so replaying the same
command text twice produces *different* state. Palabra's resolution: the utterance
**records the identity that was minted** rather than re-deriving it. Replay becomes
a function of the *utterance*, not of the command string.

This keeps determinism where it matters (the same utterance set always yields the
same state) while **preserving** the property that random IDs give for free: two
peers who each create a rune named `title` produce genuinely distinct runes, with
no conflict at all. That is a CRDT-friendly accident of Core's design and Palabra
should not throw it away.

# One residue, stated rather than hidden

Core's `undo` slice is `mantles` **plus** `active`; the table above versions
`mantles` alone. A journal entry cannot tell them apart — `use x` moves only the
cursor and still records as `slice: "undo"` — so a cursor-only command becomes an
utterance whose replay leaves the versioned slice unchanged.

It was not fixed here, and the reason is the one Core gave for declining a
which-verbs-can-mint table: **an omission from a maintained list of verbs
under-reports silently**, and a silently short record is worse than a redundant
one. [Canonical form](/concepts/canonical-form.md) states the asymmetry for
ordering and it applies unchanged — recording something that turns out to be
meaningless costs a redundant utterance; dropping something that turns out to be
real is a silent wrong answer. The failure modes are not symmetric, so neither is
the default.

The clean fix is one more value in a field Core already emits, and it is asked for
rather than assumed.

# Status

**`current` as of 2026-08-27.** `include/voidpalabra/utterance.hpp` +
`src/utterance/`, normative in [SPEC.md](../../SPEC.md) §8, pinned by 72
conformance vectors (`conformance/cases/11`–`14`) and 200 property checks in
`tests/utterance_test.cpp`.

Built: the content address, the journal filter, the graph, cut names, the linear
extension, and a verifying JSON round trip. Not built: **replay** (it needs Void
Core, which Palabra does not link — an utterance carries `command` and `minted`
precisely so the replayer can live in the application), **inverses**, and
**n-utterances-per-batch**. See [roadmap](/roadmap.md).
