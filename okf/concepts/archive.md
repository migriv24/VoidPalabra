---
type: Concept
title: Archive
description: Save, load, and local version tracking — the surface an application actually calls. A linear list of named cuts, which is the correct structure for one device and needs no history graph.
resource: src/archive/archive.cpp
tags: [status:current, audience:dev, audience:library, confidence:asserted]
timestamp: 2026-07-27T00:00:00Z
---

An **archive** is what an application opens, saves into, and loads from. It is the
first Palabra surface aimed at a *client* rather than at the library's own layers,
and it exists because of a specific one — Void Hormiga's save system
([forcing clients](/design/forcing-clients.md)).

```cpp
Archive a;
std::string v = a.save(state, "before the rewrite");   // -> "v:8c41f2…"
cJSON* old = a.load(v);                                // any past save
a.put_asset("assets/header.png", bytes);               // deduplicated
std::string file = a.to_bytes();                       // one VPAL container
```

# It is a linear list, and that is correct

The obvious objection: *Palabra's whole argument is that history is a partial order,
so why is this a list?*

Because [why not linear](/design/why-not-linear.md) §4 says so. The railguard cuts
both ways, and one of the places it names is exactly this one:

> **On one device, with one user at one keyboard, the history genuinely IS a
> sequence.** Forcing a partial order onto it is the mirror-image mistake.

A save is a **cut** — named by its canonical hash, verifiable, order-independent
already. What a linear list gives up is the *partial* order: concurrency,
cross-peer blame, merge. **None of that exists until device two.** So this is not a
simplified history graph; it is the right structure for the situation, and the
history graph is what replaces it when the situation changes.

**The practical consequence is large:** local version tracking needs **no Phase 3**,
so it is not gated behind Core's reified commands. It ships on
[canonical form](/concepts/canonical-form.md) plus
[persistence](/concepts/persistence.md), both built.

# The vocabulary stays the application's

Hormiga will keep calling this **save** and **load**. That is the same client-word /
system-word split [version as cut](/concepts/version-as-cut.md) draws for `undo`:
the user says "save", the system says "a cut named `v:8c41f2…`". A version-control
vocabulary leaking into a newsletter tool's UI would be the tail wagging the dog.

# What it guarantees

- **Saving the same state twice is free.** No blocks, no entry. Because the name is
  a hash of the content, this is a comparison rather than a heuristic — Ctrl+S twice
  costs nothing, which users notice and file formats usually get wrong.
- **Every past save is reachable**, and `load` **verifies**: the stored bytes must
  still name the version they are filed under, or it returns nothing. A store that
  returned the wrong state under the right name would defeat the point of naming by
  content.
- **Equal archives write equal files.** Determinism is inherited from the
  [canonical form](/concepts/canonical-form.md) rather than re-invented.
- **Damage is refused, not guessed at** — bad magic, unknown version, truncation,
  trailing garbage, and any block that does not hash to its key.

# Wall clock appears here and nowhere else

A `Save` carries `when`, and it is **display metadata only**.
[History graph](/concepts/history-graph.md) is strict: time may be *recorded* for
humans and must never be *consulted* to decide causality or resolve a conflict.
Nothing in the library reads it. It exists so a list of saves can be shown to a
person, which is the one thing wall clock is legitimately for.

# Status

`current` — **built 2026-07-27** (`src/archive.cpp`); 16 tests / 86 checks in
`tests/archive_test.cpp`, written against the workload Hormiga actually has: one
device, many saves of a slowly-changing document with a few large assets.

**The measurement.** 100 saves of a 60-rune document plus a 400 KB asset:

| | `.miga` v3 | Archive |
|---|---|---|
| total | ~54 MB | **560 KB** |

97× smaller — and `.miga` keeps *one* version where this keeps a hundred. The
comparison is fair rather than flattering: `.miga` inlines assets as base64
(inflating them 4/3) on every save, which is exactly the design this replaces.

**`import_miga` is one-way by design.** It reads a v3 bundle, decodes its base64
assets into the block store, and records `state` as the first save. There is no
exporter back — a migration that can round-trip is a migration nobody finishes. A
bundle with an asset that does not decode is **refused entirely**, because a partial
import that looks complete is worse than a refusal.

**Structural chunking**, added the same day, is why those numbers hold. A save is
split **one block per rune** plus a manifest, so an edit to one rune cannot dirty
another — 100 revisions cost **5.1x** one save rather than 11.3x. The remaining
per-save cost is the manifest, which changes every time because one key inside it
moves; the suite asserts the figure as a **ceiling** so it cannot silently regress.

# It stores the whole document, and names it by the slice

**Fixed 2026-08-21, reported by Void Hormiga after measuring against a real state
document.** An earlier version stored `mantles` and nothing else, so `config`,
`scripts`, `domains`, `bindings` and `active` vanished on a round trip — and
because the dedup guard compared the version *name*, which is derived from
`mantles` alone, **a config-only edit produced no save and no error**. A user
changing `site.base_url` — their website's address — pressed Save and lost it
silently.

The mistake was collapsing two jobs that separate cleanly:

| job | scope | why |
|---|---|---|
| **name** a version | `mantles` only | a name that moved when a deploy command changed would make two peers disagree about a cut they share |
| **store** a document | every top-level key | an archive is not a sync payload; it is the file the user's whole database lives in |

The exclusions in [canonical form](/concepts/canonical-form.md) are an argument
about **sync**, and Hormiga was right that an archive is not sync. The remainder —
the document minus `mantles` — now rides as one content-addressed block, stored as
**exact JSON text** rather than canonicalized, because `config` and `scripts` are
the application's content and folding numbers in them is not Palabra's business.

**Dedup compares `content`, not `version`.** Two saves may therefore share a
version name and differ in content, which is honest: they *are* the same cut of
the versioned slice. Now normative — [SPEC.md](../../SPEC.md) §7.

**The second bug this surfaced.** Making two saves able to share a version broke
something that had been correct only by accident: `load_latest()` resolved *by
version name* and `load(version)` returned the *first* match, so it handed back the
older document — the same data loss one layer down. Caught by the regression test
written for the first bug, which is the argument for writing the test before
believing the fix. `load` now returns the most recent save under a name,
`load_latest` addresses the last entry by position, and `load_at(index)` exists for
callers who mean one specific entry.

**Files.** `write_file` / `read_file` do the I/O, atomically — temporary beside the
target, flushed, renamed — so a crash costs the *save* and never the archive, which
matters because this file is the user's whole database. A failed write elsewhere
leaves the existing archive byte-identical, and no temporary is left behind on either
path. Read failures are **distinguished**: *cannot open* and *not a valid archive*
are different problems and a user needs to know which.
