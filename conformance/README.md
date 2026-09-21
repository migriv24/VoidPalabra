# Conformance cases

Language-neutral test vectors for [`SPEC.md`](../SPEC.md). Any implementation of
Void Palabra — in C, Rust, on an ESP32 — can be proven to agree with this one
**byte for byte**, without sharing a line of code.

This directory is why SPEC.md is a *contract* and not a *document*, and it exists
because of a lesson Void Core taught us that we had written in a message before
taking ourselves:

> **A layer with a contract gets ported. A layer without one gets reinvented.**

That matters more here than in most projects. Palabra's entire claim is that two
independent peers compute the same name for the same state — and that is a claim
exactly one implementation cannot test.

## Running

```bash
cmake --build build
./build/bin/voidpalabra_conformance          # from the repo root
ctest --test-dir build -R conformance
```

`--regen` rewrites the expectations from the current implementation. It is how the
vectors were first produced and how they are updated when `CANON_VERSION`
legitimately changes.

> **Regenerating to make a failure go away is how a contract stops being one.** If
> the implementation changed on purpose, bump `CANON_VERSION` — the version is
> mixed into every digest precisely so old and new never quietly agree.

## Format

Each file is one JSON object:

```jsonc
{ "kind": "<what to compute>",
  "about": "which part of the SPEC this covers",
  "cases": [ { "name": "…", "in": <input>, "out": "<expected>",
               "policy": { "include_view": true } } ] }
```

`out` is always a **string**, so there is exactly one comparison rule. `policy` is
optional and defaults to SPEC §4.4's default.

| `kind` | `in` | `out` |
|---|---|---|
| `encode` | any JSON value | lowercase hex of the §2 encoding |
| `rune` | a rune | hex of the §3 digest |
| `mantle` | a mantle | hex of the §3 digest |
| `slice` | a state document | the `v:…` version name |
| `canon_slice` | a state document | hex of the §4.4 canonical bytes |
| `enrich` | a state document | hex of the §5.2 enriched document's canonical bytes |
| `join` | `[state_a, state_b]` | hex of the merged document's canonical bytes |
| `conflicts` | `[state_a, state_b]` | concatenated §5.3 conflict hashes, or `none` |
| `validate` | an enriched document or delta, as a peer sends it | `valid` or `refused` |
| `merge_anomalies` | `{base, a, b}` — A observes base, B takes it, A goes to `a` while B goes to `b`, they exchange | the anomaly hashes (both replicas must agree), or `none` |
| `references` | `{fields, state}` | `address mantle/rune/glyph/field` per reference, `;`-joined, or `none` |
| `frame` | a message as JSON, or `{hex}` | hex of the frame; or `valid` / `refused` |
| `stream` | `{hex}`: bytes as they arrived on a stream | each delivered frame's hex, `,`-joined (or `none`), then `+partial` if bytes remain; or `refused` |
| `links` | `{rules: {equivalence, capacity, acyclic}, state}` | each class as `rep=member,…` `;`-joined, `\|`, the §5.11 violation hashes — `none` for either part |
| `script` | `{steps: [{on, observe} \| {on, merge}], latest}` — two replicas with fixed ids | SHA-256 of the canonical replica document (pins the Lamport tags), a space, the version name shown with `latest` fields under §5.10 — both replicas must agree |
| `merged_slice` | `[state_a, state_b]` | the version name of the merged, flattened slice |
| `replica_doc` | `{id, observe: [state, …]}` | hex of the replica document's canonical bytes, or `refused` |
| `utterance` | an utterance object | its §8.2 name (`u:…`) |
| `ingest` | a Void Core journal export | the §8.3 cut name (`c:…`) after filtering, or `refused` |
| `ingest_report` | a Void Core journal export | `"<n> recorded"` plus `"; <reason> <verb>"` per skip |
| `linear` | an array of utterance objects | §8.4's extension as space-separated 8-hex prefixes, `empty`, or `incomplete` |

Two conventions carry real weight:

- **`"refused"`** is the expected `out` for input SPEC says MUST be rejected. An
  implementation that produces bytes there is non-conforming even if the bytes are
  self-consistent.
- **`enrich` / `join` / `conflicts` use a deterministic tag mint** (`t_0001`,
  `A_0001`, …). SPEC §5.1 requires tags to be *unique*, not *random*, so a
  deterministic source is conforming — and it is the only kind that can be pinned
  in a vector.

## The relation checks — read this before adding a case

A vector suite that records whatever the implementation printed and calls it
correct is worthless. A file regenerated against a *broken* implementation would
still be perfectly self-consistent.

So case **names** carry assertions the runner enforces against the preceding case:

- `"… — must equal …"` — the two cases MUST produce identical output.
- `"… — must DIFFER"` — they MUST NOT.

A name that states its own outcome — `… — refused`, `… — valid`, `… — none` —
is held to it too. That rule was added after a case named *"a well-formed hello —
valid"* was regenerated to `refused`: its hand-written bytes were one short, the
implementation was right, and `--regen` recorded the wrong expectation silently.

These encode the SPEC's actual invariants as relations, which survive any
regeneration:

| relation | SPEC |
|---|---|
| `tags ba` == `tags ab` | §4.1 — tags are a set |
| `fully hydrated` == `minimal` | §4.1 — defaults applied before encoding |
| `undirected b→a` == `undirected a→b` | §4.3 — an undirected wire reads either way |
| `directed b→a` != `directed a→b` | §4.3 — a directed one does not |
| `content list yx` != `content list xy` | §4.1 — content sequences keep order |
| `with peer-local fields` == `bare state` | §4.4 — only `mantles` is versioned |
| `mantle order reversed` == `mantle order` | §4.2 / `VoidCore:SPEC.md §4` |
| `empty glyphs map` == `declares nothing` | §4.4 — absent and empty are one state |
| `source=host` == `source=document` | §4.5 — peer-local resolution is not the type |
| `with a glyph declaration` != `with peer-local fields` | §4.4 — the line between versioned and peer-local |
| `a and r merged with a, r and z` == `a, r and z` | §5.6 — an OrSet is recognised by shape, not by member names |
| `observed twice` == `observed once` | §5.7 — an idle tick mints nothing |
| `source=document` == `source=host`, replica side | §4.5 applied to observation, not only to names |
| `seq changed` == baseline | §8.2 — a dispatch counter is not part of a name |
| `parents reordered` == baseline | §8.2 — parents are a set |
| `a save appended` == without it | §8.1 — the consumer obligation, as a relation |
| `diamond reversed` == `diamond in order` | §8.4 — the extension is a function of the graph |

**If you add a case, add it next to the one it relates to and say so in the name.**
A vector with no relation to any other vector only pins a number.

## Files

| file | covers |
|---|---|
| `01-encoding.json` | §2 — the tag byte and body of every type |
| `02-numbers-and-strings.json` | §2.1–2.2 — the int/float fold, length-prefixed strings |
| `03-sets-and-order.json` | §4.1 — tags as a set; content order preserved |
| `04-rune-hydration.json` | §4.1 — defaults; the `include_view` policy |
| `05-mantle-and-edges.json` | §4.2–4.3 — runes as a set; edge normalization |
| `06-slice-and-exclusions.json` | §4.4 — what is and is not versioned |
| `07-refusals.json` | §2.1, §4 — input with no honest byte form |
| `08-enriched-document.json` | §5.2 — the enriched document |
| `09-join.json` | §5.3 — merging two documents |
| `10-conflicts.json` | §5.3 — conflicts as addressed values |
| `15-glyph-declarations.json` | §4.4–§4.5 — what a declaration contributes to a name, and what is excluded |
| `16-glyph-merge.json` | §5.3 — declarations across a merge; redeclaration as a conflict |
| `17-validation.json` | §5.6 — the door: what a peer's document must look like to be merged |
| `18-merged-slice.json` | §5.2–§5.4 — what a user sees after a merge; two silent data-loss defects |
| `19-replica.json` | §5.7 — tag format, how a removal is recorded, and that an idle observation mints nothing |
| `20-merge-anomalies.json` | §5.8 — rules each device kept that the merge broke |
| `21-references.json` | §5.9 — what a document names but does not hold |
| `22-frames.json` | §11 — the sync frame: what is written, and what must be refused |
| `23-links.json` | §5.11 — declared link rules: the equivalence quotient, capacity per slot, acyclicity |
| `24-stream.json` | §11.9 — the stream envelope, and what a stream reader must refuse |
| `25-lamport-and-latest.json` | §5.7, §5.10 — the Lamport counter behind every tag, and `latest` reading it |


## The §8 vectors, and why `ingest` reports a cut rather than a count

`12-journal-ingest.json` records a **cut name**, not "how many entries survived".
That is deliberate, and it is what makes the case named *"a save appended — must
equal"* a real assertion rather than a recorded number: an implementation that
kept the effectful entry would hold an extra head, so it would name a **different
cut**, and the relation would break loudly.

`13-ingest-report.json` then covers what a cut name cannot show — that a skip was
*reported*. Both files are needed because the two obligations in SPEC §8.1 are
different: one says what MUST NOT be recorded, the other says that the omission
MUST be visible.

`14-linear-extension.json` is **generated**, by `tools/gen_linear_vectors.cpp`, because its
utterances name each other by hash. The graph is a diamond — one root, two
concurrent children, a merge naming both, and a tail — delivered three ways. It is
the smallest graph whose extension involves an actual choice, which is the only
kind that can pin a tiebreak.

**Regenerating it takes two steps, and they are separate on purpose:**

```bash
cmake --build build --target gen_linear_vectors   # rewrites the `in` hashes
./build/bin/gen_linear_vectors                    # from the repo root
./build/bin/voidpalabra_conformance --regen       # then fills in `out`
```

The generator is `EXCLUDE_FROM_ALL` and has no ctest entry: it **writes** a
contract rather than checking one, and one that ran during an ordinary build is
how a suite quietly starts agreeing with whatever the implementation does today.
Read the diff before committing it — that is the whole safeguard.

## What is deliberately not covered

- **The container format** (`VPAL`). It is a local file format, not an interchange
  format, and nothing yet reads another implementation's container. It belongs in
  SPEC.md before it belongs here.
- **Chunk boundaries.** Content-defined chunking affects how much two peers *share*,
  never what they *compute*, so a divergent chunker costs bandwidth rather than
  correctness. Worth pinning once a second implementation exists; not a correctness
  contract today.
- **Unicode normalization**, because this version does not do it — see SPEC §6.1.
  When it lands it needs vectors here first.
- **Replay.** An utterance carries `command` and `minted` so that an application
  can apply it to a Void Core state, but performing that application needs Void
  Core, and Palabra does not link it (SPEC §8.5). A vector here could pin the
  utterance and not the result.
