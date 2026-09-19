/* export.cpp — the shareable projection of a replica. See sync.hpp, ExportSet.
 *
 * Two passes. The first learns every NAME a withheld rune holds or held, in every
 * mantle, because edges address runes by name — a link is how a private rune's name
 * would leak if only its node were withheld. The second copies what may leave.
 */
#include "crdt/internal.hpp"

#include "voidpalabra/sync.hpp"
#include "voidpalabra/canonical.hpp"
#include "cJSON.h"

#include <map>
#include <set>
#include <string>

namespace voidpalabra {
namespace sync {

using namespace crdt;

namespace {

bool was_removed(const cJSON* n) {
    OrSet p = orset_from_json(get(n, "present"));
    return p.empty() && !p.removes.empty();
}

std::string shown_name(const cJSON* rune, const JoinPolicy& policy) {
    Live l = resolve(get(get(rune, "fields"), "spirit.name"), policy.lookup("spirit.name"));
    if (l.values.empty()) return "";
    cJSON* v = decode(l.values[0]);
    std::string s = (v && cJSON_IsString(v) && v->valuestring) ? v->valuestring : "";
    cJSON_Delete(v);
    return s;
}

/* Only the removal travels: the `present` set, and nothing else about the thing. */
cJSON* removal_only(const cJSON* node) {
    cJSON* o = cJSON_CreateObject();
    cJSON_AddItemToObject(o, "present", cJSON_Duplicate(get(node, "present"), 1));
    return o;
}

bool names_withheld(const cJSON* endpoint, const std::string& own,
                    const std::set<std::string>& withheld_mantles,
                    const std::map<std::string, std::set<std::string>>& withheld_names) {
    std::string mantle = own, rune;
    if (cJSON_IsString(endpoint) && endpoint->valuestring) {
        rune = endpoint->valuestring;
    } else if (cJSON_IsObject(endpoint)) {
        const cJSON* m = get(endpoint, "mantle");
        const cJSON* r = get(endpoint, "rune");
        if (m && cJSON_IsString(m) && m->valuestring) mantle = m->valuestring;
        if (r && cJSON_IsString(r) && r->valuestring) rune = r->valuestring;
    } else {
        return false;
    }
    if (withheld_mantles.count(mantle)) return true;
    auto it = withheld_names.find(mantle);
    return it != withheld_names.end() && it->second.count(rune);
}

}  // namespace

ExportSet share_everything() {
    return [](const std::string&, const std::string&) { return true; };
}

Doc exportable(const Doc& doc, const ExportSet& share, const JoinPolicy& policy) {
    Doc out;
    out.root = cJSON_CreateObject();
    cJSON_AddNumberToObject(out.root, "palabra", 1);
    cJSON* mantles_out = cJSON_CreateObject();
    cJSON_AddItemToObject(out.root, "mantles", mantles_out);
    if (const cJSON* g = get(doc.root, "glyphs"))
        cJSON_AddItemToObject(out.root, "glyphs", cJSON_Duplicate(g, 1));

    const cJSON* mantles = get(doc.root, "mantles");
    auto allowed = [&](const std::string& m, const std::string& r) { return !share || share(m, r); };

    /* Pass 1: what is withheld, and every name it holds or held. */
    std::set<std::string> withheld_mantles;
    std::map<std::string, std::set<std::string>> withheld_names;
    for (const cJSON* m = mantles ? mantles->child : nullptr; m; m = m->next) {
        if (!m->string) continue;
        if (!allowed(m->string, "")) { withheld_mantles.insert(m->string); continue; }
        /* Every name a withheld rune holds, live or removed — including a name a
         * shared rune ALSO holds. An edge naming it cannot say which of the two it
         * means, so it could reveal that the private one exists. Ambiguity resolves
         * toward not leaking. */
        std::set<std::string>& names = withheld_names[m->string];
        const cJSON* runes = get(m, "runes");
        for (const cJSON* r = runes ? runes->child : nullptr; r; r = r->next)
            if (r->string && !allowed(m->string, r->string)) names.insert(shown_name(r, policy));
    }

    /* Pass 2: copy what may leave. */
    for (const cJSON* m = mantles ? mantles->child : nullptr; m; m = m->next) {
        if (!m->string) continue;
        if (withheld_mantles.count(m->string)) {
            if (was_removed(m)) cJSON_AddItemToObject(mantles_out, m->string, removal_only(m));
            continue;
        }
        cJSON* mo = cJSON_CreateObject();
        for (const char* k : {"present", "fields"})
            if (const cJSON* v = get(m, k)) cJSON_AddItemToObject(mo, k, cJSON_Duplicate(v, 1));

        cJSON* runes_out = cJSON_CreateObject();
        const cJSON* runes = get(m, "runes");
        for (const cJSON* r = runes ? runes->child : nullptr; r; r = r->next) {
            if (!r->string) continue;
            if (allowed(m->string, r->string))
                cJSON_AddItemToObject(runes_out, r->string, cJSON_Duplicate(r, 1));
            else if (was_removed(r))
                cJSON_AddItemToObject(runes_out, r->string, removal_only(r));
        }
        cJSON_AddItemToObject(mo, "runes", runes_out);

        if (const cJSON* edges = get(m, "edges")) {
            OrSet s = orset_from_json(edges), kept;
            kept.removes = s.removes;
            for (const auto& kv : s.adds) {
                cJSON* e = decode(kv.second);
                bool leaks = e && (names_withheld(get(e, "from"), m->string, withheld_mantles, withheld_names) ||
                                   names_withheld(get(e, "to"), m->string, withheld_mantles, withheld_names));
                cJSON_Delete(e);
                if (!leaks) kept.adds.insert(kv);
            }
            cJSON_AddItemToObject(mo, "edges", orset_to_json(kept));
        }
        cJSON_AddItemToObject(mantles_out, m->string, mo);
    }
    return out;
}

}  // namespace sync
}  // namespace voidpalabra
