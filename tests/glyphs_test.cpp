/* glyphs_test.cpp — the versioned slice grew a key (2026-09-03).
 *
 * Void Core 0.2.14 added `state.glyphs`: the schemas that say what a rune's
 * content means. Core argued it belongs in Palabra's sync slice rather than
 * beside `domains`, and this suite is the argument turned into assertions.
 *
 * The headline test is `a_declaration_travels_to_a_peer_that_never_saw_it`. It is
 * the failure the whole change exists to prevent, and before this release it
 * failed: a mantle merged across without its declarations, so the receiving peer
 * held the content in its document and could not reach it through the projection,
 * with no error anywhere. A sync that looks like it worked.
 *
 * The second headline is `concurrent_redeclaration_surfaces_rather_than_merging`.
 * Two peers giving one type two schemas is a real disagreement, and the wrong
 * answer is to merge them per-key into a descriptor neither peer authored.
 */
#include "voidpalabra/canonical.hpp"
#include "voidpalabra/join.hpp"
#include "voidpalabra/archive.hpp"

#include "cJSON.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace voidpalabra;

namespace {

int g_checks = 0;
std::vector<std::string> g_failures;
const char* g_current = "";

void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) g_failures.push_back(std::string(g_current) + ": " + what);
}

/* A cJSON tree that frees itself, so an early check cannot leak the document. */
struct Json {
    cJSON* p;
    explicit Json(const std::string& text) : p(cJSON_Parse(text.c_str())) {}
    ~Json() { if (p) cJSON_Delete(p); }
    Json(const Json&) = delete;
    Json& operator=(const Json&) = delete;
};

bool has_key(const cJSON* doc, const char* k) {
    return doc && cJSON_GetObjectItemCaseSensitive(const_cast<cJSON*>(doc), k) != nullptr;
}

const cJSON* at(const cJSON* doc, const char* k) {
    return cJSON_GetObjectItemCaseSensitive(const_cast<cJSON*>(doc), k);
}

std::string unformatted(const cJSON* v) {
    char* t = cJSON_PrintUnformatted(const_cast<cJSON*>(v));
    std::string s = t ? t : "";
    if (t) cJSON_free(t);
    return s;
}

/* The running example: one mantle, one rune, and a `measure` declaration of the
 * kind 0.2.14 introduced. */
const char* kWithGlyphs =
    "{\"mantles\":[{\"name\":\"m\",\"runes\":[{\"spirit\":{\"id\":\"rune_a\","
    "\"name\":\"title\"},\"glyph\":\"text\",\"content\":{\"body\":\"hi\"}}]}],"
    "\"glyphs\":{\"stat\":{\"glyph\":\"stat\",\"label\":\"Stat\",\"kind\":\"measure\","
    "\"fields\":[\"note\"]}},"
    "\"config\":{\"actor\":\"ada\"},\"domains\":[],\"bindings\":[],\"active\":\"m\"}";

/* ── the slice ───────────────────────────────────────────────────────────── */

void glyphs_are_part_of_the_versioned_slice() {
    g_current = "glyphs_are_part_of_the_versioned_slice";
    Json with(kWithGlyphs);
    Json without(kWithGlyphs);
    cJSON_DeleteItemFromObjectCaseSensitive(without.p, "glyphs");
    check(version_name(with.p) != version_name(without.p),
          "declaring a type changes what version this is");
}

void an_absent_glyphs_key_and_an_empty_one_are_one_state() {
    g_current = "an_absent_glyphs_key_and_an_empty_one_are_one_state";
    /* The same hydration rule SPEC §4.1 applies to a partial rune: a document
     * written before 0.2.14 and one that declared nothing and then undeclared it
     * are the same state, and must say so. */
    Json absent("{\"mantles\":[]}");
    Json empty("{\"mantles\":[],\"glyphs\":{}}");
    check(version_name(absent.p) == version_name(empty.p),
          "absent and empty must not be two different states");
}

void peer_local_resolution_does_not_move_the_name() {
    g_current = "peer_local_resolution_does_not_move_the_name";
    /* Core stamps each descriptor `source: "document" | "host"` to say how THIS
     * peer resolved it. Two peers holding one schema must name it identically
     * however each of them came by it — so `source` is excluded, for the same
     * reason `domains` is excluded from the slice entirely. */
    Json doc_src("{\"mantles\":[],\"glyphs\":{\"s\":{\"glyph\":\"s\",\"source\":\"document\"}}}");
    Json host_src("{\"mantles\":[],\"glyphs\":{\"s\":{\"glyph\":\"s\",\"source\":\"host\"}}}");
    Json no_src("{\"mantles\":[],\"glyphs\":{\"s\":{\"glyph\":\"s\"}}}");
    check(version_name(doc_src.p) == version_name(host_src.p),
          "how a peer resolved a declaration is not part of the type");
    check(version_name(host_src.p) == version_name(no_src.p),
          "and neither is whether it was stamped at all");
}

void a_changed_schema_moves_the_name() {
    g_current = "a_changed_schema_moves_the_name";
    Json a("{\"mantles\":[],\"glyphs\":{\"s\":{\"glyph\":\"s\",\"kind\":\"measure\"}}}");
    Json b("{\"mantles\":[],\"glyphs\":{\"s\":{\"glyph\":\"s\",\"kind\":\"entity\"}}}");
    Json c("{\"mantles\":[],\"glyphs\":{\"t\":{\"glyph\":\"t\",\"kind\":\"measure\"}}}");
    check(version_name(a.p) != version_name(b.p), "a different kind is a different type");
    check(version_name(a.p) != version_name(c.p), "a different name is a different type");
}

void declaration_order_is_not_information() {
    g_current = "declaration_order_is_not_information";
    /* `glyphs` is a JSON object, and object key order is an artifact of how it was
     * written down — the same rule §2.3 already applies to every other map. */
    Json ab("{\"mantles\":[],\"glyphs\":{\"a\":{\"glyph\":\"a\"},\"b\":{\"glyph\":\"b\"}}}");
    Json ba("{\"mantles\":[],\"glyphs\":{\"b\":{\"glyph\":\"b\"},\"a\":{\"glyph\":\"a\"}}}");
    check(version_name(ab.p) == version_name(ba.p),
          "two peers who declared the same types in different orders agree");
}

void a_malformed_glyphs_key_is_refused() {
    g_current = "a_malformed_glyphs_key_is_refused";
    Json bad("{\"mantles\":[],\"glyphs\":[\"not\",\"an\",\"object\"]}");
    bool threw = false;
    try { version_name(bad.p); } catch (const CanonicalError&) { threw = true; }
    check(threw, "a `glyphs` that is not a map has no honest byte form");
}

/* ── the merge: the reason any of this matters ───────────────────────────── */

void a_declaration_travels_to_a_peer_that_never_saw_it() {
    g_current = "a_declaration_travels_to_a_peer_that_never_saw_it";
    Json a("{\"mantles\":[{\"name\":\"m\",\"runes\":[]}],"
           "\"glyphs\":{\"stat\":{\"glyph\":\"stat\",\"kind\":\"measure\",\"fields\":[\"note\"]}}}");
    Json b("{\"mantles\":[{\"name\":\"m\",\"runes\":[]}]}");
    CounterMint ma("A"), mb("B");
    Doc da = enrich(a.p, ma), db = enrich(b.p, mb);
    Doc merged = flatten(join(da, db));

    check(has_key(merged.root, "glyphs"), "the declaration crossed to the peer without it");
    const cJSON* g = at(at(merged.root, "glyphs"), "stat");
    check(g != nullptr, "and it arrived under its own name");
    check(g && unformatted(at(g, "kind")) == "\"measure\"",
          "and arrived intact rather than as a shell");
}

void merging_is_still_commutative_over_declarations() {
    g_current = "merging_is_still_commutative_over_declarations";
    Json a("{\"mantles\":[],\"glyphs\":{\"x\":{\"glyph\":\"x\",\"kind\":\"measure\"}}}");
    Json b("{\"mantles\":[],\"glyphs\":{\"y\":{\"glyph\":\"y\",\"kind\":\"act\"}}}");
    CounterMint m1("A"), m2("B"), m3("A"), m4("B");
    Doc ab = join(enrich(a.p, m1), enrich(b.p, m2));
    Doc ba = join(enrich(b.p, m4), enrich(a.p, m3));
    check(canon_doc(ab) == canon_doc(ba), "a ⊔ b == b ⊔ a with declarations in play");

    Doc flat = flatten(ab);
    const cJSON* g = at(flat.root, "glyphs");
    check(g && cJSON_GetArraySize(const_cast<cJSON*>(g)) == 2,
          "disjoint declarations both survive — this is not a conflict");
}

void concurrent_redeclaration_surfaces_rather_than_merging() {
    g_current = "concurrent_redeclaration_surfaces_rather_than_merging";
    /* Two peers gave `stat` two schemas. Core asked for this to be shown, not
     * guessed at, and per-key merging would have produced peer A's `fields` with
     * peer B's `kind` — a type neither of them declared. */
    Json a("{\"mantles\":[],\"glyphs\":{\"stat\":{\"glyph\":\"stat\","
           "\"kind\":\"measure\",\"fields\":[\"note\"]}}}");
    Json b("{\"mantles\":[],\"glyphs\":{\"stat\":{\"glyph\":\"stat\","
           "\"kind\":\"entity\",\"fields\":[\"body\"]}}}");
    CounterMint ma("A"), mb("B");
    Doc merged = join(enrich(a.p, ma), enrich(b.p, mb));

    std::vector<Conflict> cs = conflicts(merged);
    check(cs.size() == 1, "one disagreement, reported once");
    if (cs.empty()) return;
    check(cs[0].glyph == "stat", "located by glyph name");
    check(cs[0].mantle.empty() && cs[0].rune.empty(),
          "and not by a mantle, because a declaration belongs to the document");
    check(cs[0].field == "descriptor", "the whole schema is the value in dispute");
    check(cs[0].sides.size() == 2, "both schemas are handed back");

    /* A conflict is a value with an address: two peers who see the same
     * divergence must name it identically. */
    CounterMint mc("B"), md("A");
    Doc other_way = join(enrich(b.p, mc), enrich(a.p, md));
    std::vector<Conflict> cs2 = conflicts(other_way);
    check(cs2.size() == 1 && to_hex(cs2[0].hash()) == to_hex(cs[0].hash()),
          "and it has the same name whichever peer merged first");

    cJSON* j = conflict_to_json(cs[0]);
    std::string rendered = unformatted(j);
    check(rendered.find("\"glyph\":\"stat\"") != std::string::npos,
          "rendered with its glyph: " + rendered);
    check(rendered.find("\"mantle\"") == std::string::npos,
          "and without a blank mantle for a renderer to print");
    cJSON_Delete(j);
}

void the_name_and_the_merge_agree_about_what_a_declaration_is() {
    g_current = "the_name_and_the_merge_agree_about_what_a_declaration_is";
    /* Written because the first version of this feature got it wrong. `canon_slice`
     * excluded the peer-local `source` key and `enrich` did not, so two peers
     * differing only in how they had resolved one schema computed the SAME version
     * name and still reported a redeclaration conflict.
     *
     * The state said "identical", the merge said "you disagree", and both were
     * speaking for the same library. Neither answer alone was wrong, which is what
     * made it worth a permanent test: the bug was in the gap between two rules that
     * should have been one rule. They are now one function. */
    Json a("{\"mantles\":[],\"glyphs\":{\"stat\":{\"glyph\":\"stat\","
           "\"kind\":\"measure\",\"source\":\"document\"}}}");
    Json b("{\"mantles\":[],\"glyphs\":{\"stat\":{\"glyph\":\"stat\","
           "\"kind\":\"measure\",\"source\":\"host\"}}}");

    bool same_name = version_name(a.p) == version_name(b.p);
    CounterMint ma("A"), mb("B");
    Doc merged = join(enrich(a.p, ma), enrich(b.p, mb));
    bool no_conflict = conflicts(merged).empty();

    check(same_name, "the two peers name the same state");
    check(no_conflict, "and the merge agrees they do");
    check(same_name == no_conflict,
          "the canonical form and the CRDT must not disagree about what a "
          "declaration IS");
}

void an_identical_redeclaration_is_not_a_conflict() {
    g_current = "an_identical_redeclaration_is_not_a_conflict";
    /* Two peers declaring the same type the same way agreed. Idempotence, which
     * is what makes receiving the same declaration twice free. */
    Json a("{\"mantles\":[],\"glyphs\":{\"stat\":{\"glyph\":\"stat\",\"kind\":\"measure\"}}}");
    Json b("{\"mantles\":[],\"glyphs\":{\"stat\":{\"glyph\":\"stat\",\"kind\":\"measure\"}}}");
    CounterMint ma("A"), mb("B");
    Doc merged = join(enrich(a.p, ma), enrich(b.p, mb));
    check(conflicts(merged).empty(), "agreement is not a conflict");
    Doc flat = flatten(merged);
    check(has_key(flat.root, "glyphs"), "and the declaration survives");
}

/* ── the round trip, and what it does and does not cover ─────────────────── */

void flatten_returns_the_slice_and_only_the_slice() {
    g_current = "flatten_returns_the_slice_and_only_the_slice";
    /* Stated as an assertion because the header used to call this a "round-trip
     * law" without saying what it round-trips. `flatten(enrich(x))` reproduces the
     * VERSIONED SLICE of x, not x. A caller that writes the result back as its
     * whole state document loses `config`, `domains`, `bindings` and `active` —
     * so callers splice the slice into the document they already have. */
    Json state(kWithGlyphs);
    CounterMint mint("p");
    Doc back = flatten(enrich(state.p, mint));
    check(has_key(back.root, "mantles"), "mantles are in the slice");
    check(has_key(back.root, "glyphs"), "and so are declarations, as of 0.2.14");
    check(!has_key(back.root, "config"), "config is peer-local and is NOT returned");
    check(!has_key(back.root, "domains"), "domains carry deploy commands and are NOT returned");
    check(!has_key(back.root, "active"), "active is a cursor and is NOT returned");
}

void a_document_with_no_declarations_flattens_unchanged() {
    g_current = "a_document_with_no_declarations_flattens_unchanged";
    /* No empty `glyphs: {}` appears where there was none. The canonical form
     * treats absent and empty as one state, so adding the key would be a change a
     * caller has to notice for no gain. */
    Json state("{\"mantles\":[{\"name\":\"m\",\"runes\":[]}]}");
    CounterMint mint("p");
    Doc back = flatten(enrich(state.p, mint));
    check(!has_key(back.root, "glyphs"), "nothing invented for a document that declared nothing");
}

void the_archive_still_stores_every_top_level_key() {
    g_current = "the_archive_still_stores_every_top_level_key";
    /* VoidCore 0.2.14 §2(c): "if you RECONSTRUCT the state document rather than
     * round-tripping it, you will drop `glyphs`." The archive stores the whole
     * document and always has, since the 2026-08-21 data-loss fix — this asserts
     * the new key rides along with the rest rather than needing to be listed. */
    Json state(kWithGlyphs);
    Archive a;
    a.save(state.p, "with a declaration");
    cJSON* loaded = a.load_latest();
    check(loaded != nullptr, "the save loads");
    check(has_key(loaded, "glyphs"), "glyphs survived the round trip");
    check(has_key(loaded, "config") && has_key(loaded, "domains") &&
              has_key(loaded, "active"),
          "and so did every other top-level key");
    if (loaded) {
        const cJSON* g = at(at(loaded, "glyphs"), "stat");
        check(g && unformatted(at(g, "label")) == "\"Stat\"",
              "stored verbatim, not canonicalized — an archive is not a sync payload");
        cJSON_Delete(loaded);
    }
}

void a_declaration_only_edit_is_saved() {
    g_current = "a_declaration_only_edit_is_saved";
    /* The 2026-08-21 bug in its 0.2.14 shape: the dedup guard compares stored
     * CONTENT, not the version name, so a change confined to one key cannot
     * silently produce no save. Declaring a type now moves both, but the guard is
     * asserted here anyway because that is the property that was wrong before. */
    Json first("{\"mantles\":[{\"name\":\"m\",\"runes\":[]}]}");
    Json second("{\"mantles\":[{\"name\":\"m\",\"runes\":[]}],"
                "\"glyphs\":{\"s\":{\"glyph\":\"s\",\"kind\":\"act\"}}}");
    Archive a;
    a.save(first.p, "before");
    a.save(second.p, "after declaring");
    check(a.saves().size() == 2, "declaring a type is a change worth saving");
}

}  // namespace

int main() {
    glyphs_are_part_of_the_versioned_slice();
    an_absent_glyphs_key_and_an_empty_one_are_one_state();
    peer_local_resolution_does_not_move_the_name();
    a_changed_schema_moves_the_name();
    declaration_order_is_not_information();
    a_malformed_glyphs_key_is_refused();

    a_declaration_travels_to_a_peer_that_never_saw_it();
    merging_is_still_commutative_over_declarations();
    concurrent_redeclaration_surfaces_rather_than_merging();
    an_identical_redeclaration_is_not_a_conflict();
    the_name_and_the_merge_agree_about_what_a_declaration_is();

    flatten_returns_the_slice_and_only_the_slice();
    a_document_with_no_declarations_flattens_unchanged();
    the_archive_still_stores_every_top_level_key();
    a_declaration_only_edit_is_saved();

    std::printf("glyphs: %d checks", g_checks);
    if (g_failures.empty()) {
        std::printf(", all passed\n");
        return 0;
    }
    std::printf(", %d FAILED\n", static_cast<int>(g_failures.size()));
    for (const std::string& f : g_failures) std::printf("  - %s\n", f.c_str());
    return 1;
}
