---
type: Design
title: World models — where a learned model earns its keep
description: An assessment of the DINO/JEPA proposal. Three of its arguments do not survive; the framing does. The rule that decides where a learned model belongs, and the argument the proposal missed.
tags: [status:planned, audience:dev, confidence:exploratory]
timestamp: 2026-07-27T00:00:00Z
---

Relayed to the author 2026-07-27: a technical memo proposing **DINO** (self-distilled
vision features) and **JEPA** (joint-embedding predictive architecture, predicting in
latent space) as "sensory cortices" for Palabra — watching peers' interfaces,
simulating merges, and navigating the sync protocol, on the argument that a
decoder-only LLM assumes a total order and these models do not.

The framing is right and worth keeping. Three of its specific proposals do not
survive, and the strongest argument for its own thesis is one it does not make. All
three are recorded because the proposal is persuasive and will be made again.

---

# 1. What does not survive

**The architectural argument is a pun.** The memo maps "partial order" onto
"bidirectional attention" and "total order" onto "causal mask." These are unrelated.
A causal mask is about which *token positions* a model may attend to during
training; a Mazurkiewicz partial order is about which *changes commute* in a
history. A bidirectional encoder does not thereby respect an independence relation,
and an autoregressive model does not thereby violate one. The alignment the memo
wants is real at the level of *taste* and absent at the level of *mechanism* — so it
cannot carry a design decision.

**Watching a peer's UI is strictly worse than reading the hash.** The proposal has
DINO observe a peer's interface to detect that a file appeared or a sync finished.
But Palabra's state is **content-addressed and self-verifying**
([canonical form](/concepts/canonical-form.md)): "has this peer's state changed" is a
hash comparison — exact, cheap, and provable without trusting the sender. Replacing
it with pixel inference substitutes an approximation for a certainty, and injects a
nondeterministic component into a system whose entire value proposition is that two
peers compute the same answer. This one is not a close call.

**Predicting merge outcomes is a downgrade.** The memo's headline use is a JEPA that
predicts the state resulting from a merge without replaying history. Palabra already
computes that, **exactly**, from the two current states, in time proportional to the
diff — that is the whole content of [join](/concepts/join.md), and *history is
optional* is already the reason no replay is needed. A learned approximation of a
cheap exact function is a regression, and it would break the property that makes the
system trustworthy.

> The general error: the memo aims learned models at the parts of Palabra that are
> **already solved exactly**. Those are the parts with the highest cost of being
> approximately right.

---

# 2. The rule

> **A learned model belongs exactly where Palabra's mathematics runs out.**

The mathematics runs out in a short and specific list. Everywhere else, there is an
algorithm with a proof, and a model would be replacing a theorem with a guess.

| where the math runs out | why there is no algorithm | how good the fit is |
|---|---|---|
| **[Conflict](/concepts/conflict.md) *resolution*** | the join says *that* two values conflict; **choosing** between them is a judgment about meaning | strong — and it is the one place the design deliberately declines to decide |
| **Pruning policy** ([open questions](/design/open-questions.md) §5) | safe pruning needs to know what peers will need; in an open mesh **the peer set is unknowable** — the same property that makes reconciliation cheap | strongest fit, and the memo never mentions it |
| **Peer scheduling** | which peer to reconcile with next, under battery, bandwidth and intermittent presence — a control problem, not a correctness one | good; MPC over a latent model is genuinely the right shape |
| **Semantic search over history** | "when did the layout stop working" is not a query the tag grammar can express | good, and note it is **just another scry** — a cut projected into ℝⁿ |

Two of these deserve emphasis.

**Conflict resolution is the honest opening.** [Conflict](/concepts/conflict.md)
makes a conflict a first-class value precisely so the system does not have to guess,
and resolution is *"a new utterance naming both sides as parents."* Nothing in that
says a **human** must author it. A model that proposes a resolution — which is then
an ordinary, attributed, syncable, disagreeable-with utterance — adds capability
without weakening a single guarantee. It cannot corrupt state, because its output
goes through the same door as everyone else's.

**Pruning is where this could actually matter.** [Why not
linear](/design/why-not-linear.md) §5 names metadata growth as *"the most likely
thing to sink the design at scale,"* and [open questions](/design/open-questions.md)
§5 calls it genuinely unsolved. It is unsolved *because* it requires prediction: you
must estimate what will be needed without knowing who will ask. That is a learned
model's actual job description. If a world model earns a place in Palabra, this is
where — and the roadmap already says to prototype pruning against a real workload
rather than reason about it.

---

# 3. The argument the memo missed

The best case for its own thesis is not that Palabra needs these models. It is that
**Palabra produces the training corpus they need.**

Every [utterance](/concepts/utterance.md) is a `(state, delta, state')` triple that
is **labeled** (the delta is the label), **content-addressed** (each element has a
verifiable name), **replayable** (a cut reconstructs the exact input), and
**attributed** (`who`, plus `group` marking which changes were one human intent).
Ordinary systems do not have this: logs lack the before-state, snapshots lack the
delta, and neither is addressable.

That is close to the ideal corpus for a latent predictive model over structured
state, and it accumulates for free as a side effect of the system working.

**The consequence is a schedule, not a subsystem:** build Palabra first, and the
world-model question answers itself later with data instead of argument. Building
both together means training a predictor on a corpus that does not exist yet, to
approximate functions that are not yet implemented exactly. The order is not
negotiable and it is the memo's own logic, followed one step further than the memo
follows it.

---

# 4. The one structural point worth adopting now

The memo's dual-system recommendation — LLM as executive function, latent model as
perception and planning — implies that a Palabra deployment eventually holds
**several kinds of agent** with different interfaces.

That does not need designing today, and it does not need a new mechanism when it
does. [Compute](/concepts/compute.md)'s capability declaration already carries
`{ kind, id, via }` per compute resource, and `kind: "embed"` is in the sketch
alongside `kind: "llm"` for exactly this reason. A world model is another declared
capability on a peer, reached through a holiday, recorded with provenance — not a
new citizen of the architecture.

**And *where* one would run is now answered too**, boringly and on purpose: **a world
model is a model in Void Bicho** (`VoidBicho:/README.md`) — a rune with
`kind: "world"`, loaded by a runtime, invoked with a budget, stamped with provenance
(`VoidBicho:/design/boundaries.md` §5). If adding a fundamentally different model
architecture had required a new subsystem on either side, the vocabulary would have
been wrong.

**Nothing here is scheduled.** This page exists so the proposal is answered rather
than re-argued, and so that if a learned model is ever added, it is added at one of
the four places in §2 and not at the three in §1.
