---
type: Design
title: Transport shape — sans-IO
description: The protocol is a pure state machine; transports are dumb byte pipes. Why this is the ruling that makes LAN, BLE and sneakernet genuinely interchangeable, and how it lets protocol work start before the trust model lands.
tags: [status:planned, audience:dev, confidence:asserted]
timestamp: 2026-07-27T00:00:00Z
---

# The ruling

[Reconciliation](/concepts/reconciliation.md) says *"Palabra defines the protocol; it
does not own the pipe,"* and that transports are holidays. Correct, but underspecified
— it does not say what shape the protocol takes, and the obvious shape is wrong.

> **The protocol is a pure state machine. Transports are dumb byte pipes that cannot
> influence it.**

```
step(local, incoming) -> (local', [outgoing])
```

No sockets. No async. No clock. No retries. `step` is a pure function: same inputs,
same outputs, byte for byte — the same purity contract
`VoidCore:okf/concepts/scry.md` holds itself to.

This is the **sans-IO** pattern (`h11`, `quic-go`'s core, `rustls`), and it is not a
style preference here — three concrete things depend on it.

# 1. It is what actually makes transports interchangeable

The tempting design is an interface:

```
interface Transport { send(bytes); recv() -> bytes; }
```

That looks abstract and is not. Whoever writes the first implementation writes it
against the transport they have, and LAN assumptions leak in immediately — a request
gets a response, messages arrive whole, a round trip is cheap, the peer is reachable
*now*. Then BLE arrives, with 20-byte MTUs, no request/response, and a peer that is
present for four seconds while someone walks past, and the interface does not fit.
The usual outcome is a second protocol for small devices, which is precisely the
failure [peer and tier](/concepts/peer-and-tier.md) exists to prevent.

Under sans-IO the transport cannot leak anything, because it is never consulted. It
receives bytes and hands back bytes. LAN, QUIC, BLE, a USB stick carried across a
room, an SD card in an envelope — all of them are the same to `step`, because none of
them can reach it. That is the only version of "transport-agnostic" that survives
contact with a second transport.

# 2. The whole protocol becomes testable with no network

This is the practical payoff, and it is large. A pure `step` can be driven by a test
harness that is deliberately hostile:

- deliver messages **out of order**
- **duplicate** every message
- **drop** a third of them
- **partition** the mesh and heal it
- run *n* peers in one process, deterministically, with a seeded schedule
- **shrink** a failing schedule to the minimal reproduction

None of that is possible against sockets, and all of it is possible on the first day.
[History graph](/concepts/history-graph.md) already claims Palabra survives a
transport that *"may drop, reorder and duplicate"* — sans-IO is how that claim gets
**proved** rather than asserted.

The precedent is in the family already: Core's `reduce/reduce_test.py`
property-tests **strong confluence under randomized schedules**, and
`temper/temper_test.py` property-tests idempotence. Palabra's sync suite is the same
shape one level up, and [join](/concepts/join.md)'s three-law suite is its sibling.
The two together are the real validation of this architecture:

> If the join laws hold and the protocol converges under adversarial scheduling,
> order-independence is **proven**. If they do not, nothing later rescues it.

# 3. It unblocks work the trust model is currently freezing

[Open questions](/design/open-questions.md) §6 blocks all transport code behind the
trust model, under Latin-OS's standing *security railguard missing = code blocked*.
That constraint is right and stays.

But `step` is **not transport**. It is algebra — a pure function over message values,
with no more access to the world than [join](/concepts/join.md) has. The railguard
targets code that opens a socket, and nothing here does.

**The condition on proceeding**, so this is not a loophole:

1. **Signature and capability slots exist in the message types from day one**, even
   while unpopulated. Retrofitting authentication into a wire format is how protocols
   acquire permanent vulnerabilities.
2. **No byte leaves the process.** No socket, no file handle, no listener. The moment
   something binds a port, [open questions](/design/open-questions.md) §6 is back in
   force.
3. **The state machine is written assuming a hostile peer** — every message
   validated, every hash checked, no trust in framing, no unbounded allocation from a
   length field.

Condition (3) is worth its own line: sans-IO makes hostile-peer handling *testable*,
because "adversarial peer" is just another schedule in the harness.

This should be read as an **amendment to [open questions](/design/open-questions.md)
§6**, not a reinterpretation of it: the blocker still stands for transport; it does
not stand for the protocol's algebra.

# What the state machine contains

Sketch only; the detail belongs to [reconciliation](/concepts/reconciliation.md)
once built.

| in the machine | not in the machine |
|---|---|
| range fingerprints and the split/recurse decision | when to send, how often, on what thread |
| [tier](/concepts/peer-and-tier.md) and capability negotiation | connection establishment, discovery, mDNS |
| which Δ-state fragments to offer or request | framing, MTU, chunking, backpressure |
| signature verification, capability checking | key storage, the OS keychain |
| the decision to stop | timeouts, retries, exponential backoff |

The right-hand column is the **transport holiday's** job, and it is the column where
a LAN implementation and a BLE implementation genuinely differ. Keeping the
difference on that side is the whole design.

# What a stand-in transport taught — **2026-09-16**

A client built its own sealed LAN transport while this one does not exist, and
reported two lessons it wanted this design to keep. Both are right, and both turn
out to say more than they first appear to once they are generalized past one client.

**1. "A receive ceiling per message" is necessary and nowhere near sufficient.** The
client added `max_bytes` after noticing a receiver would hold any size in memory.
Working from that lesson found that every dangerous payload this library could be
sent was *small*:

- **amplification** — nine bytes declaring 2⁶³ sequence elements made the decoder
  allocate until the process died;
- **type confusion** — one node of the wrong kind made the join non-commutative, so
  two peers never converged again, with no error;
- **quadratic work** — map lookups by linear scan made merging two 20,000-rune
  mantles cost 400 million comparisons.

All three are fixed and pinned ([SPEC.md](../../SPEC.md) §5.6,
`tests/hostile_test.cpp`). The general rule, for the state machine when it exists:
**every parser bounds its work by its input**, not only its input by a constant. A
ceiling on the socket read belongs in the right-hand column; bounded parsing belongs
in the left, because it is the same on every transport.

**2. "A timeout on every blocking read" is the sans-IO argument, arrived at from the
other side.** The client's join waits on a human to click Allow, and a thread that
cannot time out cannot be cancelled. In a sans-IO design **nothing blocks**: the
machine is a function, time is one of its inputs (`step(state, event, now)`), and
cancelling is simply not calling it again. "Waiting for approval" is a state the
machine is in, not a thread that is stuck. Timeouts therefore stay in the right-hand
column as they already were, and the left-hand column gains an obligation: every
state that waits must have a transition on elapsed time, or it is a leak with a nicer
name.

# Ephemeral messages

The same client needs **presence** — who is here, what they have selected — and
asked whether the protocol has a notion of messages that do not enter history. It
should, and the rules for one are sharper than "not versioned":

- **A different message type, not a flag.** Presence must be structurally unable to
  carry a document or a delta, so that no bug can route one into a merge. A flag on a
  shared type is one missed check away from versioning a cursor.
- **Never merged, never enriched, never persisted.** It does not reach a
  [replica](/concepts/replica.md) at all.
- **Latest wins, per sender and topic, by a sender-local sequence number** — not a
  clock, and not causal order, because none of the history-graph guarantees apply and
  none are needed. A duplicate or a stale message is dropped by the number.
- **Expires on the receiver's clock.** Consulting wall-clock time is forbidden for
  anything that decides causality ([history graph](/concepts/history-graph.md)).
  Presence decides nothing, so a receiver may expire it by its own elapsed time —
  and should, because a peer that vanishes sends no goodbye.
- **Sealed like everything else.** Presence leaks more about people than most
  content does: who is online, and when.
- **Bounded, small, and cheap to refuse.** Of every message kind, this is the one a
  misbehaving peer can send most often.

**Built 2026-09-19**, as specified above, in the [sync session](/concepts/sync-session.md):
presence is its own message kind with an opaque payload capped at 16 KB, and a test
delivers two presences out of order with a duplicate and checks that only the newest
arrives, once, and that the replica never moved.

# The PROP note, cashed

[Academic foundations](/references/academic-foundations.md) §7 places PROPs at
**communication topology** — peers have ports, a sync session is a morphism, and the
permutation part says *which peer you sync with first does not matter*.

Sans-IO is what makes that statement checkable rather than decorative. Composition of
sync sessions is composition of pure functions; the permutation claim becomes a
property test (*run the same peer set in every order, assert the same final states*)
rather than a hope. A session that owned a socket could not be composed at all.

# The first transport, when it is allowed

LAN, as the author specified. Concretely: mDNS/DNS-SD for discovery, QUIC for the
pipe (multiplexed, encrypted by construction, survives a network change — and Iroh
has already built this layer, per [prior art](/references/prior-art.md)). Both live
entirely in the right-hand column above. Neither is designed here, and neither is
started before §6 is answered.
