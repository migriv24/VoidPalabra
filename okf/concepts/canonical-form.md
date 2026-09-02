---
type: Concept
title: Canonical form
description: The deterministic, order-independent serialization of a Void Core slice and its hash — the rung underneath everything else. If two peers disagree on this hash, nothing else in Palabra works.
resource: src/canonical/bindings.cpp
tags: [status:current, audience:dev, audience:library, confidence:asserted, foundation]
timestamp: 2026-07-27T00:00:00Z
---

The **canonical form** of a Void Core slice is its unique byte representation under
a rule that ignores everything meaningless: the order runes were written down, the
order edges were listed, the order keys appear in a JSON object. Its **hash** is the
slice's content address.

Every other name in Palabra is built on it:

| thing | is a hash of |
|---|---|
| a [version](/concepts/version-as-cut.md) | the canonical form of a cut |
| an [utterance](/concepts/utterance.md) | the canonical form of the change object |
| a [reconciliation](/concepts/reconciliation.md) fingerprint | the canonical forms in a range |
| a prolly-tree block | the canonical form of its contents |
| a [conflict](/concepts/conflict.md)'s `sides` ordering | the canonical forms of the two utterances |

So this is not a serialization detail. **It is the first rung**, and it is why the
[roadmap](/roadmap.md) opens with it rather than with the container format: a format
can be revised, a hash that two peers compute differently is a system that silently
does not converge.

# The requirement, stated exactly

> Two peers holding *the same slice*, reached by *any route*, must compute the
> *same bytes*.

"Reached by any route" is the load-bearing clause. Peer A authored runes in one
order; peer B received them in the reverse order over a lossy transport; peer C
reconstructed the slice from a prolly tree. All three must agree. This is the same
demand [version as cut](/concepts/version-as-cut.md) makes of a version name,
pushed down to the level where it is actually enforced.

# A mantle is a labeled graph, and that is a stroke of luck

Content-addressing a `VoidCore:/concepts/mantle.md` means
content-addressing a **graph** — runes as nodes, `layout.edges` as edges. In
general, hashing a graph so that isomorphic graphs agree is **graph
canonicalization**, which has no known polynomial algorithm and would be a serious
problem to discover late.

Palabra does not have that problem, because Void Core's runes carry a `spirit.id`:
a **frozen, unique, randomly minted** identifier. That makes the graph
**vertex-labeled**, and canonicalizing a labeled graph is just sorting:

```
H(mantle) = H(
    sorted[ (spirit.id, glyph, arity, sorted(tags), H(content)) for each rune ]
 ++ sorted[ normalized(edge) for each edge in layout.edges ]
 ++ H(rules) )
```

`O(n log n)`, no isomorphism testing, no heuristics. An edge is **normalized** by
writing its endpoints as `(id, port)` pairs in a fixed orientation, so that the
same wire listed from either end produces the same bytes — the port indices already
ride the edge `relation` as `"i:j"`
(`VoidCore:okf/concepts/reduce.md`).

> **Record this as load-bearing.** Core's random rune IDs now pay for themselves
> **twice**: once in [join](/concepts/join.md), where they make concurrent creation
> conflict-free for free, and once here, where they collapse graph
> canonicalization to a sort. A future Core change to deterministic *content-derived*
> IDs would break both. This is a constraint Palabra places on Core, and it should
> be said out loud before someone "cleans up" `vc_mint_id`.

# Where the luck ran out — reduction, reported and fixed

**A real defect, found here 2026-07-27 and fixed by Void Core the same day.** The
report was right that it existed, right that it was small, and right about the fix —
and **wrong about the cause, in a way worth keeping**.

*Reported:* reduction-created agents get random ids from `vc_mint_id`, so two peers
hash differently.

*Actually:* the reducer's `fresh()` was a **monotonic counter** (`_r1`, `_r2`, …)
with no CSPRNG anywhere in the reduce path. But `_r1` names *"the first agent the
first firing happened to create"* — a fact about the **schedule**, which Core's
contract deliberately leaves free. So divergence needed only two peers choosing
different, equally valid, redex orders. And that counter becomes **`spirit.name`**,
which `layout.edges` references and tag expressions match:

> Two peers held structurally identical mantles whose runes were **named
> differently**. The hashes were the symptom; the names were the disease.

The fix, adopted as proposed with both components made unordered, is now normative —
`conformance/reduce/README.md`'s *"fresh agent ids are implementation-defined"* was
the sentence that licensed the bug, and it is gone:

```
id = H( sorted(glyph_a, glyph_b), sorted(parent_id_a, parent_id_b), ordinal )
```

**What remains on Palabra's side:** `reduce --commit` still re-mints a random id,
deliberately, because committing is *authoring*. So merge-by-reduction must compare
the **pure preview**, never a committed mantle
([open questions](/design/open-questions.md) §3.2).

**A second, smaller consequence of the same area, unchanged:** Core's reducer
resolves a closed loop by making it **vanish** (the 2026-07-14 contract ruling).
Reduction is therefore lossy, so `H(normal_form(net)) ≠ H(net)` and the two are not
interchangeable. Palabra must store the **authored** net's hash as the slice's
identity and treat the normal form's hash as a *derived* value — the same
source/derived split Reduce already draws.

# What canonical form is not

- **Not the container format.** The format is *how bytes land on a device*
  ([persistence](/concepts/persistence.md)); canonical form is *which bytes*. A
  format may compress, chunk, or reorder on disk as long as it reproduces the
  canonical bytes on read.
- **Not a wire encoding.** Δ-state fragments on the wire are shaped by
  [reconciliation](/concepts/reconciliation.md); they are *hashed* by canonical form,
  not *transmitted* as it.
- **Not Core's state document shape.** Core owns the schema
  (`VoidCore:SPEC.md §2–§3`). Palabra owns only the rule that turns it into bytes.

# What building it settled

`current` — **built 2026-07-27**, the first code in Void Palabra. C++20 behind
`include/voidpalabra/canonical.hpp`, over a `cJSON` tree because that is what Void
Core and Void Maiz already hold. 27 property tests / 816 checks in
`tests/canonical_test.cpp`, in two halves: *permute everything meaningless and
assert the bytes do not move*, then *change anything meaningful and assert they
do*. The second half is the one that makes the first mean anything.

Five things the implementation decided that the design had left implicit:

1. **A set must deduplicate, not merely sort.** Caught by a failing test. The
   `_SET` encoding first sorted without collapsing duplicates, so a mantle
   holding the same edge twice hashed differently from one holding it once —
   which breaks **idempotence** (`a ⊔ a = a`, [join](/concepts/join.md)), the law
   that makes receiving the same thing twice free. A peer that got a duplicate
   over a lossy transport would have diverged from one that did not. Sorting is
   presentation; deduplication is the algebra.
2. **Integral floats must fold to integers.** cJSON stores *every* number as a
   `double`, so `1` and `1.0` are indistinguishable by the time Palabra sees
   them. Without the fold, the bytes would depend on how a host happened to write
   a literal. This also folds `-0.0` into `0`.
3. **Numbers are big-endian on the wire.** Byte order is a property of the
   machine, not of the value, and two peers are not guaranteed to share one.
4. **Refusal beats guessing.** `NaN`, infinity, `cJSON_Raw`, invalid UTF-8 and
   duplicate object keys all raise rather than hash. A name computed from bytes
   nobody can interpret would look authoritative and mean nothing.
5. **The policy belongs inside the digest**, so two peers configured differently
   produce *visibly* different hashes instead of silently disagreeing about one
   name.

**And one parameter was deleted the same day it was written.** `rune_order` existed
to carry [open questions](/design/open-questions.md) §2 — is a mantle's rune order
semantic? — into the content address without pretending it was settled. Core settled
it hours later (`VoidCore:SPEC.md §4`: not semantic, and a canonical form **MUST**
be order-insensitive), so the knob went rather than acquiring a default. A policy
whose wrong setting is *silently* wrong is worse than no policy; `include_view`
stays because both of its settings are correct, for different questions. The
encoding version was bumped to 2 on the same principle that made the parameter
disappear: the rule is *if the bytes move, bump*, and reasoning case-by-case about
whether a change "really counts" is how that guard rots.

The full byte-level contract is [SPEC.md](../../SPEC.md), written language-neutrally
and offered to Void Core, who asked for it — and since 2026-07-27 it is backed by
**76 conformance vectors** (`conformance/`), which is what turns it from a document
into a contract. The vectors enforce *relations* between cases, not just recorded
values, because a suite regenerated against a broken implementation would otherwise
still look perfectly self-consistent.

**One gap, recorded honestly:** Unicode normalization is **not** applied. Two
peers whose editors emit NFC and NFD for the same visible name compute different
hashes. Doing it properly needs the Unicode decomposition tables, which is
disproportionate here and heavy for an ESP32; UTF-8 *validity* is checked, which
is the cheap half. See [open questions](/design/open-questions.md) §8.

SHA-256 is implemented in-tree rather than vendored, because Palabra is
zero-dependency at Rung 0 — and verified against the FIPS 180-4 vectors,
including the block-boundary and multi-block cases, because a hand-rolled hash
must be proven rather than assumed. When Phase 4 brings signatures it should
become `crypto_hash_sha256`: Hormiga already vendors libsodium, built.
