/* history.cpp — the Merkle-DAG: heads, ancestry, cuts, and one honest timeline.
 *
 * See include/voidpalabra/utterance.hpp for the contract, and
 * okf/concepts/history-graph.md for why a partial order rather than a log.
 *
 * Every traversal here walks the PARENT direction, which is the only direction a
 * content address can point: a parent's hash is inside its child, so a child can
 * name its parents and a parent can never name its children. `children_` is a
 * derived index maintained on insert, and it exists purely so `heads()` and the
 * linear extension do not have to rescan the graph.
 */
#include "voidpalabra/utterance.hpp"

#include "encoding/internal.hpp"

#include "cJSON.h"

#include <algorithm>
#include <map>
#include <queue>
#include <set>
#include <string>
#include <vector>

namespace voidpalabra {

using namespace enc;

/* ── membership ───────────────────────────────────────────────────────────── */

bool History::has(const std::string& hash) const {
    return by_hash_.find(hash) != by_hash_.end();
}

const Utterance* History::get(const std::string& hash) const {
    auto it = by_hash_.find(hash);
    return it == by_hash_.end() ? nullptr : &it->second;
}

History::Add History::add(const Utterance& u) {
    if (u.hash.empty()) return Add::malformed;
    std::string recomputed;
    try {
        recomputed = u.name();
    } catch (const CanonicalError&) {
        return Add::malformed;
    }
    /* A hash that does not match its content is not merely wrong, it is
     * dangerous: everything downstream trusts the name, so admitting one bad
     * pairing poisons every later verification that agrees with it. */
    if (recomputed != u.hash) return Add::malformed;
    if (has(u.hash)) return Add::duplicate;
    for (const std::string& p : u.parents)
        if (!has(p)) return Add::missing_parent;

    by_hash_[u.hash] = u;
    for (const std::string& p : u.parents) children_[p].push_back(u.hash);
    return Add::ok;
}

/* ── heads and cuts ───────────────────────────────────────────────────────── */

std::vector<std::string> History::heads() const {
    std::vector<std::string> out;
    for (const auto& kv : by_hash_) {
        auto it = children_.find(kv.first);
        if (it == children_.end() || it->second.empty()) out.push_back(kv.first);
    }
    /* `by_hash_` is a std::map, so this is already sorted — but sorting is stated
     * rather than inherited, because the guarantee belongs to this function and
     * not to the container it happens to be built on. */
    std::sort(out.begin(), out.end());
    return out;
}

std::string History::cut_name(const std::vector<std::string>& heads) {
    std::vector<std::string> encoded;
    encoded.reserve(heads.size());
    for (const std::string& h : heads) encoded.push_back(encode_str(h));
    /* A SET, not a sequence: two peers who received the same utterances in
     * opposite orders must name the same cut, and encode_set sorts and dedups so
     * the caller's ordering cannot leak into the name. */
    return "c:" + to_hex(domain_digest("cut", encode_set(encoded), ""));
}

std::string History::cut_name() const { return cut_name(heads()); }

/* ── ancestry ─────────────────────────────────────────────────────────────── */

std::set<std::string> History::ancestors(const std::string& hash) const {
    std::set<std::string> seen;
    if (!has(hash)) return seen;
    std::vector<std::string> stack;
    const Utterance* start = get(hash);
    for (const std::string& p : start->parents) stack.push_back(p);
    while (!stack.empty()) {
        std::string h = stack.back();
        stack.pop_back();
        if (!seen.insert(h).second) continue;
        const Utterance* u = get(h);
        if (!u) continue;
        for (const std::string& p : u->parents) stack.push_back(p);
    }
    return seen;
}

bool History::is_ancestor(const std::string& a, const std::string& b) const {
    if (a == b) return false;
    if (!has(a) || !has(b)) return false;
    /* Walk from b toward the roots and stop at the first hit, rather than
     * materializing all of b's ancestors: the answer is usually near. */
    std::set<std::string> seen;
    std::vector<std::string> stack;
    for (const std::string& p : get(b)->parents) stack.push_back(p);
    while (!stack.empty()) {
        std::string h = stack.back();
        stack.pop_back();
        if (h == a) return true;
        if (!seen.insert(h).second) continue;
        const Utterance* u = get(h);
        if (!u) continue;
        for (const std::string& p : u->parents) stack.push_back(p);
    }
    return false;
}

bool History::concurrent(const std::string& a, const std::string& b) const {
    if (!has(a) || !has(b)) return false;
    if (a == b) return false;
    return !is_ancestor(a, b) && !is_ancestor(b, a);
}

/* ── one reading of the order ─────────────────────────────────────────────── */

std::vector<std::string> History::linear_extension() const {
    /* Kahn's algorithm. The ready set is a std::set of hashes, so "smallest hash
     * first" falls out of the container rather than being applied afterwards —
     * and that tiebreak is the whole reason this function is deterministic. A
     * topological sort without one returns whatever the traversal happened to
     * find, which would make the rendered timeline a fact about insertion order,
     * i.e. exactly the arbitrary-choice-stored-as-fact that
     * okf/design/why-not-linear.md refuses. */
    std::map<std::string, std::size_t> remaining;
    std::set<std::string> ready;
    for (const auto& kv : by_hash_) {
        std::size_t n = kv.second.parents.size();
        remaining[kv.first] = n;
        if (n == 0) ready.insert(kv.first);
    }

    std::vector<std::string> order;
    order.reserve(by_hash_.size());
    while (!ready.empty()) {
        std::string h = *ready.begin();
        ready.erase(ready.begin());
        order.push_back(h);
        auto kids = children_.find(h);
        if (kids == children_.end()) continue;
        for (const std::string& c : kids->second) {
            auto it = remaining.find(c);
            if (it == remaining.end()) continue;
            if (it->second > 0 && --it->second == 0) ready.insert(c);
        }
    }
    /* A cycle is unreachable through `add` — a parent must already be present, so
     * a child can never be an ancestor of its own parent — but a short result is
     * returned rather than asserted, because a caller that built a History by some
     * other route deserves a wrong-looking answer instead of a crash. */
    return order;
}

/* ── growing the graph ────────────────────────────────────────────────────── */

IngestReport History::record(const std::vector<JournalEntry>& entries,
                             const IngestOptions& options) {
    IngestReport report = ingest(entries, heads(), options);
    for (const Utterance& u : report.recorded) add(u);
    return report;
}

std::size_t History::absorb(const History& other) {
    /* `other.by_hash_` is a std::map keyed by hash, so this walks in hash order
     * rather than in dependency order — which is exactly why `receive` does the
     * work: it makes no assumption about the order at all. */
    std::vector<Utterance> inbox;
    inbox.reserve(other.by_hash_.size());
    for (const auto& kv : other.by_hash_) inbox.push_back(kv.second);

    /* No unplaceable output, because there cannot be one: every History is
     * dependency-closed by `add`'s refusal, so every node in `other` has its
     * ancestry inside `other`. That is why this returns a count and `receive`
     * returns a count plus a remainder — the difference is not convenience, it is
     * which invariant the caller may rely on. */
    return receive(inbox, nullptr);
}

std::size_t History::receive(const std::vector<Utterance>& inbox,
                             std::vector<Utterance>* unplaceable) {
    /* Delivery order is not assumed, because a transport is allowed to be
     * terrible (okf/concepts/history-graph.md). Each pass places whatever has
     * become placeable; the loop ends when a pass places nothing, which is at
     * most depth-many passes and never fewer than needed.
     *
     * A malformed utterance is left in `unplaceable` alongside a merely early
     * one. They are different problems — one is corruption, one is arrival order
     * — but they are the caller's to tell apart with `add`, and conflating them
     * here would mean this function silently discarding the corrupt one. */
    std::vector<Utterance> pending = inbox;
    std::size_t added = 0;
    bool progress = true;
    while (progress && !pending.empty()) {
        progress = false;
        std::vector<Utterance> still;
        for (const Utterance& u : pending) {
            Add r = add(u);
            if (r == Add::ok) { ++added; progress = true; }
            else if (r == Add::duplicate) { progress = true; }
            else still.push_back(u);
        }
        pending.swap(still);
    }
    if (unplaceable) *unplaceable = pending;
    return added;
}

/* ── persistence ──────────────────────────────────────────────────────────── */

cJSON* History::to_json() const {
    cJSON* o = cJSON_CreateObject();
    cJSON_AddNumberToObject(o, "palabra_history", 1);
    cJSON* arr = cJSON_AddArrayToObject(o, "utterances");
    /* Written in linear-extension order so a reader that ignores `parents`
     * entirely still sees every parent before its child — which makes the file
     * loadable in one pass. The order carries no meaning beyond that, and
     * `from_json` does not depend on it. */
    for (const std::string& h : linear_extension()) {
        const Utterance* u = get(h);
        if (u) cJSON_AddItemToArray(arr, utterance_to_json(*u));
    }
    return o;
}

bool History::from_json(const cJSON* obj, History& out, std::string* error) {
    out = History();
    if (!obj || !cJSON_IsObject(const_cast<cJSON*>(obj))) {
        if (error) *error = "history is not an object";
        return false;
    }
    const cJSON* arr =
        cJSON_GetObjectItemCaseSensitive(const_cast<cJSON*>(obj), "utterances");
    if (!arr || !cJSON_IsArray(const_cast<cJSON*>(arr))) {
        if (error) *error = "history has no 'utterances' array";
        return false;
    }

    History loaded;
    std::vector<Utterance> parsed;
    for (const cJSON* e = arr->child; e; e = e->next) {
        Utterance u;
        if (!utterance_from_json(e, u, error)) return false;
        parsed.push_back(u);
    }

    /* Same fixpoint as `absorb`, for the same reason: the file need not be in
     * dependency order for the load to be correct, only for it to be fast. */
    bool progress = true;
    while (progress && !parsed.empty()) {
        progress = false;
        std::vector<Utterance> still;
        for (const Utterance& u : parsed) {
            Add r = loaded.add(u);
            if (r == Add::ok || r == Add::duplicate) progress = true;
            else still.push_back(u);
        }
        parsed.swap(still);
    }
    if (!parsed.empty()) {
        if (error)
            *error = "history is incomplete: " + std::to_string(parsed.size()) +
                     " utterance(s) name a parent that is not in the file";
        return false;
    }

    out = loaded;
    return true;
}

}  // namespace voidpalabra
