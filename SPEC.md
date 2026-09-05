# Void Palabra — Specification (v0.0.1)

The normative, language-agnostic contract. What any implementation must do to
interoperate: the **canonical form** (§2–§4), the **enriched document** that
carries the merge law (§5), and the **utterance and history graph** that name
change rather than state (§8). All three are built; nothing else in the bundle
is.

Written in the shape of `VoidCore:SPEC.md`, and offered to Void Core in answer to
their 2026-07-27 note: *"If your spec lands and is as small and language-neutral as
you describe, we would rather adopt it than grow a second one."* It is deliberately
independent of Void Palabra — an implementation of §2 needs a hash function, a
sorter, and nothing else.

Keywords MUST / MUST NOT / SHOULD / MAY are used in the RFC 2119 sense.

---

## 1. What this specifies, and why it exists

A **canonical form** is a function from a value to bytes such that two peers holding
the same value produce the same bytes, whatever route the value arrived by.

The requirement it exists to satisfy:

> Two peers holding the same Void Core slice, reached by any route, MUST compute
> the same bytes — and therefore the same name.

"Any route" is load-bearing. One peer authored runes in one order; another received
them reversed over a lossy transport; a third rebuilt the slice from storage. All
three MUST agree.

### 1.1 Why not canonical JSON

Canonical JSON (sorted keys, no whitespace) is the obvious candidate and is not
sufficient. Three hazards, each of which surfaces as *two peers disagree*:

1. **The int/float ambiguity.** `1` and `1.0` are the same JSON value and different
   JSON text. Void Core hit exactly this: its `provenance` hash is canonical JSON,
   and `{"n": 1}` and `{"n": 1.0}` hash differently because Python writes `1.0`
   where JavaScript writes `1` (`VoidCore:conformance/scry/07-provenance.json`).
2. **Float formatting.** Shortest-round-trip repr is not universal across languages
   or standard libraries.
3. **String escaping.** `/`, non-ASCII, and the surrogate range admit multiple
   legal encodings of one string.

A tagged, length-prefixed binary form decides all three by construction rather than
by inheriting whatever the nearest JSON writer does.

### 1.2 The one design rule

A serializer MUST be told, for every sequence, whether its order is **real** or
**arbitrary**. This is the whole content of the specification and the reason there
are two sequence encodings.

> **When in doubt, preserve order.** Discarding an order that turns out to be real
> is a silent wrong answer. Preserving an order that turns out to be arbitrary
> costs a false conflict. The failure modes are not symmetric, so the default MUST
> NOT be symmetric either.

---

## 2. The encoding **[normative]**

A value is encoded as a one-byte **tag** followed by a tag-specific body. All
lengths and counts are unsigned LEB128 **varints** (7 bits per byte, low group
first, high bit set on all but the last).

| tag | type | body |
|---|---|---|
| `0x00` | null | — |
| `0x01` | false | — |
| `0x02` | true | — |
| `0x03` | integer | varint of the zigzag encoding of the value |
| `0x04` | float | 8 bytes, IEEE-754 binary64, **big-endian** |
| `0x05` | string | varint byte-length, then UTF-8 bytes |
| `0x06` | bytes | varint length, then the bytes |
| `0x07` | sequence | varint count, then each encoded item **in order** |
| `0x08` | map | varint count, then each `(key, value)` encoded pair |
| `0x09` | set | varint count, then each encoded item |

Zigzag of a signed integer `n` is `(n << 1) ^ (n >> 63)` with `>>` arithmetic.

### 2.1 Numbers

- A value that is mathematically integral and exactly representable as a signed
  64-bit integer MUST be encoded with tag `0x03`. This makes `1`, `1.0` and `-0.0`
  encode as `1`, `1` and `0` respectively.
- Any other finite number MUST be encoded with tag `0x04`.
- `NaN`, `+Infinity` and `-Infinity` MUST be **rejected**. They have no canonical
  form and MUST NOT be encoded as anything.

> The integral fold is not an optimization. cJSON — and every JSON parser that
> stores numbers as doubles — cannot distinguish `1` from `1.0`, so without the
> fold the bytes would depend on how a host happened to write a literal.

### 2.2 Strings

- Input MUST be valid UTF-8. Invalid UTF-8 MUST be rejected, including overlong
  forms, surrogates (`U+D800`–`U+DFFF`), and values above `U+10FFFF`.
- Strings are **not escaped**; the length prefix delimits them. Consequently
  `["ab","c"]` and `["a","bc"]` MUST encode differently.
- **Unicode normalization is NOT applied.** Callers MUST supply NFC — §6.1, normative.

### 2.3 Maps

- Keys MUST be strings.
- Pairs MUST be sorted by the **encoded key bytes**, ascending, compared as
  unsigned octets. Sorting by the raw string is *not* equivalent and MUST NOT be
  substituted.
- Duplicate keys MUST be rejected.

### 2.4 Sets

- Items MUST be sorted by their **encoded bytes**, ascending, as unsigned octets.
- Duplicate items MUST be **removed**, and the count is the count after removal.

> Deduplication is normative, not cosmetic. A set encoding that preserved
> multiplicity would break **idempotence** (`a ⊔ a = a`), so a peer that received a
> duplicate over a lossy transport would diverge from one that did not. Sorting is
> presentation; deduplication is the algebra.

### 2.5 Sequences

Items are encoded in the order given. A sequence MUST be used wherever an order is
or may be meaningful — including any value the implementation does not interpret.

---

## 3. Domain separation and naming **[normative]**

A **digest** is `SHA-256(header ‖ payload)` where the payload is the §2 encoding and
the header is the ASCII string

```
voidpalabra/v<CANON_VERSION>/<kind>/<policy>\0
```

- `CANON_VERSION` is the integer version of this encoding. It MUST be incremented
  whenever a change moves any byte.
- `kind` is `rune`, `mantle` or `slice` (§4), or `utterance` or `cut` (§8), and
  prevents two structurally identical payloads of different types from colliding.
  Two names built from the same bytes under different kinds are unrelated, which
  is what lets §8 add kinds without touching §4's vectors.
- `policy` is the §4.4 policy string, or **empty** for a kind that carries no
  policy (`utterance`, `cut`). The slot stays present so the header format is one
  format rather than one per kind.
- The header terminates with a NUL byte, which cannot occur in the ASCII fields.

A **version name** is `"v:"` followed by the lowercase hex of the 32-byte slice
digest. §8 adds two more names on the same rule: `"u:"` for an utterance and
`"c:"` for a cut. The prefixes are distinct because the objects are, and a
comparison across two of them is always a mistake.

**SHA-256** is mandated rather than a faster hash because it is universal and is
hardware-accelerated on the ESP32, which Palabra requires to be a full peer.

---

## 4. Void Core bindings **[normative]**

This section maps `VoidCore:SPEC.md` §2–§3 shapes onto §2. It is the only part that
depends on Void Core; §2 and §3 stand alone.

### 4.1 Rune

Encoded as a map with exactly these keys: `spirit` (a map of `id`, `name`),
`glyph`, `facets` (a map of exactly `who`, `what`, `when`, `where`, `why`, `how`),
`tags`, `content`, `relations`, and `placement` if the policy includes it.

- **Defaults MUST be applied before encoding** (`VoidCore:SPEC.md §3.2`), so a
  partial rune and its fully-hydrated twin produce identical bytes.
- A rune with no `spirit.id` MUST be rejected.
- `tags` MUST be a **set** — Core treats tags as membership (`§5`) and their join is
  an OR-Set.
- `content` MUST be encoded generically per §2, **preserving all sequence order**.
  Core does not interpret content (`§3.2`), so an implementation of this spec MUST
  NOT either.
- `relations` is reserved by Core; it MUST be a **sequence** (see §1.2).

### 4.2 Mantle

A map with keys `id`, `name`, `domain`, `runes`, `tags`, `edges`, `rules`.

- A mantle with no `name` MUST be rejected.
- `runes` MUST be a **set**. This is required by `VoidCore:SPEC.md §4`: rune order
  is preserved but not semantic, and *"an order-sensitive consumer (a canonical
  form, a content hash, a sync join) MUST be order-insensitive at the Core level."*
  There MUST NOT be an option to make rune order significant.
- `edges` (from `layout.edges`) MUST be a **set**, per §4.3.
- `rules` is reserved by Core; it MUST be a **sequence**.

### 4.3 Link

A map with keys `from`, `to`, `relation`, `weight`, `directed`, defaults applied per
`VoidCore:SPEC.md §3.7` (`""`, `1.0`, `true`).

- If `directed` is false, `from` and `to` MUST be sorted ascending before encoding.
  An undirected wire is the same wire read from either end, and the canonical form
  MUST be invariant under exactly the deformations that do not change meaning — and
  under nothing else. If `directed` is true they MUST NOT be reordered.

### 4.4 Slice, and what is excluded

The **versioned slice** of a Void Core state document is a **map** with exactly
two members:

| member | encoding |
|---|---|
| `mantles` | a **set** of §4.2 mantles. Names MUST be unique; duplicates MUST be rejected |
| `glyphs` | a **map** from glyph name to §4.5 declaration |

An **absent** `glyphs` key and an **empty** one MUST encode identically — the same
hydration rule §4.1 applies to a partial rune. A document written before the key
existed and one that declares nothing are the same state, and an implementation
that distinguished them would report a change where none happened.

> **`glyphs` joined the slice at `CANON_VERSION` 3** (2026-09-03), when Void Core
> 0.2.14 added `state.glyphs` — the declarations that say what a rune's content
> means. The slice was `mantles` alone and was encoded as a bare set; it is now a
> two-member map, so **every name moved**, including for documents that declare
> nothing. That is what the version counter is for.
>
> The line this settles is not "is the key data?" — `domains` is data too. It is:
>
> | key describes | example | versioned |
> |---|---|---|
> | the world this machine sits in | `domains`, `config` | no |
> | the thing the user made | `mantles`, `glyphs` | **yes** |
>
> A declaration is inert: Core stores `presentations` without reading it and
> executes none of it, so the forcing argument against `domains` — that syncing
> one runs device A's deploy command on device B — does not reach it. And
> excluding it has a measured cost rather than a theoretical one: sync a mantle
> without its declarations and the receiving peer holds the content in its
> document and cannot reach it through the projection, with **no error anywhere**.
> `mantles` and `glyphs` are a value and its type.

> **Naming and storing are different jobs, and this section is about naming.**
> The exclusions below say what may not contribute to a *version name*, because a
> name that moved when a deploy command changed would make two peers disagree
> about a cut they in fact share. They do **not** say a file format should discard
> those keys. An archive stores the whole document and names it by the slice — see
> §7. Conflating the two cost Void Hormiga a silent data-loss bug, reported
> 2026-08-21.

The following MUST NOT contribute to the digest:

| excluded | why |
|---|---|
| `domains` | holds real `build`/`deploy` commands; syncing one runs one device's deploy on another |
| `bindings` | resolved against local domains |
| `config` | setup, not content |
| `active` | a cursor |
| `scripts` | host-local |
| `_baseline` | dirty-tracking |
| `glyphs[…].source` | see §4.5 — how *this peer* resolved a declaration |

**Policy.** One flag, `include_view`, controls whether a rune's `placement` (Core's
view slice) is encoded. Both settings are correct — for different questions —
so the policy string MUST appear in the §3 header:

```
include_view=1   |   include_view=0
```

### 4.5 Glyph declaration

A **declaration** is the schema that says what a rune's content means
(`VoidCore:SPEC.md §2`). Palabra does not interpret one: `kind`, `fields`,
`presentations`, `kinds` and anything else an author writes are an application's
vocabulary, and Core itself stores `presentations` without reading it. A
declaration is therefore encoded as an ordinary §2.3 map of its members.

**One key MUST be excluded: `source`.**

Core stamps each descriptor `"source": "document"` or `"source": "host"` to record
whether a declaration traveled with the data or was registered by the host at
boot. That is an answer about **how this peer resolved the declaration**, not
about what the type is — the same schema is `document` on the peer that received
it and may be `host` on a peer whose application also registered it. Including it
would make two peers holding one schema compute different names for it, which is
the divergence §1 exists to prevent.

It is the same judgment as `domains`: real state, peer-local resolution, not
versioned content. The difference is that `source` sits *inside* a versioned key,
so it is excluded here rather than by omitting the key.

**An implementation MUST apply this exclusion in every place a declaration is
compared, not only when computing a name.** The reference implementation briefly
excluded `source` from the canonical form and not from the CRDT register that
carries a declaration across a merge, so two peers differing only in `source`
computed the same version name *and* reported a redeclaration conflict — the state
said "identical" and the merge said "you disagree". Neither answer was wrong on
its own, which is what made it worth stating normatively.

A `glyphs` value that is not a map, and a declaration that is not an object, MUST
be refused (§2 has no honest encoding for either).

---

## 5. The enriched document **[normative]**

A **join** over a bare Void Core state document is impossible, and this is the
reason the rest of this section exists.

> Two peers holding `{a}` and `{}` cannot distinguish *"I never had `a`"* from
> *"I removed `a`"*. Any join of bare state is therefore union, and **removes never
> propagate**.

Distinguishing them requires per-element metadata. An implementation MUST therefore
merge an **enriched document**, not a state document, and MUST provide the two
conversions in §5.4.

### 5.1 The primitive: an observed-remove set

One structure underlies everything here.

```jsonc
{ "a": { "<tag>": <value> },   // adds: a unique tag -> a value
  "r": [ "<tag>", … ] }        // removes: tags retired by an observed remove
```

- A **tag** MUST be unique across all peers and all time. Any collision-resistant
  unique identifier is acceptable; Void Core's `spirit.id` minting is the intended
  source.
- A value is **present** iff at least one tag maps to it and is not in `r`.
- **Add** MUST mint a fresh tag. **Remove** MUST retire exactly the tags *visible to
  the removing peer* and MUST NOT retire anything else.
- **`join` is union on both members**, and nothing else. The three laws follow by
  construction, because set union is commutative, associative and idempotent.

Two consequences are normative rather than incidental:

- **Add-wins.** A concurrent add carries a tag the remover never observed, so it
  survives. This is forced by the definition of remove, not chosen.
- **Version vectors MUST NOT be used** for this purpose. They are O(peers) and a
  mesh has unbounded peers. Tags are O(adds), which is a growth problem (§5.5), not
  a scaling wall.

The same primitive is used three ways, and an implementation MUST NOT introduce a
second: as a **set** (a rune's tags), as a **multi-value register** (a field, where
a write retires what it observed and adds one value), and as a **keyed map**
(presence of runes and mantles).

### 5.2 Shape

```jsonc
{ "palabra": 1,
  "mantles": { "<mantle name>": {
      "present": <OrSet>,
      "fields":  { "id": <Reg>, "domain": <Reg> },
      "runes":   { "<spirit.id>": {
          "present": <OrSet>,
          "fields":  { "spirit.name": <Reg>, "glyph": <Reg>,
                       "facets.<facet>": <Reg>, "content.<key>": <Reg>,
                       "placement": <Reg> },
          "tags":    <OrSet> } },
      "edges":   <OrSet> } },
  "glyphs":  { "<glyph name>": {
      "present": <OrSet>,
      "fields":  { "descriptor": <Reg> } } } }
```

- Runes MUST be keyed by **`spirit.id`**, which is immutable — so two peers renaming
  one rune are editing one object rather than creating two.
- Mantles are keyed by **name**, which Core makes unique (`SPEC §3.4`); a mantle's
  `id` is carried as an ordinary field.
- **Each content key MUST be its own register.** Two peers editing different fields
  of one rune therefore do not conflict, which is most of what makes concurrent
  editing tolerable.
- **A declaration MUST be ONE register holding the whole descriptor** — the opposite
  granularity, and deliberately so. Splitting content per key means two peers
  editing different fields of one rune do not conflict. Splitting a *schema* per
  key would let a merge assemble peer A's `fields` with peer B's `kind` and hand
  the result back as a type **neither peer declared**. A schema nobody wrote is
  worse than a disagreement somebody has to answer, so concurrent redeclaration
  surfaces as a conflict (§5.3) instead of being merged away.
- The register's value MUST be the declaration **with `source` excluded**, per
  §4.5 — the same rule that computes its name.

### 5.3 Merging and conflicts

`join` on two documents is defined recursively: union the maps, join the OrSets. An
implementation MUST satisfy, over arbitrary documents:

| law | statement |
|---|---|
| commutative | `a ⊔ b = b ⊔ a` |
| associative | `(a ⊔ b) ⊔ c = a ⊔ (b ⊔ c)` |
| idempotent | `a ⊔ a = a` |

Equality here means **byte-equality of the canonical form**, which requires:

> **Canonicalizing a document MUST normalize every OrSet before encoding** — sorting
> and deduplicating `a` and `r`. Without this, two representations of the same value
> receive different names, and `a ⊔ a = a` holds as a value while failing as a name.

A register with **more than one present value is a conflict**, and a conflict is a
value rather than an error. `sides` MUST be ordered canonically (by the encoded
value bytes) so that two peers naming the same divergence produce the same object.
No side is privileged; there is no "ours".

**A conflict is located by exactly one of two addresses:**

| where | locator | `field` |
|---|---|---|
| in a mantle | `mantle`, and `rune` for a rune-level register | e.g. `content.body`, `domain` |
| in a declaration | `glyph` | `descriptor` |

A declaration belongs to the document rather than to any mantle, so a glyph
conflict MUST NOT be reported with an empty mantle name standing in for one — a
renderer given `""` prints a blank where a name should be. Implementations MUST
enumerate conflicts in a deterministic order so that two peers list them
identically.

### 5.4 The round-trip law

```
flatten(enrich(x)) == x
```

for any state `x` that contains no conflicts, compared through §4's canonical form.
This is the law `VoidCore:scry/roundtrip.py` holds a Lens to, for the same reason: a
mapping written separately for each direction drifts into silent data loss.

**"Compared through §4's canonical form" is load-bearing, and an implementation
MUST NOT present this law without it.** §4 names the *versioned slice*, so what
round-trips is `mantles` and `glyphs`. `flatten` returns the slice, not the
document: `config`, `domains`, `bindings` and `active` are not in it and never
were. A caller who writes the result back as their whole state document loses
every one of them.

The correct use is to **splice**: take the slice out of the flattened result and
put it into the document you already have, which keeps this device's peer-local
resolution its own. Void Core 0.2.14 §2(c) makes the same point from the other
side — *"if you RECONSTRUCT the state document rather than round-tripping it, you
will drop `glyphs`"* — and the document will grow keys again.

### 5.5 Growth

Tags and retired tags accumulate and this specification does **not** say how to
prune them. Safe pruning requires knowing what every peer has seen, which an open
mesh does not know — the same property that makes reconciliation cheap. This is a
genuinely unsolved problem and is stated here so no implementer assumes otherwise.

---

## 6. Known gaps

### 6.1 Unicode normalization — a PRECONDITION, not a gap **[normative]**

An implementation **MUST NOT** normalize Unicode, and callers **MUST** supply
**NFC**.

Decided 2026-08-21 on Void Hormiga's proposal, after they identified themselves as
the case that hits it: a bilingual Spanish/English community database, on mixed
platforms, whose real content carries `á é í ó ú ñ ü`.

**Why the precondition sits with the caller.** Normalizing properly needs the
Unicode decomposition and composition-exclusion tables, which is disproportionate
for this specification and heavy for an ESP32 — which §3 requires to be a full
peer. The application has the keyboard; this specification has a hash function.
Normalizing where text is *entered* is both cheaper and more correct, because that
is the only place that knows text is being entered.

**The risk this creates, stated rather than hidden.** A caller that violates the
precondition gets **silent divergence**: `café` composed and decomposed are
different byte sequences, so two peers compute different names for what a person
calls the same rune, and it presents as *"the merge did nothing"* rather than as an
error.

**So the precondition is checkable.** An implementation SHOULD provide an advisory
that reports whether a string contains combining marks — a **diagnostic, not a
validator**, since some sequences are legitimately decomposed and have no composed
form. It exists so a host can assert over its own corpus in its own tests, where a
false positive costs a glance rather than a refusal. The reference implementation
exposes `has_combining_marks`.



Stated in the spec rather than a tracker, because an implementer needs them.

1. ~~Unicode normalization is not applied.~~ **Decided 2026-08-21 — see §6.1
   below, which is now normative rather than a gap.**
2. **`layout.edges` endpoints are mutable names**, not `spirit.id`
   (`VoidCore:SPEC.md §3.7`). This is faithfully encoded and is correct for a
   canonical form, but a join over edges must resolve names to ids before comparing
   (`VoidCore:SPEC.md §12` records the same point).
3. **The container format is not specified here.** `VPAL` is a local file format
   rather than an interchange one, and nothing yet reads another implementation's
   container. It belongs in this document before it belongs in the vectors.

---

## 7. Naming versus storing **[normative]**

A **version name** (§3) covers the versioned slice. A **file format** covers the
document. These are different jobs and an implementation MUST NOT collapse them.

- A version name MUST be derived from `mantles` alone, per §4.4.
- An archive or container that persists a state document MUST store **every
  top-level key**, not only the versioned slice.
- An implementation that deduplicates saves MUST compare the **stored content**,
  not the version name. Comparing the name means a change confined to `config`,
  `scripts`, `domains`, `bindings` or `active` produces no save and no error.

> This is written normatively because the reference implementation got it wrong.
> Void Hormiga measured it on 2026-08-21: `save` stored `mantles` alone, so a
> user changing `config.site.base_url` — their website's address — pressed Save,
> received no error, and lost the change. Two saves MAY therefore share a version
> name while differing in content; that is honest, because they *are* the same cut
> of the versioned slice.

---

## 8. Utterances and the history graph **[normative]**

Normative since 2026-08-27, and only possible since then: it depends on
`VoidCore:SPEC.md §6.2`, which reified commands and closed the pure/effectful
question. Everything above this section names **state**; this section names
**change**.

### 8.1 The consumer obligation

Void Core §6.2 states the rule and this section inherits it verbatim:

> A command is **effectful** iff its verb can reach the host through the effect
> handler. The complete list is `save`, `deploy`, `build`, `preview`, `effect`.
> Every other verb is **pure**.

An implementation building a replayable or transmissible history **MUST** record
only entries whose `pure` is true. An effectful command is not replayable, not
invertible, and not addressable by its result, so recording one produces a history
that lies.

Two further exclusions are **Palabra's**, not Core's, and follow from §4.4's
versioned slice:

- An entry whose `slice` is `view` MUST NOT become an utterance. `placement` is
  real state with a declared join, and it is not versioned content.
- An entry whose `slice` is `host` MUST NOT become an utterance. It changed
  nothing in the state document.

Core's `slice` field **reports** where a change landed and deliberately declines to
legislate what is synced; the decision above is made here.

An implementation **MUST** report what it excluded, rather than silently producing
a shorter history. A consumer that cannot distinguish *"nothing was excluded"* from
*"something was excluded and not reported"* cannot tell a complete replay from an
incomplete one — which is Core's own argument for recording effectful entries,
applied one layer up.

An implementation **MUST** refuse a journal entry that is missing `command`,
`verb`, `who`, `pure`, `slice` or `minted`, or whose `slice` is not one of the
three named values. It MUST NOT default a missing field. `who` is nullable and
MUST be present as an explicit null when there is no actor.

**Known residue.** Core's `undo` slice is `mantles` + `active` + `glyphs`, and
§4.4 versions `mantles` + `glyphs`. The overlap grew on 2026-09-03 and the residue
did not: `active` is still the one member of the undo slice that is not versioned
content, and a journal entry cannot distinguish it, so a cursor-only command
(`use x`) becomes an utterance whose replay leaves the versioned slice unchanged.

This is a redundant utterance, never a missing one, and the choice follows §1.2:
recording something that turns out to be meaningless costs a false conflict, and
dropping something that turns out to be real is a silent wrong answer. A future
Core `slice` value distinguishing `mantles` from `active` would remove it.

`glyph declare` needs no special handling and gets none. Core journals it `pure:
true`, `slice: "undo"`, so it arrives as an ordinary entry and becomes an ordinary
utterance — and because §4.4 now versions what it changed, replaying that utterance
moves the version name, which is what makes it a real entry rather than a
redundant one.

### 8.2 The utterance

An **utterance** is a content-addressed change naming its causal parents by hash.
Its digest is a §3 digest with `kind` = `utterance` and an **empty** policy string,
over the §2 encoding of a map with exactly these six members:

| member | encoding |
|---|---|
| `command` | STR — Core's **canonical** line (`rm x` records as `rune rm x`) |
| `verb` | STR |
| `who` | STR, or NULL when there is no actor |
| `group` | STR, or NULL when there is none |
| `parents` | SET of STR |
| `minted` | SET of STR |

An **utterance name** is `"u:"` followed by the lowercase hex of that digest.

Three properties are normative because each is a way an implementation could
diverge:

1. **`parents` and `minted` are SETS.** A causal predecessor set has no order, and
   `minted` reaches an implementation from a merge-diff over sorted id images, so
   its arrival order is an artifact of that walk. An implementation that encoded
   either as a SEQ would compute a different name for the same change.
2. **No other field participates.** In particular a journal `seq` MUST NOT be
   hashed: it is a fact about one manager instance's dispatch counter, and two
   peers' counters are unrelated. An implementation MAY carry it as local
   provenance.
3. **Wall-clock time MUST NOT appear at all** — not in the digest and not in the
   causal structure. It MAY be recorded beside an utterance for display.

Two utterances with equal content and equal parents are **the same utterance**.
This deduplication is required, not merely permitted: it is what makes receiving
the same utterance twice free.

### 8.3 The history graph

The **history graph** is a set of utterances ordered by the parent relation. An
implementation:

- **MUST NOT** admit an utterance whose recomputed digest differs from the name it
  arrived under.
- **MUST NOT** admit an utterance any of whose parents it does not hold. A node
  whose ancestry is absent has no position in the partial order, and holding one
  while reporting a complete history is the failure a Merkle clock exists to
  prevent. An implementation MUST make the unplaceable node available to its
  caller rather than discarding it.
- **MUST** treat re-admitting a held utterance as a no-op.

**Heads** are the utterances with no children. Several heads is a normal resting
state and MUST NOT be treated as an error condition.

A **cut name** is `"c:"` followed by the lowercase hex of a §3 digest with `kind` =
`cut` and an empty policy string, over the §2 SET encoding of the head names. A cut
is downward-closed, so its maximal elements determine it.

A cut name and a version name (§3) MUST NOT be compared. They are digests of
different objects — one of a history, one of a state — and are never equal. The
distinct prefixes exist so that the mistake is visible.

### 8.4 The linear extension

A **timeline** is derived, never stored. An implementation that renders one MUST
produce a topological order of the parent relation, and MUST break ties among
simultaneously-ready utterances by **ascending name**.

The tiebreak is normative because without one the rendered order is a fact about
insertion order rather than about the graph, and two peers holding identical
histories would show different timelines. With it, the extension is a function of
the graph.

### 8.5 What this section does not specify

- **Replay.** Applying an utterance to a Void Core state is the application's, and
  an utterance carries `command` plus `minted` precisely so that it can be.
- **Inverses.** Core's undo is memento-based and commands have no inverses; the
  system-level inverse-of-undo (append an inverting utterance) is unbuilt.
- **Batch granularity.** `VoidCore:SPEC.md §6.2` records a `batch` as **one**
  entry, so this specifies one utterance per batch. `okf/concepts/utterance.md`
  argues for *n* utterances plus a shared `group` — that requires data Core does
  not currently emit, and the gap is recorded rather than papered over.
- **Transport, signatures and capabilities.** Blocked on the trust model.

---

## 9. Conformance

`conformance/` holds **188 language-neutral vectors** covering every section above,
in the shape `VoidCore:conformance/reduce/` proved. An implementation is conforming
iff it reproduces every `out` exactly.

Case names carry assertions the runner enforces between adjacent cases — `must
equal` / `must DIFFER` — so the SPEC's invariants (tags as a set, hydration,
undirected symmetry, order sensitivity, peer-local exclusion) are checked as
**relations** and survive regeneration. A suite that only recorded what one
implementation printed would be self-consistent and worthless; this is the guard
against that.

See `conformance/README.md`.

---

## 10. Reference implementation

`include/voidpalabra/canonical.hpp` + `src/canonical/` — C++20, zero
dependencies. `tests/canonical_test.cpp` is 27 property tests / 880 checks, in two
halves: *permute everything meaningless and assert the bytes do not move*, then
*change anything meaningful and assert they do*. The second half is what makes the
first mean anything.

§8 is `include/voidpalabra/utterance.hpp` + `src/utterance/`, and
`tests/utterance_test.cpp` is built in the same two halves for the same reason.

The SHA-256 implementation is verified against the FIPS 180-4 vectors, and against
`VoidCore:conformance/scry/07-provenance.json`'s pinned
`provenance({}) == 44136fa355b3678a`, which is `sha256("{}")` truncated — the one
point of byte-level agreement Palabra and Core already share.
