---
type: Concept
title: Content reference
description: What a document names but does not hold — pictures, fonts, sprites, weights. Palabra says what is referred to and checks bytes on arrival; what a device lacks is a local fact; fetching belongs to whoever owns I/O.
resource: src/canonical/references.cpp
tags: [status:current, audience:dev, audience:library, confidence:measured]
timestamp: 2026-09-18T00:00:00Z
---

The versioned slice holds runes; it does not hold the bytes runes point at. Syncing a
document syncs a reference and never the thing, so a device that joins later holds a
rune pointing at a file it has never seen.

It was asked as *"a picture added after joining never reaches the other member"*. It is
the same question for every file a document can name: a font a newsletter uses, a
sprite a glyph declaration's `presentations` names, model weights a compute run names by
hash, an attachment. One answer covers them all, by giving each of three jobs to the one
owner that can do it.

# Three jobs, three owners

| job | a fact about | owner | here |
|---|---|---|---|
| what the document refers to | the shared document | Palabra | `references(state, policy)` |
| what this device lacks | one device | the host, asked by Palabra | `missing(refs, have)` |
| getting it | the world | whoever owns I/O | not here — [transport shape](/design/transport-shape.md) |

And one obligation on the receiving end, Palabra's because it is about bytes and names:
**check before storing** (`content_matches`). An address that is a hash means a peer —
any peer, including one you do not trust — cannot hand over the wrong file under the
right name.

# Why "what I lack" never enters the document

It is true on one device and false on the next; it changes when a file arrives; and it
must never be mistaken for "this was deleted". A want-list is recomputed, not stored,
and it is not a [conflict](/concepts/conflict.md) or an [anomaly](/concepts/anomaly.md),
both of which are facts every peer shares.

# Why the fields are declared

Neither Void Core nor Palabra interprets `content`. A 64-character hex string in a field
nobody declared is someone's data — a checksum, an id — not a file to go and fetch. So
the application declares which fields hold addresses, in a `ReferencePolicy` keyed
exactly like a `JoinPolicy` ([integration](/design/integration.md) §2.4). The default
finder reads the common case (any SHA-256 inside a path like `assets/<hash>.png`), so
most applications declare a field name and nothing else.

# What may be deleted

A file the current version no longer names may still be needed: an
[archive](/concepts/archive.md) keeps old versions, and opening one needs its files. What
a device must keep is the **union of references over every version it keeps** — which
is also the answer to "when can the picture a user replaced be deleted?": when no kept
version names it.

# Not built

Fetching, and deciding **who may fetch what**. A hash protects integrity, not
confidentiality: anyone who knows an address can ask for it. Whether a peer should
serve a file to a peer that asks is a capability question, and it waits with the rest
of trust ([open questions](/design/open-questions.md) §6.1).

# Status

**`current` as of 2026-09-18.** `include/voidpalabra/references.hpp`,
`src/canonical/references.cpp`, normative in [SPEC.md](../../SPEC.md) §5.9.
`tests/merge_rules_test.cpp` runs the joining-member case end to end through two
replicas and a block store; conformance file `21-references.json` pins the finder and
the order.
