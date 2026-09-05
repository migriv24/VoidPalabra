# Void Palabra

**The system layer for Void Core: how state is remembered, named, and shared
between devices.** Founded 2026-07-24. A **library**, not an application —
C++20, CMake, zero dependencies. The OKF comes before the code, as always.

*Palabra* — word. Void Core gave applications a vocabulary for what they are
made of. Void Palabra is how two of them **say it to each other**.

One sentence: **Void Palabra turns a Void Core state into something that can be
stored, named, merged, and spoken to another device — without a server, without
a main branch, and without pretending that time is a straight line.**

## Three pillars

1. **Remembering** — version control that is **not linear**, because independent
   changes genuinely have no order and inventing one is what makes merge hard.
2. **Speaking** — device-to-device communication, **LAN first**, but built so that
   BLE or a USB stick is the same protocol rather than a second dialect.
3. **Computing** — the system layer: which peer can run what, how a prompt is built
   and named, and what a model actually produced. Bounded by one ruling —
   **Palabra owns the naming, routing and history of compute; it does not own the
   computing.** The computing is **Void Bicho**'s, a sibling library that
   neither imports Palabra nor is imported by it.

## The three faces, extended

Void Core's founding principle is *one core, three faces*. Palabra is the fourth,
and it makes the set legible:

| face | who it serves | what it needs from history |
|---|---|---|
| **Void Maiz** | humans | a *timeline* — one readable ordering |
| **Void Core** (CLI) | AI agents | a *recent, filtered slice* — what changed that matters to me |
| **Void Palabra** | the system | the *whole partial order* — everything, forever, verifiable |

These are not three vocabularies. They are three **projections of one object** —
which is what Void Core's **Scry** already does for mantles.
A full history is useless to a human and mostly useless to an agent; it is
non-negotiable for a version-control system. Palabra holds it so the other two
don't have to.

## What is different here

Void Palabra is **not** a Void Core-flavored git. The two central departures:

1. **History is a partial order, not a sequence.** Independent changes have no
   order, and inventing one throws away the fact that they were independent —
   which is exactly the fact that makes them mergeable. A "timeline" is one
   *linear extension* of the partial order, computed for display.
2. **History is optional.** Convergence is a property of the *state* merge law,
   not of the history. A device that stores no history at all still merges
   correctly. This is what lets an ESP32 and a data server be peers in one
   protocol.

Neither is a stylistic preference. Both are consequences of the mathematics in
[academic foundations](okf/references/academic-foundations.md), which is written
into the OKF *explicitly* — because an LLM reading this bundle will not infer it.

## Status

**What is built**, as of 2026-08-27 — five layers, each `status:current` in the
bundle with a `resource:` link to the code that backs it:

| layer | what it gives you |
|---|---|
| [canonical form](okf/concepts/canonical-form.md) | deterministic bytes and a name for any Void Core slice — `mantles` + `glyphs`, and every other name rests on it |
| [join](okf/concepts/join.md) | the merge law: commutative, associative, idempotent, conflicts as values |
| [persistence](okf/concepts/persistence.md) ◑ | content-addressed storage — a 400 KB asset with a 4-byte edit costs **9 KB**, not 400 KB |
| [archive](okf/concepts/archive.md) | save / load / every past version, with atomic file writes — 100 saves + a 400 KB asset: **560 KB**, where `.miga` would take ~54 MB |
| [utterance](okf/concepts/utterance.md) + [history graph](okf/concepts/history-graph.md) | a Void Core command journal becomes a content-addressed **partial order** — time travel, blame, and a cut name two peers agree on |

Everything else in the bundle is
`status:planned`, per the honesty convention inherited from Void Core (no
`status:current` without a `resource:` link to real code).

## Building from a clean clone

**Nothing else needs to be present.** Void Palabra links no sibling and fetches
nothing: cJSON is vendored in `vendor/`, and the sibling projects named below are
things Palabra exchanges *data* with, not things it builds against. A compiler
with C++20 and CMake ≥ 3.20 is the whole list.

```bash
git clone https://github.com/migriv24/VoidPalabra
cd VoidPalabra
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build      # 10 suites: property tests, leaks, 188 conformance vectors
```

`tools/check_okf.py` runs as the ninth suite when a `python` is on PATH and is
skipped silently otherwise — it checks the documentation's honesty convention,
not the library, which stays dependency-free.

```cpp
#include "voidpalabra/canonical.hpp"

cJSON* state = /* a Void Core state document */;
// names the VERSIONED SLICE: `mantles` and `glyphs` (the schemas that say what
// the content means). `domains`, `config` and `active` are peer-local and excluded.
std::string v = voidpalabra::version_name(state);
// a real one, from conformance/cases/06-slice-and-exclusions.json:
//   v:ad239d983af0bad5a97850aa0b6be3e586ecccb92a284648a24ba345c4bf52f3
```

Two peers who reached the same state by different routes compute the same string;
one who reached a different state cannot.

```cpp
#include "voidpalabra/join.hpp"

CounterMint mint("peerA");
Doc a = enrich(state_a, mint), b = enrich(state_b, mint);
Doc merged = join(a, b);              // commutative, associative, idempotent
for (auto& c : conflicts(merged)) { /* a conflict is a value, never an error */ }
cJSON* core_state = flatten(merged).root;
```

Merging needs **only the two current states** — no history, no server, no peer
registry — which is why a device that stores nothing still converges.

```cpp
#include "voidpalabra/utterance.hpp"

std::vector<JournalEntry> entries;
parse_journal(vc_export_journal_output, entries);   // VoidCore:SPEC.md §6.2

History h;
IngestReport r = h.record(entries);   // effectful entries are SKIPPED — and reported
h.cut_name();                         // "c:e7a0373…" — what version is this?
h.linear_extension();                 // one reading of the partial order, for a human
```

Every value above is reproducible: the cut names in
`conformance/cases/12-journal-ingest.json` are the same function of the same
input, which is the point of the vectors existing.

The history is where the *partial* order lives: `heads()` may hold several, and
that is a normal resting state rather than a problem to be fixed. It buys time
travel, blame and selective sync — **not** convergence, which is the join's, above,
and works on a peer that stores no history at all.

**Layout.** `include/voidpalabra/` public headers · `src/` implementation ·
`tests/` property suites + an allocation-balance check · `conformance/` 188
language-neutral vectors any implementation can be checked against
([SPEC.md](SPEC.md)) · `vendor/` cJSON, vendored whole as Void Maiz and Void Core do. A consumer that already vendors cJSON should link its own copy; the
public header only forward-declares it, so the two never conflict.

Start reading at **[okf/index.md](okf/index.md)**. The central argument — *why
not linear?* — is [design/why-not-linear.md](okf/design/why-not-linear.md). The
decisions still open are [design/open-questions.md](okf/design/open-questions.md),
and what has changed is [log.md](okf/log.md).

If you are porting this rather than reading it, start at
[concepts/canonical-form.md](okf/concepts/canonical-form.md) and
[SPEC.md](SPEC.md) §2–§4. That layer decides whether two peers can agree on the
name of the same state, and everything else here is a name built on top of it —
so an implementation that gets it wrong will pass nothing else.

Licensing is [MIT](LICENSE); the one vendored component is indexed in
[THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md). [`void.json`](void.json) is the
family manifest that describes this repo to its siblings.

## Siblings

These are developed alongside Palabra and sit beside it in one folder. **None of
them is required to build or test this repo** — the relationships below are data
seams, not link dependencies, which is what `requires: []` in
[`void.json`](void.json) records.

- **Void Core** — the engine Palabra versions. Owns rune, mantle, holiday, glyph,
  dispatcher. Palabra owns none of them, and does not link it: it reads Core's
  state documents and its command journal as JSON.
- **Void Bicho** — the compute layer: loading and running models. The other half
  of Palabra's compute ruling; **neither depends on the other**.
- **Void Maiz** — the human face (node-graph UI).
- **Void Buzz** — energy/IoT; the constrained-device forcing client.
- **[Void Hormiga](https://github.com/migriv24/VoidHormiga)** — the forcing client
  for versioning + sync, and Palabra's first consumer: it compiles `src/` directly
  via `VOIDPALABRA_ROOT`. The only one of these that is public so far.
- **Latin-OS** — the far-horizon OS; Palabra is its inter-device layer.
