---
type: Concept
title: Version as cut
description: A version is a downward-closed set of utterances named by its Merkle hash — not a point in time. How to answer "what version is this?" and how to recover a timeline.
resource: src/utterance/history.cpp
tags: [status:current, audience:dev, audience:library, confidence:measured, foundation]
timestamp: 2026-08-27T00:00:00Z
---

> *"EVERYTHING operates under the assumption that things happen linearly, so we
> will need a lot of work translating concepts. Such as 'what version is this
> application on?' — we have to give a concrete answer."* — the author, 2026-07-24

We can. This page is that answer.

# A version is a cut

A **version** is a **downward-closed subset** of the
[history graph](/concepts/history-graph.md): a set of
[utterances](/concepts/utterance.md) that contains every ancestor of every member.
"Downward-closed" is the formal way of saying *nothing in it depends on anything
outside it* — the version is self-contained.

The same object has three names in three literatures, which is a good sign it is
the right object:

| field | name |
|---|---|
| event structures / concurrency | a **configuration** |
| patch theory (Pijul) | a **dependency-closed set of patches** |
| lattice theory / CRDTs | an **element of the lattice** |

A version is therefore a **cut through a graph**, not a **point on a line**.

# Its name is a hash

The name of a version is the **Merkle hash of its cut**. This gives exactly the
properties the question demands:

- **Concrete.** `v:8c41f2…` is a real, sayable, comparable answer to "what version
  is this?"
- **Order-independent.** Two peers who received the same utterances in *opposite
  orders* compute the **same name**. This is the whole point, and it is why the
  name is over the *set*, not the sequence.
- **Verifiable.** The name proves the contents; you cannot be handed the wrong
  thing under the right name.
- **Deduplicating.** Shared structure between two versions is stored once
  ([academic foundations](/references/academic-foundations.md) §5).

Human-facing labels (`v0.2.6`, `admin`, `summer-2026`) are **tags** — optional
names attached to a cut, carrying no authority of their own.

**Built 2026-08-27 as `c:…`, not `v:…`.** This page wrote `v:8c41f2…` at founding,
before there was a second thing called a version name. There now is: `v:` names a
**state** (the canonical form of `mantles`), and `c:` names a **history** (the set
of heads). They are digests of different objects and are never equal, so one
prefix for both would invite a comparison that silently always fails. Two prefixes
make the mistake visible at a glance, which is worth more than the tidiness of one
letter.

Naming the **heads** is enough to name the whole cut, and that is not a shortcut:
a cut is downward-closed, so its maximal elements determine it, and each head's
hash already covers its ancestry. That is the Merkle property doing the work the
definition promised.

**All four properties depend on one thing being right:** that two peers compute the
same bytes for the same cut. That is [canonical form](/concepts/canonical-form.md),
and it is why the [roadmap](/roadmap.md) builds it before anything else. A version
name is only as order-independent as the serialization underneath it.

> **"How can there be a version 2 without a version 1 before it?"** The dissolution:
> version *numbers* are ordinals; Palabra's version *names* are hashes. You may still
> tag a cut `v0.3` and nothing breaks, because tags carry no authority. What is given
> up is not versions — it is the claim that **every pair of versions is comparable**.
> Two cuts may be incomparable (neither contains the other), and that is not an error;
> it is the true statement that two devices did different things. They remain
> comparable in the way that matters: you can always [join](/concepts/join.md) them.

# Getting a timeline back

A timeline is not discarded, it is **derived**: a **linear extension** (topological
sort) of the partial order, made deterministic by a canonical tiebreak — hash order
— among concurrent utterances. **Built**, and the tiebreak is normative
([SPEC.md](../../SPEC.md) §8.4) rather than an implementation detail: without one
the rendered order is a fact about insertion order, so two peers holding identical
histories would show different timelines and one of them would be lying.

> **The translation, stated once:** the partial order is the truth; a timeline is
> one valid *reading* of it, computed for a human. The same object, projected.

This is what makes the author's requirement satisfiable in both directions: *give
up the notion of a single timeline, without giving up determinism or the ability to
predict state.* The partial order is **more** deterministic than a log, not less —
it declines to invent an ordering it does not know, and every question a timeline
could answer is still answerable by choosing an extension.

# The three faces, as three cuts

This is also why the README's three faces need no separate machinery:

| face | what it asks for | the operation |
|---|---|---|
| **Void Maiz** (human) | a timeline | a **linear extension** |
| **Void Core** (agent) | "what changed that matters to me" | a **filtered downward-closed set** |
| **Void Palabra** (system) | everything | the **whole partial order** |

Three projections of one object — which is what `VoidCore:/concepts/scry.md`
already is. Palabra does not need a projection layer; Core has one.

# Undo is a client word

> *"as a system managing a whole repo or database, 'undo' wouldn't mean anything.
> The changes made to a system are made by clients."* — the author, 2026-07-24

Correct, and the algebra agrees rather than merely permitting it. In a
**join-semilattice**, `join` is *monotone increasing* — merging only ever moves
**up** the lattice. **There is no un-join.** A system-level "undo" is therefore not
a rewind; it is necessarily a **new utterance whose delta is the inverse** of the
one being retracted.

So the vocabulary must split, and the split is a theorem, not a style choice:

| level | word | mechanism |
|---|---|---|
| client (Void Core) | `undo` | pop a snapshot off a stack — local, linear, lossy |
| system (Palabra) | *(name TBD)* | append an inverting utterance — global, partial-order, append-only |

`revert` is already taken by Core (discard to `_baseline`). Naming is open —
[open questions](/design/open-questions.md) §1.

**Confirmed from the other side 2026-08-27.** Core built the journal and kept undo
memento-based, and reported that the two structures cannot be one: the undo stack
is **bounded** (it drops its oldest frame) and **consumed** (`undo`/`redo` move
frames between stacks), while a record must forget nothing and must *gain* an entry
when a change is taken back. So Core's `undo` records **as an utterance** — which
is the client word producing a system-level entry, exactly as this table says it
must. The system-level inverse is still unbuilt and still unnamed; what is settled
is that it cannot be a rewind.

# Status

**`current` as of 2026-08-27.** Cut names and the linear extension are built in
`src/utterance/history.cpp` and normative in [SPEC.md](../../SPEC.md) §8.3–§8.4.

Not built: **tags** on a cut (a name attached to a head set — cheap, and waiting on
the naming question in [open questions](/design/open-questions.md) §1), **recovering
a state from a cut** (that is replay, which needs Void Core), and the **filtered
downward-closed set** the agent face asks for.
