---
type: Design
title: Forcing clients
description: The two applications that will prove or break Void Palabra — Hormiga's save system and a LAN chatroom on Void Maiz. What each one actually tests, why they are in this order, and what each blocks on.
tags: [status:planned, audience:dev, confidence:asserted]
timestamp: 2026-07-27T00:00:00Z
---

Named by the author 2026-07-27 as the intended stress tests. Recorded here because
they change what is worth building next, and because **what each one does *not*
test** is as informative as what it does.

| | client | tests hard | tests weakly | blocked on |
|---|---|---|---|---|
| **1** | Void Hormiga's save system | [persistence](/concepts/persistence.md), [join](/concepts/join.md) under real editing | nothing about transport | **nothing** |
| **2** | a LAN chatroom on Void Maiz | [reconciliation](/concepts/reconciliation.md), transport, discovery, tiers | the join — a chatroom is append-mostly | the **trust model** ([open questions](/design/open-questions.md) §6) |

They are complementary in exactly the right way, and the order is right: the first
exercises the merge algebra with no network, the second exercises the network with a
merge that can barely fail. Neither alone would be convincing; each covers the
other's blind spot.

---

# 1. Hormiga's save system — and it is not blocked

> *"the save system in Void Hormiga is going to be replaced by utilizing this. For
> UI/UX reasons, it will still be called 'save' and 'load' and stuff, but local
> version tracking will be used."* — the author, 2026-07-27

## The vocabulary stays the application's

Hormiga keeps saying **save** and **load**. That is correct and should not be
negotiated. It is the same client-word/system-word split
[version as cut](/concepts/version-as-cut.md) already draws for `undo`: the user
says "save", the system says "a cut named `v:8c41f2…`", and neither has to learn the
other's word. A version-control vocabulary leaking into a newsletter tool's UI would
be the tail wagging the dog.

## The finding: this needs **no history graph**

The obvious reading was that "local version tracking" needs Phase 3, which was then
gated on Core's reified commands. It does not, and the reason is a genuine
consequence of [why not linear](/design/why-not-linear.md) §4 rather than a
shortcut:

> **On one device, with one user at one keyboard, the history genuinely IS a
> sequence.** §4 is explicit that linear thinking is correct there, and that forcing
> a partial order onto it is the mirror-image mistake.

So a linear list of named saves is not a degraded version of the history graph — it
is the *right* structure for this client. Each save is a **cut**, already named by
its canonical hash, already verifiable, already order-independent. The history graph
buys the **partial order** — concurrency, cross-peer blame, merge — and none of that
exists until device two.

**Consequence: Hormiga ships on Rung 0 plus Phase 2, today.** Built and measured in
[archive](/concepts/archive.md).

## What `.miga` v3 maps to

Hormiga's bundle is `{magic, version:3, meta, state, assets:{path: base64}}` — the
whole database in one file, assets inlined as base64 *because there was no dedup*.
That last clause is the entire reason this migration is worth doing.

| `.miga` v3 | Palabra |
|---|---|
| `state` | one save — a cut, named by hash |
| `assets: {path: base64}` | blobs in the content-addressed store, chunked and deduplicated |
| `meta` | save labels |
| — | **every previous save**, which `.miga` never kept |

`import_miga` is **one-way by design**. A migration that can round-trip is a
migration nobody finishes.

## The honest limitation, so it is not discovered late

State documents currently chunk **by bytes**, so a save that changes two characters
still bills a whole ~2 KB chunk: 100 revisions cost ~11× one save. That is the floor
for byte-chunking, not a bug — but it is beaten by chunking the document by its
**structure** (one block per rune), which is the outstanding prolly-tree item on the
[roadmap](/roadmap.md). Asserted as a ceiling in the test suite so the improvement is
visible when it lands.

In context it is already a large win — 100 saves with a 400 KB asset cost **76×
less** than the same history in `.miga` — but "76× better than a format with no
dedup at all" is a low bar honestly stated, not a victory lap.

---

# 2. A LAN chatroom on Void Maiz

> *"I think we might wanna make a dummy application (with Void Maiz as a base)
> that's just like, a chatroom? And this will be a basic test of LAN communication
> between devices."* — the author, 2026-07-27

## What it genuinely tests

Everything Phase 4 is about, and nothing else can: **peer discovery**, the
[reconciliation](/concepts/reconciliation.md) protocol over a real network, tier and
capability declaration, and whether the
[sans-IO state machine](/design/transport-shape.md) survives contact with sockets.
It is also the most legible possible demonstration of the architecture's central
claims — two people typing at once is **concurrency, not conflict**, and there is
visibly **no main branch** and no server.

## What it does *not* test — and this matters

**A chatroom is a weak test of the join.** Messages are almost purely *adds*: no
edits, few removes, no concurrent modification of one field. That is the easiest
CRDT workload there is — very nearly a grow-only set — so a chatroom converging
proves the *transport* works and says little about whether the merge law does.

This is precisely why Hormiga comes first. Hormiga is concurrent editing of
structured content: registers, conflicts, observed-removes, the parts that are
actually hard. Doing the chatroom first would produce a demo that works and a merge
algebra nobody had stressed.

## The one hard part it does force

**Message ordering.** A chatroom wants messages in an order, and per
`VoidCore:SPEC.md §4` an application that needs an ordering **must put it in a
content field**. So the chatroom is the forcing client for the **sequence CRDT
(Fugue)** that Phase 1 deliberately left as a per-glyph opt-in
([join](/concepts/join.md)) — with the non-interleaving guarantee mattering visibly,
because interleaved messages from two people are nonsense a user will notice
instantly.

It is also where wall-clock time will try to sneak back in. Chat UIs show
timestamps, and the temptation to *order by* them is strong.
[History graph](/concepts/history-graph.md) is strict: time may be **recorded** for
display and must never be **consulted** to decide causality. A chatroom is the first
place that rule will be under real pressure.

## It is blocked, and the blocker is the right one

No transport code before the trust model
([open questions](/design/open-questions.md) §6) — Latin-OS's standing *security
railguard missing = code blocked*. A chatroom on a LAN with no identity model is
exactly the irresponsible thing that constraint exists to prevent.

**But the protocol's algebra is not blocked.** Per
[transport shape](/design/transport-shape.md) §3, the sans-IO state machine can be
built and tested now, against an in-process harness of *n* simulated peers with
adversarial drop/duplicate/reorder/partition. **The chatroom can therefore be built
and proven correct with no network at all** — and when the trust model lands, it
gains a socket rather than a protocol. That is the useful thing to notice: the
blocker delays the *demo*, not the *work*.

---

# What this changes about priorities

1. **Hormiga's integration is the nearest real payoff** and needs no permission from
   anyone. The library surface it wants exists ([archive](/concepts/archive.md)).
2. **Structural chunking moves up.** It is what turns Hormiga's version history from
   good to genuinely cheap, and it is the same prolly-tree work
   [reconciliation](/concepts/reconciliation.md) needs later.
3. **The sequence CRDT is now scheduled by a client** rather than being an open
   option — the chatroom needs it, so it stops being hypothetical.
4. **The trust model is the long pole for pillar 2**, and it is worth starting
   before the chatroom is otherwise ready, because everything else in Phase 4 can
   proceed without it and nothing can ship without it.
