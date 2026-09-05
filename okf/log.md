# Bundle Update Log

## 2026-09-03 — the versioned slice grew a key, and CANON_VERSION went to 3

Message received: `MESSAGE_FOR_VOIDPALABRA_voidcore-0.2.14-a-new-top-level-key-2026-09-03.md`.
Core added `state.glyphs` — glyph *declarations*, the schemas that say what a
rune's content means — and asked the one question they could not answer for us:
does it join the sync slice? It does.

* **The decision, and why it was not close.** Palabra's rule for the slice has
  never been *"is this key data?"* — `domains` is data. It is whether a key
  describes **the world this machine sits in** or **the thing the user made**.
  Core's own sentence is the sharpest statement of it we have:

  > A domain is *how this machine reaches the world*. A glyph declaration is *what
  > the runes you are already syncing mean*.

  The forcing argument that keeps `domains` out — a domain carries real
  `build`/`deploy` commands, so syncing one **executes** device A's command on
  device B — does not reach a declaration, which executes nothing.

* **And the cost of leaving it out was measured rather than argued.** Void
  Hormiga's merge splices only `mantles` out of `flatten` and keeps the rest of
  its local document, which is exactly right and is why nothing there broke. It is
  also why the declarations never travel: peer B declares a type, creates runes of
  it, syncs, and peer A receives content it holds in its document and cannot reach
  through the projection — **with no error anywhere**. Not a degraded sync; one
  that looks like it worked. `mantles` and `glyphs` are a value and its type.

* **`CANON_VERSION` 2 → 3, and every name moved.** The slice used to encode as a
  bare set of mantles; it is now a two-member map, so documents that declare
  nothing get new names too. That was a choice: the bytes could have been left
  alone by encoding `glyphs` only when present, and that was rejected because it
  would make **absent and empty two different states forever** to avoid one bump
  once. Absent and empty are now asserted equal, by the same hydration rule §4.1
  already applies to a partial rune.

* **One key inside the new key is excluded: `source`.** Core stamps each descriptor
  `"document"` or `"host"` to record how **this peer** resolved it. That is
  peer-local in precisely the way `domains` is — the same schema is `document` on
  the peer that received it and may be `host` on a peer that also registered it —
  so hashing it would make two peers holding one schema disagree about its name.
  It is the `domains` judgment applied one level down, inside a key that is
  otherwise versioned.

* **A bug we wrote and caught in the same hour, worth keeping because of its
  shape.** The first version excluded `source` from the canonical form and **not**
  from the CRDT register that carries a declaration across a merge. So two peers
  differing only in `source` computed the **same version name** *and* reported a
  **redeclaration conflict**: the state said "identical", the merge said "you
  disagree", and both were this library speaking.

  Neither answer was wrong on its own, which is what made it dangerous — there was
  no failing assertion anywhere, only two correct components disagreeing about what
  a declaration *is*. The fix is that they are now literally one function
  (`enc::canon_glyph_descriptor`), shared across the canonical and CRDT layers, and
  the rule is normative in [SPEC.md](../SPEC.md) §4.5. Pinned by
  `the_name_and_the_merge_agree_about_what_a_declaration_is`, which asserts the two
  answers are *equal to each other* rather than asserting either one.

* **A declaration is ONE register, not one per key — the opposite of `content`.**
  A rune's content is split per key so two peers editing different fields do not
  collide. Splitting a **schema** that way would let a merge assemble peer A's
  `fields` with peer B's `kind` and hand the result back as a type neither peer
  declared. A schema nobody wrote is worse than a disagreement somebody has to
  answer, so concurrent redeclaration surfaces through `conflicts()` — which is
  what Core asked for, and for the same reason.

  A conflict therefore has **two possible addresses** now: `mantle`/`rune` for a
  disagreement inside a mantle, `glyph` for a redeclaration. The mantle key is
  *absent* rather than empty on a glyph conflict — `""` standing in for "not in a
  mantle" is how a renderer prints a blank where a name should be.

* **Core's §2(c) check, answered in both directions.** They warned that a host
  which *reconstructs* the state document will silently drop the new key. The
  archive was already safe — it stores every top-level key since the 2026-08-21
  data-loss fix, and `glyphs` rides along without being named. `flatten` is the
  other direction and was **already documented misleadingly**: the header called
  `flatten(enrich(x)) == x` a "round-trip law" without saying that §4's canonical
  form — what the comparison runs through — is defined over the *slice*. `flatten`
  returns the slice, not the document, and a caller who writes it back whole loses
  `config`, `domains`, `bindings` and `active`. Now stated at the call site, in
  SPEC §5.4, and asserted by `flatten_returns_the_slice_and_only_the_slice`.

* **The generator moved into the tree.** `conformance/cases/14-linear-extension.json`
  holds utterances that name each other by hash, so it can be regenerated by
  neither hand nor `--regen` (which only rewrites `out`; the hashes are in `in`).
  It was produced by a throwaway program that then had to be written twice.
  `tools/gen_linear_vectors.cpp` is now a real target — `EXCLUDE_FROM_ALL` and no
  ctest entry, because it **writes** a contract rather than checking one, and a
  regenerator that runs during an ordinary build is how a suite quietly starts
  agreeing with whatever the implementation does today.

* **And one defect found on the way past.** `Conflict::hash()` built its own domain
  header — `"voidpalabra/conflict"` plus a NUL — instead of going through SPEC §3's,
  so it was the **only digest in the library with no `CANON_VERSION` in it**. An old
  and a new implementation could therefore compute the same name for a conflict over
  encodings that had moved underneath them, which is the single thing the counter
  exists to prevent.

  Pre-existing rather than new, and fixed here because the cheapest moment to move a
  hash is a release where hashes are already moving. The conflict vectors regenerated
  and their `must equal` / `must DIFFER` relations still hold, which is what
  distinguishes a legitimate regeneration from papering over a break.

* **Where the tree stands:** 10 suites, no leaks, **188 conformance vectors**
  (from 148 — two new files, one new case in §4.4's, and every expectation
  regenerated for CANON_VERSION 3), clean under `-Wall -Wextra`, zero dependencies.

* **Not adopted, and deliberately.** Core's `values`/`measure` verbs, the three
  rune kinds and attribute-assertions-on-edges are all *inside* a mantle, so they
  reach Palabra as ordinary rune and edge content that the canonical form already
  carries byte-for-byte. Nothing here interprets a `kind` or a unit, and nothing
  should: §7 of their message warns that a graph holding both "supports, 0.8" and
  "speed, 900" has no meaningful weighted degree, and Palabra computes no weighted
  anything. The one place it could have leaked in is `layout.edges`, which is
  encoded as an opaque set and stays that way.

## 2026-08-27 — Core cleared the blocker; Phase 3's first rung is built

Message received: `MESSAGE_FOR_VOIDPALABRA_reified-commands-and-purity-2026-08-27.md`.
Core 0.2.8 shipped both halves of [open questions](/design/open-questions.md) §3.1
— *"Owner: Void Core. Blocks: all utterance work."* — so the work was done rather
than acknowledged.

* **The blocker was real and the unblocking is complete.** `VoidCore:SPEC.md §6.2`
  makes the pure/effectful split normative with a **closed** list (`save`, `deploy`,
  `build`, `preview`, `effect`), closed because `vc_set_effect_handler` is the only
  way out of a core that does no I/O by definition. Palabra's rule from
  [utterance](/concepts/utterance.md) — *an utterance may only record a pure change*
  — stopped being a wish and became a filter that can be written down.

  Core also carried `minted`, which is the field this bundle had specified without
  asking for it: *"the utterance records the identity that was minted rather than
  re-deriving it."* That is what makes replay a function of the entry rather than of
  the command string, and it is the reason the built utterance carries a `command`
  rather than a derived delta.

* **Built the same day, and three concepts moved from `planned` to `current`.**
  [Utterance](/concepts/utterance.md), [history graph](/concepts/history-graph.md)
  and [version as cut](/concepts/version-as-cut.md) — the first pages in this bundle
  to earn `status:current` since 2026-07-27, and the first that describe *change*
  rather than *state*.

  `include/voidpalabra/utterance.hpp` + `src/utterance/` (three files, ~640 lines):
  the journal parser, the content address, the filter, and the Merkle-DAG. New
  [SPEC.md](../SPEC.md) **§8**, four new vector files, and `tests/utterance_test.cpp`
  at 200 checks.

* **Palabra agrees with all three of the decisions Core flagged for review.** They
  were made specifically because a second peer exists, and Core asked to be told if
  any was wrong. None was.

  The static classification is the important one. Core offered the alternative —
  classify by whether an effect *actually fired* — and named the trade honestly:
  *"precision per-device versus agreement across devices."* Agreement, without
  reservation. A host-dependent classification would make the same command a
  recordable change on one peer and not on another, so convergence would depend on
  how a device was configured, which is the exact property
  [join](/concepts/join.md) exists to deny.

  Their argument for recording effectful entries — that a consumer must be able to
  tell *"nothing effectful happened"* from *"a deploy happened and was not
  recorded"* — was **generalized one layer up**: Palabra's ingest returns what it
  skipped, with the reason, rather than silently producing a shorter history. It is
  the same argument and it applies to the same failure.

* **One disagreement, recorded as one.** Core records a `batch` as **one** journal
  entry, matching its one undo frame, and considers the granularity question
  answered. [Utterance](/concepts/utterance.md) argues for *n* utterances plus a
  shared `group`, because collapsing a batch destroys the independence relation
  between the commands inside it — and that relation is what lets utterances
  commute, so **coarse units mean more false conflicts**.

  The cost lands on Palabra's side, not Core's, which is why it needed saying. The
  built code does what the data allows and the ask is on the record.

* **And one small thing Core can fix cheaply.** Its `slice` field reports `undo`
  for both `mantles` and `active`, and Palabra versions `mantles` alone — so a
  cursor-only command (`use x`) becomes an utterance whose replay changes nothing.
  Not fixed here, deliberately: the fix inside Palabra would be a table of verbs
  that touch `active`, and a table is what Core itself declined to build for
  `minted`, for the reason that an omission from one **under-reports silently**.
  [Canonical form](/concepts/canonical-form.md)'s asymmetry decides it — a redundant
  utterance is annoying, a missing one is a silent wrong answer.

* **What the vectors pin, and why two files rather than one.**
  `12-journal-ingest.json` records a **cut name** for a journal, so *"a save
  appended — must equal"* is a real assertion about §6.2's consumer obligation: an
  implementation that kept the effectful entry would hold an extra head and name a
  different cut. A count would have been a recorded number; a name is a
  consequence. `13-ingest-report.json` covers what a cut name cannot show — that
  the skip was *reported* — because §8.1 has two obligations and one vector can
  only carry one of them.

  `14-linear-extension.json` was **generated**, because its utterances name each
  other by hash and cannot be hand-written. The graph is a diamond delivered three
  ways, which is the smallest graph whose extension involves an actual choice.

* **Two design decisions that departed from the 2026-07-24 sketches, both stated
  on the pages that made them.** An utterance is named `u:` and a cut `c:`, not
  `utt_…` and `v:` — `v:` already names a *state*, and one prefix for two objects
  that are never equal would invite a comparison that silently always fails. And
  `seq` is carried but **not hashed**: it is a fact about one manager's dispatch
  counter, and two peers' counters are unrelated, so hashing it would make the same
  change name itself differently on two machines. Both are pinned by vectors.

* **The transport claim stopped being a claim.**
  [History graph](/concepts/history-graph.md) has asserted since founding that
  Palabra survives a transport that *"may drop, reorder and duplicate"*. Measured:
  a twelve-utterance graph delivered in forty randomized orders reached the same
  cut name **and** the same rendered timeline every time, and delivering everything
  twice cost nothing. That is not the Phase 4 state machine — it is the graph's
  half of the same property, and it was free to check.

* **One refactor, made because a second caller appeared.** SPEC §3's domain header
  was assembled inside `src/canonical/bindings.cpp`; the utterance layer needs the
  same header with two new kinds. It moved to `src/encoding/` as `domain_digest`
  and both layers call it. A second copy of a header format is a second chance to
  get it wrong, and a divergent domain header is a divergence that only surfaces
  when two peers meet. Verified by the 76 pre-existing vectors passing unchanged:
  the refactor moved no bytes.

* **Where the tree stands:** 9 suites, no leaks across 161,889 allocations, **148
  conformance vectors**, clean under `-Wall -Wextra`, zero dependencies.

## 2026-08-21 — Hormiga found a silent data-loss bug; fixed, plus the one it surfaced

Report received: `MESSAGE_FOR_VOIDPALABRA_hormiga-archive-readiness-2026-08-21.md`
— measured against a real Hormiga state document rather than read. Reproduced
before agreeing.

* **The blocker: `Archive::save` stored `mantles` and discarded the rest of the
  document.** Seven of eight top-level keys vanished on a round trip. Worse, and
  silently: the dedup guard compared the version *name*, which is derived from
  `mantles` alone, so **a config-only edit produced no save and no error**. A user
  changing `config.site.base_url` — their website's address — pressed Save and lost
  it. For a tool whose users are volunteers, that is the worst shape a bug can have.

* **The mistake was collapsing two jobs.** `canonical.hpp` argues the exclusions
  well and Hormiga agreed with the argument — *"and it is an argument about SYNC"*.
  They were right. An archive is not a sync payload; it is the file the user's whole
  database lives in. **Name** from `mantles`; **store** everything.

  Fixed as they proposed: the remainder rides as one content-addressed block, as
  **exact JSON text** rather than canonicalized (`config` and `scripts` are the
  application's content, and folding numbers in them is not Palabra's business), and
  the dedup guard now compares a `content` digest. Two saves may share a `version`
  and differ in `content` — honest, because they *are* the same cut.

* **The second bug, surfaced by the first fix.** Making two saves able to share a
  version broke something correct only by accident: `load_latest()` resolved **by
  version name** and `load(version)` returned the **first** match, so it handed back
  the *older* document — the same data loss one layer down. Caught by the regression
  test written for the original bug, which is the argument for writing the test
  before believing the fix.

* **Made normative rather than remembered.** [SPEC.md](../SPEC.md) has a new **§7,
  Naming versus storing**: an implementation MUST NOT collapse the two, MUST store
  every top-level key, and MUST dedup on stored content rather than on the version
  name. §4.4 now says its exclusions are about *naming*, not about what a file
  format may discard. Five permanent regression tests.

* **Unicode decided, on Hormiga's offer: normalize at the boundary.**
  [SPEC.md](../SPEC.md) §6.1 is now **normative** — callers MUST supply NFC; Palabra
  does not normalize. Their framing settled it (*"we will normalize on input, where
  our keyboard is"*): the application is the only layer that knows text is being
  *entered*. Their bilingual Spanish/English database on mixed platforms is named in
  the SPEC as the case that forced the decision.

  **A precondition with no way to check it is a trap**, so `has_combining_marks` now
  exists — a **diagnostic, not a validator**, since some sequences are legitimately
  decomposed. It is for a host to assert over its own corpus. The test suite
  demonstrates the hazard directly: `café` composed and decomposed hash differently,
  which *is* the bug, stated as an assertion.

* **Encryption answered: ours, deliberately not yet, keys never ours.** The
  container should carry it — every app encrypting its own bundle is the failure
  that founded this library. But Palabra hand-rolled SHA-256 and that was defensible
  (a hash with published vectors); **AEAD is not that kind of primitive**, and
  rolling one before libsodium is in the tree would be the thing this project keeps
  refusing. Phase 4 brings it. Key management stays the application's, per
  [persistence](/concepts/persistence.md) — Hormiga keeps its vault.

* **The OKF honesty convention is now enforced, because it had already rotted.**
  The 2026-07-27 refactor moved every source file and left **all four `resource:`
  links dangling** while the bundle still claimed `status:current`. Nobody noticed
  until a hand sweep. `tools/check_okf.py` now runs as an eighth ctest suite,
  checking that every `status:current` page points at code that exists and that
  every internal link resolves. The convention is only a guarantee if something
  checks it.

* **Taken from their integration notes:** cJSON coexistence is not a problem —
  they linked against both `libvoidmaiz.a` and `libvoidpalabra.a` (same 1.7.18) and
  it worked. Better evidence than the caveat the README was offering, and it came
  from linking rather than reasoning.

* **Where the tree stands:** 8 suites, no leaks across 158,907 allocations, 76
  conformance vectors, clean under `-Wall -Wextra`, zero dependencies.


## 2026-07-27 — modular restructure, and the integration surface designed

* **The library is now modular by concern**, at the author's request (*"I don't
  want gigantic scripts"*). `join.cpp` was 619 lines with a 286-line `.inc` spliced
  into it; `canonical.cpp` was 557. Now:

  | folder | knows about | largest file |
  |---|---|---|
  | `src/encoding/` | values and bytes — **not runes** | 209 |
  | `src/canonical/` | runes and mantles — **not CRDTs** | 219 |
  | `src/crdt/` | merging — **not files** | 331 |
  | `src/store/` | blocks and containers — **not Void Core** | ~120 |
  | `src/archive/` | composes them | 319 |

  Public headers split to match, with `join.hpp` kept as an umbrella. Each layer has
  an `internal.hpp` for what is shared *within* it and hidden *from* callers —
  hand-assembling CRDT metadata or canonical bytes is how invariants get broken
  quietly, and an early test fixture proved exactly that.

* **The refactor was verified rather than hoped.** Every step was checked against
  the full suite and the 76 conformance vectors — which is what they were built
  for. Three real bugs fell out of the splits: `kFacets` and
  `JoinPolicy::lookup`/`core_defaults` duplicated across translation units, and a
  `live_of` deleted by an over-eager regex. All caught by the build and the tests;
  none by inspection.

  Worth recording as a toolchain fact: **`collect2` swallowed the linker diagnostic
  entirely**, reporting only `ld returned 1 exit status`. Invoking `ld` directly
  with explicit library paths produced the actual "multiple definition" messages.
  A toolchain that hides its errors is worth knowing about before a deadline.

* **[Integration](/design/integration.md) written** — the hooks an application
  implements, prompted by the author's note that Void Hormiga has an **antfarm**
  for protocol management while other apps will not.

  * **The advantage is knowing what every client is made of.** Palabra is not
    integrating with arbitrary software; every client is a Void Core application,
    so the integration surface is expressible in **Core's existing vocabulary**
    rather than a new one.
  * **The transport hook was already designed and did not know it.**
    [Transport shape](/design/transport-shape.md)'s sans-IO ruling means Palabra
    produces and consumes messages and never opens a socket — so *the application
    decides how they travel*. The antfarm carries them for Hormiga; a socket does
    for a chatroom; a plain loop does for an app with no graph. **None is a special
    case, because Palabra cannot tell them apart.** "Not all applications will have
    an antfarm" turns out not to be a problem to solve.
  * **On "universal holiday concepts", the honest split.** A holiday is a **port**,
    not a type — defined by position in the structure rather than by what is on the
    other end — so generality is inherited from the interaction-net model rather
    than invented. Two shapes are worth extracting, both sound for the same reason,
    **substitutability**: a *block source* (content addressing makes two
    implementations interchangeable, and a partial store is not a wrong one) and a
    *message pipe* (the protocol already assumes drops, reordering and duplication,
    so the interface has no guarantees to get wrong). A universal *semantic*
    holiday is **not** sound — Core's `query`/`get`/`insert` is general precisely
    because it says almost nothing, and making it say more would fit one backend
    and lie about the rest.
  * **The CLI, sketched and deliberately left unnamed.** Applications will extend
    these verbs into their own Voidscript frameworks, so the vocabulary is a
    compatibility surface, not a convenience. Four of seven candidate verbs have
    **no good word yet**, and `history` is actively dangerous — Core's is a local
    undo list and Palabra's would be a partial order, which is exactly the
    one-word-two-meanings failure [why not linear](/design/why-not-linear.md)
    exists to prevent. **What can be settled now:** every capability is reachable
    through Core's `effect` seam *before* it is a verb, so applications integrate
    today with no naming commitment.

* **Next structural move, recorded not done:** extract `BlockStore` to an interface
  so an app can back storage with its own (Hormiga's SQLite). Worth doing when that
  second implementation exists — an interface with one implementation is a guess
  about the second.

* **Where the tree stands:** 7 suites, clean under `-Wall -Wextra`, zero
  dependencies, no leaks across 154,392 allocations, 76 conformance vectors.


## 2026-07-27 — Fugue, and a test that was passing for the wrong reason

* **The sequence CRDT is built** ([join](/concepts/join.md)) — the last known gap
  before two people can edit one document comfortably, and the piece Void Hormiga's
  multi-user newsletter needs.

* **It needed no new merge code.** A sequence field is two OrSets — `nodes` (tag =
  the element's own id) and `dead` — so it merges by **union** and inherits the
  three laws from the primitive. `join` did not have to learn anything. That is the
  payoff of having exactly one primitive, showing up a phase later than it was
  designed.

  Deletion is an **add** to `dead` rather than a removal from `nodes`, because a
  removed element may still be another element's parent: a tombstone is
  load-bearing structure here, not litter.

* **A sequence is a representation, not a `FieldJoin`.** The obvious move was to
  add `FieldJoin::Sequence`, and it would have been wrong. `FieldJoin` decides how
  to *resolve* two concurrent writes to one scalar; a sequence never has that
  problem, because concurrent edits merge structurally instead of competing. They
  are orthogonal axes. The storage is self-describing, so no declaration is needed
  at read time either.

* **The headline test was passing for the wrong reason, and the fix is the
  interesting part.** Non-interleaving is the whole reason to choose Fugue over a
  simpler list CRDT: one editor typing `abc` while another types `xyz` must not
  converge on `axbycz`. The first version of the test used mints producing
  `Ae_0001` and `Be_0001` — which **sort into two clean groups**, so interleaving
  was impossible however broken the algorithm was. The test asserted a property the
  fixture had already guaranteed.

  Replaced with mints whose ids **already alternate when sorted** (`00000000`,
  `00000001`, …), so sorting the ids alone yields exactly `axbycz`. Plus a
  `the_fixture_is_actually_adversarial` test that guards the guard. Measured:
  **ids that sort to `axbycz` merge to `abcxyz`.**

  This is the third time today that a check turned out not to be checking —
  after the chunker that silently did nothing and the leak run that built no
  binaries. The pattern is consistent enough to name: **a passing test proves
  nothing until you have seen it capable of failing.**

* **Leak coverage extended** to `seq_order` / `seq_read`, which decode in loops and
  discard most of what they build, plus every refusal path. **12/12 balanced across
  154,392 allocations.**

* **Where the tree stands:** **7 suites** — canonical, join, sequence, store,
  archive, leaks, conformance — clean under `-Wall -Wextra`, zero dependencies, no
  leaks, 76 conformance vectors.

* **Next:** the reconciliation state machine, now item 3 and the last *unmeasured*
  claim in the bundle.


## 2026-07-27 — declared per-field joins, and "LWW" turned out to be unbuildable

Context from the author: **Void Hormiga is close to finished and needs this for
multi-user networking.** Not a request to build for one client, but a real signal
about which gaps matter — and it caught the roadmap in a mis-scheduling.

* **A wart found before it was built on.** `flatten` **silently picked the first
  value** of a conflicted field while `conflicts()` reported it separately. A
  caller who forgot the second call got an arbitrary answer with no signal — the
  silent-wrong-answer failure, sitting inside the function whose whole job is
  honest resolution.

* **Declared per-field joins built** ([join](/concepts/join.md)). A host supplies a
  `JoinPolicy`; a field resolves as `Conflict` (default), `Pick`, or `Max`.

  **The load-bearing decision: the policy is a READ-TIME projection, not a merge
  rule.** It is applied by `flatten` and `conflicts`, never by `join`. So two peers
  running *different* policies still merge to byte-identical documents — a UI
  difference rather than a data divergence — and nothing is destroyed, because both
  values stay in the document and a policy only chooses which to show. Had the
  policy touched the merge, **convergence would silently depend on
  configuration**, which would undo the central claim of the join page. Asserted
  directly: reading a merged document under three different policies must not move
  its bytes.

* **"LWW" turned out to be unbuildable here, and the name was a small lie.**
  [Join](/concepts/join.md) had said last-writer-wins is "acceptable" for
  `placement`. Implementing it revealed there is **no "last" without a wall
  clock**, and [history graph](/concepts/history-graph.md) forbids consulting wall
  clock to resolve anything. What is actually available is a **deterministic but
  arbitrary pick** — every peer chooses the same value, for reasons unrelated to
  recency. The enum says `Pick`, because a name promising recency would be a lie
  told at every call site. The concept page now says so too.

* **`Max` over a non-numeric value falls back to `Conflict`** rather than guessing:
  a declaration that does not match the data is a misconfiguration, and picking
  silently would hide it behind plausible output. The fallback for an *undeclared*
  field stays `Conflict` permanently — a field nobody thought about silently losing
  data is the failure this whole design exists to prevent.

* **The roadmap was wrong and is corrected.** It said the sequence CRDT was
  *"scheduled by the chatroom rather than by Hormiga"*. [Join](/concepts/join.md)
  names Hormiga's **newsletter block order** as the forcing case for
  non-interleaving, and Hormiga is going multi-user — so Fugue is now **item 2**,
  ahead of the reconciliation state machine. With per-field policies shipped, it is
  the last known gap between "converges" and "converges into something a person
  wanted".

* **Leak coverage extended to the new paths.** The first run after this work
  reported the same 70,455 allocations as before — meaning the `Max` resolver,
  which decodes every candidate in a loop and discards most of them, was never
  exercised. Added; now **11/11 balanced across 105,693 allocations**.

* **Where the tree stands:** 6 suites, clean under `-Wall -Wextra`, zero
  dependencies, no leaks, 76 conformance vectors. Join suite: 22 tests / 807 checks.


## 2026-07-27 — structural chunking, file I/O, and a roadmap made honest

* **Structural chunking built.** A state document is no longer byte-chunked: it is
  split **one block per rune**, plus a compact binary manifest naming them. This
  escapes the tuning tradeoff rather than moving it — coarser byte-chunks bill more
  per edit, finer ones spend it on keys — because *structure* aligns block boundaries
  with **edit** boundaries. An edit to one rune now **cannot** dirty another,
  whatever the byte offsets do.

  Measured: 100 revisions of a 60-rune document fell from **11.3x** one save to
  **5.1x**, and the archive as a whole from 76x to **97x** smaller than `.miga`.
  This is the prolly-tree idea at the granularity that matters here.

* **File I/O added, and it belongs here.** [Archive](/concepts/archive.md) handed
  back bytes and opened nothing — a real gap rather than a clean boundary, because
  Core does no file I/O **by definition**, so a library that declines it forces every
  application to re-solve it. That is the founding failure, one layer up.

  `write_file` is write-temp-then-rename, with the temporary **beside** the target
  (so the rename stays on one filesystem and is atomic) and an `fflush` before it
  (so a power loss cannot leave a correctly-named empty file). Tested for the
  observable halves of atomicity: no temporary left behind, and a failed write
  elsewhere leaves the existing archive byte-identical. Read errors are
  **distinguished** — *cannot open* and *not a valid archive* are different problems.

* **The [roadmap](/roadmap.md) rewritten, because it had drifted into fiction.** Its
  frontmatter still said *"nothing is built"*; it violated its own stated convention
  (*"lists only what is not yet done"*) in six places by celebrating finished work
  with checkmarks; and its research section listed two questions Core had answered
  that morning. Rewritten to Core's convention — **shipped work lives in the log and
  in each concept's `status:`; the roadmap is forward-looking only** — with a compact
  state table and an explicit **next three things**:

  1. **File I/O** — done this turn; it was the gap between "built" and "usable".
  2. **The reconciliation state machine** — the biggest *unmeasured* risk. The join
     suite proves convergence under arbitrary **merge** order, which is not the same
     claim as arbitrary **message** order, and the second has never been tested.
     Unblocked per [transport shape](/design/transport-shape.md) §3.
  3. **The trust model** — the long pole, and design work rather than code. It gates
     the chatroom, thinking time cannot be compressed by writing code faster, and
     Hormiga's libsodium is already vendored so the implementation is cheap once the
     design lands.

* **Where the tree stands:** 6 suites, clean under `-Wall -Wextra`, zero
  dependencies, **no leaks across 70,455 allocations**, 76 conformance vectors.


## 2026-07-27 — foundations pass: a contract, a leak check, and two real bugs

A deliberate consolidation turn rather than a feature one — *"making sure we aren't
just trying to achieve an end result, but that we are making something we can truly
build on top of."* It found three things.

* **`conformance/` exists — SPEC.md is now a contract rather than a document.**
  76 language-neutral vectors across §2–§5, in the shape
  `VoidCore:conformance/reduce/` proved. We told Core *"a layer with a contract gets
  ported; a layer without one gets reinvented"* and then had no contract ourselves;
  that is closed. Done **ahead of** a second implementation deliberately: it is what
  makes one possible, and Palabra's whole claim — two independent peers compute the
  same name — is one that exactly one implementation cannot test.

  **The part worth copying: the suite enforces RELATIONS, not just recorded values.**
  A vector file regenerated against a broken implementation would still be perfectly
  self-consistent, so case names carry assertions the runner checks between adjacent
  cases (`— must equal`, `— must DIFFER`): tags as a set, hydration invisibility,
  undirected-edge symmetry, directed-edge asymmetry, content order preserved,
  peer-local exclusion. Verified by deliberately corrupting a vector and confirming
  the runner catches it. Regenerating to silence a failure is called out in the
  README as how a contract stops being one — the honest move is a `CANON_VERSION`
  bump.

* **A real correctness bug in `conflicts()`: it built JSON by string
  concatenation.** A mantle name or `spirit.id` containing `"` or `\` produced
  malformed output — and `spirit.name` is user-editable text, so this was
  injection-shaped. It also **silently skipped mantle-level registers**, reporting a
  converged document while two peers disagreed about a mantle's `domain`.

  Rewritten as a proper value: a `Conflict` struct with a **content address**, built
  through cJSON so escaping is not hand-rolled, covering mantle and rune fields, and
  ordered deterministically. [Conflict](/concepts/conflict.md) always said a conflict
  *"has a hash… can be stored, synced, queried, tagged, and rendered"* — it returns
  strings no longer.

* **A leak check that actually runs.** The first attempt used
  `-fsanitize=address`, reported everything clean, and had **built no binaries at
  all** — the MinGW/ucrt64 toolchain has no working ASan. A check that quietly does
  not run is worse than no check, so it was replaced with allocation counting through
  cJSON's own hooks: portable, and it covers the paths that matter because cJSON is
  the only allocator Palabra's structures use.

  **10/10 balanced across 66,108 allocations**, including every refusal path —
  malformed input, truncated containers, bad base64, unparseable bundles — which is
  exactly where leaks hide. Plus a 20x repetition pass, so a leak that only appears
  under repetition cannot hide either.

* **Also fixed:** a literal NUL byte that a scripted edit wrote into `join.cpp`
  source (compiler warning, worked by accident); `decode` moved from a file-local
  duplicate into [canonical form](/concepts/canonical-form.md) where it belongs, with
  the header explicit that it is **not** a round-trip law.

* **Where the tree stands:** **6 suites** — canonical, join, store, archive, leaks,
  conformance — clean under `-Wall -Wextra`, zero dependencies, no leaks, 76 vectors.

* **What this turn deliberately did not do:** structural chunking, still the top
  Phase 2 item. Consolidating first was the point; it turned up two bugs that more
  features would have built on top of.


## 2026-07-27 — the first client surface, and the two forcing clients named

* **The author named the two stress tests** — recorded in
  [forcing clients](/design/forcing-clients.md), because they change what is worth
  building next:
  1. **Void Hormiga's save system**, replaced by Palabra. Still called "save" and
     "load" in the UI; local version tracking underneath.
  2. **A LAN chatroom on Void Maiz**, as the first device-to-device test.

* **They are complementary, and in the right order.** Hormiga tests the merge algebra
  hard and tests transport not at all; a chatroom tests transport hard and the join
  **weakly** — chat is append-mostly, very nearly a grow-only set, which is the
  easiest CRDT workload there is. Doing the chatroom first would produce a demo that
  works over a merge nobody had stressed.

* **The finding: Hormiga's save/load needs no history graph, so it is blocked on
  nothing.** [Why not linear](/design/why-not-linear.md) §4 already says linear
  thinking is *correct* for one user at one keyboard — so a linear list of named cuts
  is not a degraded history graph, it is the right structure. The graph buys the
  **partial** order, and that does not exist until device two. Local version tracking
  therefore ships on Rung 0 + Phase 2, without waiting on Core's reified commands.

* **[Archive](/concepts/archive.md) built** (`src/archive.cpp`, 13 tests / 68 checks)
  — the first Palabra surface aimed at a *client* rather than at the library's own
  layers. `save` / `load` / `load_latest` / assets / `to_bytes`, plus `import_miga`
  for the `.miga` v3 bundle.

  **The measurement:** 100 saves of a 60-rune document plus a 400 KB asset cost
  **710 KB**, where `.miga` would cost **~54 MB** — 76× smaller, and `.miga` keeps
  *one* version where this keeps a hundred.

  Saving the same state twice is free (a hash comparison, not a heuristic); `load`
  **verifies** that stored bytes still name the version they are filed under;
  `import_miga` is **one-way by design**, and refuses a bundle whose asset fails to
  decode rather than producing a partial import that looks complete.

* **A tuning finding that turned into a design finding.** The first archive reused
  the asset chunk parameters for state documents, and 100 saves cost **64×** one
  save — barely dedup at all. Cause: a 20 KB state document lands in ~3 chunks at an
  8 KB average, so editing one paragraph rewrites a third of it. Finer chunks took it
  to **11×**, which is *exactly one chunk per save* — the floor for chunking by
  bytes.

  The real answer is not a better constant: it is chunking by **structure** (one
  block per rune) so an edit to one rune can never dirty another, which would give
  roughly 2×. That is the prolly-tree item, now **promoted to the top of Phase 2**
  because it is what makes Hormiga's version history genuinely cheap. The suite
  asserts the current 11× as a **ceiling**, so the improvement is visible when it
  lands and cannot silently regress meanwhile.

* **`decode` moved into [canonical form](/concepts/canonical-form.md) and made
  public** — the archive index needs it, and it had been duplicated file-locally in
  the join. The header is explicit that this is **not** a round-trip law: the
  canonical form is one-way by design, so `encode(decode(b)) == b` holds while
  `decode(encode(x))` may differ from `x` in ways §2 declares meaningless.

* **What the chatroom will force, recorded before it exists:** the **sequence CRDT
  (Fugue)**, since per `VoidCore:SPEC.md §4` an app needing an order puts it in a
  content field — and message interleaving between two typists is nonsense a user
  notices instantly. It is also where **wall clock will try to sneak back in**, since
  chat UIs show timestamps and ordering by them is tempting;
  [history graph](/concepts/history-graph.md) forbids consulting time for causality,
  and this is the first place that rule will be under real pressure.

* **Where the tree stands:** 4 suites, **69 tests / 1,991 checks**, clean under
  `-Wall -Wextra`, zero dependencies.


## 2026-07-27 — Phase 2: dedup works, and a property test caught a bug the round-trips could not

* **[Persistence](/concepts/persistence.md) is `status:current`** for the store and
  container — `src/store.cpp`, 14 tests / 254 checks. Codecs and platform mapping
  are **not** built, and the page says so.

* **The measurement, which is the whole point of the rung.** A 400 KB asset with a
  4-byte edit costs **9,138 new bytes** — one chunk. Re-storing an unchanged asset
  twenty times costs **zero**. That is Void Hormiga's base64 inlining fixed, with
  one device and no sync, which is what
  [the roadmap rule](/roadmap.md) demands of every rung.

* **Content-defined chunking is now a measurement rather than a citation.** Across
  an insertion near the *front* of a 400 KB blob — the worst case — it keeps
  **97.8%** of its chunks where fixed-size splitting keeps **0.0%**. The suite
  asserts *both* numbers, so the comparison cannot quietly become meaningless if the
  baseline changes. [Academic foundations](/references/academic-foundations.md) §5
  claimed this property; it is now checked.

* **The bug, recorded because it looked like it worked.** The first chunker masked
  the **low** bits of the gear hash. In `h = (h << 1) + g[b]` the low bits barely
  mix — bit 0 of `h` is just bit 0 of the current byte's gear value — so over a
  small alphabet the mask could never reach zero: **no cut ever fired, every chunk
  came out at `max_size`, and dedup silently did nothing.**

  Every round-trip test passed the entire time. Storing worked, reading worked,
  containers verified. Only the property test that **measured chunk survival against
  a fixed-size baseline** could see it — a test that asserts a *number* rather than
  an invariant, which is a shape this suite had not needed until now.

  The fix: test the **high** bits, which accumulate over the last ~64 bytes, and
  prime the hash across that window before the first legal cut point so a boundary
  depends on content rather than on where the previous chunk ended.

* **The container is self-verifying, not merely parseable.** Every block is
  re-hashed against its key on read; a mismatch is a refusal, because handing out
  wrong data under a right-looking name is the one thing content addressing exists
  to prevent. Bad magic, unknown version, truncation and trailing garbage are all
  refused rather than guessed at. Blocks are written in key order, so equal content
  produces byte-identical files — the container **inherits** the canonical form's
  determinism instead of having a weaker notion of its own.

* **[SPEC.md](../SPEC.md) §5 written** — the enriched document, which was the loose
  end named at the end of Phase 1 and is what a second implementation would most
  need. It states the observed-remove primitive, the add-wins consequence, the
  MUST-NOT on version vectors, the shape, the three laws with byte-equality as the
  definition of equal, the round-trip law, and §5.5's honest note that pruning is
  unsolved. The container format is **not** in SPEC yet and is now the next gap.

* **Where the tree stands:** 3 suites, **56 tests / 1,923 checks**, clean under
  `-Wall -Wextra`, zero dependencies.
    * Rung 0 — [canonical form](/concepts/canonical-form.md) ✅
    * Phase 1 — [join](/concepts/join.md) ✅ (sequence CRDT and declared per-field
      joins outstanding)
    * Phase 2 — [persistence](/concepts/persistence.md) ◑ (prolly trees, codecs,
      platform mapping outstanding)


## 2026-07-27 — Phase 1: the join is built, and it needed a document Core does not have

* **[Join](/concepts/join.md) is `status:current`** — `src/join.cpp`, 15 property
  tests / 789 checks. The laws are checked on **random** documents and compared
  through the [canonical form](/concepts/canonical-form.md), so "equal" means
  byte-identical, which is the only definition that survives two machines. Beyond
  the three laws: the **round-trip law**, replay-tolerance (the same peer's state
  received twice, interleaved, changes nothing), and **convergence under every
  gossip order of three peers**.

* **The finding that shaped the phase: a join over plain Void Core state is
  impossible.** Two peers holding `{a}` and `{}` cannot tell *"I never had a"* from
  *"I removed a"*, so any join of bare state is union and **removes never
  propagate**. Distinguishing them requires per-element metadata that lives beside
  Core's state document — so Palabra merges an **enriched document**, with
  `enrich`/`flatten` as the seam.

  This is the concrete vindication of the 2026-07-27 roadmap reordering. The
  container format has to store this metadata, and Phase 2 could not have designed
  it without knowing this shape. Putting the join first was right for the stated
  reason.

* **Unique tags, not version vectors** — one tag per add (Shapiro's original
  OR-Set). Version vectors are O(peers) and
  [history graph](/concepts/history-graph.md) rejects them for exactly that; tags
  are O(adds), a growth problem rather than a scaling wall, and it is the growth
  already recorded as [open questions](/design/open-questions.md) §5.
  **Core's random ids pay for themselves a third time**: conflict-free concurrent
  creation, `O(n log n)` canonicalization, and now observed-remove with no peer
  registry.

* **One primitive, three uses.** An observed-remove set of `tag → value` is the
  OR-Set (tags), the multi-value register (a content field), and the keyed OR-Map
  (runes by `spirit.id`). `join` is union on both members, so the three laws hold
  **by construction** — set union is commutative, associative and idempotent, and
  nothing in the structure can break them. That is the entire correctness argument,
  and it is why there is exactly one primitive.

* **Add-wins is a consequence, not a preference.** A remove retires only the tags it
  *observed*; a concurrent add carries a tag the remover never saw, so it survives.

* **What the property test caught, and it was the good kind.** Document-level
  idempotence failed while the primitive's held — `canon_doc` was encoding the
  document *as written*, so two byte-different representations of the same CRDT
  value got different names. **`a ⊔ a = a` held as a value and failed as a name.**
  That is the Rung 0 failure exactly one level up. `canon_doc` now normalizes every
  OrSet before encoding.

  It was only visible because the test fixture hand-wrote CRDT metadata, which is
  also why `set_field` / `add_tag` / `remove_tag` now exist — nothing outside the
  library should be writing that metadata, and the fixture proved why.

* **Conflicts are real values now**, in the [conflict](/concepts/conflict.md) shape,
  with `sides` canonically ordered so two peers name the same divergence
  identically — and symmetric, because there is no main branch to privilege a side
  from. Concurrent edits to *different* fields of one rune do not conflict; two
  peers writing the *same* value concurrently do not conflict either.

* **Still open under this phase:** the sequence CRDT (Fugue) as a per-glyph opt-in,
  per-field declared joins (LWW for a cursor, a counter for a tally), and — now the
  most valuable — **the enriched document's shape belongs in
  [SPEC.md](../SPEC.md)**, which currently specifies only the canonical form.


## 2026-07-27 — Core answered: one question closed, one defect fixed, one parameter deleted

Reply received: `MESSAGE_FOR_VOIDPALABRA_core-two-contracts-and-the-id-defect-2026-07-27.md`.
All four asks answered, two of them shipped. Every claim below was verified against
Core's tree before acting on it.

* **Rune order is NOT semantic — `VoidCore:SPEC.md §4`, normative.** The highest-
  leverage question in this bundle, answered *empirically* rather than by opinion
  (Core ran the sequence: append, rename-keeps-position, rm-closes-gap,
  undo-restores-position, export-round-trips — and no verb's semantics depend on any
  of it). Order is *incidental information faithfully carried*, not meaning. Core
  drew both consequences for us: a canonical form **MUST** be order-insensitive, and
  an app needing an order **MUST** put it in a content field.
  * **[Open questions](/design/open-questions.md) §2 is closed** (kept in place with
    its number so references still resolve).
  * **[Join](/concepts/join.md) rewritten:** the sequence CRDT (Fugue) drops from
    *foundational* to a **per-glyph opt-in over an ordered content field**. Hormiga's
    newsletter ordering is Hormiga's, and it is a field. **That is about half of
    Phase 1 removed.**

* **A parameter deleted the same day it was written.** `Policy::rune_order` existed
  only to carry §2 into the content address without pretending it was settled. Core
  settled it hours later, so the knob **went rather than acquiring a default** — a
  policy whose wrong setting is *silently* wrong is worse than no policy.
  `include_view` stays, because both of its settings are correct for different
  questions. **`CANON_VERSION` bumped to 2** on the principle that made the parameter
  vanish: the rule is *if the bytes move, bump*, and reasoning case-by-case about
  whether a change "really counts" is how that guard rots. Nothing consumed v1.

* **The reduction defect is fixed — and our diagnosis was wrong in a way worth
  keeping.** We reported CSPRNG ids. It was actually a **monotonic counter**
  (`_r1`, `_r2`, …), with no CSPRNG in the reduce path at all — which is *worse*,
  because `_r1` is a fact about the **schedule**, and Core's contract deliberately
  leaves schedule free. So divergence needed only two peers picking different,
  equally valid redex orders. And the counter becomes **`spirit.name`**, the thing
  `layout.edges` references and tag expressions match:

  > Two peers held structurally identical mantles whose runes were **named
  > differently**. Our hashes were the symptom; the names were the disease.

  Our proposed fix was adopted (with both components made unordered), is **normative**
  now — the licensing sentence *"fresh agent ids are implementation-defined"* is gone
  — and is pinned by new case **15-derived-ids**, which requires 16 randomized
  schedules to agree on literal ids and was verified to fail against the old counter.
  15/15 reduce conformance.

* **What that leaves on us:** `reduce --commit` still re-mints a random id,
  deliberately, because committing is *authoring*. So **merge-by-reduction must
  compare the pure preview, never a committed mantle** — recorded in
  [conflict](/concepts/conflict.md) and [open questions](/design/open-questions.md)
  §3.2. Whether Phase 3 ever needs byte-identical *committed* mantles is now an open
  question on our side; Core has asked that a caller-supplied-id API be discussed
  rather than quietly added.

* **`spirit.id` will not become content-derived** — recorded normatively in
  `VoidCore:SPEC.md §3.1`, with the reasoning rather than just the prohibition. Core
  noted the vertex-labeling argument was one they had not made themselves and would
  not have thought of. The §3.2 carve-out stays consistent with it: derived-but-
  deterministic ids are still *unique labels*, merely reproducible.

* **Both conformance contracts shipped** — `conformance/temper/` 8/8 and
  `conformance/scry/` 8/8, in the shape `conformance/reduce/` proved. Core kept the
  framing rather than just the request: *a layer with a contract gets **ported**; a
  layer without one gets **reinvented***. Their runners enforce each layer's **laws
  on every case** (temper: idempotence + purity; scry: purity) and report them by
  name — an implementation that matches every expected output but breaks a law is
  not conforming. Verified present in Core's tree.

* **[SPEC.md](../SPEC.md) written** — Core asked for the canonical byte encoding
  *"when it is real"*, so it is now a language-neutral normative document:
  the tagged length-prefixed encoding, the map/set/sequence rules, domain separation,
  the Void Core bindings, and §5's known gaps stated in the spec rather than a
  tracker. §1.1 argues canonical JSON is insufficient using **Core's own** evidence —
  their `provenance` hash makes `{"n":1}` and `{"n":1.0}` differ because Python
  writes `1.0` where JavaScript writes `1`.

* **One point of byte-level agreement with Core, now tested.** Their
  `provenance({}) == 44136fa355b3678a` (pinned in `conformance/scry/07`) is
  `sha256("{}")` truncated — verified independently, then added as a test. The
  encodings differ and should; the **hash underneath must be the same function** or
  nothing downstream can be compared. 27 tests / **880 checks**.

## 2026-07-27 (earlier) — first code: Rung 0 ships, in C++

* **Language ruled: C++20 behind a C ABI, CMake, zero dependencies.** Rung 0 was
  started in Python and the author corrected it the same day. The reasoning was
  bad and the correction is right: the **consumers are C++** — Hormiga is C++20 and
  is the forcing client for Phase 2's container format, Maiz is C++ and Hormiga
  builds it as a subdirectory — so a Python library is one they cannot link. And
  `okf/concepts/peer-and-tier.md` makes an ESP32 a full peer; it will never run
  CPython. The Python default was inherited from Core's `scry`/`temper`/`reduce`
  without checking who links Palabra.

* **Rung 0 built** — [canonical form](/concepts/canonical-form.md) is the first
  concept in this bundle to earn `status:current` with a `resource:` link.
  `src/canonical.cpp`, `include/voidpalabra/canonical.hpp`,
  `tests/canonical_test.cpp`: **27 property tests, 816 checks, no warnings under
  `-Wall -Wextra`**. Input is a `cJSON` tree, because that is what Core and Maiz
  already hold — Palabra deliberately defines no value type of its own.

  The suite is deliberately in two halves. *Permute everything meaningless — rune
  order, edge order, tag order, key insertion order, arrival order — and assert
  the bytes do not move.* Then *change anything meaningful and assert they do*.
  A canonicalizer that hashes everything alike passes the first half perfectly and
  is worthless; the second half is what makes the first mean something.

* **Five things the code decided that the design had left implicit:**
  * **A set must deduplicate, not merely sort** — caught by a failing test. Sorting
    without collapsing duplicates breaks **idempotence** (`a ⊔ a = a`), so a peer
    that received a duplicate over a lossy transport would have diverged from one
    that did not. Sorting is presentation; deduplication is the algebra.
  * **Integral floats fold to integers.** cJSON stores every number as a `double`,
    so `1` and `1.0` are indistinguishable by the time Palabra sees them; without
    the fold the bytes would depend on how a host wrote a literal.
  * **Big-endian on the wire** — byte order is a property of the machine, not the
    value, and two peers need not share one.
  * **Refusal beats guessing** — `NaN`, infinity, `cJSON_Raw`, invalid UTF-8 and
    duplicate keys raise rather than hash.
  * **The policy is inside the digest**, so two peers running different answers to
    [open questions](/design/open-questions.md) §2 disagree *loudly*.

* **One honest gap, recorded rather than hidden** — Unicode normalization is not
  applied ([open questions](/design/open-questions.md) §8, new). UTF-8 *validity* is
  checked; NFC is not, because it needs the decomposition tables. This is the only
  known way the built code can violate its own headline requirement, and it would
  first bite when a mac and a Windows device sync.

* **SHA-256 in-tree and proven.** Chosen over BLAKE2 because it is
  hardware-accelerated on the ESP32. Implemented rather than vendored to keep Rung 0
  zero-dependency, and **verified against the FIPS 180-4 vectors** including the
  block-boundary and million-byte cases — a hand-rolled hash must be proven, not
  assumed. It should become libsodium's `crypto_hash_sha256` at Phase 4, which
  Hormiga already vendors built.

* **A message was sent to Void Core** —
  `VoidCore:MESSAGE_FOR_VOIDCORE_palabra-cxx-and-three-blockers-2026-07-27.md`,
  the one the last entry recorded as *not done*. It carries the reduction-identity
  defect, the "do not make `spirit.id` content-derived" constraint, the
  `mantles`-only slice opinion, and one finding that came out of checking the
  family before writing it:

  > **Reduce has a portable contract and Scry and Temper do not — and it already
  > cost two reimplementations.** `VoidMaiz/src/reduce/reduce.cpp` opens *"port of
  > reduce/net.py + reduce/reduce.py **against the conformance contract**"*, while
  > `VoidMaiz/src/project/project.cpp` (scry) and `VoidHormiga/src/temper.hpp`
  > were rebuilt from the concept pages, because `conformance/` has a `reduce/`
  > directory and nothing else. The layer with a contract got ported and checked;
  > the layers without one got guessed at. That is the same "re-invented
  > incompatibly" failure that founded [persistence](/concepts/persistence.md),
  > one step behind. The ask is two conformance directories, which is additive and
  > needs no new Core code.

* **Also noted for Palabra's own Phase 1:** `layout.edges` reference runes by
  **mutable `spirit.name`** (`VoidCore:SPEC.md §3.7`), not by immutable id. Coherent
  for one user; across peers, two who concurrently rename a rune and add an edge to
  it produce edge sets that cannot be reconciled by name alone. The OR-Set join must
  resolve names to `spirit.id` before comparing. No Core change requested.

## 2026-07-27 (later) — Void Bicho founded; the compute pillar splits

* **Void Bicho founded** at `../VoidBicho`, by the
  author's ruling, directly out of this bundle's [compute](/concepts/compute.md)
  ruling. It owns the half Palabra refused: loading AI models and running them inside
  Void Core applications. **Palabra is developed first**; Bicho's bundle exists now
  only so the boundary is settled before either side hardens.

* **Neither library imports the other — ruled**, and the reason is symmetric and
  concrete. *If Palabra imported Bicho*, **every peer in the mesh would need a model
  runtime to reconcile state**, which undoes the `State` tier in one line — an ESP32
  is a full participant precisely because it may decline what it cannot hold. *If
  Bicho imported Palabra*, a single laptop with no peers would need a version-control
  library to summarize a document. They meet at **two data shapes**: the **capability
  fragment** (which Palabra's peer declaration already embeds) and the **provenance
  stamp** (which Palabra records without validating, exactly as it records `who`).
  The standing test: *a shared data shape is fine; a shared function call means one
  library is about to depend on the other.*

* **The joint property, named on both sides.** Palabra's addressable prompt
  (`prompt_cut: v:8c41f2…`) plus Bicho's stamp make **an agent run a reproducible
  experiment** — exact input state recoverable by hash, exact model and parameters
  recorded beside it. Neither library can do this alone, and the seam costs neither a
  dependency. Bicho's prior-art survey records it as the one genuinely unbuilt thing
  in that bundle.

* **Where a world model would run is now answered** —
  [world models](/design/world-models.md) said *whether* (mostly no; four narrow
  places yes) and left *where* open. It is a model in Bicho with `kind: "world"`: a
  rune, a runtime, a budget, a stamp. Deliberately boring, which was the test.

* **Pages touched:** [compute](/concepts/compute.md) (the excluded-work table now
  names owners rather than gesturing; the provenance stamp is split into Palabra's
  half and Bicho's; the no-import argument recorded),
  [world models](/design/world-models.md), [index](/index.md), [roadmap](/roadmap.md),
  [open questions](/design/open-questions.md) §7 (and the note that
  *may-a-peer-spend-my-compute* is the one compute question genuinely Palabra's, since
  it has no analogue in a read/write capability model), and the README.

* **Not changed:** nothing on this roadmap waits on Bicho, and nothing on Bicho's
  waits on Palabra. That was the point of the ruling.

## 2026-07-27 — three pillars, one defect, and a reordering

* **The three pillars named** (author). Palabra is not only versioning: it is
  **remembering** (non-linear version control), **speaking** (device-to-device
  communication, LAN first but abstract enough for BLE), and **computing** (system
  management, including agents and models). Mapped in [index](/index.md); the first
  two were already the bundle, the third was not.

* **Scope ruling on the compute pillar** — new [compute](/concepts/compute.md):
  **Palabra owns the naming, routing and history of compute; it does not own the
  computing.** The same split that founded Palabra, applied one level up. Concretely:
  capability declaration **generalizes tiering** (history tier, codecs and compute are
  three axes of one statement); a **prompt is a scry**, and because a
  [cut](/concepts/version-as-cut.md) has a name, a prompt is **addressable by the hash
  of the state it was built from** — reproducible agent runs, which nobody else has;
  and **inference is an effect, so it can never be an utterance**, though the write of
  its result can be, stamped with provenance (Core's `materialize(stamp=)` idiom).
  Explicitly excluded, with owners named: model loading (a holiday), agent scheduling
  and budgets (**Latin-OS**, whose §2 design is already good — an agent is a rune in
  task × compute mantles, scheduling is reduction, budget is the termination guard),
  and durable agent memory (already solved: a memory is a rune).

* **A defect found in the existing design.** Agents created by rules **during
  reduction** get fresh CSPRNG identities and are recorded by no utterance, so two
  peers reducing the same net independently produce **different hashes for identical
  state**. This **invalidates merge-by-normal-form** as written in
  [conflict](/concepts/conflict.md): confluence gives the same normal form *up to
  renaming*, which is not the same bytes. Proposed fix is small and belongs to Core —
  mint from the redex, `H(rule_id, sorted(parent_ids), port_index, occurrence)`.
  Recorded as a **third Core blocker** ([open questions](/design/open-questions.md)
  §3.2) and the **smallest of the three**, so it should be asked for first: it gates
  the one genuinely novel thing in the project.

* **Canonical form promoted to a concept and to Rung 0** —
  [canonical form](/concepts/canonical-form.md). Content-addressing a mantle is
  content-addressing a *graph*, which is graph canonicalization in general — but
  Core's `spirit.id` makes the graph **vertex-labeled**, collapsing it to an
  `O(n log n)` sort. Core's random IDs now pay for themselves **twice** (the other
  being conflict-free concurrent creation), which is recorded as a **constraint
  Palabra places back on Core**: do not "clean up" `vc_mint_id` into content-derived
  IDs without Palabra in the room.

* **Roadmap reordered.** **Rung 0 (canonical form)** goes underneath everything — a
  format can be revised, but a hash two peers compute differently is a system that
  silently fails to converge. And **the join now precedes persistence**, because the
  container format must store CRDT metadata whose shape Phase 1 decides; the bundle
  already warned against designing the format first, and this is that warning applied
  one step earlier. The founding rule (*every rung earns its keep with zero peers*) is
  unchanged. Neither Rung 0 nor Phase 1 is blocked by anything.

* **Transport shape ruled — sans-IO.** New [transport shape](/design/transport-shape.md):
  the protocol is a **pure state machine** `step(local, incoming) -> (local',
  [outgoing])`; transports are dumb byte pipes that cannot influence it. This is what
  actually makes LAN/BLE/sneakernet interchangeable (a `send`/`recv` interface leaks
  LAN assumptions and then forces the second cut-down protocol that
  [tiering](/concepts/peer-and-tier.md) exists to prevent), and it makes the whole
  protocol **testable under adversarial scheduling with no network** — the same shape
  as Core's confluence property tests. Recorded as an **amendment** to
  [open questions](/design/open-questions.md) §6: the security blocker covers
  *transport*, not the protocol's *algebra*, subject to three stated conditions.

* **Domains are peer-local — ruled.** An [utterance](/concepts/utterance.md) targets
  **`mantles` and nothing else**; `domains`, `bindings` and `config` are peer-local
  resolution and never sync. The forcing argument is not aesthetic: a domain carries
  real deploy commands, so syncing one means **one device's deploy command runs on
  another device**. This is Palabra's answer to the author's *"how will they update
  each other's domains?"* — they do not; a mantle names a domain, and each peer
  resolves the name locally, exactly as a git remote's path is not cloned. Also
  Palabra's stated opinion on Core's `SPEC.md §12`.

* **Mathematics extended** ([academic foundations](/references/academic-foundations.md)
  §3.1, §3.2, §9, §10), in answer to the author's *"graph theory, 4-dimensional
  mathematics, topology?"*:
  * **Birkhoff** — the set of all versions is a **distributive lattice** determined
    entirely by the history poset, and **an utterance is precisely a join-irreducible
    version**. That is the sharpest definition of "atomic change" available, and it is
    a theorem; it also independently confirms the n-utterances-per-batch ruling, since
    a collapsed batch is join-*reducible*.
  * **Dimension and width** — the "4D" instinct is really **order dimension**
    (Dushnik–Miller): **git is exactly dimension 1**. Dimension is NP-hard, so it is
    vocabulary; **width** (Dilworth) is polynomial and operational — heads form an
    antichain, and **Dilworth's chain decomposition is the rendering algorithm for
    Void Maiz**: a width-`w` history draws as `w` swim lanes with nothing invented,
    which is strictly better than a linear extension.
  * **Topology — real and modest.** Cuts are the open sets of the **Alexandrov
    topology**; merge is union. Same information, re-labelled: vocabulary, no new
    theorems. **Sheaves** are the one place it could pay — conflict *is* nonzero H¹ —
    but in a join-semilattice associativity makes the obstruction vanish, so the sheaf
    view **confirms** the design rather than extending it. Where topology genuinely
    bites is interaction nets: a net's identity is its wiring up to deformation, which
    is why `swap` is load-bearing and why §10 exists.

* **The DINO/JEPA proposal answered** — [world models](/design/world-models.md).
  Three of its arguments do not survive: partial-order-as-bidirectional-attention is a
  **pun** (unrelated mechanisms); watching a peer's UI is **strictly worse than
  reading a hash** in a content-addressed system; and predicting merge outcomes
  **approximates a cheap exact function**. The rule adopted instead: *a learned model
  belongs exactly where Palabra's mathematics runs out* — conflict **resolution**,
  **pruning policy** (the strongest fit, and the one the memo never mentions),
  **peer scheduling**, and **semantic search over history**. And the argument the memo
  missed, which is the best case for its own thesis: **Palabra produces the training
  corpus these models need** — every utterance is a labeled, content-addressed,
  replayable `(state, delta, state')` triple. That makes it a **schedule, not a
  subsystem**: build Palabra first.

* **Not done (deliberate):** no code; no message sent to Void Core about the
  reduction-identity blocker; naming still the author's call
  ([open questions](/design/open-questions.md) §1).

## 2026-07-24 — founding

* **Void Palabra founded.** Split out of Void Hormiga's message
  `MESSAGE_FOR_VOIDCORE_hormiga-versioning-and-sync-2026-07-23.md` (Part 2), which
  proposed a versioning + peer-sync layer **native to Void Core**. The author's
  ruling: it is **its own library**, not a Core subsystem — Core does no file or
  network I/O by definition (`VoidCore:SPEC.md §9`), and a store plus a transport
  are both, in Core's own vocabulary, holidays. Scope was widened past versioning
  to **the system layer generally**: persistence, file formats, filesystem mapping,
  and device-to-device communication. Palabra is a **library**, not an application.

* **Framing — the fourth face.** Void Core's *one core, three faces* extends:
  Void Maiz serves humans (a timeline), Void Core's CLI serves agents (a filtered
  recent slice), Void Palabra serves the system (the whole partial order). These
  are **three projections of one object**, not three vocabularies — which is what
  `VoidCore:/concepts/scry.md` already does for mantles. A full history is useless
  to a human and mostly useless to an agent, and non-negotiable for version
  control; Palabra holds it so the others need not.

* **The central ruling — history is a partial order, not a sequence.** The author's
  position (*"why does version control need to be sequential? treating things as
  existing in a 1-dimensional axis of time seems like it won't be the best
  architecture"*) is adopted, and grounded rather than asserted: four independent
  formalisms agree (Mazurkiewicz traces, Mimram–Di Giusto's patch category,
  Merkle-clocks, multiway systems), and two ship in production (Pijul, Matrix's
  room-state DAG). Recorded as the project railguard in
  [why not linear](/design/why-not-linear.md), with a **translation table** from
  git's vocabulary — because the failure mode is invisible and an agent will
  reconstruct git from words alone.
  * **Determinism is not given up.** The partial order is *more* deterministic
    than a log: a log records an arbitrary choice as fact, while a partial order
    declines to invent an ordering nobody knew. A timeline is recovered as a
    **linear extension** with a canonical (hash-order) tiebreak.
  * **"What version is this?"** has a concrete answer: a version is a
    **downward-closed cut**, named by the **Merkle hash of that cut** —
    order-independent, verifiable, and identical on two peers who received the same
    utterances in opposite orders.

* **Decision — Δ-state CRDT, adopted with two rejections** (the author asked for a
  ruling). **Adopted:** the join-semilattice (commutative, associative, idempotent)
  as the merge law; Δ-state on the wire; **Merkle-clocks instead of vector clocks**
  (O(peers) is fatal in a mesh). **Rejected:** last-writer-wins as a default — it is
  a silent wrong answer *and* it smuggles wall-clock time back in as arbiter — and
  op-based CRDTs, which need causally-ordered exactly-once delivery a mesh cannot
  give. Conflicts are instead **first-class objects**, which is not merely honest
  but **required**: Mimram's free cocompletion is what makes merge *total*.

* **The structural consequence — history is optional.** Convergence comes from the
  join on *state*, not from history; a join needs only two current states. So a
  device storing **zero** utterances still converges. This is the direct answer to
  the relayed research objection (*"you cannot store a month of patch history on an
  ESP32"*) — correct, and it does not have to. Formalized as
  [peer and tier](/concepts/peer-and-tier.md): **State / Recent / Full** peers, all
  speaking **one protocol**, because a second cut-down protocol for small devices
  guarantees the dialects drift and the mesh dies.

* **Granularity ruling.** A Core `batch` becomes **n utterances plus a shared
  `group` label**, not one atom — collapsing it would destroy the independence
  relation *between* its commands, which is exactly what makes utterances commute.
  Coarse units mean more false conflicts. Confirms the author's answer to question 6.

* **Confluence, scoped honestly.** Merge-by-normal-form is real **only** inside
  Core's restricted confluent subset (≤1 rule per glyph pair). Core's roadmap plans
  to leave that subset, and confluence of general graph rewriting is **undecidable**
  (Plump). So the guarantee is **conditional and must be flag-gated per mantle**.
  Also recorded: confluence and the Wolfram model's **causal invariance** are
  *different* properties — SetReplace gives counterexamples both directions — and
  Palabra needs confluence, not causal invariance.

* **PROPs, placed.** Real, but not where the proposal put them. `reduce/` genuinely
  is a PROP; a PROP is the algebra of the **reduction layer**, and Palabra's v1
  store is a monoid plus a graph. PROPs' honest home here is **composing
  communication topology** — devices have ports, a sync session is a morphism, and
  the permutation part is "which peer you sync with first does not matter."

* **Bundle created** — `README.md` plus 8 concepts, 2 design notes, 2 references,
  roadmap and this log. Every concept is `status:planned` (nothing built), per the
  honesty convention inherited from Void Core.

* **Prior art surveyed** — the answer to *"am I the only one who's built something
  like this?"* is **no**: Merkle-CRDTs (OrbitDB, DefraDB), Matrix's no-main-branch
  event DAG, Pijul, Dolt's prolly trees, Willow/Iroh/Meadowcap, Automerge/Fugue.
  Every individual piece ships somewhere. **Unbuilt:** version control over an
  interaction-net model, the tier-declaration design, and the combination.

* **Two blockers recorded, both owned by Void Core:** reified commands + the
  pure-vs-effectful split, and the scope of the undoable slice (`SPEC.md §12`).
  Neither is Palabra's to answer. A third, **the trust model**, blocks all transport
  work under Latin-OS's standing *security railguard missing = code blocked*.

* **Not done (deliberate):** no reply sent to Hormiga; naming left to the author
  (the *dicho / voz / coro* proposal is recorded and **not adopted**); no code.
