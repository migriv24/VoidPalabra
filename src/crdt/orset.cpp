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

namespace {

/* Lowercase hex only, even length — the only spelling `orset_to_json` writes. Any
 * other spelling is either damage or an attempt to make two peers read one value
 * differently ("AB" and "ab" used to decode to different bytes), so it is refused
 * rather than interpreted. */
bool unhex_strict(const char* hex, std::string& out) {
    if (!hex) return false;
    std::size_t n = std::strlen(hex);
    if (n % 2) return false;
    out.clear();
    out.reserve(n / 2);
    auto nib = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        return -1;
    };
    for (std::size_t i = 0; i < n; i += 2) {
        int hi = nib(hex[i]), lo = nib(hex[i + 1]);
        if (hi < 0 || lo < 0) return false;
        out.push_back(static_cast<char>(hi * 16 + lo));
    }
    return true;
}

}  // namespace

bool orset_json_well_formed(const cJSON* o) {
    if (!o || !cJSON_IsObject(o)) return false;
    const cJSON* a = get(o, "a");
    const cJSON* r = get(o, "r");
    if (!a || !cJSON_IsObject(a) || !r || !cJSON_IsArray(r)) return false;
    int members = 0;
    for (const cJSON* it = o->child; it; it = it->next) ++members;
    if (members != 2) return false;
    std::string scratch;
    for (const cJSON* it = a->child; it; it = it->next) {
        if (!it->string || !it->string[0]) return false;
        if (!cJSON_IsString(it) || !unhex_strict(it->valuestring, scratch)) return false;
    }
    for (const cJSON* it = r->child; it; it = it->next)
        if (!cJSON_IsString(it) || !it->valuestring || !it->valuestring[0]) return false;
    return true;
}

OrSet orset_from_json(const cJSON* o) {
    /* Never undefined behaviour, whatever arrives. An `a` that is an array used to
     * construct a std::string from a null key and crash the reader; a malformed
     * entry is now skipped. Skipping is safe for convergence because every peer
     * that reads the node skips the same entries — but it is still silent, which
     * is why a document from a peer goes through `validate` first and is refused
     * whole rather than read in part. */
    OrSet s;
    const cJSON* a = get(o, "a");
    if (a && cJSON_IsObject(a)) {
        std::string raw;
        for (const cJSON* it = a->child; it; it = it->next) {
            if (!it->string || !it->string[0] || !cJSON_IsString(it)) continue;
            if (!unhex_strict(it->valuestring, raw)) continue;
            s.adds[it->string] = raw;
        }
    }
    const cJSON* r = get(o, "r");
    if (r && cJSON_IsArray(r))
        for (const cJSON* it = r->child; it; it = it->next)
            if (cJSON_IsString(it) && it->valuestring && it->valuestring[0])
                s.removes.insert(it->valuestring);
    return s;
}


std::string hexify(const std::string& raw) {
    static const char* d = "0123456789abcdef";
    std::string hex;
    for (unsigned char c : raw) { hex += d[c >> 4]; hex += d[c & 0xF]; }
    return hex;
}

std::string unhexify(const std::string& hex) {
    std::string raw;
    return unhex_strict(hex.c_str(), raw) ? raw : std::string();
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
