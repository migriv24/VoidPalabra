/* anomaly.cpp — see include/voidpalabra/crdt/anomaly.hpp for what and why.
 *
 * Built in two passes: index every mantle (who holds which name now, which names
 * were held by something removed, which names a live rune was renamed away from),
 * then check the three rules against the index. The index is what lets a broken
 * link say WHY it is broken — which is the difference between an anomaly and the
 * dangling link Core allows on purpose.
 */
#include "internal.hpp"

#include "encoding/internal.hpp"

#include "voidpalabra/crdt/anomaly.hpp"
#include "voidpalabra/canonical.hpp"
#include "cJSON.h"

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <vector>

namespace voidpalabra {

using namespace crdt;

namespace {

bool was_present_and_is_not(const cJSON* node) {
    OrSet p = orset_from_json(get(node, "present"));
    return p.empty() && !p.removes.empty();
}

bool present(const cJSON* node) { return !orset_from_json(get(node, "present")).empty(); }

/* The string `flatten` would show for a register, or "" if it is not a string. */
std::string shown(const cJSON* reg, const std::string& field, const JoinPolicy& policy) {
    Live l = resolve(reg, policy.lookup(field));
    if (l.values.empty()) return "";
    cJSON* v = decode(l.values[0]);
    std::string out = (v && cJSON_IsString(v) && v->valuestring) ? v->valuestring : "";
    cJSON_Delete(v);
    return out;
}

/* Names a register once held and no longer does: values whose every tag is retired. */
std::set<std::string> former_values(const cJSON* reg) {
    OrSet s = orset_from_json(reg);
    std::set<std::string> live;
    for (const auto& v : s.values()) live.insert(v);
    std::set<std::string> out;
    for (const auto& kv : s.adds) {
        if (!s.removes.count(kv.first) || live.count(kv.second)) continue;
        cJSON* v = decode(kv.second);
        if (v && cJSON_IsString(v) && v->valuestring) out.insert(v->valuestring);
        cJSON_Delete(v);
    }
    return out;
}

struct MantleIndex {
    std::map<std::string, std::vector<std::string>> live_names;   // name -> ids holding it
    std::map<std::string, std::vector<std::string>> removed;      // name -> removed ids
    std::map<std::string, std::vector<std::string>> renamed_from; // old name -> live ids
    std::map<std::string, std::string> glyph_of;                  // live id -> glyph
};

MantleIndex index_mantle(const cJSON* m, const JoinPolicy& policy) {
    MantleIndex ix;
    const cJSON* runes = get(m, "runes");
    for (const cJSON* r = runes ? runes->child : nullptr; r; r = r->next) {
        if (!r->string) continue;
        const cJSON* f = get(r, "fields");
        const cJSON* name_reg = get(f, "spirit.name");
        std::string name = shown(name_reg, "spirit.name", policy);
        if (present(r)) {
            ix.live_names[name].push_back(r->string);
            ix.glyph_of[r->string] = shown(get(f, "glyph"), "glyph", policy);
            for (const auto& old : former_values(name_reg))
                if (old != name) ix.renamed_from[old].push_back(r->string);
        } else if (was_present_and_is_not(r)) {
            ix.removed[name].push_back(r->string);
        }
    }
    return ix;
}

/* An edge endpoint is a rune name in the edge's own mantle, or an object naming a
 * mantle and a rune (VoidCore SPEC §3.7). */
bool endpoint(const cJSON* e, const std::string& own, std::string& mantle, std::string& rune) {
    if (!e) return false;
    if (cJSON_IsString(e) && e->valuestring) {
        mantle = own;
        rune = e->valuestring;
        return true;
    }
    if (cJSON_IsObject(e)) {
        const cJSON* rm = get(e, "rune");
        if (!rm || !cJSON_IsString(rm) || !rm->valuestring) return false;
        const cJSON* mm = get(e, "mantle");
        mantle = (mm && cJSON_IsString(mm) && mm->valuestring) ? mm->valuestring : own;
        rune = rm->valuestring;
        return true;
    }
    return false;
}

std::vector<std::string> sorted_unique(std::vector<std::string> v) {
    std::sort(v.begin(), v.end());
    v.erase(std::unique(v.begin(), v.end()), v.end());
    return v;
}

const char* kind_name(AnomalyKind k) {
    switch (k) {
        case AnomalyKind::duplicate_name: return "duplicate_name";
        case AnomalyKind::link_broken:    return "link_broken";
        case AnomalyKind::type_removed:   return "type_removed";
    }
    return "?";
}

}  // namespace

std::vector<Anomaly> anomalies(const Doc& doc, const JoinPolicy& policy) {
    std::vector<Anomaly> out;
    const cJSON* mantles = get(doc.root, "mantles");

    std::map<std::string, MantleIndex> index;
    for (const cJSON* m = mantles ? mantles->child : nullptr; m; m = m->next)
        if (m->string && present(m)) index[m->string] = index_mantle(m, policy);

    std::set<std::string> removed_glyphs;
    const cJSON* glyphs = get(doc.root, "glyphs");
    for (const cJSON* g = glyphs ? glyphs->child : nullptr; g; g = g->next)
        if (g->string && was_present_and_is_not(g)) removed_glyphs.insert(g->string);

    for (const auto& [mname, ix] : index) {
        /* duplicate_name */
        for (const auto& [name, ids] : ix.live_names) {
            if (ids.size() < 2 || name.empty()) continue;
            Anomaly a;
            a.kind = AnomalyKind::duplicate_name;
            a.mantle = mname;
            a.subject = name;
            a.runes = sorted_unique(ids);
            out.push_back(a);
        }

        /* type_removed */
        std::map<std::string, std::vector<std::string>> orphaned;
        for (const auto& [id, glyph] : ix.glyph_of)
            if (removed_glyphs.count(glyph)) orphaned[glyph].push_back(id);
        for (const auto& [glyph, ids] : orphaned) {
            Anomaly a;
            a.kind = AnomalyKind::type_removed;
            a.mantle = mname;
            a.subject = glyph;
            a.runes = sorted_unique(ids);
            out.push_back(a);
        }
    }

    /* link_broken — a separate pass, because an edge may point into another mantle. */
    for (const cJSON* m = mantles ? mantles->child : nullptr; m; m = m->next) {
        if (!m->string || !present(m)) continue;
        std::map<std::tuple<std::string, std::string>, Anomaly> found;
        for (const auto& bytes : orset_from_json(get(m, "edges")).values()) {
            cJSON* edge = decode(bytes);
            for (const char* end : {"from", "to"}) {
                std::string tm, tr;
                if (!endpoint(get(edge, end), m->string, tm, tr)) continue;
                auto target = index.find(tm);
                if (target == index.end()) continue;  // a mantle that is not here: Core's to report
                const MantleIndex& ix = target->second;
                if (ix.live_names.count(tr)) continue;  // resolves
                std::string cause;
                std::vector<std::string> who;
                if (ix.removed.count(tr)) { cause = "removed"; who = ix.removed.at(tr); }
                else if (ix.renamed_from.count(tr)) { cause = "renamed"; who = ix.renamed_from.at(tr); }
                else continue;  // dangling on purpose, which Core allows
                std::string subject = tm == m->string ? tr : tm + "/" + tr;
                Anomaly& a = found[{subject, cause}];
                a.kind = AnomalyKind::link_broken;
                a.mantle = m->string;
                a.subject = subject;
                a.cause = cause;
                a.runes.insert(a.runes.end(), who.begin(), who.end());
            }
            cJSON_Delete(edge);
        }
        for (auto& kv : found) {
            kv.second.runes = sorted_unique(kv.second.runes);
            out.push_back(kv.second);
        }
    }

    std::sort(out.begin(), out.end(), [](const Anomaly& a, const Anomaly& b) {
        return std::tie(a.kind, a.mantle, a.subject, a.cause) <
               std::tie(b.kind, b.mantle, b.subject, b.cause);
    });
    return out;
}

cJSON* anomaly_to_json(const Anomaly& a) {
    cJSON* o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "kind", kind_name(a.kind));
    cJSON_AddStringToObject(o, "mantle", a.mantle.c_str());
    cJSON_AddStringToObject(o, "subject", a.subject.c_str());
    if (!a.cause.empty()) cJSON_AddStringToObject(o, "cause", a.cause.c_str());
    cJSON* runes = cJSON_AddArrayToObject(o, "runes");
    for (const auto& r : a.runes) cJSON_AddItemToArray(runes, cJSON_CreateString(r.c_str()));
    return o;
}

Digest Anomaly::hash() const {
    cJSON* o = anomaly_to_json(*this);
    std::string bytes = encode(o);
    cJSON_Delete(o);
    return enc::domain_digest("anomaly", bytes, "");
}

}  // namespace voidpalabra
