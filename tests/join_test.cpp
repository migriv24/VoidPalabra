/* join_test.cpp — the three laws, the round-trip law, and the cases they miss.
 *
 * okf/roadmap.md, Phase 1: "This is where the design is actually validated: if the
 * laws hold here, order-independence is proven; if they do not, nothing later can
 * rescue it."
 *
 * The laws are checked on RANDOM documents rather than hand-picked ones, and they
 * are checked through the canonical form — so "equal" means "byte-identical",
 * which is the only definition that survives contact with two machines.
 */
#include "voidpalabra/join.hpp"
#include "voidpalabra/canonical.hpp"

#include "cJSON.h"

#include <algorithm>
#include <cstdio>
#include <random>
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

struct Json {
    cJSON* p = nullptr;
    explicit Json(const std::string& t) : p(cJSON_Parse(t.c_str())) {}
    ~Json() { if (p) cJSON_Delete(p); }
    Json(const Json&) = delete;
    Json& operator=(const Json&) = delete;
};

std::string state_text(int n_runes, const char* prefix = "r", int content = 0) {
    std::string runes;
    for (int i = 0; i < n_runes; ++i) {
        if (i) runes += ",";
        runes += std::string("{\"spirit\":{\"id\":\"rune_") + prefix +
                 std::to_string(i) + "\",\"name\":\"" + prefix + std::to_string(i) +
                 "\"},\"glyph\":\"text\",\"tags\":[\"a\",\"b\"],"
                 "\"content\":{\"value\":\"v" + std::to_string(i + content) + "\"}}";
    }
    return "{\"mantles\":[{\"id\":\"m1\",\"name\":\"demo\",\"runes\":[" + runes +
           "],\"layout\":{\"edges\":[]}}]}";
}

Doc doc_from(const std::string& text, const char* peer) {
    Json j(text);
    CounterMint mint(peer);
    return enrich(j.p, mint);
}

/* A random enriched document, built by replaying random edits through the PUBLIC
 * API — which is what a peer actually does. An earlier version of this fixture
 * hand-wrote the CRDT metadata, and that is exactly how it produced denormalized
 * documents that broke document-level idempotence while the primitive was fine. */
Doc random_doc(std::mt19937& rng, const char* peer) {
    int n = 2 + static_cast<int>(rng() % 3);
    Doc d = doc_from(state_text(n, peer), peer);
    CounterMint mint(std::string(peer) + "e");
    int edits = static_cast<int>(rng() % 4);
    for (int i = 0; i < edits; ++i) {
        std::string rune = std::string("rune_") + peer + std::to_string(rng() % n);
        cJSON* v = cJSON_CreateString(("w" + std::to_string(rng() % 5)).c_str());
        if (rng() % 4 == 0) {
            add_tag(d, "demo", rune, "t" + std::to_string(rng() % 3), mint);
        } else if (rng() % 4 == 0) {
            remove_tag(d, "demo", rune, "a");
        } else {
            set_field(d, "demo", rune, "content.value", v, mint);
        }
        cJSON_Delete(v);
    }
    return d;
}

constexpr int kSeeds = 64;

/* --- the three laws ------------------------------------------------------ */

void test_commutative() {
    for (int seed = 0; seed < kSeeds; ++seed) {
        std::mt19937 rng(seed);
        Doc a = random_doc(rng, "A"), b = random_doc(rng, "B");
        check(canon_doc(join(a, b)) == canon_doc(join(b, a)),
              "a join b != b join a (seed " + std::to_string(seed) + ")");
    }
}

void test_associative() {
    for (int seed = 0; seed < kSeeds; ++seed) {
        std::mt19937 rng(seed);
        Doc a = random_doc(rng, "A"), b = random_doc(rng, "B"), c = random_doc(rng, "C");
        Doc ab = join(a, b), bc = join(b, c);
        check(canon_doc(join(ab, c)) == canon_doc(join(a, bc)),
              "(a⊔b)⊔c != a⊔(b⊔c) (seed " + std::to_string(seed) + ")");
    }
}

void test_idempotent() {
    for (int seed = 0; seed < kSeeds; ++seed) {
        std::mt19937 rng(seed);
        Doc a = random_doc(rng, "A");
        check(canon_doc(join(a, a)) == canon_doc(a),
              "a ⊔ a != a (seed " + std::to_string(seed) + ")");
    }
}

void test_join_is_a_fixed_point_under_repetition() {
    /* Idempotence is not just a ⊔ a = a: receiving the SAME PEER's state twice,
     * interleaved with others, must also be free. This is the property that makes
     * a lossy transport survivable (history-graph.md: "drops, reordering and
     * duplication are all survivable"), and it is where a subtly wrong merge that
     * still passes the three one-shot laws would show up. */
    for (int seed = 0; seed < kSeeds; ++seed) {
        std::mt19937 rng(seed);
        Doc a = random_doc(rng, "A"), b = random_doc(rng, "B");
        Doc once = join(a, b);
        Doc twice = join(join(join(a, b), a), b);
        check(canon_doc(once) == canon_doc(twice),
              "replaying messages changed the result (seed " + std::to_string(seed) + ")");
    }
}

void test_arbitrary_gossip_order_converges() {
    /* The end-to-end claim: n peers, every peer merging what it hears in whatever
     * order it hears it, all reach ONE state. Three peers, every permutation. */
    for (int seed = 0; seed < kSeeds; ++seed) {
        std::mt19937 rng(seed);
        Doc a = random_doc(rng, "A"), b = random_doc(rng, "B"), c = random_doc(rng, "C");
        std::string reference = canon_doc(join(join(a, b), c));
        const Doc* order[][3] = {{&a, &c, &b}, {&b, &a, &c}, {&b, &c, &a},
                                 {&c, &a, &b}, {&c, &b, &a}};
        for (auto& o : order) {
            Doc partial = join(*o[0], *o[1]);
            check(canon_doc(join(partial, *o[2])) == reference,
                  "a gossip order diverged (seed " + std::to_string(seed) + ")");
        }
    }
}

/* --- the round-trip law -------------------------------------------------- */

void test_flatten_of_enrich_is_identity() {
    /* flatten(enrich(x)) == x, the same law VoidCore:scry/roundtrip.py holds a
     * Lens to. Compared through the canonical form, so field order and defaults
     * cannot mask a loss. */
    for (int n = 0; n <= 5; ++n) {
        std::string text = state_text(n);
        Json original(text);
        Doc round = flatten(doc_from(text, "P"));
        check(slice_hash(round.root) == slice_hash(original.p),
              "round trip lost data with " + std::to_string(n) + " runes");
    }
}

void test_round_trip_preserves_tags_and_edges() {
    const char* text =
        "{\"mantles\":[{\"id\":\"m1\",\"name\":\"demo\",\"runes\":["
        "{\"spirit\":{\"id\":\"rune_1\",\"name\":\"a\"},\"glyph\":\"text\","
        "\"tags\":[\"x\",\"y\",\"z\"],\"content\":{\"v\":1}}],"
        "\"layout\":{\"edges\":[{\"from\":\"a\",\"to\":\"b\",\"relation\":\"s\"}]}}]}";
    Json original(text);
    Doc round = flatten(doc_from(text, "P"));
    check(slice_hash(round.root) == slice_hash(original.p),
          "tags or edges were lost in the round trip");
}

/* --- what the join actually does ----------------------------------------- */

void test_concurrent_creation_does_not_conflict() {
    /* Core's randomly-minted spirit.id means two peers each creating a rune
     * produce genuinely distinct runes. join.md calls this "a CRDT-friendly
     * accident of Core's design"; here it is, holding. */
    Doc a = doc_from(state_text(1, "a"), "A");
    Doc b = doc_from(state_text(1, "b"), "B");
    Doc merged = flatten(join(a, b));
    cJSON* ms = cJSON_GetObjectItemCaseSensitive(merged.root, "mantles");
    cJSON* runes = cJSON_GetObjectItemCaseSensitive(ms->child, "runes");
    check(cJSON_GetArraySize(runes) == 2, "concurrent creations did not both survive");
    check(conflicts(join(a, b)).empty(), "concurrent creation produced a conflict");
}

void test_concurrent_edits_to_one_field_conflict() {
    /* The honest default (join.md): a scalar content field with no declared join
     * conflicts rather than picking a winner. No last-writer-wins, so no silent
     * discarding of one side's work. */
    Doc a = doc_from(state_text(1, "r", 0), "A");
    Doc b = doc_from(state_text(1, "r", 100), "B");
    auto c = conflicts(join(a, b));
    check(c.size() == 1, "concurrent edits to one field did not produce one conflict");
    if (c.size() == 1) {
        check(c[0].field == "content.value", "conflict did not name the field");
        check(c[0].mantle == "demo", "conflict did not name the mantle");
        check(c[0].sides.size() == 2, "a conflict must carry both sides");
        cJSON* j = conflict_to_json(c[0]);
        char* txt = cJSON_PrintUnformatted(j);
        std::string s = txt ? txt : "";
        if (txt) cJSON_free(txt);
        cJSON_Delete(j);
        check(s.find("\"v0\"") != std::string::npos &&
                  s.find("\"v100\"") != std::string::npos,
              "both sides are not present in the rendered conflict");
    }
}

void test_a_conflict_is_symmetric_and_deterministic() {
    /* conflict.md: "No side is privileged; there is no ours and no theirs." Two
     * peers seeing the same divergence must name it identically. */
    Doc a = doc_from(state_text(1, "r", 0), "A");
    Doc b = doc_from(state_text(1, "r", 100), "B");
    auto ab = conflicts(join(a, b));
    auto ba = conflicts(join(b, a));
    check(ab.size() == ba.size() && ab.size() == 1, "expected one conflict each way");
    if (ab.size() == 1 && ba.size() == 1)
        check(ab[0].hash() == ba[0].hash(),
              "two peers named the same divergence differently");
}

void test_edits_to_different_fields_do_not_conflict() {
    Doc a = doc_from("{\"mantles\":[{\"id\":\"m\",\"name\":\"d\",\"runes\":["
                     "{\"spirit\":{\"id\":\"r1\",\"name\":\"a\"},"
                     "\"content\":{\"x\":1,\"y\":1}}]}]}", "A");
    Doc b = doc_from("{\"mantles\":[{\"id\":\"m\",\"name\":\"d\",\"runes\":["
                     "{\"spirit\":{\"id\":\"r1\",\"name\":\"a\"},"
                     "\"content\":{\"x\":1,\"y\":2}}]}]}", "B");
    /* Both peers wrote x=1 concurrently — same value, so no disagreement — and
     * disagree only on y. One conflict, not two. */
    auto c = conflicts(join(a, b));
    check(c.size() == 1, "expected exactly one conflict, got " + std::to_string(c.size()));
    if (c.size() == 1)
        check(c[0].field == "content.y", "the wrong field conflicted");
}

void test_tags_merge_as_a_set() {
    Doc a = doc_from("{\"mantles\":[{\"id\":\"m\",\"name\":\"d\",\"runes\":["
                     "{\"spirit\":{\"id\":\"r1\",\"name\":\"a\"},"
                     "\"tags\":[\"x\",\"y\"]}]}]}", "A");
    Doc b = doc_from("{\"mantles\":[{\"id\":\"m\",\"name\":\"d\",\"runes\":["
                     "{\"spirit\":{\"id\":\"r1\",\"name\":\"a\"},"
                     "\"tags\":[\"y\",\"z\"]}]}]}", "B");
    Doc merged = flatten(join(a, b));
    char* txt = cJSON_PrintUnformatted(merged.root);
    std::string s = txt ? txt : "";
    if (txt) cJSON_free(txt);
    check(s.find("\"x\"") != std::string::npos && s.find("\"y\"") != std::string::npos &&
              s.find("\"z\"") != std::string::npos,
          "the tag union lost a tag");
    check(conflicts(join(a, b)).empty(), "merging tag sets produced a conflict");
}

void test_a_remove_survives_a_merge() {
    /* The property that a naive union of bare state CANNOT have, and the whole
     * reason the enriched document exists: peer A removes a tag, peer B has seen
     * the same adds, and after merging the tag stays gone. */
    Doc a = doc_from("{\"mantles\":[{\"id\":\"m\",\"name\":\"d\",\"runes\":["
                     "{\"spirit\":{\"id\":\"r1\",\"name\":\"a\"},"
                     "\"tags\":[\"x\",\"y\"]}]}]}", "A");
    Doc b = join(a, a);            // B holds exactly what A does
    remove_tag(a, "d", "r1", "y"); // A removes, observing what it can see

    Doc merged = flatten(join(a, b));
    char* txt = cJSON_PrintUnformatted(merged.root);
    std::string out = txt ? txt : "";
    if (txt) cJSON_free(txt);
    check(out.find("\"x\"") != std::string::npos, "the remove took the wrong tag");
    check(out.find("\"y\"") == std::string::npos,
          "a remove did not survive the merge - this is the bare-state failure");
}


/* --- declared per-field joins -------------------------------------------- */

/* Two peers that each moved the same rune to a different place. */
void two_peers_move_a_rune(Doc& a, Doc& b) {
    const char* base = "{\"mantles\":[{\"id\":\"m\",\"name\":\"d\",\"runes\":["
                       "{\"spirit\":{\"id\":\"r1\",\"name\":\"a\"},"
                       "\"content\":{\"v\":1},\"placement\":{\"x\":0,\"y\":0}}]}]}";
    a = doc_from(base, "A");
    b = doc_from(base, "B");
    CounterMint ma("Ae"), mb("Be");
    Json pa("{\"x\":10,\"y\":10}"), pb("{\"x\":99,\"y\":99}");
    set_field(a, "d", "r1", "placement", pa.p, ma);
    set_field(b, "d", "r1", "placement", pb.p, mb);
}

void test_policy_does_not_change_the_merged_document() {
    /* THE property. The policy is a read-time projection, so two peers running
     * DIFFERENT policies must still hold byte-identical documents. If a policy
     * could change the merge, convergence would silently depend on
     * configuration — which would undo the whole claim of okf/concepts/join.md. */
    Doc a, b;
    two_peers_move_a_rune(a, b);
    Doc merged = join(a, b);
    std::string bytes = canon_doc(merged);

    /* Reading it under three different policies must not move the bytes. */
    JoinPolicy strict;
    JoinPolicy picky = JoinPolicy::core_defaults();
    JoinPolicy maxy; maxy.fields["content.v"] = FieldJoin::Max;
    for (const JoinPolicy& p : {strict, picky, maxy}) {
        Doc f = flatten(merged, p);
        (void)conflicts(merged, p);
        check(canon_doc(merged) == bytes,
              "reading under a policy mutated the stored document");
    }
}

void test_placement_conflicts_by_default_and_not_under_core_defaults() {
    Doc a, b;
    two_peers_move_a_rune(a, b);
    Doc merged = join(a, b);

    auto strict = conflicts(merged, JoinPolicy{});
    check(strict.size() == 1 && strict[0].field == "placement",
          "moving a rune on two peers should conflict under the default policy");

    auto relaxed = conflicts(merged, JoinPolicy::core_defaults());
    check(relaxed.empty(),
          "core_defaults declares placement Pick, so a move must not conflict");
}

void test_pick_is_deterministic_across_peers() {
    /* Every peer must choose the SAME winner, without consulting a clock. */
    Doc a, b;
    two_peers_move_a_rune(a, b);
    JoinPolicy p = JoinPolicy::core_defaults();
    Doc ab = flatten(join(a, b), p);
    Doc ba = flatten(join(b, a), p);
    check(slice_hash(ab.root) == slice_hash(ba.root),
          "two merge orders picked different winners");
}

void test_pick_does_not_leak_into_other_fields() {
    /* A policy for `placement` must not quietly resolve a content conflict. */
    Doc a = doc_from(state_text(1, "r", 0), "A");
    Doc b = doc_from(state_text(1, "r", 100), "B");
    auto c = conflicts(join(a, b), JoinPolicy::core_defaults());
    check(c.size() == 1 && c[0].field == "content.value",
          "declaring placement Pick suppressed an unrelated content conflict");
}

void test_max_resolves_numbers_and_refuses_anything_else() {
    const char* base = "{\"mantles\":[{\"id\":\"m\",\"name\":\"d\",\"runes\":["
                       "{\"spirit\":{\"id\":\"r1\",\"name\":\"a\"},"
                       "\"content\":{\"n\":0}}]}]}";
    JoinPolicy p; p.fields["content.n"] = FieldJoin::Max;

    Doc a = doc_from(base, "A"), b = doc_from(base, "B");
    CounterMint ma("Ae"), mb("Be");
    Json n7("7"), n3("3");
    set_field(a, "d", "r1", "content.n", n7.p, ma);
    set_field(b, "d", "r1", "content.n", n3.p, mb);
    Doc merged = join(a, b);
    check(conflicts(merged, p).empty(), "Max should resolve two numbers");
    Doc f = flatten(merged, p);
    Json expect("{\"mantles\":[{\"id\":\"m\",\"name\":\"d\",\"runes\":["
                "{\"spirit\":{\"id\":\"r1\",\"name\":\"a\"},"
                "\"content\":{\"n\":7}}]}]}");
    check(slice_hash(f.root) == slice_hash(expect.p), "Max did not choose 7");

    /* A non-numeric value under Max is a declaration that does not match the
     * data. Falling back to Conflict is the honest answer; picking silently
     * would hide the misconfiguration behind plausible output. */
    Doc c = doc_from(base, "C"), d = doc_from(base, "D");
    CounterMint mc("Ce"), md("De");
    Json num("5"), str("\"five\"");
    set_field(c, "d", "r1", "content.n", num.p, mc);
    set_field(d, "d", "r1", "content.n", str.p, md);
    check(conflicts(join(c, d), p).size() == 1,
          "Max over a non-numeric value must fall back to Conflict, not guess");
}

void test_prefix_rules_and_specificity() {
    JoinPolicy p;
    p.fields["content.*"] = FieldJoin::Pick;
    p.fields["content.body"] = FieldJoin::Conflict;
    check(p.lookup("content.x") == FieldJoin::Pick, "prefix rule did not apply");
    check(p.lookup("content.body") == FieldJoin::Conflict, "exact rule must win");
    check(p.lookup("placement") == FieldJoin::Conflict, "fallback should be Conflict");

    JoinPolicy q;
    q.fields["content.*"] = FieldJoin::Pick;
    q.fields["content.meta.*"] = FieldJoin::Max;
    check(q.lookup("content.meta.n") == FieldJoin::Max,
          "the longer prefix should win");
}

void test_default_policy_still_conflicts_on_everything() {
    /* The fallback must stay Conflict: a field nobody thought about silently
     * losing data is the failure this whole design exists to prevent. */
    JoinPolicy p;
    check(p.fallback == FieldJoin::Conflict, "the default fallback changed");
    check(p.lookup("anything.at.all") == FieldJoin::Conflict, "an unknown field resolved");
}

/* --- the primitive, directly --------------------------------------------- */

void test_orset_primitive_laws() {
    for (int seed = 0; seed < kSeeds; ++seed) {
        std::mt19937 rng(seed);
        auto build = [&](const char* p) {
            OrSet s;
            CounterMint m(p);
            for (unsigned k = rng() % 5; k; --k)
                s.add(m.next(), "v" + std::to_string(rng() % 3));
            if (rng() % 2) s.remove_value("v1");
            return s;
        };
        OrSet a = build("A"), b = build("B"), c = build("C");
        check(join(a, b).values() == join(b, a).values(), "OrSet: not commutative");
        check(join(join(a, b), c).values() == join(a, join(b, c)).values(),
              "OrSet: not associative");
        check(join(a, a).values() == a.values(), "OrSet: not idempotent");
    }
}

void test_concurrent_add_beats_a_concurrent_remove() {
    /* Add-wins, and it is a consequence rather than a preference: a remove can
     * only retire the tags it OBSERVED, and a concurrent add carries a tag the
     * remover never saw. */
    OrSet a;
    a.add("t1", "x");
    OrSet b = a;          // b has seen t1
    b.remove_value("x");  // b removes what it saw
    a.add("t2", "x");     // a concurrently re-adds under a fresh tag
    OrSet m = join(a, b);
    check(m.values() == std::vector<std::string>{"x"},
          "a concurrent add did not survive a concurrent remove");
}

/* --- runner -------------------------------------------------------------- */

struct Test { const char* name; void (*fn)(); };

const Test kTests[] = {
    {"commutative", test_commutative},
    {"associative", test_associative},
    {"idempotent", test_idempotent},
    {"join_is_a_fixed_point_under_repetition", test_join_is_a_fixed_point_under_repetition},
    {"arbitrary_gossip_order_converges", test_arbitrary_gossip_order_converges},
    {"flatten_of_enrich_is_identity", test_flatten_of_enrich_is_identity},
    {"round_trip_preserves_tags_and_edges", test_round_trip_preserves_tags_and_edges},
    {"concurrent_creation_does_not_conflict", test_concurrent_creation_does_not_conflict},
    {"concurrent_edits_to_one_field_conflict", test_concurrent_edits_to_one_field_conflict},
    {"a_conflict_is_symmetric_and_deterministic",
     test_a_conflict_is_symmetric_and_deterministic},
    {"edits_to_different_fields_do_not_conflict",
     test_edits_to_different_fields_do_not_conflict},
    {"tags_merge_as_a_set", test_tags_merge_as_a_set},
    {"a_remove_survives_a_merge", test_a_remove_survives_a_merge},
    {"policy_does_not_change_the_merged_document", test_policy_does_not_change_the_merged_document},
    {"placement_conflicts_by_default_and_not_under_core_defaults", test_placement_conflicts_by_default_and_not_under_core_defaults},
    {"pick_is_deterministic_across_peers", test_pick_is_deterministic_across_peers},
    {"pick_does_not_leak_into_other_fields", test_pick_does_not_leak_into_other_fields},
    {"max_resolves_numbers_and_refuses_anything_else", test_max_resolves_numbers_and_refuses_anything_else},
    {"prefix_rules_and_specificity", test_prefix_rules_and_specificity},
    {"default_policy_still_conflicts_on_everything", test_default_policy_still_conflicts_on_everything},
    {"orset_primitive_laws", test_orset_primitive_laws},
    {"concurrent_add_beats_a_concurrent_remove",
     test_concurrent_add_beats_a_concurrent_remove},
};

}  // namespace

int main() {
    int failed = 0;
    for (const Test& t : kTests) {
        g_current = t.name;
        std::size_t before = g_failures.size();
        try { t.fn(); }
        catch (const std::exception& e) {
            g_failures.push_back(std::string(t.name) + ": threw: " + e.what());
        }
        bool ok = g_failures.size() == before;
        if (!ok) ++failed;
        std::printf("%s %s\n", ok ? "ok  " : "FAIL", t.name);
    }
    for (const auto& f : g_failures) std::printf("  - %s\n", f.c_str());
    std::printf("\n%d/%zu tests passed (%d checks)\n",
                static_cast<int>(sizeof(kTests) / sizeof(kTests[0])) - failed,
                sizeof(kTests) / sizeof(kTests[0]), g_checks);
    return failed ? 1 : 0;
}
