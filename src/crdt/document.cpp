/* document.cpp — the enriched document: building it, merging it, reading it back.
 *
 * The merge is "union the maps, join the leaves", so the three laws are inherited
 * from the OrSet at every level of the tree and this file adds no new algebra.
 */
#include "internal.hpp"

#include "encoding/internal.hpp"

#include "voidpalabra/canonical.hpp"
#include "cJSON.h"

#include <algorithm>
#include <array>
#include <map>
#include <cstdio>
#include <cstring>
#include <set>

namespace voidpalabra {

namespace crdt {

/* Recursive join over the document tree. Every leaf is an OrSet, and every
 * interior node is a map — so the merge is "union the maps, join the leaves",
 * and the three laws are inherited from the leaves at every level. */
cJSON* join_node(const cJSON* a, const cJSON* b);

/* An OrSet is an object whose `r` is an ARRAY. That is the whole test, and the
 * previous one — "has children named `a` and `r`" — was a real defect rather than
 * a theoretical one.
 *
 * Mantles are keyed by name and glyph declarations by glyph name, and both names
 * are the user's. A user with two mantles called `a` and `r` made the `mantles`
 * map look like an OrSet, so `join` read it as one and the merge produced ZERO
 * mantles — every mantle erased by an ordinary sync, with no attacker involved
 * (demonstrated 2026-09-16). A map node's children are always objects and an
 * OrSet's `r` is always an array, so the array is what tells them apart. */
bool is_orset(const cJSON* n) {
    const cJSON* a = get(n, "a");
    const cJSON* r = get(n, "r");
    return cJSON_IsObject(n) && a && cJSON_IsObject(a) && r && cJSON_IsArray(r);
}

namespace {

/* Where two peers disagree about what KIND of node sits at a path. */
int node_rank(const cJSON* n) {
    if (is_orset(n)) return 2;
    if (cJSON_IsObject(n)) return 3;
    return 1;
}

std::string printed(const cJSON* n) {
    char* s = cJSON_PrintUnformatted(const_cast<cJSON*>(n));
    std::string out = s ? s : "";
    if (s) cJSON_free(s);
    return out;
}

}  // namespace

cJSON* join_map(const cJSON* a, const cJSON* b) {
    /* Indexed, not scanned. `cJSON_GetObjectItem` is a linear walk, so looking up
     * each of a's keys in b was O(n*m): a mantle of 20,000 runes cost 400 million
     * string comparisons per merge, and a peer could force that with a message well
     * inside any size ceiling. */
    std::map<std::string, const cJSON*> in_b;
    for (const cJSON* it = b ? b->child : nullptr; it; it = it->next)
        if (it->string) in_b.emplace(it->string, it);

    cJSON* out = cJSON_CreateObject();
    std::set<std::string> seen;
    for (const cJSON* it = a ? a->child : nullptr; it; it = it->next) {
        if (!it->string || !seen.insert(it->string).second) continue;
        auto other = in_b.find(it->string);
        cJSON_AddItemToObject(out, it->string,
                              other != in_b.end() ? join_node(it, other->second)
                                                  : cJSON_Duplicate(it, 1));
    }
    for (const auto& kv : in_b)
        if (!seen.count(kv.first))
            cJSON_AddItemToObject(out, kv.first.c_str(), cJSON_Duplicate(kv.second, 1));
    return out;
}

cJSON* join_node(const cJSON* a, const cJSON* b) {
    int ra = node_rank(a), rb = node_rank(b);
    /* THE LAWS MUST HOLD ON INPUT THAT IS WRONG.
     *
     * The old fallback was "take a". When two peers held different kinds of node at
     * one path — one sent by a buggy or hostile peer — join(a, b) kept a and
     * join(b, a) kept b, so the two peers never converged again and nothing
     * reported it (demonstrated 2026-09-16). One malformed message split a mesh
     * permanently at that path.
     *
     * The fix is to make the choice a function of the two nodes rather than of
     * argument order: a fixed ranking between kinds, and the smaller printed form
     * between two scalars. That is a join on its own (a max over a total order), so
     * commutativity, associativity and idempotence hold for every input, valid or
     * not.
     *
     * This does NOT make bad input harmless — the higher-ranked node wins, and it
     * may be the wrong one. Correctness is `validate`'s job, at the door. This is
     * the guarantee that whatever gets past the door still converges. */
    if (ra != rb) return cJSON_Duplicate(ra > rb ? a : b, 1);
    if (ra == 2) return orset_to_json(join(orset_from_json(a), orset_from_json(b)));
    if (ra == 3) return join_map(a, b);
    std::string pa = printed(a), pb = printed(b);
    return cJSON_Duplicate(pb < pa ? b : a, 1);
}

void add_field(cJSON* fields, const char* name, const cJSON* value, Mint& mint) {
    OrSet r;
    r.add(mint.next(), as_text(value));
    cJSON_AddItemToObject(fields, name, orset_to_json(r));
}



}  // namespace crdt

using namespace crdt;


Doc::~Doc() { if (root) cJSON_Delete(root); }
Doc::Doc(Doc&& o) noexcept : root(o.root) { o.root = nullptr; }
Doc& Doc::operator=(Doc&& o) noexcept {
    if (this != &o) { if (root) cJSON_Delete(root); root = o.root; o.root = nullptr; }
    return *this;
}

Doc enrich(const cJSON* state, Mint& mint) {
    Doc doc;
    doc.root = cJSON_CreateObject();
    cJSON_AddNumberToObject(doc.root, "palabra", 1);
    cJSON* mantles = cJSON_CreateObject();
    cJSON_AddItemToObject(doc.root, "mantles", mantles);

    const cJSON* arr = get(state, "mantles");
    for (const cJSON* m = arr ? arr->child : nullptr; m; m = m->next) {
        const cJSON* mname = get(m, "name");
        if (!mname || !cJSON_IsString(mname)) continue;

        cJSON* mo = cJSON_CreateObject();
        OrSet present;
        present.add(mint.next(), "1");
        cJSON_AddItemToObject(mo, "present", orset_to_json(present));

        cJSON* mfields = cJSON_CreateObject();
        add_field(mfields, "id", get(m, "id"), mint);
        add_field(mfields, "domain", get(m, "domain"), mint);
        /* A mantle's own `tags` and `rules` are part of the versioned slice
         * (SPEC §4.2 hashes both) and were not carried here until 2026-09-16, so
         * every merge that spliced mantles wrote them back empty. Nothing failed:
         * the round-trip test had no fixture with either.
         *
         * `tags` is a map keyed by tag name, so each entry is its own register —
         * the same granularity as a rune's `content`, for the same reason: two
         * peers relating different tags should not conflict. `rules` is an ordered
         * list whose meaning is Core's, so it is one register and a concurrent edit
         * to it is a conflict rather than a guess.
         *
         * Neither is written when it holds its default, so a document that never
         * used them enriches to exactly the bytes it did before. */
        const cJSON* mtags = get(m, "tags");
        if (mtags && cJSON_IsObject(mtags))
            for (const cJSON* tg = mtags->child; tg; tg = tg->next)
                if (tg->string) {
                    std::string key = std::string("tags.") + tg->string;
                    add_field(mfields, key.c_str(), tg, mint);
                }
        const cJSON* rules = get(m, "rules");
        if (rules && !(cJSON_IsArray(rules) && cJSON_GetArraySize(rules) == 0))
            add_field(mfields, "rules", rules, mint);
        cJSON_AddItemToObject(mo, "fields", mfields);

        cJSON* runes = cJSON_CreateObject();
        const cJSON* rarr = get(m, "runes");
        for (const cJSON* r = rarr ? rarr->child : nullptr; r; r = r->next) {
            const cJSON* id = get(get(r, "spirit"), "id");
            if (!id || !cJSON_IsString(id)) continue;

            cJSON* ro = cJSON_CreateObject();
            OrSet rp;
            rp.add(mint.next(), "1");
            cJSON_AddItemToObject(ro, "present", orset_to_json(rp));

            cJSON* f = cJSON_CreateObject();
            add_field(f, "spirit.name", get(get(r, "spirit"), "name"), mint);
            add_field(f, "glyph", get(r, "glyph"), mint);
            for (const char* k : kFacets) {
                std::string key = std::string("facets.") + k;
                add_field(f, key.c_str(), get(get(r, "facets"), k), mint);
            }
            /* Each content key is its own register, so two peers editing
             * different fields of one rune do not conflict — which is most of
             * what makes concurrent editing tolerable. */
            const cJSON* content = get(r, "content");
            for (const cJSON* c = content ? content->child : nullptr; c; c = c->next) {
                std::string key = std::string("content.") + (c->string ? c->string : "");
                add_field(f, key.c_str(), c, mint);
            }
            add_field(f, "placement", get(r, "placement"), mint);
            /* Reserved by Core (SPEC §3.2/§3.7), hashed by §4.1, and lost on every
             * merge until it was carried. One register: its meaning is undecided,
             * so nothing here may merge inside it. */
            const cJSON* relations = get(r, "relations");
            if (relations && !(cJSON_IsArray(relations) && cJSON_GetArraySize(relations) == 0))
                add_field(f, "relations", relations, mint);
            cJSON_AddItemToObject(ro, "fields", f);

            OrSet tags;
            const cJSON* tarr = get(r, "tags");
            for (const cJSON* t = tarr ? tarr->child : nullptr; t; t = t->next)
                tags.add(mint.next(), as_text(t));
            cJSON_AddItemToObject(ro, "tags", orset_to_json(tags));

            cJSON_AddItemToObject(runes, id->valuestring, ro);
        }
        cJSON_AddItemToObject(mo, "runes", runes);

        OrSet edges;
        const cJSON* earr = get(get(m, "layout"), "edges");
        for (const cJSON* e = earr ? earr->child : nullptr; e; e = e->next)
            edges.add(mint.next(), as_text(e));
        cJSON_AddItemToObject(mo, "edges", orset_to_json(edges));

        cJSON_AddItemToObject(mantles, mname->valuestring, mo);
    }

    /* GLYPH DECLARATIONS (VoidCore:SPEC.md §2, 0.2.14).
     *
     * One register per declaration, holding the WHOLE descriptor — deliberately
     * not one register per descriptor key, which is what a rune's `content` gets.
     *
     * The two are different objects and want different granularity. Splitting
     * content per key means two peers editing different fields of one rune do not
     * conflict, which is most of what makes concurrent editing bearable. Splitting
     * a SCHEMA per key would let a merge synthesize a descriptor neither peer
     * declared — peer A's `fields` with peer B's `kind` — and hand it back as a
     * type somebody authored. A schema that nobody wrote is worse than a conflict
     * somebody has to answer, so the whole descriptor is one value and concurrent
     * redeclaration surfaces through `conflicts()`.
     *
     * That is also what Void Core asked for: "two peers gave the same type two
     * schemas [...] a disagreement a human should settle rather than something to
     * guess at." */
    cJSON* glyphs = cJSON_CreateObject();
    cJSON_AddItemToObject(doc.root, "glyphs", glyphs);
    const cJSON* gin = get(state, "glyphs");
    if (gin && cJSON_IsObject(gin)) {
        for (const cJSON* g = gin->child; g; g = g->next) {
            if (!g->string) continue;
            cJSON* go = cJSON_CreateObject();
            OrSet present;
            present.add(mint.next(), "");
            cJSON_AddItemToObject(go, "present", orset_to_json(present));
            cJSON* fields = cJSON_CreateObject();
            /* Not `add_field`, which encodes the value verbatim. A declaration's
             * `source` key is peer-local resolution and is excluded from the
             * canonical form (SPEC §4.4), so it must be excluded from the register
             * too — otherwise two peers differing only in how they resolved one
             * schema compute the same version name and still report a conflict. */
            OrSet reg;
            reg.add(mint.next(), enc::canon_glyph_descriptor(g));
            cJSON_AddItemToObject(fields, "descriptor", orset_to_json(reg));
            cJSON_AddItemToObject(go, "fields", fields);
            cJSON_AddItemToObject(glyphs, g->string, go);
        }
    }
    return doc;
}

Doc join(const Doc& a, const Doc& b) {
    Doc out;
    out.root = join_node(a.root, b.root);
    return out;
}

namespace {

/* Re-encode every OrSet through the primitive, so the output depends only on the
 * VALUE (which tags are added, which retired) and not on how a producer happened
 * to write it down — duplicate entries, insertion order, either spelling of an
 * empty set.
 *
 * Found by the idempotence property test on 2026-07-27, and it is the same
 * failure Rung 0 exists to prevent, one level up: without this, a document that
 * had been through a join hashed differently from an equal one that had not, so
 * `a ⊔ a = a` held as a value and failed as a name. The primitive's laws were
 * never in question — `canon_doc` was comparing representations. */
cJSON* normalize_node(const cJSON* n) {
    if (is_orset(n)) return orset_to_json(orset_from_json(n));
    if (cJSON_IsObject(n)) {
        cJSON* out = cJSON_CreateObject();
        for (const cJSON* it = n->child; it; it = it->next)
            cJSON_AddItemToObject(out, it->string, normalize_node(it));
        return out;
    }
    return cJSON_Duplicate(n, 1);
}

}  // namespace

std::string canon_doc(const Doc& doc) {
    cJSON* norm = normalize_node(doc.root);
    std::string out = encode(norm);
    cJSON_Delete(norm);
    return out;
}

/* --- editing an enriched document ---------------------------------------- */
/* These exist so nothing outside this file has to hand-write CRDT metadata. A
 * caller that edits the JSON directly can produce a document that is valid but
 * denormalized, which `canon_doc` now tolerates — but writing metadata by hand is
 * how invariants get broken quietly, so the API comes first. */

namespace {

cJSON* rune_node(Doc& doc, const std::string& mantle, const std::string& rune_id) {
    cJSON* ms = cJSON_GetObjectItemCaseSensitive(doc.root, "mantles");
    cJSON* m = cJSON_GetObjectItemCaseSensitive(ms, mantle.c_str());
    if (!m) return nullptr;
    cJSON* runes = cJSON_GetObjectItemCaseSensitive(m, "runes");
    return cJSON_GetObjectItemCaseSensitive(runes, rune_id.c_str());
}

void put_orset(cJSON* parent, const char* key, const OrSet& s) {
    cJSON_DeleteItemFromObjectCaseSensitive(parent, key);
    cJSON_AddItemToObject(parent, key, orset_to_json(s));
}

}  // namespace

bool set_field(Doc& doc, const std::string& mantle, const std::string& rune_id,
               const std::string& field, const cJSON* value, Mint& mint) {
    cJSON* r = rune_node(doc, mantle, rune_id);
    if (!r) return false;
    cJSON* fields = cJSON_GetObjectItemCaseSensitive(r, "fields");
    cJSON* f = cJSON_GetObjectItemCaseSensitive(fields, field.c_str());
    OrSet reg = f ? orset_from_json(f) : OrSet{};
    write(reg, mint.next(), as_text(value));
    put_orset(fields, field.c_str(), reg);
    return true;
}

bool add_tag(Doc& doc, const std::string& mantle, const std::string& rune_id,
             const std::string& tag, Mint& mint) {
    cJSON* r = rune_node(doc, mantle, rune_id);
    if (!r) return false;
    cJSON* t = cJSON_GetObjectItemCaseSensitive(r, "tags");
    OrSet s = orset_from_json(t);
    cJSON* v = cJSON_CreateString(tag.c_str());
    s.add(mint.next(), as_text(v));
    cJSON_Delete(v);
    put_orset(r, "tags", s);
    return true;
}

bool remove_tag(Doc& doc, const std::string& mantle, const std::string& rune_id,
                const std::string& tag) {
    cJSON* r = rune_node(doc, mantle, rune_id);
    if (!r) return false;
    cJSON* t = cJSON_GetObjectItemCaseSensitive(r, "tags");
    OrSet s = orset_from_json(t);
    cJSON* v = cJSON_CreateString(tag.c_str());
    s.remove_value(as_text(v));  // observed-remove: only the tags visible here
    cJSON_Delete(v);
    put_orset(r, "tags", s);
    return true;
}


Doc flatten(const Doc& doc, const JoinPolicy& policy) {
    Doc out;
    out.root = cJSON_CreateObject();
    cJSON* mantles = cJSON_CreateArray();
    cJSON_AddItemToObject(out.root, "mantles", mantles);

    const cJSON* min = get(doc.root, "mantles");
    std::vector<const cJSON*> ms;
    for (const cJSON* m = min ? min->child : nullptr; m; m = m->next) ms.push_back(m);
    /* Emit in name order. Core says rune and mantle order carry no meaning
     * (SPEC §4), so ANY order round-trips through the canonical form — sorting
     * just makes the output stable to read. */
    std::sort(ms.begin(), ms.end(), [](const cJSON* a, const cJSON* b) {
        return std::strcmp(a->string, b->string) < 0;
    });

    for (const cJSON* m : ms) {
        if (orset_from_json(get(m, "present")).empty()) continue;  // removed mantle
        cJSON* mo = cJSON_CreateObject();
        const cJSON* mf = get(m, "fields");
        cJSON_AddItemToObject(mo, "id", first_or_null(resolve(get(mf, "id"), policy.lookup("id"))));
        cJSON_AddItemToObject(mo, "name", cJSON_CreateString(m->string));
        cJSON_AddItemToObject(mo, "domain", first_or_null(resolve(get(mf, "domain"), policy.lookup("domain"))));

        cJSON* runes = cJSON_CreateArray();
        const cJSON* rin = get(m, "runes");
        std::vector<const cJSON*> rs;
        for (const cJSON* r = rin ? rin->child : nullptr; r; r = r->next) rs.push_back(r);
        std::sort(rs.begin(), rs.end(), [](const cJSON* a, const cJSON* b) {
            return std::strcmp(a->string, b->string) < 0;
        });

        for (const cJSON* r : rs) {
            if (orset_from_json(get(r, "present")).empty()) continue;  // removed rune
            cJSON* ro = cJSON_CreateObject();
            const cJSON* f = get(r, "fields");

            cJSON* spirit = cJSON_CreateObject();
            cJSON_AddStringToObject(spirit, "id", r->string);
            cJSON_AddItemToObject(spirit, "name",
                                  first_or_null(resolve(get(f, "spirit.name"), policy.lookup("spirit.name"))));
            cJSON_AddItemToObject(ro, "spirit", spirit);
            cJSON_AddItemToObject(ro, "glyph", first_or_null(resolve(get(f, "glyph"), policy.lookup("glyph"))));

            cJSON* facets = cJSON_CreateObject();
            for (const char* k : kFacets) {
                std::string key = std::string("facets.") + k;
                cJSON_AddItemToObject(facets, k,
                                      first_or_null(resolve(get(f, key.c_str()), policy.lookup(key))));
            }
            cJSON_AddItemToObject(ro, "facets", facets);

            cJSON* content = cJSON_CreateObject();
            for (const cJSON* fi = f ? f->child : nullptr; fi; fi = fi->next) {
                if (std::strncmp(fi->string, "content.", 8) != 0) continue;
                /* A register with no live value is a key that was REMOVED — an
                 * observed-remove retired it. Emitting it as `null` would put back
                 * a key the user deleted, and `null` is a value. */
                if (live_of(fi).values.empty()) continue;
                cJSON_AddItemToObject(content, fi->string + 8,
                                      first_or_null(resolve(fi, policy.lookup(fi->string ? fi->string : ""))));
            }
            cJSON_AddItemToObject(ro, "content", content);

            cJSON* tags = cJSON_CreateArray();
            for (const auto& v : orset_from_json(get(r, "tags")).values())
                cJSON_AddItemToArray(tags, decode(v));
            cJSON_AddItemToObject(ro, "tags", tags);

            cJSON_AddItemToObject(ro, "placement",
                                  first_or_null(resolve(get(f, "placement"), policy.lookup("placement"))));
            {
                Live rel = resolve(get(f, "relations"), policy.lookup("relations"));
                cJSON* v = rel.values.empty() ? nullptr : decode(rel.values[0]);
                cJSON_AddItemToObject(ro, "relations", v ? v : cJSON_CreateArray());
            }
            cJSON_AddItemToArray(runes, ro);
        }
        cJSON_AddItemToObject(mo, "runes", runes);
        {
            cJSON* mtags = cJSON_CreateObject();
            for (const cJSON* fi = mf ? mf->child : nullptr; fi; fi = fi->next) {
                if (!fi->string || std::strncmp(fi->string, "tags.", 5) != 0) continue;
                Live l = resolve(fi, policy.lookup(fi->string));
                if (l.values.empty()) continue;  // a tag that was removed
                cJSON* v = decode(l.values[0]);
                if (v) cJSON_AddItemToObject(mtags, fi->string + 5, v);
            }
            cJSON_AddItemToObject(mo, "tags", mtags);
        }

        cJSON* layout = cJSON_CreateObject();
        cJSON* edges = cJSON_CreateArray();
        for (const auto& v : orset_from_json(get(m, "edges")).values())
            cJSON_AddItemToArray(edges, decode(v));
        cJSON_AddItemToObject(layout, "edges", edges);
        cJSON_AddItemToObject(mo, "layout", layout);
        {
            Live rl = resolve(get(mf, "rules"), policy.lookup("rules"));
            cJSON* v = rl.values.empty() ? nullptr : decode(rl.values[0]);
            cJSON_AddItemToObject(mo, "rules", v ? v : cJSON_CreateArray());
        }

        cJSON_AddItemToArray(mantles, mo);
    }

    /* Glyph declarations come back as the object Core wrote, keyed by name.
     *
     * Emitted only when there is at least one, so a document that never declared
     * anything flattens to exactly the shape it had. Adding an empty `glyphs: {}`
     * to every flattened slice would be a change a caller has to notice, and the
     * canonical form already treats absent and empty as the same state. */
    const cJSON* gin = get(doc.root, "glyphs");
    std::vector<const cJSON*> gs;
    for (const cJSON* g = gin ? gin->child : nullptr; g; g = g->next) {
        if (orset_from_json(get(g, "present")).empty()) continue;  // undeclared
        gs.push_back(g);
    }
    if (!gs.empty()) {
        std::sort(gs.begin(), gs.end(), [](const cJSON* a, const cJSON* b) {
            return std::strcmp(a->string, b->string) < 0;
        });
        cJSON* glyphs = cJSON_CreateObject();
        cJSON_AddItemToObject(out.root, "glyphs", glyphs);
        for (const cJSON* g : gs) {
            Live l = live_of(get(get(g, "fields"), "descriptor"));
            if (l.values.empty()) continue;
            /* A conflicted descriptor resolves the same way a conflicted field
             * does — by the policy, lowest-ordered — and the caller is expected to
             * have read `conflicts()`. `flatten` cannot represent two schemas for
             * one name any more than it can two values for one field. */
            cJSON* d = decode(l.values.front());
            if (d) cJSON_AddItemToObject(glyphs, g->string, d);
        }
    }
    return out;
}


}  // namespace voidpalabra
