---
type: Concept
title: Anomaly
description: A rule every device kept that a merge broke anyway — two runes with one name, a link a concurrent change broke, a rune left on a type removed elsewhere. Checked after every merge and reported as an addressed value, because no join can preserve a rule that spans objects.
resource: src/crdt/anomaly.cpp
tags: [status:current, audience:dev, audience:library, confidence:measured]
timestamp: 2026-09-18T00:00:00Z
---

A [join](/concepts/join.md) preserves exactly one thing: the join. It is a union of
independent objects, so a rule that relates **two** objects can hold on every device
and fail on the merge of them — and no CRDT can prevent that, because each device's
change was valid against the state it could see.

An **anomaly** is such a rule, broken by a merge, reported the way a
[conflict](/concepts/conflict.md) is: a value with a content address, identical on
every peer holding the same document.

# Why this is one concept and not three fixes

It was asked as one question — *two members each made `note-1`; after the merge every
command naming it was ambiguous* — and answering only that would have left the same
defect standing in every other place it occurs. The same shape appears wherever a rule
spans objects:

| what happened on two devices | the rule it breaks | kind |
|---|---|---|
| each minted a rune with the same name | a name is unique in its mantle | `duplicate_name` |
| one drew a link to a rune the other deleted | a link resolves | `link_broken` / `removed` |
| one drew a link to a name the other renamed away | a link resolves | `link_broken` / `renamed` |
| one undeclared an unused type while the other started using it | a type in use cannot be removed | `type_removed` |
| each created a mantle with the same name | a mantle's `id` is its own | already a value [conflict](/concepts/conflict.md) on `id`, because mantles are keyed by name |

# Two limits that keep it honest

**Only what a merge can explain.** Each kind is a rule Void Core owns, and Core's
`validate` already checks the state. What `validate` cannot know is that *two devices*
were involved. So a dangling link is **not** an anomaly — Core allows a link to
something not written yet, on purpose — but a link that dangles *because* a concurrent
removal or rename broke it is. That is the line that keeps this from becoming a second
copy of `validate`.

**Never resolved here.** An anomaly asks for an ordinary edit — rename one rune,
redraw the link, redeclare the type — and disappears when the state stops breaking the
rule. Palabra never renames: which of two members' `note-1` keeps the name is a human
decision, and a rename changes what every command and edge addressing it means.

# On "an id parameterized by the user"

The instinct behind the question — make identity carry who created it — is the wrong
repair for the right worry. Core's `spirit.id` is random, and that randomness is
load-bearing: it is why concurrent creation never conflicts and why the canonical form
collapses to a sort ([open questions](/design/open-questions.md) §3.2). The collision
was never in identity; it was in the **name**, which is for humans. So prevent it
where names are minted (a default name that includes the device, as the asking client
now does), and detect what prevention misses (this page). Identity stays meaningless.

# Status

**`current` as of 2026-09-18.** `include/voidpalabra/crdt/anomaly.hpp`,
`src/crdt/anomaly.cpp`, normative in [SPEC.md](../../SPEC.md) §5.8, exposed on the
[replica](/concepts/replica.md) as `anomalies()`. `tests/merge_rules_test.cpp` runs every
case through two real replicas merging; conformance file `20-merge-anomalies.json` pins
the names.
