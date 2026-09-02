---
type: Design
title: Why not linear — the railguard
description: The central argument, and the translation table from git's vocabulary to Palabra's. Read before designing anything here; the default model is wrong in a specific way.
tags: [status:planned, audience:dev, confidence:asserted]
timestamp: 2026-07-24T00:00:00Z
---

# Why Not Linear — The Railguard

> Void Core has a railguard (*"are we making the core compute the application's
> results?"*). This is Palabra's. A single question keeps the design honest:
>
> **Are we assuming an order that we do not actually know?**
>
> If yes, we have drifted back into git and should stop.

This page exists because **the failure mode is invisible**. Nobody decides to build
a linear VCS; it happens by accident, one reasonable-sounding word at a time —
"the log", "the previous commit", "the main branch", "revert to". Each smuggles in
a total order. An agent reading this bundle will rebuild git from vocabulary alone
unless stopped.

---

## 1. The argument

A version control system's hard problem is merge. Merge is hard **because the
system does not know which orderings were real**.

When two people edit a project independently, some pairs of changes genuinely
depend on each other and some genuinely do not. A linear log **cannot represent
that distinction** — every change must be written down before or after every other
change. So the log records:

> *change A came before change B*

when the truth was:

> *A and B are independent; there is no fact of the matter*

The system then spends enormous effort trying to recover, at merge time, the
information it threw away at commit time. **That is the whole difficulty.** Git's
three-way merge is an heuristic for reconstructing independence from snapshots that
destroyed it.

**Palabra's move is to not throw it away.** Record the partial order —
[utterances](/concepts/utterance.md) naming their real causal parents, and nothing
more. Independent changes stay independent. Merge stops being a guess.

This is not novel and not speculative. Four separate traditions formalize it, and
at least two ship in production (Pijul; Matrix's room-state DAG). See
[academic foundations](/references/academic-foundations.md).

---

## 2. What is *not* being given up

The author was explicit, and this must be honored precisely:

> *"just because we're gonna give up the notion of time, doesn't mean we shouldn't
> be able to predict states or accurately assess possible behavior (that's disaster
> waiting to happen)."*

Nothing about determinism is sacrificed. **The partial order is more deterministic
than a log, not less:**

- A linear log makes an **arbitrary** choice (which of two independent changes to
  write first) and then presents it as fact. Two peers make *different* arbitrary
  choices and now disagree about history.
- A partial order makes **no** arbitrary choice. Two peers with the same utterances
  have literally the same object, and compute the same
  [version name](/concepts/version-as-cut.md), regardless of arrival order.

And every question a timeline could answer remains answerable: a timeline is a
**linear extension**, computed on demand with a canonical tiebreak. Determinism is
*gained* — what is lost is the pretense of an ordering nobody ever knew.

---

## 3. The translation table

Every left-hand term is a linear-time assumption. Do not use them in this bundle.

| git word | why it misleads | Palabra |
|---|---|---|
| **commit** | a snapshot, at a time, with one parent | [utterance](/concepts/utterance.md) — a delta, with *n* parents |
| **the log** | a sequence | [history graph](/concepts/history-graph.md) — a partial order |
| **HEAD** | *the* current position | **heads** (plural) — the maximal elements; several is normal |
| **main branch** | one privileged line | nothing. There is no privileged line, by ruling |
| **branch** | a named mutable pointer | a **cut** you can name with a tag; carries no authority |
| **"what version am I on"** | a point on a line | a **downward-closed set**, named by its Merkle hash |
| **revert / undo** | rewind the line | **append an inverting utterance** — you cannot descend a join-semilattice |
| **conflict** | an error to resolve before proceeding | a **first-class value** with a hash ([conflict](/concepts/conflict.md)) |
| **ours / theirs** | asymmetry implies a privileged side | symmetric `sides`, canonically ordered |
| **staging area** | a linear pipeline stage | an **unpublished cut** — a set you have not yet uttered |
| **clone** | copy the whole history | **[tier](/concepts/peer-and-tier.md) declaration** — history is optional |
| **timestamp decides** | wall clock as arbiter | causality decides; wall clock is display metadata only |

### On staging

The author flagged this specifically: *"'git add' won't just be putting something
in a staging area, it will be about adding some sort of change in
runes/mantles/holidays into a staging area… those concepts may have to be
rethought."*

They dissolve cleanly. Working-directory → staging → local → remote is a
**four-stage linear pipeline**, and it only exists because git needs somewhere to
accumulate a snapshot before freezing it. In Palabra a change **is** an utterance
the moment it is made; "staging" is just **which subset you have chosen to
publish**. Not a place — a *selection*. The four stages collapse to two questions:
*does this utterance exist?* and *have I shared it?*

---

## 4. Where linear thinking is still correct

The railguard cuts both ways. Some things really are sequential, and forcing a
partial order onto them is the mirror-image mistake.

- **Client undo.** A single user, at one keyboard, in one session, *is* a sequence.
  Core's stack-based `undo` is correct and should not be replaced. Undo is a client
  word ([version as cut](/concepts/version-as-cut.md)); the system-level analogue is
  a different operation with a different name.
- **Causal dependency.** When B genuinely depends on A, that order is **real** and
  must be recorded. Palabra is anti-*arbitrary*-order, not anti-order.
- **Display.** Humans read timelines. Producing one is a feature, not a betrayal.
- **Within one utterance.** A delta is applied atomically; there is no partial
  order inside it.

The distinction to hold: **reject invented order, keep observed order.**

---

## 5. The honest risks

Recorded now, so they are not discovered as surprises.

1. **Everyone's intuition is git.** Every collaborator, every agent, and every
   library Palabra might reuse assumes a linear log. The cost of departing is paid
   continuously, in explanation.
2. **CRDT metadata grows** and pruning it safely requires knowing what peers have
   seen — which an open mesh does not know. Genuinely unsolved
   ([open questions](/design/open-questions.md) §4).
3. **The sequence-CRDT case is hard**, and Hormiga's ordered newsletter blocks mean
   it cannot be avoided by declaring mantles unordered.
4. **"No main branch" is not the same as "no authority."** Freely-flowing data still
   needs to answer *who may write this*. Capabilities, not branches — but the
   question does not disappear, and it currently **blocks** all transport work.
5. **This is a research-shaped project inside an engineering schedule.** The rungs
   in the [roadmap](/roadmap.md) are ordered so each pays for itself alone, because
   the theory will take longer than it looks.
