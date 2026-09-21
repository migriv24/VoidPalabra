---
type: Design
title: Integration — the hooks an application implements
description: What a Void Core application must supply to use Palabra, what Palabra supplies back, and why the answer is expressible entirely in Core's existing vocabulary. Includes the CLI surface applications will extend.
tags: [status:planned, audience:dev, confidence:tentative]
timestamp: 2026-07-27T00:00:00Z
---

Raised by the author 2026-07-27: Void Hormiga has **the antfarm**, a node graph
for managing protocols, and Palabra will plug in somewhere inside it. *"Not all
applications will have an antfarm. They'll probably have similar concepts, but
maybe expressed in some other graph."*

So the question is what the **hooks** are — and the answer is shaped by one large
advantage.

---

# 1. The advantage: we know what every client is made of

Palabra is not integrating with arbitrary software. Every client is a Void Core
application, so **every client already has** runes, mantles, glyphs, holidays, a
tag grammar, a dispatcher, and a graph. That is an unusually strong precondition,
and it means the integration surface can be written in **Core's vocabulary
instead of a new one**.

Concretely, Palabra needs four things from an application, and Core supplies the
shape of all four:

| Palabra needs | the app already has it as | why Palabra cannot supply it |
|---|---|---|
| **the state** | Core's state document | it is the app's content |
| **which fields resolve how** | glyph knowledge | only the glyph knows whether a field is a cursor or a paragraph |
| **which fields are ordered** | glyph knowledge | same |
| **a way to move bytes** | a **holiday** | Core does no I/O by definition; the pipe is the app's |

And Palabra gives back: naming, storage, merging, conflicts, and (later) the sync
protocol. Nothing in that list requires the application to adopt a new model.

---

# 2. The hooks

## 2.1 The `JoinPolicy` — a glyph declaration, not a Palabra one

Built. A field resolves as `Conflict`, `Pick` or `Max`
([join](/concepts/join.md)), and **the application declares which**, because only
a glyph knows what its fields mean. Palabra's default is `Conflict` for
everything, permanently, since a field nobody thought about silently losing data
is the failure the whole design exists to prevent.

> **Where this should eventually live:** a glyph already declares `fields` and an
> `editor` (`VoidCore:SPEC.md §3.3`). A join is the same kind of statement — *how
> is this field edited* and *how do two edits combine* are the same question asked
> at one and two users. If Core ever grows a `joins` key on the glyph registry,
> Palabra should read it rather than keep its own table. **Not asked for**; noted
> so the duplication is deliberate rather than accidental.

## 2.2 Ordered fields — the same declaration, one axis over

Also built ([join](/concepts/join.md)): a field is a sequence, and the storage is
**self-describing**, so no read-time declaration is needed. The app declares it
once by calling `seq_init`, and everything afterwards knows.

## 2.3 The transport hook — and this is where the antfarm lands

**The important one, and it is already designed.**
[Transport shape](/design/transport-shape.md) rules that the protocol is a **pure
state machine**:

```
step(local, incoming) -> (local', [outgoing])
```

Palabra never opens a socket. It produces messages and consumes them; **the
application decides how they travel.** That is exactly the division the author
described — *"applications will probably decide HOW the data transfer works"* —
and it was not designed for the antfarm, it just happens to be the shape an
antfarm wants.

So:

| the app | what it does with Palabra's messages |
|---|---|
| **Hormiga** | the antfarm carries them; a Palabra sync becomes a protocol node in the existing graph |
| **a Maiz-based chatroom** | a socket, directly |
| **Void Buzz** | a constrained radio link, later |
| **an app with no graph at all** | calls `step` in a loop and writes bytes wherever it likes |

None of those is a special case in Palabra, because Palabra cannot tell them
apart. That is the point of sans-IO, and it is why *"not all applications will
have an antfarm"* is not a problem to solve — it is a difference that never
reaches this library.

---

## 2.4 The reference hook — which fields name bytes the document does not hold

**Added 2026-09-18.** The same shape as the join policy, one question over: a
`ReferencePolicy` declares, per field, where a rune's content names a file by address,
so Palabra can say what a document refers to and what a device lacks
([content reference](/concepts/content-reference.md)). Declared by the application for
the reason the join policy is: content means what the application says it means, and
nobody else may guess.

Like the other hooks, it is a natural candidate to move into a glyph declaration one
day — a field whose schema says "this is a picture" is a field whose schema says "this
is a reference". That is Void Core's call to make, not this library's.

# 3. On "universal holiday concepts" — what is and is not sound

The author flagged uncertainty about whether these are *"mathematically sound"*.
Here is the honest split.

**A holiday is a port, not a type.** `VoidCore:okf/design/interaction-nets-theory.md`
treats a holiday as a **boundary port** of the net — the place a wire leaves the
graph. That is what makes the concept general: it is defined by *position in the
structure*, not by what is on the other end. So "a universal holiday" is not a
grand abstraction to be discovered; it is the observation that every external
system attaches the same way, which Core already gets from the interaction-net
model.

**Two holiday shapes Palabra can genuinely offer**, because both are defined by
their algebra rather than by their implementation:

1. **A block source** — `put(bytes) -> key`, `get(key) -> bytes`, `has(key)`.
   Sound because it is *content-addressed*: the key is a function of the bytes, so
   any two implementations that agree on SHA-256 are interchangeable, and a store
   is allowed to be **partial** without being wrong. Hormiga could back this with
   its existing SQLite instead of Palabra's in-memory map, and nothing above would
   notice. **Not yet extracted as an interface** — it is a concrete class today;
   see §5.
2. **A message pipe** — carries opaque byte blobs between peers, may drop,
   reorder and duplicate. Sound because the *protocol assumes nothing better*
   ([history graph](/concepts/history-graph.md)), so the interface has no
   guarantees to get wrong.

**What is not sound, and should not be attempted:** a universal *semantic*
holiday — one interface for "any external system". Core's `query`/`get`/`insert`
is already the general shape and it is general precisely because it says almost
nothing. Trying to make it say more would produce an abstraction that fits one
backend and lies about the rest.

> **The test to apply:** an interface is worth extracting when two implementations
> would be **substitutable without the caller knowing**. Block storage passes
> (content addressing makes it so). A message pipe passes (the protocol tolerates
> everything). "A data backend" does not.

---

# 4. The CLI surface — because applications will extend it

The author's point is the load-bearing one: **applications will extend Palabra's
CLI into their own Voidscript frameworks**, so whatever verbs exist here will
appear inside every adopting app. That makes the vocabulary a compatibility
surface, not a convenience.

Which is why [roadmap](/roadmap.md) puts the CLI **last** and
[open questions](/design/open-questions.md) §1 leaves the naming to the author.
This section is a **sketch to be argued with**, not a proposal to adopt.

## 4.1 What the verbs must satisfy

1. **They must not collide with Core's.** `save`, `revert`, `history`, `diff` are
   taken (`VoidCore:SPEC.md §7`), and `history` is the dangerous one — Core's is a
   local undo list, Palabra's would be a partial order. Two different things under
   one word is the failure [why not linear](/design/why-not-linear.md) is about.
2. **They must not smuggle linear time back in.** No `log`, no `HEAD`, no
   `checkout`. The translation table exists for exactly this.
3. **They must work under an app's own name.** If Hormiga renames them, the
   underlying dispatch has to be the same, or the ports drift — the lesson Void
   Core just taught with `conformance/temper/`.

## 4.2 A sketch, deliberately unnamed

| what the user wants | Core's word | Palabra's, if it must differ |
|---|---|---|
| write the current state as a named cut | `save` — **keep it** | — |
| list past cuts | — | *(needs a word; not `history`)* |
| return to a past cut | — | *(not `checkout`; a cut is not a place)* |
| what is the current cut called | — | *(prints `v:8c41f2…`)* |
| merge another peer's state | — | — |
| show unresolved conflicts | — | — |
| resolve one conflict | — | — |

**The gap in the table is the point.** Four of the seven have no good word yet,
and inventing them now — before the reconciliation protocol exists and before the
author has ruled on the *dicho / voz / coro* question — would freeze a vocabulary
into every downstream app on the strength of a guess.

## 4.3 One thing that can be settled now

**Palabra's verbs should be reachable through Core's `effect` seam before they
are verbs at all.** `effect <op> [args…]` already routes arbitrary host operations
and returns the result as `data` (`VoidCore:okf/concepts/holiday.md`), so an
application can drive every built capability **today** with no new Core surface
and no naming commitment. When the vocabulary settles, the verbs become sugar over
calls that already work.

That ordering is deliberate: it lets applications integrate now and lets the words
be chosen late, which is the correct way round for a decision this expensive to
reverse.

---

# 5. What this implies for the code

Two consequences, one done and one not.

**Done.** The library is split by concern — `encoding/` knows nothing about
runes, `canonical/` knows nothing about CRDTs, `crdt/` knows nothing about files,
`store/` knows nothing about Void Core, `archive/` composes them. That is what
makes §3's block-source hook extractable later without disturbing anything above
it.

**Not done.** `BlockStore` is a concrete class, so an application cannot yet
supply its own. Extracting it to an interface is the next structural move, and
§3's test says it is one of the two that would survive the extraction. It is worth
doing when a second implementation actually exists — Hormiga's SQLite is the
obvious candidate — and not before, since an interface with one implementation is
a guess about the second.

# 6. Two consumer shapes — an application, and a library

**Recorded 2026-09-18**, when the proposal arrived to make networking an optional
**Void Maiz** module with this library as its required dependency. Until then every
consumer was an application. A view library consuming Palabra changes three things,
and all three are about packaging rather than about the algebra.

**Consume the target, never a file list.** Siblings in this family have consumed each
other by copying lists of source files into their own CMake. That drifts silently: Void
Maiz's list of Void Core's sources went months without the command journal, and a
consumer of this library had to be told by message to add two files. The contract is
now `add_subdirectory(${VOIDPALABRA_ROOT} …)` and `voidpalabra::voidpalabra`. New source
files never reach a consumer's build.

**Embedding must leave the host's build as it was.** Before this, an embedding project
built thirteen test programs of ours and inherited twelve tests into its own ctest — two
of which failed there. Tests are now built only when this repo is the top-level project,
and two tests embed the library in a fresh project to keep it that way.

**One cJSON per process.** Void Core, Void Maiz and this library each vendor the same
cJSON. `VOIDPALABRA_VENDOR_CJSON=OFF` compiles against our header and leaves the
implementation to what the host already links, and a test proves it links.

**Ruled by the author, 2026-09-18:** *Void Maiz owns what networking looks like, Void
Palabra owns what networking is, the host owns what is shared.* The transport stays
this library's pillar; a Maiz networking module drives a
[sync session](/concepts/sync-session.md) and draws what it reports. The seams Maiz named
— an ephemeral channel for opaque bytes, an export set asked through one function, and
files with a "known but not fetched" state — are all in the session.

**What the networking module is, in this bundle's terms**: the right-hand column of
[transport shape](/design/transport-shape.md) — sockets, discovery, framing, timeouts, the
fetching of [content references](/concepts/content-reference.md). This library stays the
left-hand column: what to send, what a received document may contain, what is missing.
Neither imports the other's concerns. The trust blocker (open questions §6) moves with
the transport, not with the algebra: a networking module in a shared library is the same
exposure a stand-in transport in one application was, multiplied by every application that
links it.

# 7. Phones, and the socket layer

**Recorded 2026-09-20**, when Void Maiz asked for a `voidpalabra_lan` companion target —
Void Hormiga's UDP beacon, X25519 handshake and sealed stream, lifted so every
application links one copy — and the author said the family is starting Android
applications that will need networking, with the lean that **this library does not own
it**.

The line that follows from the lean, and from the sans-IO ruling, is the same line as
§6, drawn one level lower:

| every transport must agree on it | it belongs to one platform or one policy |
|---|---|
| the frame (SPEC §11.1) and the stream envelope (§11.9) | sockets, and which interface to bind (a VPN or WSL adapter is not the LAN) |
| what a dead link means: a new session, not an error | discovery: beacons out, unicast answers back |
| coalescing a burst (`Timing::coalesce`) | Android's `MulticastLock`, a JNI call |
| a live fetch-policy change (`Session::set_fetch`) | join codes, QR codes, pairing strings |
| the signature slot and its hooks | which cryptography fills them, and libsodium's NDK build |

**The left column is here, tested, and specified.** The right column is platform code,
and its natural home is beside the platform code that already exists — the view
library's networking module, or a sibling of its own — consuming `StreamReader` rather
than inventing framing. Nothing in the left column needs a socket to be measured, and
nothing in the right column needs to know what a document is.

**Android, from this side:** the library cross-compiles for `arm64-v8a` with NDK r28 at
API 26, library and every test program, with no warnings. It has not yet been RUN on a
device. It needs no Java, no permission, no thread, and no filesystem path beyond what
the host passes to the archive.

**Lessons a transport author inherits** (from Void Hormiga's defects and Void Maiz's
Android work, generalized): a quiet socket is not a broken one — let the session's
`silence_timeout` decide, never a read timeout; give each device its own port, or two
copies on one machine dial themselves; keep listening while carrying a link; and treat
an app pause as a disconnect whose repair is a new session.

**Still gated.** Whoever builds the socket layer inherits open questions §6.1 exactly as
§6 said: the transport that carries frames between devices is blocked on trust, and a
LAN-only arrangement is an explicit yes from the author per application, not a default.
