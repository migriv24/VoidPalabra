---
type: Concept
title: Compute
description: How Palabra names, routes, and records compute across peers — including model inference — without owning the computing. The scope ruling for the system-management pillar.
tags: [status:planned, audience:dev, audience:library, confidence:asserted]
timestamp: 2026-07-27T00:00:00Z
---

Void Palabra's third pillar, named by the author 2026-07-27: *"system management…
how agents will actually manage themselves… communication with whatever is
computing an LLM… loading models within applications… managing prompts and
constructing them based on some context."*

This is the pillar most likely to swallow the project, so it opens with a boundary
rather than a design.

# The ruling

> **Palabra owns the naming, routing, and history of compute. It does not own the
> computing.**

**The other half of that sentence now has a home.** **Void Bicho**
(`VoidBicho:/README.md`) was founded 2026-07-27 out of this page: it
loads models, runs them, and reports what they cost. The two libraries are siblings
that **do not import each other** — see `VoidBicho:/design/boundaries.md` §3 — and
they meet at exactly the two data shapes below (the capability fragment and the
provenance stamp), which either side can produce or ignore without the other
existing.

Every clause is load-bearing:

- **Naming** — a peer declares what compute it has, in the same breath it declares
  its [tier](/concepts/peer-and-tier.md).
- **Routing** — finding a peer that can run the thing, and reaching it, is
  [reconciliation](/concepts/reconciliation.md)'s addressing problem in a new hat.
- **History** — what was computed, from what inputs, by whom, is
  [utterances](/concepts/utterance.md) and provenance.
- **Not the computing** — model loading, inference, and agent scheduling are
  **holidays** and **Latin-OS's** problem. See §"What this excludes".

This is the same split that founded Palabra in the first place. Void Core does no
I/O, so a store and a transport moved out into a library; a library that manages
state does no inference, so a model runtime stays a holiday. Repeating the split is
the point — the alternative is a second engine growing inside the first.

# Capability declaration generalizes tiering

[Peer and tier](/concepts/peer-and-tier.md) already has a peer **declaring** what it
carries: `State`, `Recent`, or `Full`. That is one axis of a general statement, and
compute is another axis of the same statement. No new mechanism:

```jsonc
{
  "peer":    "<public key>",
  "history": "State" | "Recent" | "Full",
  "codecs":  ["image/png", "audio/opus", ...],
  "compute": [ { "kind": "llm", "id": "…", "ctx": 8192, "via": "<holiday>" },
               { "kind": "embed", "id": "…", "dim": 768, "via": "<holiday>" } ]
}
```

A declaration is a **capability statement, not a class** — exactly the wording tiers
already use. An ESP32 declares `State` and no compute; a laptop declares `Full` and a
local model; a phone declares `Recent` and an accelerator it will only offer while
charging. All three speak one protocol, for the same reason the tiers do: a second,
cut-down protocol for small devices guarantees the dialects drift and the mesh dies.

**Consequence worth stating:** "which device runs the model" stops being
configuration and becomes **discovery**. That is what makes an app work when the
laptop is asleep, and it is what a central scheduler would have taken away.

# A prompt is a scry, and it is addressable

Prompt construction needs no new machinery. Assembling an agent's context out of
state is a **projection of a cut**:

```
scry(cut, context) -> prompt
```

`VoidCore:okf/concepts/scry.md` already defines that operation, guarantees it is
pure (*same inputs ⇒ byte-identical output*), and carries `Context`
(`{locale, audience, date, role}`) explicitly so output is reproducible. Palabra
contributes the one thing Core's scry could not have: **the input has a name.**

> A [cut](/concepts/version-as-cut.md) is named by its Merkle hash. So a prompt is
> **addressable by the hash of the state it was built from** — reproducible after
> the fact, comparable across devices, and verifiable by a peer that was not
> present.

This is a genuinely new property, not a restatement. "Re-run that agent against
exactly the state it saw" is normally impossible, because the state has moved on.
Here the cut is immutable and content-addressed, so it is merely a lookup. Every
agent run becomes an auditable experiment.

Prompt *templates* are ordinary Core data — a `Selector` is a projection authored as
data (`voidcore.spec.selector_from_spec`), so a prompt template is a versioned rune
like anything else, and changing one is an utterance.

# Inference is an effect, so it can never be an utterance

[Utterance](/concepts/utterance.md) is explicit: an utterance may only record a
**pure** change, because an effect is not replayable, not invertible, and not
addressable by its result. A model call is the textbook effect — nondeterministic,
expensive, and reaching a system the application does not own.

So the boundary falls in one place, cleanly:

| step | kind | recorded as |
|---|---|---|
| build the prompt | pure (`scry`) | nothing — derivable from the cut |
| call the model | **effect** (a holiday) | **never an utterance** |
| write the result into state | pure | **an utterance, with provenance** |

The provenance stamp is Core's existing idiom, not a Palabra invention —
`materialize(..., stamp=<field>)` already records *what snapshot a projection
captured* so a reader can tell whether the live data still matches. Palabra's
version of the same stamp:

```jsonc
"provenance": {
  "prompt_cut": "v:8c41f2…",   // the addressable input — Palabra's half
  "model":      "…",           // ┐
  "params":     { "temperature": …, "seed": … },  // │ Bicho's half; copied verbatim
  "deterministic": false,      // ┘ if a Bicho ran it, otherwise supplied by the caller
  "peer":       "<public key of whoever ran it>"
}
```

**Palabra does not compute the right-hand fields and does not validate them** — it
records what the runner reported, exactly as it records `who`. A Bicho supplies them
(`VoidBicho:/design/boundaries.md` §3b); anything else that
runs a model supplies them by hand; a deployment with no models never fills them in.
That is what makes this a shared *shape* and not a dependency.

**The joint property, which neither library has alone:** `prompt_cut` recovers the
exact input state by hash, and the stamp records the exact model and parameters
beside it — so **an agent run is a reproducible experiment**. Where
`deterministic: true`, it can be re-run and checked; where `false`, at least what
could not be reproduced is precisely known.

A history that recorded the inference itself would **lie** — it would claim
replayability it does not have. A history that records the write, stamped with what
produced it, is honest and is all anyone actually needs.

# What this excludes, and where it lives instead

Deliberately **not** Palabra's, with the owner named so nothing falls in a gap:

| thing | owner | where it is designed |
|---|---|---|
| acquiring, loading and running a model | **Void Bicho** | `VoidBicho:/concepts/runtime.md`, `/concepts/invocation.md` |
| what a call cost, and refusing an unaffordable one | **Void Bicho** | `VoidBicho:/concepts/budget.md` |
| scheduling agent work, budget *enforcement* across agents | **Latin-OS** | `LatinOS:/design/direction.md` §2 |
| the agent's tool surface | the application | `VoidCore:/design/agent-tools-memory.md` §1 |
| durable agent memory | **already solved** — a memory is a rune in a `memory` mantle | `VoidCore:/design/agent-tools-memory.md` §2 |

**On the Bicho seam specifically:** the reason to keep it a data shape rather than a
call is symmetric and concrete. If Palabra imported Bicho, **every peer in the mesh
would need a model runtime to reconcile state** — which would undo the `State` tier
in one line, since an ESP32 is a full participant precisely because it can decline
what it cannot hold. If Bicho imported Palabra, a single laptop with no peers would
need a version-control library to summarize a document.

Latin-OS's design is the one to defer to and it is already good: an agent is a rune
participating in a **task** mantle and a **compute** mantle at once; "scheduling" is
**reduction over the task×compute net**; `opaque` marking is *paused*; the
`max_steps` termination guard is a **budget**; edge weights carry cost. Those are
the existing Reduce semantics applied to a new domain, and they are **local** — two
devices' agents need no shared queue. Palabra reimplementing any of it would
re-introduce the central scheduler that Latin-OS's founding rejection exists to
prevent.

What Palabra adds underneath that design is exactly the three things in the ruling:
the compute mantle's runes are *discovered* by capability declaration, reached by
*routing*, and what they produced is *recorded* with provenance.

# The open edge

Whether an agent's **working** state (a half-finished plan, a scratchpad) should be
versioned at all is undecided. It is high-churn, low-value history, and versioning it
would make [metadata growth](/design/open-questions.md) §5 dramatically worse. The
likely answer is that working state is `placement`-shaped — real state that Core
already carves out of the undo slice — but this has not been thought through.

# Status

`planned`. Nothing built. The **capability declaration** rides
[peer and tier](/concepts/peer-and-tier.md) and is therefore blocked behind the same
trust model; **prompt-as-addressable-scry** is not blocked by anything and is
testable with one device.
