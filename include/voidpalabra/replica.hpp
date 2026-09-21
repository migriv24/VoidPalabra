/* replica.hpp — one device's copy of the shared state, kept BETWEEN exchanges.
 *
 * ── The bug this exists to end ──
 *
 * join.hpp says it in its first paragraph: two peers holding {a} and {} cannot
 * tell "I never had a" from "I removed a", so a join of bare state is union and a
 * removal never propagates. The enriched document exists to tell them apart — but
 * only if it SURVIVES. A client that calls `enrich` on its current state before
 * every exchange throws away the record of every removal it ever made, so the next
 * merge hands the deleted thing straight back. On a timer, that is a sync that
 * resurrects deletions forever, and it was the first thing a real client hit
 * (reported 2026-09-16).
 *
 * `enrich` is for a ONE-SHOT import. A replica is for a sync loop.
 *
 * ── The loop, and the order it must happen in ──
 *
 *     Replica r = ...;                  // restored from r.to_bytes(), or created once
 *     Doc delta = r.observe(state);     // 1. record what the user did since last time
 *     save(r.to_bytes());               // 2. PERSIST, before anything leaves the device
 *     send(delta);  or  send(r.doc());  // 3. share
 *     r.merge(received);                // 4. take in what a peer did
 *     splice(state, r.flatten());       // 5. show it — `mantles` and `glyphs` only
 *     save(r.to_bytes());
 *
 * Step 2 is not tidiness. A replica mints tags from a counter. If a delta is sent
 * and the device then dies before the replica is saved, the restarted replica has
 * a counter that is behind tags that already exist on other devices, and it will
 * mint them again — for different values. `merge` detects exactly this and refuses
 * (see `MergeResult::identity_collision`), but detecting it is the fallback, not
 * the plan.
 *
 * Step 5 must not overwrite an edit the user made after step 1. If the document
 * changed in between, observe again before splicing. This library cannot see the
 * application's document between calls, so it cannot enforce that.
 *
 * ── Not thread-safe ──
 *
 * One replica, one thread at a time. Two threads observing concurrently would
 * interleave mints, which is harmless, and interleave diffs, which is not.
 */
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "voidpalabra/join.hpp"

struct cJSON;

namespace voidpalabra {

enum class MergeResult {
    ok,
    /* The document failed `validate`. Nothing was merged. */
    invalid,
    /* The document holds tags minted under THIS replica's id that this replica has
     * no record of minting. Nothing was merged.
     *
     * Only three things produce that, and all three are serious:
     *   - this replica was restored from an older backup, so the tags it minted
     *     after the backup exist elsewhere and its counter will mint them again;
     *   - the device crashed after sending a delta and before saving the replica —
     *     the same situation, arrived at faster;
     *   - another device is using this id: its replica bytes were copied, cloned
     *     with a disk image, or handed over while provisioning a new device.
     *
     * In every case the fix is the same: `fork` to a fresh id, then merge again.
     * Continuing under the old id would reuse tags, and a remove that retires one
     * value would silently retire an unrelated one that shares its tag. */
    identity_collision,
};

class Replica {
public:
    /* A new replica with nothing in it.
     *
     * `id` must be unique per replica INSTANCE — not per user, not per profile. A
     * person with a laptop and a phone is two replicas. It must be random: this
     * library has no CSPRNG of its own and asks the application for one rather
     * than pretending. 16 or more characters of [A-Za-z0-9-] are required, which
     * refuses the ids two people would choose independently ("laptop", "main")
     * and guarantees nothing else. The real guard is `identity_collision`.
     *
     * Returns false, with a reason, for an id that does not qualify. */
    static bool create(const std::string& id, Replica& out, std::string* why = nullptr);

    /* The same document under a new identity, with a fresh counter.
     *
     * MUST be used whenever replica bytes start a life on a second device or a
     * second timeline: restoring a backup, cloning a machine, provisioning a new
     * member's device from an existing one. The bytes carry the old id, and two
     * replicas minting under one id is the failure `identity_collision` exists to
     * catch after the fact. `new_id` must differ from the current id. */
    bool fork(const std::string& new_id, Replica& out, std::string* why = nullptr) const;

    const std::string& id() const { return id_; }
    std::uint64_t issued() const { return issued_; }

    /* Moves whenever the document may have changed — an observation that recorded
     * something, a merge, a resolution. A reader that derives something expensive
     * from the document (a sync session's export, say) recomputes only when this
     * moves. Not persisted, and not comparable between replicas. */
    std::uint64_t revision() const { return revision_; }

    /* How to read a field that holds more than one value. Applied at read time
     * only, like everywhere else in this layer — two replicas with different
     * policies still hold identical documents. Not persisted: it is configuration,
     * and the application sets it again after `from_bytes`. */
    void set_policy(const JoinPolicy& p) { policy_ = p; }
    const JoinPolicy& policy() const { return policy_; }

    /* ── recording local changes ──────────────────────────────────────────── */

    struct Observed {
        bool ok = false;
        std::string error;          // why, when !ok; the replica is then unchanged
        std::size_t changes = 0;    // registers and sets this call touched
        Doc delta;                  // exactly those changes, as a mergeable document
    };

    /* Record the application's current state as this replica's own acts.
     *
     * Compares `state` with what this replica SHOWS (what `flatten` would return)
     * and records only the difference:
     *
     *   - a value that differs from what was shown is written;
     *   - something shown and now absent is REMOVED — a mantle, a rune, a content
     *     key, a tag, an edge, a glyph declaration;
     *   - something absent and now present is added, including a thing that was
     *     removed and has come back (an undo restores the same `spirit.id`, and it
     *     comes back with the values the state holds).
     *
     * Two rules decide the edge cases, and both exist because the obvious diff gets
     * them wrong in an automatic sync loop:
     *
     *   1. A CONFLICTED FIELD IS NOT RESOLVED BY OBSERVING IT. A field holding two
     *      values is shown as one of them. If `state` still holds the one that was
     *      shown, the user has not decided anything — they were shown a default.
     *      Writing it back would silently resolve every conflict in the document
     *      in favour of whatever `flatten` happened to pick, on the next tick. So a
     *      value equal to what was shown is left alone. To choose the shown value
     *      on purpose, call `resolve`.
     *
     *   2. OBSERVING ITS OWN OUTPUT CHANGES NOTHING. `observe(flatten())` records
     *      zero changes. If it did not, every tick of an automatic sync would mint
     *      new tags for unchanged data, the metadata would grow without bound, and
     *      two peers would trade "changes" with each other forever.
     *
     * A removal also records what it SAW: every live tag beneath the removed thing.
     * That is what lets `conflicts()` tell a clean delete from a delete that raced
     * an edit on another device.
     *
     * Atomic: if `state` cannot be encoded (invalid UTF-8, a NaN, duplicate mantle
     * names) nothing changes and `error` says why. Tags minted by a failed call are
     * burned, never reused. */
    Observed observe(const cJSON* state);

    /* ── taking in a peer's changes ───────────────────────────────────────── */

    /* Validate, check identity, then join. A full document and a delta are both
     * accepted — they have the same shape.
     *
     * Deltas may arrive in any order and more than once; the join does not care.
     * But a delta that is LOST is lost silently: nothing in a delta says what came
     * before it. Exchange full documents (`doc()`) from time to time, so a lost
     * delta is repaired rather than remembered as a divergence nobody can see. */
    MergeResult merge(const cJSON* remote, std::string* why = nullptr);
    MergeResult merge(const Doc& remote, std::string* why = nullptr);

    /* ── reading ──────────────────────────────────────────────────────────── */

    const Doc& doc() const { return doc_; }

    /* The versioned slice — `mantles` and `glyphs` — to SPLICE into the
     * application's document. Never a whole document; see document.hpp. */
    Doc flatten() const;

    /* Value conflicts, and deletes that raced an edit. See conflict.hpp. */
    std::vector<Conflict> conflicts() const;

    /* Rules each device kept that the merge broke — two runes with one name, a
     * link a concurrent removal or rename broke, a rune whose type was undeclared
     * elsewhere. See anomaly.hpp. Check after every merge, alongside `conflicts`:
     * a conflict asks which value, an anomaly asks for an edit. */
    std::vector<Anomaly> anomalies() const;

    /* Who wrote each live value at `place` — see crdt/provenance.hpp. A read of the
     * tags already in the document; replica ids, not people, and a claim rather
     * than proof until the trust model lands. */
    std::vector<Written> writers(const Place& place) const;

    /* Settle a conflict by choosing one of its `sides`, recorded as this replica's
     * act. For a value conflict the chosen value is written. For a
     * `deleted_while_edited` conflict the sides are "deleted" and "kept": keeping
     * brings the thing back with every value it currently holds, deleting removes
     * it again and this time records the edit as seen.
     *
     * Returns false, changing nothing, if the conflict no longer exists as given —
     * a merge since it was read may have changed it. Resolving a stale conflict
     * would overwrite a value the user never saw; read `conflicts()` again. */
    bool resolve(const Conflict& conflict, std::size_t side, Doc* delta = nullptr);

    /* ── persistence ──────────────────────────────────────────────────────── */

    /* The document, the id and the counter, in ONE blob. They are useless apart:
     * a document without its counter re-mints tags, and a counter without its
     * document does not know what it minted. Store the blob atomically —
     * `Archive::write_file` does. */
    std::string to_bytes() const;
    static bool from_bytes(const std::string& bytes, Replica& out,
                           std::string* why = nullptr);

private:
    std::string id_;
    std::uint64_t issued_ = 0;
    std::uint64_t revision_ = 0;
    Doc doc_;
    JoinPolicy policy_;
};

}  // namespace voidpalabra
