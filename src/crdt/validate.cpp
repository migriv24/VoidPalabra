/* validate.cpp — the door a peer's document comes through. SPEC.md §5.6.
 *
 * Every function in the rest of this layer assumes a well-shaped document, and
 * until 2026-09-16 nothing checked that assumption on input from another device.
 * What that cost, demonstrated rather than argued:
 *
 *   - a node of the wrong kind at one path made `join` non-commutative, so two
 *     peers stopped converging for good and nothing reported it;
 *   - an OrSet whose `a` was an array crashed `flatten`;
 *   - a register value that was not canonical could be read as a second value,
 *     manufacturing a conflict out of one value spelled twice.
 *
 * `join` now converges on anything (see document.cpp), which bounds the damage a
 * bad document can do. This file is what keeps a bad document out in the first
 * place, and it answers all-or-nothing.
 *
 * Deltas pass too: a delta is a document with most of its parts missing, so every
 * member below is optional. What is checked is that whatever IS present is in a
 * place the shape allows and is the kind of node that place holds.
 */
#include "internal.hpp"

#include "voidpalabra/canonical.hpp"
#include "cJSON.h"

#include <cstring>
#include <set>
#include <string>

namespace voidpalabra {

using namespace crdt;

namespace {

struct Check {
    std::string* why;
    bool fail(const std::string& path, const std::string& reason) {
        if (why) *why = path + ": " + reason;
        return false;
    }
};

enum class Values { Any, Canonical };

/* JSON allows an object to name one member twice, and parsers disagree about which
 * one wins: cJSON's lookup returns the FIRST, many parsers return the last, and this
 * library's own map walk would visit both. A document with a duplicate is therefore
 * read differently depending on who reads it — the one thing a document two peers
 * must agree on cannot be. Refused wherever it appears. */
bool no_duplicates(const cJSON* n, const std::string& path, Check& c) {
    std::set<std::string> seen;
    for (const cJSON* it = n ? n->child : nullptr; it; it = it->next)
        if (it->string && !seen.insert(it->string).second)
            return c.fail(path + "." + it->string, "member named twice");
    return true;
}

bool check_orset(const cJSON* n, const std::string& path, Values rule, Check& c) {
    if (cJSON_IsObject(n) && !no_duplicates(n, path, c)) return false;
    if (cJSON_IsObject(get(n, "a")) && !no_duplicates(get(n, "a"), path + ".a", c)) return false;
    if (!orset_json_well_formed(n))
        return c.fail(path, "not an OrSet ({\"a\": {tag: lowercase hex}, \"r\": [tag]})");
    if (rule == Values::Canonical) {
        OrSet s = orset_from_json(n);
        for (const auto& kv : s.adds)
            if (!is_canonical(kv.second))
                return c.fail(path + ".a." + kv.first, "value is not in canonical form");
    }
    return true;
}

bool check_name(const cJSON* n, const std::string& path, Check& c) {
    if (!n->string || !n->string[0]) return c.fail(path, "empty name");
    return true;
}

const char* const kRuneFixed[] = {"spirit.name", "glyph", "placement", "relations"};

bool rune_field_allowed(const char* k) {
    for (const char* f : kRuneFixed)
        if (std::strcmp(k, f) == 0) return true;
    if (std::strncmp(k, "facets.", 7) == 0) {
        for (const char* f : kFacets)
            if (std::strcmp(k + 7, f) == 0) return true;
        return false;
    }
    /* `content.` alone would be a key named "" — not something Core can hold. */
    return std::strncmp(k, "content.", 8) == 0 && k[8] != '\0';
}

bool mantle_field_allowed(const char* k) {
    if (std::strcmp(k, "id") == 0 || std::strcmp(k, "domain") == 0 ||
        std::strcmp(k, "rules") == 0)
        return true;
    return std::strncmp(k, "tags.", 5) == 0 && k[5] != '\0';
}

/* A node that must be an object whose members are limited to `allowed`. */
bool check_members(const cJSON* n, const std::string& path,
                   bool (*allowed)(const char*), Check& c) {
    if (!cJSON_IsObject(n)) return c.fail(path, "expected an object");
    if (!no_duplicates(n, path, c)) return false;
    for (const cJSON* it = n->child; it; it = it->next) {
        if (!it->string || !allowed(it->string))
            return c.fail(path + "." + (it->string ? it->string : "?"), "member not allowed here");
    }
    return true;
}

bool fields_of_rune(const cJSON* f, const std::string& path, Check& c) {
    if (!check_members(f, path, rune_field_allowed, c)) return false;
    for (const cJSON* it = f->child; it; it = it->next)
        if (!check_orset(it, path + "." + it->string, Values::Canonical, c)) return false;
    return true;
}

bool fields_of_mantle(const cJSON* f, const std::string& path, Check& c) {
    if (!check_members(f, path, mantle_field_allowed, c)) return false;
    for (const cJSON* it = f->child; it; it = it->next)
        if (!check_orset(it, path + "." + it->string, Values::Canonical, c)) return false;
    return true;
}

bool is_rune_member(const char* k) {
    return !std::strcmp(k, "present") || !std::strcmp(k, "fields") || !std::strcmp(k, "tags");
}
bool is_mantle_member(const char* k) {
    return !std::strcmp(k, "present") || !std::strcmp(k, "fields") ||
           !std::strcmp(k, "runes") || !std::strcmp(k, "edges");
}
bool is_glyph_member(const char* k) {
    return !std::strcmp(k, "present") || !std::strcmp(k, "fields");
}
bool is_glyph_field(const char* k) { return !std::strcmp(k, "descriptor"); }
bool is_root_member(const char* k) {
    return !std::strcmp(k, "palabra") || !std::strcmp(k, "mantles") || !std::strcmp(k, "glyphs");
}

bool check_rune(const cJSON* r, const std::string& path, Check& c) {
    if (!check_name(r, path, c)) return false;
    if (!check_members(r, path, is_rune_member, c)) return false;
    if (const cJSON* p = get(r, "present"))
        if (!check_orset(p, path + ".present", Values::Any, c)) return false;
    if (const cJSON* f = get(r, "fields"))
        if (!fields_of_rune(f, path + ".fields", c)) return false;
    if (const cJSON* t = get(r, "tags"))
        if (!check_orset(t, path + ".tags", Values::Canonical, c)) return false;
    return true;
}

bool check_mantle(const cJSON* m, const std::string& path, Check& c) {
    if (!check_name(m, path, c)) return false;
    if (!check_members(m, path, is_mantle_member, c)) return false;
    if (const cJSON* p = get(m, "present"))
        if (!check_orset(p, path + ".present", Values::Any, c)) return false;
    if (const cJSON* f = get(m, "fields"))
        if (!fields_of_mantle(f, path + ".fields", c)) return false;
    if (const cJSON* e = get(m, "edges"))
        if (!check_orset(e, path + ".edges", Values::Canonical, c)) return false;
    if (const cJSON* rs = get(m, "runes")) {
        if (!cJSON_IsObject(rs)) return c.fail(path + ".runes", "expected an object");
        if (!no_duplicates(rs, path + ".runes", c)) return false;
        for (const cJSON* r = rs->child; r; r = r->next)
            if (!check_rune(r, path + ".runes." + (r->string ? r->string : "?"), c))
                return false;
    }
    return true;
}

bool check_glyph(const cJSON* g, const std::string& path, Check& c) {
    if (!check_name(g, path, c)) return false;
    if (!check_members(g, path, is_glyph_member, c)) return false;
    if (const cJSON* p = get(g, "present"))
        if (!check_orset(p, path + ".present", Values::Any, c)) return false;
    if (const cJSON* f = get(g, "fields")) {
        if (!check_members(f, path + ".fields", is_glyph_field, c)) return false;
        if (const cJSON* d = get(f, "descriptor")) {
            if (!check_orset(d, path + ".fields.descriptor", Values::Canonical, c)) return false;
            /* A declaration is a map (SPEC §4.5). A canonical value that is not one
             * would flatten into `glyphs` as something Core cannot read. */
            OrSet s = orset_from_json(d);
            for (const auto& kv : s.adds) {
                if (kv.second.empty() || kv.second[0] != '\x08')
                    return c.fail(path + ".fields.descriptor.a." + kv.first,
                                  "a declaration must be a map");
            }
        }
    }
    return true;
}

}  // namespace

bool validate(const cJSON* root, std::string* why) {
    Check c{why};
    if (!root || !cJSON_IsObject(root)) return c.fail("$", "a document is an object");
    if (!check_members(root, "$", is_root_member, c)) return false;

    const cJSON* v = get(root, "palabra");
    if (!v || !cJSON_IsNumber(v) || v->valuedouble != 1.0)
        return c.fail("$.palabra", "this implementation reads document shape 1");

    if (const cJSON* ms = get(root, "mantles")) {
        if (!cJSON_IsObject(ms)) return c.fail("$.mantles", "expected an object");
        if (!no_duplicates(ms, "$.mantles", c)) return false;
        for (const cJSON* m = ms->child; m; m = m->next)
            if (!check_mantle(m, std::string("$.mantles.") + (m->string ? m->string : "?"), c))
                return false;
    }
    if (const cJSON* gs = get(root, "glyphs")) {
        if (!cJSON_IsObject(gs)) return c.fail("$.glyphs", "expected an object");
        if (!no_duplicates(gs, "$.glyphs", c)) return false;
        for (const cJSON* g = gs->child; g; g = g->next)
            if (!check_glyph(g, std::string("$.glyphs.") + (g->string ? g->string : "?"), c))
                return false;
    }
    return true;
}

bool validate(const Doc& doc, std::string* why) { return validate(doc.root, why); }

}  // namespace voidpalabra
