---
type: Concept
title: Persistence
description: Putting mantles, runes and holidays onto a real filesystem — formats, codecs, and the platform differences every Void Core app currently re-solves alone.
resource: src/store/blocks.cpp
tags: [status:current, audience:dev, audience:library, confidence:asserted]
timestamp: 2026-07-24T00:00:00Z
---

**Persistence** is Palabra's second job, and the one with the most immediate
payoff: how Void Core state becomes **bytes on a real device**, and how those bytes
are read back on a different one.

> *"Void Hormiga already does a lot of work in saving files and stuff, but what if
> I create a mobile app? It makes so much sense if Void Palabra also has all the
> stuff relating to the management of storing mantles, holidays, and runes onto an
> actual real system."* — the author, 2026-07-24

# Why this belongs here and not in Core

Void Core does **no file I/O by definition** (`VoidCore:SPEC.md §9` — all real I/O
is a holiday). So every application on Core has had to solve storage itself:
Hormiga built `.miga` v3 (whole-database bundle, assets inlined as base64 because
there is no dedup); other apps will each invent their own. That is the exact
"re-invented incompatibly" failure Hormiga warned about.

Persistence is a **holiday implementation**, which is why it lives in a library
outside Core rather than inside it — and why putting it in Palabra costs Core
nothing.

# What it owns

- **The container format.** One documented, versioned envelope for "a Void Core
  state, or part of one, at rest" — replacing the per-app bundle. Content-addressed
  blocks underneath ([reconciliation](/concepts/reconciliation.md)), so an asset
  that did not change is stored **once** across every version. This is the direct
  fix for `.miga`'s base64 inlining.
- **Codecs.** Rune content is not always JSON — images, audio, text, binary blobs.
  A codec maps a `VoidCore:/concepts/glyph.md` to a byte
  representation and back.
- **Filesystem mapping.** Where a mantle lands on disk, and how a rune maps to a
  file or a record. Note that Core's POSIX surface already asserts *mantle ≈
  directory, rune ≈ file*; persistence is where that analogy stops being a mnemonic
  and becomes a layout.
- **Platform differences.** Desktop paths, Android scoped storage, iOS sandboxes,
  browser OPFS, an ESP32's flash. Every one of these is a different set of rules,
  and each app currently discovers them the hard way.

# The design pressure that shapes it

Storage and sync are **the same problem** viewed at different distances. A version
that can be diffed cheaply on disk is a version that can be reconciled cheaply over
a wire — both want content addressing, both want structural sharing, both want
content-defined chunking. Designing them together is what makes
`push`/`pull`/`open`/`save` one mechanism instead of two.

The corollary: **do not design the file format first and the sync protocol
second.** A format chosen for local convenience (a single JSON blob, say) makes
reconciliation cost O(everything) and cannot be retrofitted.

**And the same corollary one step earlier (2026-07-27):** do not design the format
before the [join](/concepts/join.md), either. The container must store CRDT metadata —
causal context, tombstones, conflict objects — and that shape is decided in Phase 1,
not here. Persistence also **consumes** rather than defines the byte-level rule: the
[canonical form](/concepts/canonical-form.md) says *which* bytes represent a slice;
the format says *how they land on a device*, and may compress, chunk or reorder on
disk so long as it reproduces the canonical bytes on read. This is why the
[roadmap](/roadmap.md) now places this phase third rather than first.

# What it does not own

- **Secrets.** Key management is a trust concern, not a storage one — see
  [peer and tier](/concepts/peer-and-tier.md).
- **The state document's shape.** That is Core's (`VoidCore:SPEC.md §2–§3`).
  Palabra serializes it; it does not define it.
- **Deciding when to save.** That stays an application's call.

# What building it settled

`current` — **the store and container are built 2026-07-27** (`src/store.cpp`,
`include/voidpalabra/store.hpp`); 14 tests / 254 checks in `tests/store_test.cpp`.
Codecs and platform mapping are **not** built.

**The measurement, which is the point of the phase.** A 400 KB asset with a 4-byte
edit costs **9,138 new bytes** — one chunk — where `.miga`'s base64 inlining costs
400 KB. Re-saving an unchanged asset twenty times adds **zero**.

**Content-defined chunking earned its keep, measurably.** Across an insertion near
the *front* of a 400 KB blob — the worst case — content-defined chunking keeps
**97.8%** of its chunks; fixed-size splitting keeps **0.0%**. The suite asserts both
numbers, so the claim in
[academic foundations](/references/academic-foundations.md) §5 is now a measurement
rather than a citation.

**The bug worth recording**, because it looked like it worked. The first
implementation masked the **low** bits of the gear hash. In `h = (h << 1) + g[b]`
the low bits barely mix — bit 0 of `h` is just bit 0 of the current byte's gear
value — so over a small alphabet the mask could not reach zero at all: **zero cuts
fired, every chunk came out at `max_size`, and dedup never happened.** All the
round-trip tests passed throughout. Only the property test that *measured* chunk
survival against a fixed-size baseline caught it. The fix is to test the **high**
bits, which accumulate over the last ~64 bytes, plus priming the hash across that
window so a boundary depends on content rather than on where the previous chunk
ended.

**The container is self-verifying, not merely parseable.** On read, every block's
bytes are re-hashed against its key, and a mismatch is a refusal — handing out wrong
data under a right-looking name is the one thing content addressing exists to
prevent. Bad magic, an unknown version, truncation and trailing garbage are all
refused rather than guessed at. Blocks are written in key order, so equal content
produces byte-identical files: the container inherits the canonical form's
determinism instead of having a weaker notion of its own.

**Structural chunking landed the same day.** A state document is no longer chunked
by bytes at all — it is split along its own structure, **one block per rune**, plus a
small manifest naming them. That escapes the tuning tradeoff rather than moving it:
coarser byte-chunks bill more per edit, finer ones spend it on keys, but *structure*
aligns block boundaries with edit boundaries, so an edit to one rune **cannot** dirty
another whatever the byte offsets do. Measured: 100 revisions went from **11.3x** one
save to **5.1x**, and the whole archive from 76x to **97x** smaller than `.miga`.
This is the prolly-tree idea at the granularity that actually matters here.

**File I/O is Palabra's**, and deliberately rather than as scope creep: Core does
none by definition, so a library that declines it forces every application to
re-solve it — the founding failure. `write_file` is **write-temp-then-rename**, with
the temporary beside the target so the rename stays on one filesystem and is
therefore atomic, and an `fflush` before it so a power loss cannot leave a
correctly-named empty file. Errors are **distinguished**: a missing file, an
unreadable one, and a file that is simply not an archive are three different
messages, because collapsing them into `false` is the silent-wrong-answer failure in
a new hat.

**Still planned:** codecs (glyph → bytes → glyph) and platform mapping (Android
scoped storage, iOS sandboxes, browser OPFS, ESP32 flash). Also: the container format
belongs in [SPEC.md](../../SPEC.md), which currently specifies the canonical form and
the enriched document but not the file layout.
