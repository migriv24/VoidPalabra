/* policy.cpp — declared per-field joins, applied at READ time.
 *
 * The load-bearing decision, restated because it is easy to undo by accident:
 * a policy is applied by `flatten` and `conflicts`, never by `join`. Two peers
 * running different policies therefore merge to byte-identical documents. Had a
 * policy been able to change the merge, convergence would silently depend on
 * configuration — which would undo the central claim of okf/concepts/join.md.
 */
#include "internal.hpp"

#include "voidpalabra/canonical.hpp"
#include "cJSON.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <set>

namespace voidpalabra {
namespace crdt {

/* Apply a field's declared join. Read-time only: this never touches the stored
 * document, so two peers running different policies still hold identical bytes. */
Live live_of(const cJSON* reg) {
    Live l;
    l.values = orset_from_json(reg).values();
    /* A register is conflicted exactly when two concurrent writes both survived —
     * that is the definition, not a heuristic. */
    l.conflicted = l.values.size() > 1;
    return l;
}

Live resolve(const cJSON* reg, FieldJoin how) {
    Live l = live_of(reg);
    if (!l.conflicted) return l;

    if (how == FieldJoin::Pick) {
        /* `values()` is sorted by canonical bytes and deduplicated, so taking the
         * first is deterministic across peers without consulting a clock. It is
         * arbitrary with respect to meaning, and the enum name says so. */
        l.values.resize(1);
        l.conflicted = false;
        return l;
    }

    if (how == FieldJoin::Max) {
        const cJSON* best = nullptr;
        std::string best_bytes;
        bool all_numeric = true;
        std::vector<cJSON*> decoded;
        for (const std::string& v : l.values) {
            cJSON* d = decode(v);
            decoded.push_back(d);
            if (!d || !cJSON_IsNumber(d)) { all_numeric = false; continue; }
            if (!best || d->valuedouble > best->valuedouble) { best = d; best_bytes = v; }
        }
        /* A non-numeric value under a Max policy is a declaration that did not
         * match the data. Falling back to Conflict is the honest answer: silently
         * picking would hide a misconfiguration behind plausible output. */
        if (all_numeric && best) {
            l.values.assign(1, best_bytes);
            l.conflicted = false;
        }
        for (cJSON* d : decoded) if (d) cJSON_Delete(d);
        return l;
    }

    return l;  // Conflict: both values stand
}

/* Canonical bytes -> a cJSON value. A corrupted document yields null rather than
 * a guess. */
cJSON* decode_value(const std::string& b) {
    cJSON* v = decode(b);
    return v ? v : cJSON_CreateNull();
}



cJSON* first_or_null(const Live& l) {
    return l.values.empty() ? cJSON_CreateNull() : decode(l.values[0]);
}

}  // namespace crdt

FieldJoin JoinPolicy::lookup(const std::string& field) const {
    auto exact = fields.find(field);
    if (exact != fields.end()) return exact->second;
    /* Prefix rules: "content.*" covers every content key. The LONGEST match wins,
     * so a specific "content.order" can override a general "content.*". */
    const std::string* best = nullptr;
    FieldJoin chosen = fallback;
    for (const auto& kv : fields) {
        if (kv.first.size() < 2 || kv.first.back() != '*') continue;
        std::string prefix = kv.first.substr(0, kv.first.size() - 1);
        if (field.compare(0, prefix.size(), prefix) != 0) continue;
        if (!best || prefix.size() > best->size()) { best = &kv.first; chosen = kv.second; }
    }
    return chosen;
}

JoinPolicy JoinPolicy::core_defaults() {
    JoinPolicy p;
    /* `placement` is the view slice — Void Core already carves it out of undo
     * (SPEC §3.2) precisely because it is arrangement rather than content. Two
     * peers moving one rune should converge, not argue. */
    p.fields["placement"] = FieldJoin::Pick;
    return p;
}

}  // namespace voidpalabra
