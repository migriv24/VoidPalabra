/* bindings.cpp - SPEC §3-§4: naming, and the Void Core shapes.
 *
 * The only part of the encoding layer that knows what a rune or a mantle is.
 * Everything below it (src/encoding/) is a general value encoder; everything
 * above it names things with the result.
 */
#include "internal.hpp"

#include "cJSON.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

namespace voidpalabra {

using namespace enc;

std::string Policy::tag() const {
    return std::string("include_view=") + (include_view ? "1" : "0");
}

namespace {

/* SPEC §3's domain-separated hash, with this layer's policy folded in. The
 * header format itself lives in the encoding layer (enc::domain_digest) and is
 * shared with the utterance layer — one definition, because a divergent domain
 * header is a divergence that only surfaces when two peers meet. */
Digest digest_of(const char* kind, const std::string& payload, const Policy& p) {
    return domain_digest(kind, payload, p.tag());
}


/* SPEC §3.2 requires hydrating a partial rune to fill defaults, so a partial
 * rune and its fully-defaulted twin are the same state and must hash the same.
 * That is the smallest instance of "invariant under differences that do not mean
 * anything", and it is why defaults are applied during encoding rather than
 * assumed of the input. */

const char* const kFacets[] = {"who", "what", "when", "where", "why", "how"};

std::string canon_edge(const cJSON* edge) {
    std::string from = str_or(get(edge, "from"), "");
    std::string to = str_or(get(edge, "to"), "");

    const cJSON* rel = get(edge, "relation");
    std::string relation = (rel && cJSON_IsString(rel) && rel->valuestring)
                               ? rel->valuestring : "";

    const cJSON* w = get(edge, "weight");
    double weight = (w && cJSON_IsNumber(w)) ? w->valuedouble : 1.0;  // SPEC §3.7

    const cJSON* d = get(edge, "directed");
    bool directed = is_absent(d) ? true : cJSON_IsTrue(d);

    /* An UNDIRECTED edge is the same wire read from either end, so its endpoints
     * are sorted. A directed one is not, so they are left alone. This is the
     * smallest real instance of the requirement in academic-foundations.md §9:
     * the content address must be invariant under exactly the deformations that
     * do not change meaning, and under nothing else. */
    if (!directed && to < from) std::swap(from, to);

    std::vector<std::pair<std::string, std::string>> f;
    f.emplace_back(encode_str("from"), encode_str(from));
    f.emplace_back(encode_str("to"), encode_str(to));
    f.emplace_back(encode_str("relation"), encode_str(relation));
    f.emplace_back(encode_str("weight"), encode_number(weight));
    f.emplace_back(encode_str("directed"),
                   std::string(1, directed ? kTrue : kFalse));
    return encode_map(std::move(f));
}

}  // namespace


std::string canon_rune(const cJSON* rune, const Policy& policy) {
    const cJSON* spirit = get(rune, "spirit");
    const cJSON* id = get(spirit, "id");
    if (!id || !cJSON_IsString(id) || !id->valuestring || !id->valuestring[0])
        throw CanonicalError("rune has no spirit.id");  // SPEC §3.2

    std::vector<std::pair<std::string, std::string>> sp;
    sp.emplace_back(encode_str("id"), encode_str(id->valuestring));
    sp.emplace_back(encode_str("name"), encode_str(str_or(get(spirit, "name"), "")));

    const cJSON* facets = get(rune, "facets");
    std::vector<std::pair<std::string, std::string>> fc;
    for (const char* k : kFacets)
        fc.emplace_back(encode_str(k), encode_str(str_or(get(facets, k), "")));

    /* A rune's tags are a set — SPEC §5 treats them as membership, and
     * okf/concepts/join.md declares their join an OR-Set. Two peers who added
     * the same two tags in opposite orders hold the same rune. */
    std::vector<std::string> tags;
    const cJSON* tag_arr = get(rune, "tags");
    if (tag_arr && cJSON_IsArray(tag_arr))
        for (const cJSON* t = tag_arr->child; t; t = t->next)
            tags.push_back(encode_str(str_or(t, "")));

    const cJSON* content = get(rune, "content");
    const cJSON* relations = get(rune, "relations");

    std::vector<std::pair<std::string, std::string>> f;
    f.emplace_back(encode_str("spirit"), encode_map(std::move(sp)));
    f.emplace_back(encode_str("glyph"), encode_str(str_or(get(rune, "glyph"), "")));
    f.emplace_back(encode_str("facets"), encode_map(std::move(fc)));
    f.emplace_back(encode_str("tags"), encode_set(std::move(tags)));
    /* content is opaque to Void Core (SPEC §3.2), so its sequences keep their
     * order. Palabra does not get to decide that an application's list is
     * unordered. An absent content is {} per the hydration rule. */
    f.emplace_back(encode_str("content"),
                   is_absent(content) ? encode_map({}) : encode(content));
    /* relations is reserved (SPEC §3.2/§3.7) and its meaning is undecided, so it
     * keeps its order: a false conflict is honest, a lost order is not. */
    f.emplace_back(encode_str("relations"),
                   is_absent(relations) ? encode_seq({}) : encode(relations));
    if (policy.include_view)
        f.emplace_back(encode_str("placement"), encode(get(rune, "placement")));
    return encode_map(std::move(f));
}

std::string canon_mantle(const cJSON* mantle, const Policy& policy) {
    const cJSON* name = get(mantle, "name");
    if (!name || !cJSON_IsString(name) || !name->valuestring || !name->valuestring[0])
        throw CanonicalError("mantle has no name");  // SPEC §3.4

    /* A set, unconditionally: SPEC §4 makes rune order non-semantic and requires
     * a canonical form to be order-insensitive, because two peers who created the
     * same runes in different orders hold EQUAL state. Mixing array position into
     * the hash would make them disagree loudly about something that is not a
     * disagreement. */
    std::vector<std::string> runes;
    const cJSON* rune_arr = get(mantle, "runes");
    if (rune_arr && cJSON_IsArray(rune_arr))
        for (const cJSON* r = rune_arr->child; r; r = r->next)
            runes.push_back(canon_rune(r, policy));
    std::string runes_enc = encode_set(std::move(runes));

    /* layout.edges is an OR-Set of edges (okf/concepts/join.md): a wire exists
     * or it does not, and the order they were drawn in is not information anyone
     * has. */
    std::vector<std::string> edges;
    const cJSON* layout = get(mantle, "layout");
    const cJSON* edge_arr = get(layout, "edges");
    if (edge_arr && cJSON_IsArray(edge_arr))
        for (const cJSON* e = edge_arr->child; e; e = e->next)
            edges.push_back(canon_edge(e));

    const cJSON* tags = get(mantle, "tags");
    const cJSON* rules = get(mantle, "rules");

    std::vector<std::pair<std::string, std::string>> f;
    f.emplace_back(encode_str("id"), encode_str(str_or(get(mantle, "id"), "")));
    f.emplace_back(encode_str("name"), encode_str(name->valuestring));
    f.emplace_back(encode_str("domain"), encode(get(mantle, "domain")));
    f.emplace_back(encode_str("runes"), std::move(runes_enc));
    f.emplace_back(encode_str("tags"), is_absent(tags) ? encode_map({}) : encode(tags));
    f.emplace_back(encode_str("edges"), encode_set(std::move(edges)));
    f.emplace_back(encode_str("rules"),
                   is_absent(rules) ? encode_seq({}) : encode(rules));
    return encode_map(std::move(f));
}

/* A glyph DECLARATION (VoidCore:SPEC.md §2, 0.2.14): the schema that says what a
 * rune's content means. Palabra does not interpret one — it is an application's
 * type, and Core itself stores `presentations` without reading it — so the body
 * goes through the generic encoder.
 *
 * ONE KEY IS EXCLUDED: `source`.
 *
 * Core stamps each descriptor `"source": "document"` or `"host"` to say whether a
 * declaration traveled with the data or was registered at boot. That is an answer
 * about THIS PEER's resolution, not about the type — the same declaration is
 * `document` on the peer that received it and could be `host` on a peer whose
 * application registered it too. Hashing it would make two peers holding the same
 * schema compute different names for it, which is the exact divergence
 * okf/concepts/canonical-form.md exists to prevent.
 *
 * It is the same judgment as `domains` (§4.4): real state, peer-local resolution,
 * not versioned content. The difference is that `source` sits INSIDE a key that
 * is versioned, so it has to be excluded here rather than by omitting the key. */
std::string canon_glyph_descriptor_impl(const cJSON* descriptor) {
    if (!descriptor || !cJSON_IsObject(descriptor))
        throw CanonicalError("glyph declaration is not an object");
    std::vector<std::pair<std::string, std::string> > fields;
    for (const cJSON* it = descriptor->child; it; it = it->next) {
        if (!it->string) throw CanonicalError("glyph declaration member without a key");
        if (std::strcmp(it->string, "source") == 0) continue;  // peer-local
        fields.emplace_back(it->string, encode(it));
    }
    return encode_map_of_encoded(fields);
}

std::string canon_glyphs(const cJSON* state) {
    const cJSON* glyphs = get(state, "glyphs");
    std::vector<std::pair<std::string, std::string> > decls;
    if (glyphs && cJSON_IsObject(glyphs)) {
        for (const cJSON* g = glyphs->child; g; g = g->next) {
            if (!g->string) throw CanonicalError("glyph declaration without a name");
            decls.emplace_back(g->string, canon_glyph_descriptor_impl(g));
        }
    } else if (glyphs && !cJSON_IsNull(glyphs)) {
        throw CanonicalError("`glyphs` is present but is not an object");
    }
    /* A map, so an ABSENT `glyphs` and an EMPTY one encode identically — the same
     * hydration rule §4.1 applies to a partial rune. A document written before
     * 0.2.14 and one that declared nothing are the same state and must say so. */
    return encode_map_of_encoded(decls);
}

std::string canon_slice(const cJSON* state, const Policy& policy) {
    std::vector<std::string> mantles;
    std::vector<std::string> names;
    const cJSON* arr = get(state, "mantles");
    if (arr && cJSON_IsArray(arr)) {
        for (const cJSON* m = arr->child; m; m = m->next) {
            names.push_back(str_or(get(m, "name"), ""));
            mantles.push_back(canon_mantle(m, policy));
        }
    }
    std::vector<std::string> sorted_names = names;
    std::sort(sorted_names.begin(), sorted_names.end());
    if (std::adjacent_find(sorted_names.begin(), sorted_names.end()) !=
        sorted_names.end())
        throw CanonicalError("duplicate mantle name");  // SPEC §3.4

    /* THE VERSIONED SLICE IS TWO KEYS AS OF CANON_VERSION 3 (2026-09-03).
     *
     * It was `mantles` alone. Void Core 0.2.14 added `state.glyphs`, and Core
     * argued it belongs here rather than beside `domains`. The argument is theirs
     * and we accept it:
     *
     *     A domain is HOW THIS MACHINE REACHES THE WORLD. A glyph declaration is
     *     WHAT THE RUNES YOU ARE ALREADY SYNCING MEAN.
     *
     * Palabra's forcing case for excluding `domains` — a domain carries real
     * build/deploy commands, so syncing one runs device A's deploy on device B —
     * genuinely does not reach a declaration. A descriptor is inert data that
     * Core stores and does not execute.
     *
     * And excluding it has a measured cost rather than a theoretical one: sync a
     * mantle without its declarations and the receiving peer holds the content in
     * its document and cannot reach it through the projection, with no error
     * anywhere. `mantles` and `glyphs` are a value and its type. */
    std::vector<std::pair<std::string, std::string> > slice;
    slice.emplace_back("mantles", encode_set(std::move(mantles)));
    slice.emplace_back("glyphs", canon_glyphs(state));
    return encode_map_of_encoded(slice);
}


namespace enc {
std::string canon_glyph_descriptor(const cJSON* descriptor) {
    return canon_glyph_descriptor_impl(descriptor);
}
}  // namespace enc

Digest rune_hash(const cJSON* r, const Policy& p) {
    return digest_of("rune", canon_rune(r, p), p);
}
Digest mantle_hash(const cJSON* m, const Policy& p) {
    return digest_of("mantle", canon_mantle(m, p), p);
}
Digest slice_hash(const cJSON* s, const Policy& p) {
    return digest_of("slice", canon_slice(s, p), p);
}

std::string version_name(const cJSON* state, const Policy& p) {
    return "v:" + to_hex(slice_hash(state, p));
}

std::string to_hex(const Digest& d) {
    static const char* hex = "0123456789abcdef";
    std::string out;
    out.reserve(d.size() * 2);
    for (std::uint8_t b : d) { out.push_back(hex[b >> 4]); out.push_back(hex[b & 0xF]); }
    return out;
}

std::string short_hex(const Digest& d, std::size_t n) {
    return to_hex(d).substr(0, n);
}



bool has_combining_marks(const std::string& utf8) {
    /* Decode enough to see the code point; the encoder has already validated
     * UTF-8 for anything that reaches a hash, and this is advisory besides. */
    std::size_t i = 0;
    while (i < utf8.size()) {
        unsigned char c = static_cast<unsigned char>(utf8[i]);
        unsigned int cp = 0;
        std::size_t n = 0;
        if (c < 0x80) { cp = c; n = 0; }
        else if ((c & 0xE0) == 0xC0) { cp = c & 0x1Fu; n = 1; }
        else if ((c & 0xF0) == 0xE0) { cp = c & 0x0Fu; n = 2; }
        else if ((c & 0xF8) == 0xF0) { cp = c & 0x07u; n = 3; }
        else { ++i; continue; }
        if (i + n >= utf8.size() + (n ? 0 : 1)) break;
        for (std::size_t k = 1; k <= n; ++k)
            cp = (cp << 6) | (static_cast<unsigned char>(utf8[i + k]) & 0x3Fu);
        i += n + 1;

        /* Combining Diacritical Marks, and the Supplement / Extended blocks.
         * Enough for Latin-script content, which is where the realistic failure
         * is; a fuller table would need the Unicode data this file exists to
         * avoid vendoring. */
        if ((cp >= 0x0300 && cp <= 0x036F) ||
            (cp >= 0x1AB0 && cp <= 0x1AFF) ||
            (cp >= 0x1DC0 && cp <= 0x1DFF) ||
            (cp >= 0x20D0 && cp <= 0x20FF) ||
            (cp >= 0xFE20 && cp <= 0xFE2F))
            return true;
    }
    return false;
}

}  // namespace voidpalabra
