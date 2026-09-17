---
type: Concept
title: Peer and tier
description: Who participates, and how much they carry. Tiering is what lets an ESP32 and a data server be peers in one protocol instead of two.
tags: [status:planned, audience:dev, audience:library, confidence:asserted, foundation]
timestamp: 2026-07-24T00:00:00Z
---

A **peer** is any device holding Palabra state: a sensor, a phone, a laptop, a
server. There is no client/server distinction and no privileged node — the
`VoidCore:` Latin-OS constraint (*no design may assume a single device, a single
client, or a central scheduler*) applies here with full force, because this is the
layer where it would be easiest to violate.

# The problem tiering solves

> *"You cannot run a full Git-style clone on an ESP32. If your interaction-net VCS
> requires storing even a month's worth of patch algebra history, the ESP32 will
> run out of flash memory in hours."* — a research agent, relayed 2026-07-24

That objection is **correct**, and it is fatal to a history-first design. Palabra
survives it because of the separation established in [join](/concepts/join.md):

> **Convergence needs only the current state. History is optional.**

A device that stores **zero** utterances still merges correctly, because merging is
a join on state. History buys time travel, blame, and efficient selective sync — it
does not buy convergence. So the small device is not a degraded participant; it is
a full participant that has *declined an optional feature*.

# The tiers

A peer **declares** its tier. It is a capability statement, not a class.

| tier | carries | can | cannot |
|---|---|---|---|
| **State** | current state only | merge, converge, contribute changes | answer "what did this look like last week" |
| **Recent** | state + a bounded window of utterances | the above, plus recent blame and time travel | recover an arbitrarily old cut |
| **Full** | state + the whole history graph | everything | — |

An ESP32 is `State`. A phone is `Recent`. A laptop or archive server is `Full`.
**All three speak the same protocol**, which is the entire point — the alternative
(a second, cut-down protocol for small devices) guarantees the two dialects drift
apart, and the mesh dies.

A `State` peer is *not* a second-class citizen: its changes are as real, as
attributable, and as mergeable as anyone's. It simply cannot answer historical
questions, and it says so up front.

# Tier is one axis of a capability declaration

**Extended 2026-07-27.** History tier is not the only thing a peer needs to declare,
and the others take the same shape rather than a new mechanism: **codecs** (what it
can decode) and **[compute](/concepts/compute.md)** (what it can run). One
declaration, three axes, still one protocol:

```jsonc
{ "peer": "<public key>", "history": "Recent",
  "codecs": [...], "compute": [ { "kind": "llm", "id": "…", "via": "<holiday>" } ] }
```

The consequence is worth naming: **"which device runs the model" stops being
configuration and becomes discovery** — which is what makes an application keep
working when the laptop is asleep, and what a central scheduler would have taken
away.

# Trust — the missing railguard

Latin-OS's standing note in `VoidCore:okf/roadmap.md` is explicit: **security
railguard missing = code blocked.** A peer-to-peer sync protocol with no identity
model is exactly that hole, and it is the most likely place for this project to do
something irresponsible.

Nothing about transport may be built until this is answered. The shape Palabra
should copy is **capability-based, not identity-based**: Willow's **Meadowcap**
issues unforgeable tokens granting read or write access to a *region* of data, with
**no central authority**. That fits "data flows freely between devices" without
meaning "anyone may write anything."

Minimum bar before any transport code exists:
- **what travels** is signed by the peer that authored it — which, for state-based
  sync, means *deltas*, not utterances and not whole documents (a merged document
  has many authors; see [open questions](/design/open-questions.md) §6.1);
- a peer's identity is its **public key**, not a name or an address;
- read and write are **separate, delegable capabilities scoped to a region**;
- encryption in transit is assumed, not optional.

See [open questions](/design/open-questions.md) §6 — the earlier pointer to §5 here
was wrong; §5 is metadata growth.

# Setting up a peer is not syncing with one

**Recorded 2026-09-16, from a client that built both.** Before a new device can sync,
another device *provisions* it: hands over keys, local-only files, a member registry,
secrets. All of that is **peer-local by this bundle's own rule** — none of it is in
the versioned slice — and the client kept provisioning and syncing as two separate
paths so that a routine sync is structurally unable to carry a credential.

That separation is right, and Palabra adopts it rather than folding enrollment into a
tier:

| | provisioning | reconciliation |
|---|---|---|
| how often | once per device | forever |
| carries | peer-local state, secrets | the versioned slice |
| needs | a human's approval | a capability |
| owner | the application | Palabra |

**The one thing provisioning must never hand over is replica identity.** A new device
set up by copying an existing device's [replica](/concepts/replica.md) bytes shares
its id, and the two will mint the same tags for different changes. The receiving
device either starts an empty replica and syncs, or **forks** the one it was given.
`merge` detects the mistake after the fact (`identity_collision`), and a test
provisions a device the wrong way to prove it — but the right place to prevent it is
the provisioning path, which is the application's.

# Status

`planned`. Nothing built. Trust model **blocks** all transport work.
