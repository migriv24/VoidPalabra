---
type: Concept
title: Reconciliation
description: How two peers find out what the other has without a server, a session, or a peer registry — range-based set reconciliation over content addresses.
tags: [status:planned, audience:dev, audience:library, confidence:asserted]
timestamp: 2026-07-24T00:00:00Z
---

**Reconciliation** is how two peers converge over a network: they exchange
**fingerprints over ranges** of content-addressed data until they agree.

# The algorithm

**Range-based set reconciliation** (Meyer, 2023; shipped in Willow and Iroh):

1. Peer A hashes its whole range into a single **fingerprint** and sends it.
2. Peer B computes the fingerprint over its own items in that range.
3. **Match → done.** That entire range is reconciled, in one round trip, at
   constant cost regardless of how much data it covers.
4. **No match → split** the range and recurse on each half concurrently. Ranges
   small enough are simply sent outright.

Cost scales with **what differs**, not with how much data exists. Two peers that
are already in sync discover this in one message.

# Why this one

The property that matters is not speed — it is **statelessness with respect to
peers**:

- **No session state.** Any two peers can reconcile cold.
- **No peer registry.** A peer need not know who else exists, or what they have
  seen. There is nothing O(peers) anywhere.
- **No server.** Reconciliation is symmetric; both sides run the same algorithm.
- **Interruptible.** A partial reconciliation leaves both peers in a valid state;
  resuming is just running it again.

Compare vector clocks, which require knowing the peer set, or a "have/want"
exchange over full inventories, which costs O(data) even when nothing changed.
Range-based reconciliation is what makes an **opportunistically connected** mesh —
devices meeting briefly, on a LAN, over BLE — actually work.

# What gets reconciled

Two different things, and the distinction matters:

| tier | reconciles | result |
|---|---|---|
| **State** peers | current state, via [join](/concepts/join.md) | convergence |
| **Recent** / **Full** peers | state **and** the [history graph](/concepts/history-graph.md) | convergence + shared history |

A `State` peer and a `Full` peer reconcile **state** and both come away correct.
The `Full` peer additionally records utterances the small device never stored. One
protocol, different appetites — see [peer and tier](/concepts/peer-and-tier.md).

# Storage that makes this cheap

Reconciliation is only as cheap as diffing is. The structure that makes diffing
cheap is a **prolly tree** — a content-addressed B-tree split **by content rather
than by size**, giving O(changes) diff and structural sharing across versions
(the Noms/Dolt design; [academic
foundations](/references/academic-foundations.md) §5).

Content-defined chunking is the load-bearing detail: it keeps a version's hashes
stable under insertion, which a size-split B-tree does not. Without it, inserting
one rune near the front of a large mantle rewrites every downstream block and
reconciliation degenerates to sending everything.

# Transport is a holiday — and the protocol is a pure function

Palabra defines the *protocol*; it does not own the *pipe*. LAN, QUIC, BLE,
sneakernet — each is a holiday in Void Core's sense, reached over an interface.
This keeps the Latin-OS constraint satisfied by construction and leaves room for
Void Buzz's constrained radio links to be a transport rather than a special case.

**Ruled 2026-07-27:** the interface is *not* `send`/`recv`. The protocol is a
**pure state machine** — `step(local, incoming) -> (local', [outgoing])` — with no
socket, no clock and no async, so a transport cannot leak its assumptions into the
protocol and the whole thing is testable under adversarial scheduling with no
network at all. Full argument and the three conditions on building it before the
trust model lands: [transport shape](/design/transport-shape.md).

**No byte may leave the process before the trust model** ([peer and
tier](/concepts/peer-and-tier.md)).

# Built beside it, 2026-09-19

The [sync session](/concepts/sync-session.md) exchanges whole shareable state, resent
until acknowledged. That is the baseline this page's range-based reconciliation will be
measured against: it converges under loss, duplication, reordering and partition (thirty
simulated schedules), and it costs O(document) per change. Range fingerprints cut the
bytes; they add nothing to what converges, which is why the simpler thing came first.

# Status

`planned`. Nothing built. **Blocked** on the trust model.
