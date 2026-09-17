---
type: Design
title: Open questions
description: What is genuinely undecided in Void Palabra, what blocks what, and who owns each answer. Decisions already made live in the concept pages, not here.
tags: [status:planned, audience:dev, confidence:tentative]
timestamp: 2026-07-24T00:00:00Z
---

Only genuinely open items belong here. Settled decisions (Δ-state CRDT, no LWW
default, conflicts as objects, history-is-optional, n-utterances-per-batch) are
recorded in the concept pages and in
[academic foundations](/references/academic-foundations.md) §2.1.

Ordered by **what blocks what**, not by interest.

---

# 1. Naming — the vocabulary (author's call)

Concept names are currently descriptive placeholders. A Void-flavored set was
proposed at founding and **not adopted**:

| concept | descriptive (current) | proposed |
|---|---|---|
| the atomic change | utterance | *dicho* (a saying) |
| a peer | peer | *voz* (voice) |
| the history graph | history graph | *coro* (chorus) |

Also unnamed: **the system-level inverse of undo**. It cannot be `undo` (a client
word) and cannot be `revert` (taken by Core for discard-to-`_baseline`).

Cheap to change now, expensive later — every code identifier and CLI verb inherits
it. **Blocks nothing technically; blocks the CLI surface design.**

---

# 2. Is rune order semantic in Core? — **ANSWERED 2026-07-27: no**

*Kept in place with its number so existing references still resolve; the section
is closed.*

Void Core answered empirically rather than by opinion and made it normative in
`VoidCore:SPEC.md §4`: order is **preserved but not semantic** — creation appends,
`rename` keeps position, `rm` closes the gap, `undo` restores position, `export`
round-trips — and **no verb's semantics depend on it**. `ls` does not sort; filters
never reorder. It is *incidental information faithfully carried*, not meaning.

Two consequences Core stated for us directly:

1. **A canonical form MUST be order-insensitive.** Two peers who create the same
   runes in different orders hold **equal** state, so mixing array position into a
   hash would make them disagree loudly about something that is not a disagreement.
   Acted on: the `rune_order` policy parameter is **deleted**, not defaulted —
   see [canonical form](/concepts/canonical-form.md).
2. **An app needing an ordering MUST put it in a content field**, never lean on
   array position — the same ruling as `placement`. So Hormiga's newsletter block
   order is Hormiga's, and it is a field.

**What it buys:** the hard sequence-CRDT machinery (Fugue) drops from *foundational*
to a **per-glyph opt-in over content**, which is roughly half of Phase 1. See
[join](/concepts/join.md).

---

# 3. Purity and determinism — **both halves CLOSED**

Two things Palabra needed **from Core** before any utterance work. Both shipped,
and the section is kept with its number so existing references still resolve.

## 3.1 Pure vs effectful — **ANSWERED by Core 2026-08-27 (0.2.8)**

`VoidCore:okf/design/command-architecture.md` §2 named this *"probably the single
most important distinction to get right"* and then deferred it. It is now
normative in `VoidCore:SPEC.md §6.2`:

> A command is **effectful** iff its verb can reach the host through the effect
> handler. The complete list is `save`, `deploy`, `build`, `preview`, `effect`.
> Every other verb is **pure**.

The list is **closed** rather than maintained, because `vc_set_effect_handler` is
the only way out of a core that does no I/O by definition. Commands were reified
in the same release, as a **command journal** beside undo rather than replacing it,
and the entry carries `minted` — *"the identity that was minted rather than
re-derived"*, which is the answer [utterance](/concepts/utterance.md) had already
written down and had not asked Core for.

**Acted on the same day.** Phase 3's first rung is built:
[SPEC.md](../../SPEC.md) §8, `src/utterance/`, 72 conformance vectors. See
[log](/log.md).

Three of Core's design decisions were made specifically because Palabra exists,
and Palabra's position on each, recorded so it is on the record rather than
assumed:

1. **The classification is static, not observed** — `save` is effectful even on a
   host with no effect handler. **Agreed, without reservation.** A host-dependent
   classification makes the same command recordable on one peer and not on another,
   so convergence would depend on host configuration. That is the exact property
   [join](/concepts/join.md) exists to deny. Core offered to revisit it; Palabra
   declines the offer.
2. **Effectful commands are recorded, not omitted.** **Agreed**, and the argument
   generalizes: Palabra's ingest *reports* what it skipped for the same reason,
   because a gap you cannot see is worse than an entry you must step over.
3. **`minted` is computed by diffing the id set, not by instrumenting the minter.**
   **Agreed**, and the reasoning about manager independence is the property that
   makes N dispatchers a real answer to multi-client. The O(document) cost is
   accepted — Palabra ingests a journal *export*, not each command, so the cost
   lands on Core's side of the seam and Palabra has no measurement that argues
   with it. Declining the verb table was right for the reason given: an omission
   under-reports **silently**.

**One thing remains open, and it is small.** Core's `slice` field cannot
distinguish a change to `mantles` from one to `active`; both report `undo`. Palabra
versions `mantles` alone, so a cursor-only command (`use x`) becomes a redundant
utterance. Recorded in [utterance](/concepts/utterance.md). **What would close it:**
a fourth `slice` value, or `"undo"` split into `"mantles"` and `"active"` — Core
already computes the distinction when it decides which slice a command lands in.
**Blocks nothing** — it costs a redundant entry, never a missing one.

**And one disagreement, stated as one.** Core records a `batch` as **one** entry.
[Utterance](/concepts/utterance.md) argues for **n plus a shared `group`**, because
collapsing a batch destroys the independence relation between the commands inside
it — which is the information that lets utterances commute, so coarse units mean
more false conflicts. The built code does what the data allows (one utterance per
batch) and the ask is on the record. **Owner: Void Core. Blocks: nothing today;
costs false conflicts at device two.**

## 3.2 Deterministic identity inside reduction — **FIXED by Core 2026-07-27**

Reported by us, fixed same-day. **Our diagnosis was wrong and the true cause was
worse**, which is worth keeping rather than quietly overwriting.

**What we reported:** reduction-created agents get random ids from `vc_mint_id`, so
two peers hash differently.

**What was actually there:** the reducer's `fresh()` was a **monotonic counter**
(`_r1`, `_r2`, …) and no CSPRNG was in the reduce path at all — `reduce(net) -> net`
was already deterministic *for a fixed schedule*. But `_r1` means *"the first agent
the first firing happened to create"*, which is a fact about the **schedule**, and
Core's own contract explicitly leaves schedule free. So divergence needed only two
peers picking different, equally valid, redex orders — not different RNG state. And
the counter becomes **`spirit.name`**, which is what `layout.edges` references and
tag expressions match:

> Two peers ended up with structurally identical mantles whose runes were **named
> differently**. Our hashes were the symptom; the names were the disease.

**The fix** (ours, adopted as proposed, with both components made unordered):

```
id = H( sorted(glyph_a, glyph_b), sorted(parent_id_a, parent_id_b), ordinal )
```

Now **normative**: `conformance/reduce/README.md` §2 previously said *"fresh agent
ids are implementation-defined"* — the sentence that licensed the bug — and it is
gone. Pinned by new case **15-derived-ids**, which uses an opt-in `pin_ids` key and
requires 16 randomized schedules to agree on literal ids; Core verified it fails
against a simulated old counter. 15/15 reduce conformance, with the 14 existing
cases byte-identical because the canonical form is id-blind.

### What Palabra must do about the second source

`reduce --commit` still re-mints a random `spirit.id`, **deliberately** — committing
is *authoring*, and authored runes keep their randomness for the §3.1 reasons. So:

> **Merge-by-reduction must compare the pure preview, not the commit.** `reduce`
> returns the derived mantle in `data` without touching the core, and that path is
> fully deterministic. Comparing committed mantles would reintroduce the divergence
> at the last step.

Core also notes `spirit.name` survives *both* paths, so keying reduction-derived
runes on name-within-mantle would make `--commit` irrelevant. **Open on our side:**
which of the two Phase 3 actually wants. If we need committed mantles to be
byte-identical across peers, that needs a caller-supplied-id API and Core has asked
to discuss it rather than have it quietly added.

**Related, and not a blocker but a constraint Palabra now places on Core:** the
randomness of `spirit.id` is **load-bearing in two places** — concurrent creation is
conflict-free for free ([join](/concepts/join.md)), and graph canonicalization
collapses to a sort ([canonical form](/concepts/canonical-form.md)). A future change
to content-derived IDs would break both, and should not be made without Palabra in
the room.

---

# 4. Scope of the undoable slice — Core's other unfinished business

Raised concretely by the 0.2.6 `mantle rm`/`rename` work and now `VoidCore:SPEC.md
§12`: the undoable slice is `mantles` + `active`, which leaves `bindings` outside
it. You cannot decide **what a version contains** without deciding **what the
slice is** — Palabra's cut is defined over exactly that boundary.

**Owner:** Void Core. **Blocks:** what an utterance may target.

**Palabra has an opinion, recorded 2026-07-27 and widened 2026-09-03**: **an
utterance may target `mantles` and `glyphs`.** `domains`, `bindings` and `config`
are **peer-local resolution**, not versioned content — see
[utterance](/concepts/utterance.md) §"What an utterance may target". The forcing
argument is not aesthetic: a `domain` carries real deploy and build commands, so
syncing one as content means **one device's deploy command executes on another
device**. That is a correctness bug and a serious security hole, and it would arrive
by default if Palabra simply versioned "the state document."

**`glyphs` was added at Void Core's argument (0.2.14), and it is worth recording
that the rule survived contact with a new key rather than being bent to fit one.**
The test the table encodes is not *"is this data?"* — `domains` is data. It is
whether a key describes **the world this machine sits in** or **the thing the user
made**. A declaration is the second, it executes nothing, and omitting it makes a
sync that looks successful deliver content its receiver cannot read. The one
sub-key that *does* describe this machine — a descriptor's `source` — is excluded,
which is the same rule applied one level down.

---

# 5. Metadata growth and safe pruning — **genuinely unsolved**

CRDT tombstones and causal context accumulate. Pruning safely requires knowing what
every peer has seen; in an open mesh, you do not know the peer set — that is the
same property that makes [reconciliation](/concepts/reconciliation.md) cheap.

Directions, none verified: bounded-staleness windows; a designated archive peer per
domain; stability detection over the DAG; accepting unbounded growth for state-tier
peers and pruning only at `Full` peers.

**This is the most likely thing to sink the design at scale.** It should be
prototyped early against a real workload rather than reasoned about.

---

# 6. Trust and capability — **blocks all transport**

No transport code may be written before this is answered
([peer and tier](/concepts/peer-and-tier.md)). Latin-OS's standing constraint is
explicit: *security railguard missing = code blocked.*

**Amended 2026-07-27 — what the blocker does and does not cover.** The blocker
targets **transport**: code that opens a socket, binds a port, or moves a byte out of
the process. It does **not** cover the protocol's *algebra* — the pure state machine
`step(local, incoming) -> (local', [outgoing])`, which has no more access to the
world than [join](/concepts/join.md) does and can be built and adversarially tested
today. The conditions on proceeding (signature and capability slots present from day
one; nothing leaves the process; hostile peer assumed throughout) are stated in
[transport shape](/design/transport-shape.md) §3. This is an amendment, not a
loophole — if something binds a port, this section is back in force in full.

Open specifics:
- Adopt **Meadowcap** (Willow's capability system) or design one? Adopting is
  almost certainly right; the question is how it maps to mantles and holidays.
- What is the unit a capability is scoped to — a mantle? a tag expression? Core's
  tag grammar is a natural fit, which is suspicious enough to be worth checking.
- **Can a tag carry authority?** The author raised `admin` as a possible tag.
  Careful: if a tag grants power, then whoever can write tags can grant power, and
  tags are ordinary mergeable data. Probably capabilities and tags must stay
  **strictly separate**, but this is not yet decided.
- Key management, rotation, and revocation in a system with no central authority
  and no guaranteed connectivity.

## 6.1 What a real client's request settled — **2026-09-16**

The first client to ask for trust (LAN sharing between members of one organization:
profiles, admins, an ex-member who must not be able to write) asked for **signed
utterances** and **Meadowcap-shaped capabilities**, in that order. Working through
the request against what is built produced six findings. None of them is code yet,
deliberately: each one changes what the code would have to be, and two of them say
the obvious code would be wrong.

**1. Sign what travels.** A signature protects the unit that crosses the wire. The
client's sync ships *enriched documents*; it does not ship utterances at all. Signing
utterances would have protected nothing on the path actually in use.

And a whole document cannot carry authorship by being signed: a document merged from
five peers has five authors, so a signature on it proves who *relayed* it, not who
*wrote* any part of it. Per-element authorship in a state-based CRDT needs **signed
deltas**. The [replica](/concepts/replica.md) makes the direction concrete: every tag
it mints carries its id, so if a replica id is bound to a signing key, every *add* in
a delta is attributable to the key that signed the delta.

*Removes* are the hard part: a remove carries no tag of its own, only the tags it
retired. A remove is attributable only through the signature on the delta that
carried it, so **a peer that relays a full document cannot prove who retired what**.
Either removes travel only inside signed deltas, or relaying needs a design of its
own. Undecided, and it decides the wire format.

**2. A key-agreement key cannot sign.** The client proposed signing with its profile
key, which is an X25519 keypair — a key-exchange key. X25519 has no signature
operation; signing needs a signature scheme (Ed25519). Deriving both from one seed is
possible, but reusing one key across two protocols is how cross-protocol attacks
happen. A member has **two** keys, and the registry names the signing one.

**3. A capability's region must be decidable from the change alone, never from the
state.** The client proposed regions like *"runes carrying a tag"*. That region
depends on a rune's tags **at the moment of checking**, and tags are concurrent
state: peer P checks a change before it has merged a concurrent retag and admits it;
peer Q checks after and refuses it. Admission is not a join — P and Q now hold
different histories and nothing will ever reconcile them.

Meadowcap's areas are syntactic (a path prefix, a subspace, a time range) for exactly
this reason. So a region may name things **written in the change** — a mantle name, a
glyph name, a rune id — and not a tag expression. This answers the question above
(*"can a tag carry authority?"*): **no**, and the reason is now concrete. It is not
only that tags are ordinary mergeable data; it is that state-dependent admission
diverges.

**4. The journal does not say which mantle a command changed.** Void Core commands
act on the `active` mantle, and a journal entry (`VoidCore:SPEC.md §6.2`) carries the
canonical command line, not its target. So an utterance cannot be checked against a
mantle-scoped capability without re-deriving `active`, and re-deriving is exactly the
state-dependence finding 3 rules out. **An ask for Void Core when capabilities are
built**: the target mantle on the entry. Not raised yet, because nothing consumes it
yet.

**5. Revocation poisons descendants.** With no global time, a peer that has not yet
learned of a revocation builds honest work on top of the revoked member's last change.
A peer that has learned refuses that change — and can then never place the honest
descendants, which name it as a parent. The same shape as finding 3, one level up.
Candidate answers ("valid if authorized in its causal past"; revocation as a named cut)
each have costs nobody has measured. **Blocks shipping capabilities**, not designing
them.

**6. Precedence is a presentation, never a resolution.** The client wants
"admins-first" to *offer* the admin's side of a conflict as the default and never to
resolve silently. That is right, and the reason is the same as finding 3: roles are
synced state too, so "which side is the admin's" is evaluated against a registry that
differs between peers mid-sync. An offered default tolerates that disagreement; a
silent resolution would turn it into divergence.

It needs one thing the conflict model does not have yet: **authorship per side**. A
conflict's sides are values; which replica wrote each one is recoverable from the tags
but not exposed. Small, and it waits on finding 1, because an unsigned author is a
claim.

**What was built instead**, because it is needed whatever trust looks like: the door.
A document from a peer is now validated before any part of it is merged
([SPEC.md](../../SPEC.md) §5.6), and the join converges even on what gets past it.
Neither says *who* may send something. Both are what make "who may" worth checking,
because before them one malformed message from anyone could split a mesh for good.

---

# 7. Compute — what is settled and what is not **(new pillar, 2026-07-27)**

The scope ruling is made and lives in [compute](/concepts/compute.md): *Palabra owns
the naming, routing and history of compute; it does not own the computing.* The
computing is **Void Bicho**'s, founded 2026-07-27 out of that ruling
(`VoidBicho:/design/boundaries.md`), and **neither library imports the other**. What
remains open under it, on Palabra's side:

- **Should an agent's working state be versioned at all?** High-churn, low-value
  history that would make §5's metadata growth dramatically worse. Likely answer:
  it is `placement`-shaped — real state that Core already carves out of the undo
  slice — but this has not been thought through.
- **Is a compute capability a [tier](/concepts/peer-and-tier.md)-style declaration or
  a `VoidCore:/concepts/holiday.md` registry entry?** The
  declaration shape is proposed; Core's planned tagged holiday registry is the other
  candidate and they may be the same thing seen from two ends.
- **Does offering compute to a peer need its own capability class**, distinct from
  read and write on a region? Running someone else's prompt is not a data
  permission, and Meadowcap does not obviously have a shape for it. Folds into §6,
  and it is the one compute question that is **genuinely Palabra's** rather than
  Bicho's — a spend-someone-else's-money capability has no analogue in a
  read/write model, and Bicho's `peer` backend assumes an answer exists
  (`VoidBicho:/concepts/runtime.md`).

# 8. Unicode normalization — **a known gap in built code**

**Found while building Rung 0, 2026-07-27.** The
[canonical form](/concepts/canonical-form.md) validates UTF-8 but does **not**
normalize it. So two peers whose editors emit NFC and NFD for the same visible
rune name — `café` typed on one platform, `café` on another — compute **different
hashes for what a human would call the same state**.

This is the only known way for the built code to violate its own headline
requirement, which is why it is recorded here rather than in a TODO.

Why it was not just fixed: full NFC needs the Unicode decomposition and
composition-exclusion tables, which is a disproportionate amount of vendored data
at Rung 0 and heavy for an ESP32 (`State` tier). The cheap half — rejecting
invalid UTF-8 — is done.

Options, none decided:
- **Normalize at the boundary** — Palabra requires NFC input and says so; hosts
  normalize when they accept text. Cheapest, and pushes the problem to where the
  keyboard is.
- **Vendor a minimal NFC table.** Correct, and costs perhaps 30–60 KB.
- **Restrict identifiers.** If `spirit.name` were constrained to a normalization-
  stable subset the question would not arise — but that is Void Core's call, not
  Palabra's, and it would be a real restriction on human names.

**Blocks nothing today** (single-platform use never hits it) and would be a
genuinely confusing bug the first time a mac and a Windows device sync.

# 9. What Palabra does *not* answer yet

- **Void Buzz's constrained radio links.** Deferred by the author ("that will not
  be important right now; I want this working between normal internet-using
  devices"). The `State` tier is the seam where it will attach.
- **Versioning the reduction itself.** Versioning morphisms in a PROP — genuinely
  novel, genuinely far, explicitly out of scope. Nobody has version control for
  interaction nets; that is a research project, not a feature.
- **Whether Palabra needs its own dispatcher** or extends Core's. It needs a CLI
  (agents must drive it), but "a second dispatcher" and "more verbs on Core's"
  are different architectures with different failure modes. Depends on §1.
