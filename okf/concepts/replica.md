---
type: Concept
title: Replica
description: One device's copy of the shared state, kept between exchanges so that a deletion is a recorded act rather than an absence. The unit a sync loop runs on, and the thing that makes automatic sync correct instead of merely frequent.
resource: src/crdt/replica.cpp
tags: [status:current, audience:dev, audience:library, confidence:measured, foundation]
timestamp: 2026-09-16T00:00:00Z
---

A **replica** is one device's enriched document, **kept** — along with the identity
and counter that minted it — for as long as the device participates.

It exists because of one sentence [join](/concepts/join.md) has said since
founding and one mistake that sentence did not prevent:

> Two peers holding {a} and {} cannot tell "I never had a" from "I removed a".

The enriched document tells them apart, but **only if it survives**. A client that
builds a fresh one from its current state before every exchange discards the record
of every removal it ever made, so the next merge hands the deleted thing straight
back. That is what the first real client built (reported 2026-09-16), and on a timer
it is a sync that resurrects deletions forever.

**`enrich` is a one-shot import. A replica is what a sync loop runs on.**

# The loop

```
observe(state)  →  save  →  share delta or doc  →  merge(received)  →  splice(flatten)  →  save
```

Each arrow is load-bearing, and each is written down where it can be checked
(`include/voidpalabra/replica.hpp`):

- **Observe** compares the application's state with what the replica *shows* and
  records only the difference, including removals.
- **Save before sharing.** A replica mints tags from a counter. Send a delta and die
  before saving, and the restarted replica will mint those tags again for different
  values.
- **Merge** validates first ([SPEC §5.6](../../SPEC.md)) and checks identity (below)
  before joining anything.
- **Splice**, never replace: `flatten` returns `mantles` and `glyphs`, and the
  application's `config`, `domains`, `bindings` and `active` stay its own.

# Two rules that the obvious diff gets wrong

Both surface only in an **automatic** loop, which is why a client that syncs by
button press does not meet them and one that syncs on a timer does.

**1. Observing a conflicted field does not resolve it.** A field holding two values
is shown as one of them. The application writes back what it was shown. If that
counted as an edit, the next tick would silently resolve every conflict in the
document in favour of whatever `flatten` happened to display — and nobody would have
been asked. So a value equal to what was shown is not an edit; picking the value
that was *not* shown is; and picking the shown one on purpose is `resolve`.

**2. Observing its own output records nothing.** `observe(flatten())` mints no tags.
Otherwise every idle tick grows the metadata and two peers trade phantom changes
with each other indefinitely. Asserted across ten idle ticks, and pinned by a
conformance vector.

# What a removal records

A removal retires the thing's presence **and records every live tag beneath it** —
its fields, its tags, and for a mantle, its runes and edges. The extra tags go into
the removed thing's own `present` set, where OrSet semantics ignore them and nothing
else in the shape changes.

That record is what makes the next question answerable.

# A delete that raced an edit

Device A deletes a rune while device B edits it. After the merge the rune is not
present, so `flatten` does not show it — on **either** device. B's edit disappears
from view with no message anywhere. In an automatic sync, a colleague's work just
vanishes.

Because A's removal recorded what it saw, B's edit carries a tag A did not see, and
`conflicts()` reports a **`deleted_while_edited`** conflict. Its two sides are
`"deleted"` and `"kept"`: keeping brings the rune back with the edit that raced the
delete; deleting again records the edit as seen and closes the question.

Three details, each of which was a wrong answer first:

- **Reported once, at the outermost thing deleted.** Forty runes in a deleted mantle
  are one question about the mantle, not forty.
- **Not reported for something that was never present here.** Deltas arrive in any
  order, so an edit can land before the creation it edits. A deletion always leaves
  retired tags; a thing not yet created has none.
- **Not settled by observing the absence.** The deleting device observes its own
  output, where the rune is — correctly — absent. That is not a decision.

# Identity

A replica's id prefixes every tag it mints (`<id>_<n>`). Two replicas minting under
one id reuse tags, and a remove that retires one value then retires an unrelated one
that shares its tag. **The id must be unique per replica instance** — not per user,
not per profile; a laptop and a phone are two replicas — and random, which this
library asks the application for rather than pretending to supply.

`merge` refuses with **`identity_collision`**, merging nothing, when a document holds
a tag under this replica's id that either

- **is unknown here** — the replica was restored from an older backup, or crashed
  after sending and before saving; or
- **names something different here** — another device is minting under this id.

The second case is the one a first implementation misses, and this one did: every
tag a *copy* mints is a string the original also minted, so checking only for unknown
tags sees nothing. A test that provisioned a device by copying replica bytes caught
it.

The remedy in every case is **`fork`**: the same document under a new id with a fresh
counter. Restoring a backup, cloning a disk image and setting up a new device from an
existing one are all forks, and a client that treats any of them as a continuation
will be told so on its next merge.

# What this does not solve

- **Metadata still grows.** Every removal adds tombstones and nothing prunes them
  ([open questions](/design/open-questions.md) §5). An automatic loop makes this
  arrive sooner, not differently.
- **A lost delta is lost silently.** Nothing in a delta says what came before it.
  Exchange full documents periodically; one full exchange repairs any number of lost
  deltas, and a test asserts it.
- **Undo after a splice.** Void Core's undo is memento-based. A host that splices a
  merge into its document outside the dispatcher leaves Core's undo stack holding
  snapshots from *before* the merge, so an undo reverts the peer's changes as well —
  and the next `observe` records that revert as this device's act and sends it to
  everyone. This library cannot see the undo stack. A host must clear or rebase it
  when it splices.
- **Who sent it.** Validation checks shape, not authority. See
  [open questions](/design/open-questions.md) §6.
- **Mantles are keyed by name.** A mantle renamed on one device while edited on
  another is a delete and a create against an edit of the old name, and the edit is
  reported as `deleted_while_edited` rather than following the rename. Correct, and
  not what a user would call a rename.

# Status

**`current` as of 2026-09-16.** `include/voidpalabra/replica.hpp`,
`src/crdt/replica.cpp`, normative in [SPEC.md](../../SPEC.md) §5.7. 522 checks in
`tests/replica_test.cpp`; conformance file `19-replica.json` pins the tag format, how
a removal is recorded, and that an idle observation mints nothing.
