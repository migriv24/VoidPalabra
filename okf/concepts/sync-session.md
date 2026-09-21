---
type: Concept
title: Sync session
description: The pure state machine two peers run to converge — handed frames and the time, handing back frames and events. What networking is, with none of what networking does; the piece a host or a view library's networking module drives.
resource: src/sync/session.cpp
tags: [status:current, audience:dev, audience:library, confidence:measured]
timestamp: 2026-09-19T00:00:00Z
---

A **sync session** is the relationship between this device and one peer, as a pure
state machine: `start`, `receive(frame)`, `tick(now)`, `publish_presence`, `fetch`,
`close` — each returns the frames to send and the events to show. It never opens a
socket, never reads a clock, never sleeps. That is the sans-IO ruling of
[transport shape](/design/transport-shape.md) made into code, and it is why this could be
built and measured while the trust model still blocks every transport
([open questions](/design/open-questions.md) §6).

# The line it sits on

Ruled by the author on 2026-09-18: **Void Maiz owns what networking looks like; Void
Palabra owns what networking is; the host owns what is shared.** A session is the
middle clause. Sockets, discovery, framing onto a medium, and drawing presence and
placeholders are the others' — see [integration](/design/integration.md) §6.

# What it carries

| concern | how |
|---|---|
| state | whole exportable document, resent until acknowledged ([SPEC](../../SPEC.md) §11.3) |
| what may leave | the host's export set, asked about every mantle and rune (§11.5) |
| files | want / content / absent, only what was asked, only what matches its address (§11.4) |
| "known but not fetched" | a cautious fetch policy: the file is `deferred` until the host says `fetch` |
| presence | a separate kind, opaque bytes, newest-by-sequence, expiring (§11.6) |
| time | every waiting state leaves on elapsed time (§11.7) |
| trust | a signature slot on every frame and hooks to fill and check it (§11.8) |
| a byte stream | a u32 length before each frame, and a reader that takes bytes in any pieces and refuses a bad stream on its first bytes (§11.9, `StreamReader`) |
| bursts | `Timing::coalesce`: a changed state waits out a minimum interval, and the export is not even recomputed inside it; a state the peer just sent is not echoed back |
| a link that dies | nothing special: the transport opens a new connection and a new session over the same replica. A paused phone app, a sleeping laptop and a Wi-Fi handover are all this, and it is tested |

# What the measurement said

The claim this bundle had made since founding — that Palabra survives a transport that
*"may drop, reorder and duplicate"* — is now a test rather than a sentence: four peers,
30% loss, 20% duplication, up to 1.5 s of reordering delay, a partition that heals,
concurrent adds, edits and deletions, **thirty schedules, thirty converged.**

It did not pass the first time, and the failure is the useful part: **losing the one
answer to the first `hello` deadlocked the handshake.** One side had answered and would
not answer again; the other ignored everything from a peer that had not said hello; both
gave up. The fix is a bit on every hello — "I have heard you" — and a rule: a hello that
has not heard you is always answered. Now normative.

# What it deliberately is not yet

- **Not bandwidth-efficient.** Whole-state exchange is the baseline because its
  correctness needs no argument; [reconciliation](/concepts/reconciliation.md)'s range
  fingerprints are how the bytes will come down, measured against this.
- **One session, one peer.** A mesh is several sessions over one replica. A file wanted
  from three peers is currently asked of each separately.
- **No transport.** On purpose, until trust — and the socket layer is platform code
  (discovery, interfaces, Android's multicast lock, sealing), which the author's lean on
  2026-09-20 places outside this library ([integration](/design/integration.md) §7).
  Everything every transport must agree on is here instead.

**Measured, 2026-09-20:** a 200-change burst (a reducer stepping every 10 ms) with
`coalesce = 100` sent at most 25 states instead of 200 — and first sent 42, because the
receiver echoed every merged state back to its sender. The echo is now suppressed: a
state equal to the one the peer just sent is one it holds.

# Status

**`current` as of 2026-09-19.** `include/voidpalabra/sync.hpp`, `src/sync/`, normative in
[SPEC.md](../../SPEC.md) §11. `tests/sync_test.cpp` (the simulated network); conformance
file `22-frames.json` pins the frame bytes and what must be refused.
