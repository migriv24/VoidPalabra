/* provenance.cpp — see include/voidpalabra/crdt/provenance.hpp.
 *
 * A read of tags the document already holds. The writer of a tag is everything
 * before its last `_`, which is unambiguous because a replica id may not contain
 * one (replica.cpp, `valid_id`).
 */
#include "internal.hpp"

#include "voidpalabra/crdt/provenance.hpp"
#include "cJSON.h"

#include <algorithm>
#include <map>
#include <set>

namespace voidpalabra {

using namespace crdt;

namespace {

const cJSON* locate(const cJSON* root, const Place& p) {
    const cJSON* holder = nullptr;
    if (!p.glyph.empty()) {
        holder = get(get(root, "glyphs"), p.glyph.c_str());
    } else {
        const cJSON* m = get(get(root, "mantles"), p.mantle.c_str());
        if (p.field == "edges") return p.rune.empty() ? get(m, "edges") : nullptr;
        holder = p.rune.empty() ? m : get(get(m, "runes"), p.rune.c_str());
    }
    if (!holder) return nullptr;
    if (p.field == "present") return get(holder, "present");
    if (p.field == "tags" && !p.rune.empty()) return get(holder, "tags");
    return get(get(holder, "fields"), p.field.c_str());
}

}  // namespace

std::vector<Written> writers(const Doc& doc, const Place& place) {
    std::vector<Written> out;
    const cJSON* node = locate(doc.root, place);
    if (!node || !is_orset(node)) return out;

    OrSet s = orset_from_json(node);
    std::map<std::string, std::pair<std::set<std::string>, std::uint64_t>> by_value;
    for (const auto& kv : s.adds) {
        if (s.removes.count(kv.first)) continue;
        Stamp st = stamp_of(kv.first);
        auto& slot = by_value[kv.second];
        slot.first.insert(st.writer);
        slot.second = std::max(slot.second, st.n);
    }
    for (auto& [value, w] : by_value) {
        Written x;
        x.value = value;
        x.writers.assign(w.first.begin(), w.first.end());
        x.stamp = w.second;
        out.push_back(std::move(x));
    }
    return out;
}

}  // namespace voidpalabra
