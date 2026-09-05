/* conflict.cpp — a conflict as a VALUE with a content address.
 *
 * okf/concepts/conflict.md requires this: "it has a hash. It can be stored,
 * synced, queried, tagged, and rendered." Rendering goes through cJSON rather
 * than string concatenation because mantle names and `spirit.id` are
 * user-editable text — an earlier version built the JSON by hand and a name
 * containing a quote produced malformed output.
 */
#include "internal.hpp"

#include "encoding/internal.hpp"

#include "voidpalabra/canonical.hpp"
#include "cJSON.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <set>

namespace voidpalabra {

using namespace crdt;

namespace {

/* Collect the conflicted registers under one `fields` map. */
void scan_fields(const cJSON* fields, const std::string& mantle,
                 const std::string& rune, const JoinPolicy& policy,
                 std::vector<Conflict>& out) {
    for (const cJSON* f = fields ? fields->child : nullptr; f; f = f->next) {
        /* A field with a declared join is not a conflict — it has an answer. */
        Live l = resolve(f, policy.lookup(f->string ? f->string : ""));
        if (!l.conflicted) continue;
        Conflict c;
        c.mantle = mantle;
        c.rune = rune;
        c.field = f->string ? f->string : "";
        c.sides = l.values;  // already sorted by canonical bytes
        out.push_back(std::move(c));
    }
}

}  // namespace

Digest Conflict::hash() const {
    /* Addressed through the same encoder as everything else, so the name of a
     * conflict is computed exactly the way the name of a version is. */
    cJSON* o = conflict_to_json(*this);
    cJSON_DeleteItemFromObjectCaseSensitive(o, "hash");  // not part of its own name
    std::string bytes = encode(o);
    cJSON_Delete(o);
    /* Through the SPEC §3 header, like every other digest in this library.
     *
     * It used to build its own — `"voidpalabra/conflict"` plus a NUL — which left
     * CANON_VERSION out, so an old and a new implementation could compute the same
     * name for a conflict over encodings that had moved underneath them. Preventing
     * exactly that is what the counter is for, and this was the only digest here
     * not covered by it.
     *
     * Fixed on 2026-09-03, in the release that bumped the version for another
     * reason, because the cheapest moment to move a hash is one where hashes are
     * already moving. */
    return enc::domain_digest("conflict", bytes, "");
}

cJSON* conflict_to_json(const Conflict& c) {
    /* Built through cJSON rather than by concatenation. Mantle names and
     * `spirit.id` are user-influenced text, and a name containing a quote or a
     * backslash must not be able to produce malformed output — which is exactly
     * what the first version of this function did. */
    cJSON* o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "kind", "conflict");
    cJSON* at = cJSON_CreateObject();
    /* A glyph conflict is located by glyph name and nothing else — emitting an
     * empty `mantle` beside it would invite a reader to look for one. */
    if (!c.glyph.empty()) {
        cJSON_AddStringToObject(at, "glyph", c.glyph.c_str());
    } else {
        cJSON_AddStringToObject(at, "mantle", c.mantle.c_str());
        if (!c.rune.empty()) cJSON_AddStringToObject(at, "rune", c.rune.c_str());
    }
    cJSON_AddStringToObject(at, "field", c.field.c_str());
    cJSON_AddItemToObject(o, "at", at);

    cJSON* sides = cJSON_CreateArray();
    for (const std::string& v : c.sides) {
        cJSON* side = cJSON_CreateObject();
        cJSON* val = decode(v);
        cJSON_AddItemToObject(side, "value", val ? val : cJSON_CreateNull());
        cJSON_AddItemToArray(sides, side);
    }
    cJSON_AddItemToObject(o, "sides", sides);
    return o;
}

std::vector<Conflict> conflicts(const Doc& doc, const JoinPolicy& policy) {
    std::vector<Conflict> out;
    const cJSON* mantles = get(doc.root, "mantles");
    for (const cJSON* m = mantles ? mantles->child : nullptr; m; m = m->next) {
        std::string mname = m->string ? m->string : "";
        /* Mantle-level registers (`id`, `domain`) conflict too. The first version
         * of this function looked only at runes and silently reported a converged
         * document while two peers disagreed about a mantle's domain. */
        scan_fields(get(m, "fields"), mname, "", policy, out);

        const cJSON* runes = get(m, "runes");
        for (const cJSON* r = runes ? runes->child : nullptr; r; r = r->next)
            scan_fields(get(r, "fields"), mname, r->string ? r->string : "", policy, out);
    }

    /* CONCURRENT REDECLARATION. Void Core asked for this to be surfaced rather
     * than merged, and the reason is the one that decided the granularity in
     * enrich(): a merged schema is a type nobody authored. Two peers disagreeing
     * about what `stat` means is a real disagreement, and the honest thing to hand
     * back is both answers with an address.
     *
     * No policy lookup: `descriptor` has no declared join and must not acquire one
     * by accident. `resolve` with an absent policy entry is what every other
     * undeclared field gets, so this goes through the same path. */
    const cJSON* glyphs = get(doc.root, "glyphs");
    for (const cJSON* g = glyphs ? glyphs->child : nullptr; g; g = g->next) {
        if (orset_from_json(get(g, "present")).empty()) continue;  // undeclared
        const cJSON* fields = get(g, "fields");
        for (const cJSON* f = fields ? fields->child : nullptr; f; f = f->next) {
            Live l = resolve(f, policy.lookup(f->string ? f->string : ""));
            if (!l.conflicted) continue;
            Conflict c;
            c.glyph = g->string ? g->string : "";
            c.field = f->string ? f->string : "";
            c.sides = l.values;
            out.push_back(std::move(c));
        }
    }

    std::sort(out.begin(), out.end(), [](const Conflict& a, const Conflict& b) {
        /* Glyph conflicts sort together and after mantle conflicts: they belong to
         * the document rather than to any mantle, and a stable place in the list
         * is what lets two peers enumerate them identically. */
        if (a.glyph.empty() != b.glyph.empty()) return b.glyph.empty();
        if (a.glyph != b.glyph) return a.glyph < b.glyph;
        if (a.mantle != b.mantle) return a.mantle < b.mantle;
        if (a.rune != b.rune) return a.rune < b.rune;
        return a.field < b.field;
    });
    return out;
}


}  // namespace voidpalabra
