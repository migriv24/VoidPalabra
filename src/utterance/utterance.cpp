/* utterance.cpp — the content address, the JSON form, and the journal filter.
 *
 * See include/voidpalabra/utterance.hpp for the contract. This file is the
 * "addressed" and "filtered" halves of the journal→history conversion; history.cpp
 * is the "related" half.
 */
#include "voidpalabra/utterance.hpp"

#include "encoding/internal.hpp"

#include "cJSON.h"

#include <algorithm>
#include <string>
#include <vector>

namespace voidpalabra {

using namespace enc;

namespace {

/* An absent `who` and an absent `group` encode as the §2 null tag, which is a
 * distinct byte from any string including the empty one. That is what keeps
 * "nobody said this" and "the empty actor said this" apart in the hash. */
std::string encoded_or_null(bool present, const std::string& s) {
    return present ? encode_str(s) : std::string(1, kNull);
}

std::vector<std::string> encoded_each(const std::vector<std::string>& items) {
    std::vector<std::string> out;
    out.reserve(items.size());
    for (const std::string& s : items) out.push_back(encode_str(s));
    return out;
}

}  // namespace

/* ── the content address ──────────────────────────────────────────────────── */

Digest Utterance::digest() const {
    /* The payload is a §2 map. Its members are exactly the six fields that say
     * WHAT HAPPENED and WHAT IT FOLLOWED, and the omissions are the interesting
     * part:
     *
     *   - `seq` is out. It is a fact about one manager's dispatch counter, and
     *     two peers' counters are unrelated; hashing it would make the same
     *     change name itself differently on two machines, which is the exact
     *     failure a content address exists to prevent.
     *   - `hash` is out, necessarily — it is the output.
     *   - Wall-clock time is not here at all. okf/concepts/history-graph.md
     *     allows time to be RECORDED for humans and forbids it from being
     *     CONSULTED; leaving it out of the address is the strongest form of that,
     *     because a field that cannot enter the name cannot enter causality by
     *     accident later.
     *
     * `parents` and `minted` are SETS: parents because a causal predecessor set
     * has no order (SPEC §2.4 sorts and dedups, so two peers listing them
     * differently agree), and `minted` because Core derives it by a merge-diff
     * over sorted id images — the order it arrives in is an artifact of that
     * walk, not information. */
    std::vector<std::pair<std::string, std::string> > fields;
    fields.emplace_back("command", encode_str(command));
    fields.emplace_back("verb", encode_str(verb));
    fields.emplace_back("who", encoded_or_null(has_who, who));
    fields.emplace_back("group", encoded_or_null(!group.empty(), group));
    fields.emplace_back("parents", encode_set(encoded_each(parents)));
    fields.emplace_back("minted", encode_set(encoded_each(minted)));

    /* The policy slot of the §3 header is empty: an utterance carries no
     * `include_view` choice, because it records a COMMAND rather than a view of
     * state. Leaving the slot present-but-empty keeps the header format uniform
     * across kinds instead of giving this one a shape of its own. */
    return domain_digest("utterance", encode_map_of_encoded(fields), "");
}

std::string Utterance::name() const { return "u:" + to_hex(digest()); }

const std::string& Utterance::seal() {
    std::sort(parents.begin(), parents.end());
    parents.erase(std::unique(parents.begin(), parents.end()), parents.end());
    hash = name();
    return hash;
}

/* ── JSON ─────────────────────────────────────────────────────────────────── */

cJSON* utterance_to_json(const Utterance& u) {
    cJSON* o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "hash", u.hash.c_str());
    cJSON* parents = cJSON_AddArrayToObject(o, "parents");
    for (const std::string& p : u.parents)
        cJSON_AddItemToArray(parents, cJSON_CreateString(p.c_str()));
    cJSON_AddStringToObject(o, "command", u.command.c_str());
    cJSON_AddStringToObject(o, "verb", u.verb.c_str());
    if (u.has_who) cJSON_AddStringToObject(o, "who", u.who.c_str());
    else cJSON_AddItemToObject(o, "who", cJSON_CreateNull());
    cJSON* minted = cJSON_AddArrayToObject(o, "minted");
    for (const std::string& m : u.minted)
        cJSON_AddItemToArray(minted, cJSON_CreateString(m.c_str()));
    if (!u.group.empty()) cJSON_AddStringToObject(o, "group", u.group.c_str());
    else cJSON_AddItemToObject(o, "group", cJSON_CreateNull());
    cJSON_AddNumberToObject(o, "seq", static_cast<double>(u.seq));
    return o;
}

namespace {

bool ufail(std::string* error, const std::string& what) {
    if (error) *error = what;
    return false;
}

const cJSON* uitem(const cJSON* obj, const char* key) {
    return cJSON_GetObjectItemCaseSensitive(const_cast<cJSON*>(obj), key);
}

}  // namespace

bool utterance_from_json(const cJSON* obj, Utterance& out, std::string* error) {
    if (!obj || !cJSON_IsObject(const_cast<cJSON*>(obj)))
        return ufail(error, "utterance is not an object");

    Utterance u;

    const cJSON* command = uitem(obj, "command");
    if (!command || !cJSON_IsString(command) || !command->valuestring)
        return ufail(error, "utterance has no string 'command'");
    u.command = command->valuestring;

    const cJSON* verb = uitem(obj, "verb");
    if (!verb || !cJSON_IsString(verb) || !verb->valuestring)
        return ufail(error, "utterance has no string 'verb'");
    u.verb = verb->valuestring;

    const cJSON* who = uitem(obj, "who");
    if (who && cJSON_IsString(who) && who->valuestring) {
        u.who = who->valuestring;
        u.has_who = true;
    }

    const cJSON* group = uitem(obj, "group");
    if (group && cJSON_IsString(group) && group->valuestring)
        u.group = group->valuestring;

    const cJSON* parents = uitem(obj, "parents");
    if (parents && cJSON_IsArray(const_cast<cJSON*>(parents))) {
        for (const cJSON* p = parents->child; p; p = p->next) {
            if (!cJSON_IsString(p) || !p->valuestring)
                return ufail(error, "utterance 'parents' holds a non-string");
            u.parents.push_back(p->valuestring);
        }
    }

    const cJSON* minted = uitem(obj, "minted");
    if (minted && cJSON_IsArray(const_cast<cJSON*>(minted))) {
        for (const cJSON* m = minted->child; m; m = m->next) {
            if (!cJSON_IsString(m) || !m->valuestring)
                return ufail(error, "utterance 'minted' holds a non-string");
            u.minted.push_back(m->valuestring);
        }
    }

    const cJSON* seq = uitem(obj, "seq");
    if (seq && cJSON_IsNumber(seq)) u.seq = static_cast<std::int64_t>(seq->valuedouble);

    /* Recompute and compare. A stored hash is a claim; this is the check that
     * makes it a proof. Without it the graph would happily file corrupted content
     * under a correct-looking name, and every later verification would agree with
     * it — the failure mode a content-addressed store exists to make impossible. */
    const cJSON* claimed = uitem(obj, "hash");
    std::string recomputed;
    try {
        std::vector<std::string> sorted = u.parents;
        std::sort(sorted.begin(), sorted.end());
        sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());
        u.parents = sorted;
        recomputed = u.name();
    } catch (const CanonicalError& e) {
        return ufail(error, std::string("utterance has no canonical form: ") + e.what());
    }
    if (claimed && cJSON_IsString(claimed) && claimed->valuestring &&
        recomputed != claimed->valuestring)
        return ufail(error, std::string("utterance hash mismatch: stored ") +
                                claimed->valuestring + ", computed " + recomputed);
    u.hash = recomputed;

    out = u;
    return true;
}

/* ── the filter ───────────────────────────────────────────────────────────── */

const char* skip_reason(Skip s) {
    switch (s) {
        case Skip::effectful:  return "effectful — it crossed the holiday boundary";
        case Skip::view_slice: return "view slice — placement is not versioned content";
        case Skip::host_slice: return "host slice — it changed nothing in the document";
    }
    return "unknown";
}

IngestReport ingest(const std::vector<JournalEntry>& entries,
                    const std::vector<std::string>& parents,
                    const IngestOptions& options) {
    IngestReport report;
    std::vector<std::string> current = parents;
    std::sort(current.begin(), current.end());
    current.erase(std::unique(current.begin(), current.end()), current.end());

    for (const JournalEntry& e : entries) {
        /* The two filters, in the order their authority runs.
         *
         * First, purity — VoidCore:SPEC.md §6.2 states the obligation on
         * consumers directly: a replayable or transmissible history MUST keep
         * only pure entries. Core classifies statically, so `save` is effectful
         * even on a host with no effect handler registered, and that is
         * deliberately host-INdependent: a host-dependent answer would let the
         * same command be a recordable change on one peer and not on another,
         * and convergence would then depend on how a device was configured.
         *
         * Second, the slice — Palabra's own rule, not Core's. Core's `slice`
         * field REPORTS where a change landed and explicitly declines to
         * legislate which slice is synced; that decision is
         * okf/concepts/utterance.md's, and its answer is `mantles`. `view` is
         * `placement` and `host` is nothing in the document, so neither can be
         * versioned content.
         *
         * A pure `host`-slice entry is not a contradiction and is not dead code:
         * `journal` and `log` are pure and change nothing in the document, so
         * they land in `host` with `pure` true. The purity check running first
         * means an effectful command is reported as effectful rather than as a
         * host-slice one, which is the more informative of the two true
         * statements. */
        if (!e.pure) {
            report.skipped.push_back({e, Skip::effectful});
            continue;
        }
        if (e.slice == "view") {
            report.skipped.push_back({e, Skip::view_slice});
            continue;
        }
        if (e.slice != "undo") {
            report.skipped.push_back({e, Skip::host_slice});
            continue;
        }

        Utterance u;
        u.parents = current;
        u.command = e.command;
        u.verb = e.verb;
        u.who = e.who;
        u.has_who = e.has_who;
        u.minted = e.minted;
        u.group = options.group;
        u.seq = e.seq;
        u.seal();

        current.assign(1, u.hash);
        report.recorded.push_back(u);
    }
    return report;
}

/* A note on what the `undo` slice still carries, stated here because it is the
 * one place this layer is knowingly coarser than okf/concepts/utterance.md.
 *
 * Core's `undo` slice is `mantles` PLUS `active`, and Palabra versions `mantles`
 * alone — `active` is a cursor, like `placement`. The journal entry cannot tell
 * them apart: `use x` moves only the cursor and still records as `slice: "undo"`.
 * So a cursor-only command becomes an utterance whose replay leaves the versioned
 * slice unchanged.
 *
 * Fixing that here would mean a table of verbs that touch `active`, and a table
 * is exactly what Core declined to build for `minted` and for the same reason: an
 * omission from it under-reports SILENTLY. The asymmetry canonical.hpp states for
 * ordering applies unchanged — recording something that turns out to be
 * meaningless costs a redundant utterance, and dropping something that turns out
 * to be real is a silent wrong answer. The failure modes are not symmetric, so
 * neither is the default.
 *
 * The clean fix is not ours: it is a `slice` that distinguishes `mantles` from
 * `active`, which is one more value in a field Core already emits. Recorded in
 * okf/design/open-questions.md §3.1. */

}  // namespace voidpalabra
