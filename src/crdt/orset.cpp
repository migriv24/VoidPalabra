/* orset.cpp — the one primitive, and its JSON representation.
 *
 * `join` is union on both members, so the three laws hold BY CONSTRUCTION: set
 * union is commutative, associative and idempotent, and nothing else in the
 * structure can break them. That is the entire correctness argument for this
 * layer, and it is why there is exactly one primitive rather than a family.
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

const cJSON* get(const cJSON* o, const char* k) {
    return o ? cJSON_GetObjectItemCaseSensitive(const_cast<cJSON*>(o), k) : nullptr;
}

std::string as_text(const cJSON* v) {
    /* A field value is stored as its canonical encoding, so comparison and
     * ordering are the same operations the version name already uses. One
     * encoder, one notion of equality. */
    return encode(v);
}

cJSON* orset_to_json(const OrSet& s) {
    cJSON* o = cJSON_CreateObject();
    cJSON* a = cJSON_CreateObject();
    for (const auto& kv : s.adds) {
        /* Values are canonical bytes, which are not UTF-8, so they ride as hex.
         * Bulky and obviously correct; a real container format will store them
         * raw (persistence's job, not the join's). */
        std::string hex;
        static const char* d = "0123456789abcdef";
        for (unsigned char c : kv.second) { hex += d[c >> 4]; hex += d[c & 0xF]; }
        cJSON_AddStringToObject(a, kv.first.c_str(), hex.c_str());
    }
    cJSON_AddItemToObject(o, "a", a);
    cJSON* r = cJSON_CreateArray();
    for (const auto& t : s.removes) cJSON_AddItemToArray(r, cJSON_CreateString(t.c_str()));
    cJSON_AddItemToObject(o, "r", r);
    return o;
}

OrSet orset_from_json(const cJSON* o) {
    OrSet s;
    const cJSON* a = get(o, "a");
    if (a) {
        for (const cJSON* it = a->child; it; it = it->next) {
            std::string hex = it->valuestring ? it->valuestring : "";
            std::string raw;
            for (std::size_t i = 0; i + 1 < hex.size(); i += 2) {
                auto nib = [](char c) -> int {
                    return c >= 'a' ? c - 'a' + 10 : c - '0';
                };
                raw.push_back(static_cast<char>(nib(hex[i]) * 16 + nib(hex[i + 1])));
            }
            s.adds[it->string] = raw;
        }
    }
    const cJSON* r = get(o, "r");
    if (r)
        for (const cJSON* it = r->child; it; it = it->next)
            if (it->valuestring) s.removes.insert(it->valuestring);
    return s;
}


std::string hexify(const std::string& raw) {
    static const char* d = "0123456789abcdef";
    std::string hex;
    for (unsigned char c : raw) { hex += d[c >> 4]; hex += d[c & 0xF]; }
    return hex;
}

std::string unhexify(const std::string& hex) {
    auto nib = [](char c) -> int { return c >= 'a' ? c - 'a' + 10 : c - '0'; };
    std::string raw;
    for (std::size_t i = 0; i + 1 < hex.size(); i += 2)
        raw.push_back(static_cast<char>(nib(hex[i]) * 16 + nib(hex[i + 1])));
    return raw;
}

const char* const kFacets[6] = {"who", "what", "when", "where", "why", "how"};

}  // namespace crdt


void OrSet::add(const Tag& tag, const std::string& value) { adds[tag] = value; }

void OrSet::remove_value(const std::string& value) {
    for (const auto& kv : adds)
        if (kv.second == value) removes.insert(kv.first);
}

void OrSet::remove_all() {
    for (const auto& kv : adds) removes.insert(kv.first);
}

std::vector<std::string> OrSet::values() const {
    std::vector<std::string> out;
    for (const auto& kv : adds)
        if (!removes.count(kv.first)) out.push_back(kv.second);
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

bool OrSet::empty() const { return values().empty(); }

OrSet join(const OrSet& a, const OrSet& b) {
    OrSet out = a;
    for (const auto& kv : b.adds) out.adds[kv.first] = kv.second;
    out.removes.insert(b.removes.begin(), b.removes.end());
    return out;
}

void write(Register& r, const Tag& tag, const std::string& value) {
    r.remove_all();  // observed-remove: retire what this peer can see
    r.add(tag, value);
}

Tag CounterMint::next() {
    char buf[32];
    std::snprintf(buf, sizeof buf, "_%04lu", ++n);
    return prefix + buf;
}


}  // namespace voidpalabra
