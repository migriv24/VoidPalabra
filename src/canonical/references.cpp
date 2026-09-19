/* references.cpp — see include/voidpalabra/references.hpp.
 *
 * Walks the versioned slice of a Core state document the way the canonical form
 * does (bindings.cpp), and asks the policy about each field. It never interprets a
 * value itself: which fields hold addresses, and how to find them, is declared by
 * the application.
 */
#include "voidpalabra/references.hpp"

#include "voidpalabra/canonical.hpp"
#include "voidpalabra/store.hpp"
#include "internal.hpp"

#include "cJSON.h"

#include <algorithm>
#include <cstring>
#include <tuple>

namespace voidpalabra {

using namespace enc;

namespace {

bool lower_hex(char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }

void hex_runs(const char* s, std::set<std::string>& out) {
    if (!s) return;
    std::size_t n = std::strlen(s), i = 0;
    while (i < n) {
        if (!lower_hex(s[i])) { ++i; continue; }
        std::size_t j = i;
        while (j < n && lower_hex(s[j])) ++j;
        if (j - i == 64) out.insert(std::string(s + i, 64));
        i = j;
    }
}

void walk_strings(const cJSON* v, std::set<std::string>& out) {
    if (!v) return;
    if (cJSON_IsString(v)) { hex_runs(v->valuestring, out); return; }
    for (const cJSON* c = v->child; c; c = c->next) walk_strings(c, out);
}

bool is_hex_digest(const std::string& a) {
    return a.size() == 64 && std::all_of(a.begin(), a.end(), lower_hex);
}

struct Collector {
    const ReferencePolicy& policy;
    std::vector<Reference>& out;

    void field(const std::string& name, const cJSON* value, const std::string& mantle,
               const std::string& rune, const std::string& glyph) {
        if (!value) return;
        const AddressFinder* find = policy.lookup(name);
        if (!find || !*find) return;
        std::set<std::string> found;
        (*find)(value, found);
        for (const auto& a : found) out.push_back({a, mantle, rune, glyph, name});
    }
};

}  // namespace

AddressFinder sha256_hex_anywhere() {
    return [](const cJSON* value, std::set<std::string>& out) { walk_strings(value, out); };
}

const AddressFinder* ReferencePolicy::lookup(const std::string& field) const {
    auto exact = fields.find(field);
    if (exact != fields.end()) return &exact->second;
    const AddressFinder* best = nullptr;
    std::size_t best_len = 0;
    for (const auto& kv : fields) {
        if (kv.first.size() < 2 || kv.first.back() != '*') continue;
        std::string prefix = kv.first.substr(0, kv.first.size() - 1);
        if (field.compare(0, prefix.size(), prefix) != 0) continue;
        if (!best || prefix.size() > best_len) { best = &kv.second; best_len = prefix.size(); }
    }
    return best;
}

std::vector<Reference> references(const cJSON* state, const ReferencePolicy& policy) {
    std::vector<Reference> out;
    Collector c{policy, out};

    const cJSON* ms = get(state, "mantles");
    for (const cJSON* m = ms && cJSON_IsArray(ms) ? ms->child : nullptr; m; m = m->next) {
        std::string mname = str_or(get(m, "name"), "");
        c.field("mantle.tags", get(m, "tags"), mname, "", "");
        c.field("mantle.rules", get(m, "rules"), mname, "", "");
        const cJSON* rs = get(m, "runes");
        for (const cJSON* r = rs && cJSON_IsArray(rs) ? rs->child : nullptr; r; r = r->next) {
            std::string id = str_or(get(get(r, "spirit"), "id"), "");
            const cJSON* content = get(r, "content");
            for (const cJSON* k = content ? content->child : nullptr; k; k = k->next)
                if (k->string) c.field(std::string("content.") + k->string, k, mname, id, "");
            c.field("placement", get(r, "placement"), mname, id, "");
            c.field("relations", get(r, "relations"), mname, id, "");
            c.field("tags", get(r, "tags"), mname, id, "");
        }
    }
    const cJSON* gs = get(state, "glyphs");
    for (const cJSON* g = gs && cJSON_IsObject(gs) ? gs->child : nullptr; g; g = g->next)
        if (g->string) c.field("descriptor", g, "", "", g->string);

    std::sort(out.begin(), out.end(), [](const Reference& a, const Reference& b) {
        return std::tie(a.address, a.mantle, a.rune, a.glyph, a.field) <
               std::tie(b.address, b.mantle, b.rune, b.glyph, b.field);
    });
    return out;
}

std::vector<std::string> missing(const std::vector<Reference>& refs,
                                 const std::function<bool(const std::string&)>& have) {
    std::set<std::string> want;
    for (const auto& r : refs)
        if (!have || !have(r.address)) want.insert(r.address);
    return std::vector<std::string>(want.begin(), want.end());
}

std::function<bool(const std::string&)> held_by(const BlockStore& store) {
    return [&store](const std::string& address) {
        if (!is_hex_digest(address)) return false;
        Digest d{};
        for (std::size_t i = 0; i < 32; ++i)
            d[i] = static_cast<std::uint8_t>(std::stoi(address.substr(i * 2, 2), nullptr, 16));
        return store.has(d);
    };
}

bool content_matches(const std::string& address, const std::string& bytes) {
    return is_hex_digest(address) && to_hex(sha256(bytes)) == address;
}

}  // namespace voidpalabra
