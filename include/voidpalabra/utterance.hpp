/* utterance.hpp — Phase 3, rung 1: Void Core's command journal, turned into a
 * content-addressed partial order.
 *
 * ── Why this file exists now and did not exist on 2026-08-21 ──
 *
 * okf/design/open-questions.md §3.1 said, in these words: *"Two things Palabra
 * needs FROM Core before any utterance work. Owner: Void Core. Blocks: all
 * utterance work."* Core shipped both in 0.2.8 (`VoidCore:SPEC.md §6.2`) —
 * commands are reified as a journal, and pure-vs-effectful is normative and
 * closed:
 *
 *     A command is effectful iff its verb can reach the host through the effect
 *     handler. The complete list is `save`, `deploy`, `build`, `preview`,
 *     `effect`. Every other verb is pure.
 *
 * That list is closed rather than maintained, because `vc_set_effect_handler` is
 * the only way out of a core that does no I/O by definition. So Palabra's rule
 * from okf/concepts/utterance.md — *an utterance may only record a PURE change* —
 * stopped being a wish and became a filter that can be written down.
 *
 * ── What this layer is, in one sentence ──
 *
 *     A journal is what a machine did, in order. A history graph is what
 *     happened, ordered only where the order is real.
 *
 * The conversion between them is the whole content of this file, and it is not a
 * copy: entries are FILTERED (§6.2's consumer obligation), ADDRESSED (a hash over
 * content, so two peers name the same change with the same word), and RELATED
 * (parents by hash, which makes the graph its own logical clock — no vector
 * clocks, no peer registry, no wall clock).
 *
 * ── What it deliberately does not do ──
 *
 * It does not replay. Replay applies a command to a Void Core manager, and
 * Palabra does not link Void Core — okf/design/integration.md keeps that
 * direction of the dependency, and an utterance carries everything a replayer
 * needs (`command` plus `minted`) precisely so the replayer can live in the
 * application.
 *
 * It does not converge anything either. Convergence is join.hpp's, over STATE.
 * A peer holding no utterances at all still merges correctly, and
 * okf/concepts/history-graph.md is emphatic that this separation is what lets an
 * ESP32 and a data server be peers in one protocol. This layer buys time travel,
 * blame, and selective sync — not correctness.
 */
#pragma once

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "voidpalabra/canonical.hpp"

struct cJSON;

namespace voidpalabra {

/* ── Core's journal entry (VoidCore:SPEC.md §6.2) ─────────────────────────── */

/* One entry as `vc_export_journal` emits it. Palabra parses this shape and owns
 * none of it; the field list is Core's, and inventing a field here would be a
 * fork rather than an extension.
 *
 * `who` is nullable in the JSON (it is `config.actor`, absent until a host sets
 * one), so the string and the presence flag are separate. Collapsing "" and null
 * would make an unattributed change and a change attributed to the empty actor
 * into the same utterance, and they are not the same. */
struct JournalEntry {
    std::int64_t seq = 0;      // 1-based, dense, never reused within an instance
    std::string command;       // THE CANONICAL LINE — `rm x` records as `rune rm x`
    std::string verb;          // canonical verb, for filtering
    std::string who;           // config.actor at the time
    bool has_who = false;      // false = JSON null
    bool pure = false;         // false = the verb can reach the holiday boundary
    std::string slice;         // "undo" | "view" | "host"
    std::vector<std::string> minted;  // ids that exist after and did not before
};

/* Parse `vc_export_journal` output (a JSON array, oldest first).
 *
 * Strict: an entry missing a field, or carrying a `slice` that is not one of the
 * three, is a parse failure rather than a defaulted entry. A record with a
 * quietly invented field is worse than no record, and this is the seam where a
 * Core version drift would first show up — so it should show up loudly. */
bool parse_journal(const std::string& json_text, std::vector<JournalEntry>& out,
                   std::string* error = nullptr);
bool parse_journal(const cJSON* array, std::vector<JournalEntry>& out,
                   std::string* error = nullptr);

/* ── the utterance ────────────────────────────────────────────────────────── */

/* A content-addressed change, naming its causal parents by hash.
 *
 * Deliberately not called a commit: it is a delta rather than a snapshot, it may
 * have any number of parents, and it is ordered only by the parent relation. See
 * okf/concepts/utterance.md for why the wrong name reimports the wrong model. */
struct Utterance {
    /* "u:" + 64 hex. Empty until `seal()`. */
    std::string hash;

    /* Causal predecessors, by hash. Kept SORTED, and hashed as a set: two peers
     * that list the same parents in different orders must name the same
     * utterance, or the content address is not one. */
    std::vector<std::string> parents;

    std::string command;               // Core's canonical line, verbatim
    std::string verb;
    std::string who;
    bool has_who = false;

    /* The identities Core minted while running this command.
     *
     * This is the field that makes an utterance worth more than its text. Core
     * mints rune ids from the CSPRNG, so replaying the STRING produces different
     * state and replaying the ENTRY does not. okf/concepts/utterance.md worked
     * this out before Core built it — "the utterance records the identity that
     * was minted rather than re-deriving it" — and 0.2.8 put it in the entry. */
    std::vector<std::string> minted;

    /* A human-facing grouping label, NOT an atom (okf/concepts/utterance.md).
     * Empty means none. Never participates in causality. */
    std::string group;

    /* Core's journal sequence number. LOCAL PROVENANCE ONLY: it is not hashed,
     * not compared, and not an ordering. `seq` is a fact about one manager
     * instance's dispatch, and two peers' seq spaces have nothing to do with each
     * other — treating it as an order is exactly the linear-history mistake
     * okf/design/why-not-linear.md exists to refuse. It is kept because it makes
     * an utterance traceable back to the journal line it came from, which is what
     * a bug report needs.
     *
     * That it does not move the hash is pinned by a conformance vector. */
    std::int64_t seq = 0;

    /* The content address. Over `command`, `verb`, `who`, `parents`, `minted` and
     * `group` — and over nothing else. Throws CanonicalError on input with no
     * honest byte form (invalid UTF-8, per SPEC §2.2). */
    Digest digest() const;
    std::string name() const;   // "u:" + lowercase hex

    /* Fill `hash` from `digest()`. Returns it. */
    const std::string& seal();
};

/* Two utterances with the same content and the same parents ARE one utterance,
 * and this deduplication is deliberate rather than a hazard: it is idempotence
 * (a ⊔ a = a) at the level of history, and it is why receiving the same utterance
 * twice over a lossy transport costs nothing. The case where it collapses two
 * genuinely distinct authorings — the same command, from the same state, on two
 * peers, minting nothing — is a case where the two authorings have the same
 * effect, so collapsing them changes no state. */

/* Round-trip a single utterance through JSON, for storage and for transport.
 * `from_json` VERIFIES: it recomputes the digest and refuses a mismatch, because
 * "you cannot be handed the wrong thing under the right name" is the property a
 * content address exists to provide, and an unchecked one does not provide it. */
cJSON* utterance_to_json(const Utterance& u);
bool utterance_from_json(const cJSON* obj, Utterance& out,
                         std::string* error = nullptr);

/* ── ingest: journal entries → utterances ─────────────────────────────────── */

/* Why an entry produced no utterance. Nothing is dropped silently — see
 * `IngestReport`. */
enum class Skip {
    effectful,    // VoidCore:SPEC §6.2 — a replayable history MUST keep only pure
    view_slice,   // `placement`: real state, but not versioned content
    host_slice    // it landed nowhere in the state document at all
};
const char* skip_reason(Skip s);

struct Skipped {
    JournalEntry entry;
    Skip why;
};

/* What one ingest produced, INCLUDING what it refused.
 *
 * The refusals are returned rather than discarded, and the argument is Core's own
 * from the 2026-08-27 handoff, applied one layer up: they record effectful
 * commands even though a replay consumer must skip them, because otherwise
 * `pure` is a constant `true` and a reader cannot tell *"nothing effectful
 * happened"* from *"a deploy happened and was not recorded"*. The same reasoning
 * says Palabra must not swallow the skip. An entry you must skip is cheaper than
 * a gap you cannot see. */
struct IngestReport {
    std::vector<Utterance> recorded;
    std::vector<Skipped> skipped;
};

struct IngestOptions {
    /* Applied to every utterance this call produces. A save point, a session, an
     * agent's turn — whatever the application means by "one action". */
    std::string group;
};

/* Convert entries into a chain of utterances rooted at `parents`.
 *
 * The chain is honest rather than a convenience: a single Void Core dispatcher is
 * sequential (`VoidCore:SPEC.md §6`'s threading rule), so each command genuinely
 * observed the state its predecessor left. Concurrency enters when two HISTORIES
 * meet, not within one journal — which is why `parents` is a parameter: hand it
 * the heads of a graph that has already absorbed a peer, and the first utterance
 * of this chain is the merge point, naming both. */
IngestReport ingest(const std::vector<JournalEntry>& entries,
                    const std::vector<std::string>& parents,
                    const IngestOptions& options = {});

/* ── the history graph ────────────────────────────────────────────────────── */

/* The set of utterances a peer holds, ordered by the parent relation: a
 * Merkle-DAG that is its own logical clock (okf/concepts/history-graph.md).
 *
 * There is no main branch and no privileged line. `heads()` may hold several, and
 * that is a normal resting state rather than a problem to be fixed. */
class History {
public:
    enum class Add {
        ok,
        duplicate,        // already held; free, by idempotence
        missing_parent,   // a parent is not here yet
        malformed         // unsealed, or the hash does not match the content
    };

    /* Add one utterance. Refuses an unsealed or mis-sealed one, and refuses one
     * whose parents are not all present.
     *
     * The refusal on a missing parent is the discipline that makes the graph a
     * clock: a node whose ancestry is absent cannot be placed in the partial
     * order, so accepting it would mean holding something whose position is
     * unknown while reporting a complete history. A transport that delivers out
     * of order buffers and retries — see `absorb`, which does exactly that. */
    Add add(const Utterance& u);

    bool has(const std::string& hash) const;
    const Utterance* get(const std::string& hash) const;
    std::size_t size() const { return by_hash_.size(); }
    bool empty() const { return by_hash_.empty(); }

    /* The maximal elements — sorted, so two peers holding the same graph report
     * the same list. Their count is a LOWER BOUND on the graph's width
     * (Dilworth); it is not the width itself, and this library does not yet
     * compute one. */
    std::vector<std::string> heads() const;

    /* The name of the cut this graph currently is: `"c:"` + the hex digest of the
     * head set.
     *
     * Order-independent by construction, so two peers who received the same
     * utterances in opposite orders say the same word — which is the entire point
     * of naming a version over a SET rather than a sequence
     * (okf/concepts/version-as-cut.md). Naming the heads is enough to name the
     * whole cut, because a cut is downward-closed: its maximal elements determine
     * it, and each head's hash already covers its ancestry.
     *
     * The prefix is `c:` and not `v:` on purpose. `version_name()` in
     * canonical.hpp names a STATE and this names a HISTORY; they are different
     * digests of different objects and are never equal, so giving them one prefix
     * would invite a comparison that silently always fails. Two prefixes make the
     * mistake visible at a glance. */
    std::string cut_name() const;
    static std::string cut_name(const std::vector<std::string>& heads);

    /* Reachability over the parent relation. `is_ancestor(a, a)` is false. */
    bool is_ancestor(const std::string& a, const std::string& b) const;
    /* Neither reaches the other: genuinely unordered, not "unordered because we
     * lost the timestamps". That distinction is information and Palabra keeps
     * it. A hash the graph does not hold is not concurrent with anything. */
    bool concurrent(const std::string& a, const std::string& b) const;
    std::set<std::string> ancestors(const std::string& hash) const;

    /* One valid reading of the partial order, for a human.
     *
     * Kahn's algorithm with a canonical tiebreak — among the utterances that are
     * ready, the smallest hash goes first. The tiebreak is what makes this a
     * FUNCTION of the graph rather than of the insertion order, so two peers
     * render the same timeline. A timeline is derived, never stored. */
    std::vector<std::string> linear_extension() const;

    /* Append a journal onto the current heads. The common path. */
    IngestReport record(const std::vector<JournalEntry>& entries,
                        const IngestOptions& options = {});

    /* Merge another peer's graph into this one. Returns how many were added.
     *
     * This one cannot partially fail, and the reason is structural rather than
     * lucky: `add` refuses a node whose parents are absent, so every History is
     * dependency-closed by construction and every node in `other` has its
     * ancestry inside `other`. A graph can be merged whole or not at all.
     *
     * Incomplete delivery is a TRANSPORT condition, not a graph one, so it has a
     * different entry point — `receive`. */
    std::size_t absorb(const History& other);

    /* Take in a bag of utterances that arrived from somewhere, in any order.
     *
     * The transport-facing door. Retries until a pass places nothing, so drops,
     * duplicates and reordering cost passes rather than correctness
     * (okf/concepts/history-graph.md: *"transport may be terrible"*). Whatever is
     * still unplaceable at the end named an ancestor that never arrived; it is
     * handed back through `unplaceable` so a caller can hold it and ask for the
     * missing region, which is the honest thing to do with a message you cannot
     * yet place — and is never held here under a complete-looking history. */
    std::size_t receive(const std::vector<Utterance>& inbox,
                        std::vector<Utterance>* unplaceable = nullptr);

    /* Persistence. Core's journal is session-scoped and in-memory by its own
     * statement, so keeping an utterance beyond a session is Palabra's layer, and
     * this is the seam where it hands off to store.hpp / archive.hpp. */
    cJSON* to_json() const;
    static bool from_json(const cJSON* obj, History& out,
                          std::string* error = nullptr);

private:
    std::map<std::string, Utterance> by_hash_;
    std::map<std::string, std::vector<std::string> > children_;
};

}  // namespace voidpalabra
