---
type: Concept
title: Conflict
description: A conflict is a first-class state in the graph — a real, nameable, addressable value — never an error and never a silent winner. Required by the algebra, not just by good manners.
tags: [status:planned, audience:dev, audience:library, confidence:asserted, foundation]
timestamp: 2026-07-24T00:00:00Z
---

When two concurrent [utterances](/concepts/utterance.md) change the same thing in
ways that do not [join](/concepts/join.md), Palabra produces a **conflict** — and a
conflict is a **value**, not a failure.

It has a hash. It can be stored, synced, queried, tagged, and rendered. Peers
converge **on the conflict itself** — two peers who see the same divergence reach
the same conflict state, with the same name.

# Why this is required, not merely nice

The instinct is that this is an ergonomic choice — "surface conflicts instead of
guessing." It is stronger than that: **the algebra does not close without it.**

Mimram & Di Giusto's result is that the category of files and patches **does not
have all pushouts** — merge is a *partial* function. The repair is to take the
**free finite conservative cocompletion**, and the objects that construction adds
**are the conflicts**. With conflicts as objects, merge becomes **total**.

So there are two coherent designs and Palabra picks the second:

| | merge is partial | merge is total |
|---|---|---|
| conflicts are… | errors that abort | **objects in the graph** |
| the lattice is… | incomplete — some joins missing | **closed** |
| a peer receiving bad news… | fails, and must be told what to do | converges, and holds a conflict |

An incomplete lattice breaks §2's three laws in exactly the cases that matter, so
"conflicts are errors" is not a simpler design — it is a broken one.

# It is also Core's honesty principle

`VoidCore:` states it as *never a silent wrong answer*. Last-writer-wins is a
silent wrong answer: it discards one side's work and reports success. Palabra's
conflict object is that principle, arrived at independently from category theory.
The two agreeing is a good sign.

# Shape

```jsonc
{
  "kind":  "conflict",
  "at":    { "mantle": "...", "rune": "...", "field": "..." },
  "sides": [ { "utterance": "u:…", "value": …, "who": "…" },
             { "utterance": "u:…", "value": …, "who": "…" } ]
}
```

**A conflict is located by one of two addresses**, and a reader should branch on
which. A disagreement inside a mantle carries `mantle` (and `rune` for a
rune-level field). A **concurrent redeclaration** — two peers giving one glyph
name two schemas, possible since 2026-09-03 — carries `glyph` instead, with
`field: "descriptor"`:

```jsonc
{ "kind": "conflict",
  "at":   { "glyph": "stat", "field": "descriptor" },
  "sides": [ { "value": {"glyph":"stat","kind":"measure","fields":["note"]} },
             { "value": {"glyph":"stat","kind":"entity","fields":["body"]} } ] }
```

A declaration belongs to the **document**, not to any mantle, so the mantle key is
absent rather than empty. An empty string standing in for "not in a mantle" is how
a renderer ends up printing a blank where a name should be — the shape says which
kind of thing disagreed, and a reader never has to guess from a sentinel.

- **Symmetric.** No side is privileged; there is no "ours" and no "theirs",
  because there is no main branch.
- **At the granularity the object deserves.** A rune's `content` is split into one
  register per key, so two peers editing different fields do not collide. A glyph
  **declaration is one register for the whole descriptor**, deliberately the
  opposite: merging a schema per key would assemble peer A's `fields` with peer B's
  `kind` and hand back a type **neither of them declared**. A schema nobody wrote is
  worse than a disagreement somebody has to answer.
- **Deterministic.** `sides` is ordered canonically (by utterance hash) so every
  peer names the conflict identically.
- **Not blocking.** A mantle containing a conflict is still readable, still
  syncable, still valid. Reading a conflicted field yields the conflict; reading
  everything else is unaffected.

# Resolution is an utterance

Resolving a conflict is **not** an edit to the conflict and **not** a rewind. It is
a **new utterance** naming both sides as parents and carrying the chosen value.
This means:

- resolution is itself versioned, attributable, and syncable;
- two peers can resolve the same conflict differently, which produces a *new*
  conflict — correctly, because they genuinely disagree;
- nothing is ever destroyed to make a conflict go away.

# Interaction-net state is the special case

Where a mantle's rules sit in Void Core's **restricted confluent subset** (≤1 rule
per glyph pair), divergent reductions have a **canonical normal form** — so
conflicting reactive state can be resolved *by reducing*, not by asking. Merge is a
theorem there.

**But this is conditional.** Core's roadmap plans general rule LHS **without the
confluence guarantee**, and confluence of general graph rewriting is undecidable
([academic foundations](/references/academic-foundations.md) §4). Palabra must
therefore **record per mantle whether its rule set is in the confluent fragment**
and fall back to an explicit conflict when it is not. Assuming confluence
unconditionally would reintroduce exactly the silent wrong answer this page exists
to prevent.

> **A second unsoundness was found here on 2026-07-27 and fixed by Core the same
> day.** Confluence gives two peers the same normal form **up to renaming**, which
> is not the same as the same **bytes** — so two peers reducing the same net
> produced different names for identical state. The cause was not what we reported
> (not the CSPRNG, but a schedule-dependent counter that becomes `spirit.name`);
> the fix is deterministic minting from the redex, now normative and pinned by
> `conformance/reduce/` case 15. See
> [open questions](/design/open-questions.md) §3.2.
>
> **The condition that remains on us:** merge-by-reduction must compare the **pure
> preview** — `reduce` returning the derived mantle in `data` — and never a
> `--commit`ted mantle, because committing is *authoring* and re-mints random ids
> by design. Compare previews and this works; compare commits and the divergence
> comes back at the last step.

# Status

`planned`. Nothing built.
