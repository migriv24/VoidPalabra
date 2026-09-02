/* sequence_test.cpp — ordered content, and the property that is the whole point.
 *
 * Convergence is the easy half: a list CRDT that merges deterministically is not
 * hard to write. The headline test here is `concurrent_runs_never_interleave`,
 * because a naive list CRDT converges on "axbycz" when one person types "abc" and
 * another types "xyz" at the same spot — every peer agrees, and the result is
 * nonsense nobody wrote. That is the failure Fugue exists to prevent and the one
 * a convergence-only suite would sail straight past.
 *
 * Forcing client: Void Hormiga's newsletter block order (okf/concepts/join.md).
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

const char* kBase =
    "{\"mantles\":[{\"id\":\"m\",\"name\":\"news\",\"runes\":["
    "{\"spirit\":{\"id\":\"r1\",\"name\":\"body\"},\"content\":{\"blocks\":[]}}]}]}";

Doc fresh(const char* peer) {
    Json j(kBase);
    CounterMint mint(peer);
    Doc d = enrich(j.p, mint);
    Json empty("[]");
    seq_init(d, "news", "r1", "content.blocks", empty.p, mint);
    return d;
}

/* Read the sequence back as a plain string, one character per element, so the
 * assertions read like the text a user would see. */
std::string text_of(const Doc& d) {
    cJSON* arr = seq_read(d, "news", "r1", "content.blocks");
    std::string out;
    if (!arr) return "<none>";
    for (cJSON* it = arr->child; it; it = it->next)
        if (it->valuestring) out += it->valuestring;
    cJSON_Delete(arr);
    return out;
}

void type(Doc& d, Mint& mint, std::size_t at, const std::string& run) {
    /* One insert per character, each just after the previous — which is what an
     * editor actually emits, and what makes the run a chain in the tree. */
    for (std::size_t i = 0; i < run.size(); ++i) {
        cJSON* v = cJSON_CreateString(std::string(1, run[i]).c_str());
        seq_insert(d, "news", "r1", "content.blocks", at + i, v, mint);
        cJSON_Delete(v);
    }
}

/* A mint whose ids DELIBERATELY interleave in sort order.
 *
 * This matters more than it looks. The obvious fixture gives peer A ids like
 * "Ae_0001" and peer B "Be_0001", which sort into two clean groups - so a run
 * from A can never interleave with one from B no matter how broken the algorithm
 * is, and a non-interleaving test built on it passes for the wrong reason.
 *
 * Here A mints 00000000, 00000002, 00000004 and B mints 00000001, 00000003,
 * 00000005, so sorting the ids alone yields a,x,b,y,c,z - perfect interleaving.
 * Any suite that still reports contiguous runs is measuring the tree, not the
 * fixture. */
struct AlternatingMint : Mint {
    unsigned long next_value;
    unsigned long step;
    explicit AlternatingMint(unsigned long offset, unsigned long stride = 2)
        : next_value(offset), step(stride) {}
    Tag next() override {
        char buf[32];
        std::snprintf(buf, sizeof buf, "%08lu", next_value);
        next_value += step;
        return buf;
    }
};

/* --- the basics ---------------------------------------------------------- */

void test_insert_and_read_in_order() {
    Doc d = fresh("A");
    CounterMint m("Ae");
    type(d, m, 0, "hello");
    check(text_of(d) == "hello", "got '" + text_of(d) + "'");
}

void test_insert_in_the_middle() {
    Doc d = fresh("A");
    CounterMint m("Ae");
    type(d, m, 0, "ac");
    type(d, m, 1, "b");
    check(text_of(d) == "abc", "got '" + text_of(d) + "'");
}

void test_erase_and_tombstones() {
    Doc d = fresh("A");
    CounterMint m("Ae");
    type(d, m, 0, "abcd");
    seq_erase(d, "news", "r1", "content.blocks", 1);   // remove 'b'
    check(text_of(d) == "acd", "got '" + text_of(d) + "'");
    /* Inserting after a tombstone must still land correctly: the removed node is
     * still structure, and the index is over VISIBLE elements. */
    type(d, m, 1, "X");
    check(text_of(d) == "aXcd", "insert after a tombstone: got '" + text_of(d) + "'");
}

void test_out_of_range_is_refused() {
    Doc d = fresh("A");
    CounterMint m("Ae");
    type(d, m, 0, "ab");
    cJSON* v = cJSON_CreateString("z");
    check(!seq_insert(d, "news", "r1", "content.blocks", 9, v, m),
          "an out-of-range insert should be refused, not clamped");
    check(seq_insert(d, "news", "r1", "content.blocks", 2, v, m),
          "inserting at length is an append and must be allowed");
    cJSON_Delete(v);
    check(!seq_erase(d, "news", "r1", "content.blocks", 99),
          "an out-of-range erase should be refused");
}

void test_a_field_knows_it_is_a_sequence() {
    Doc d = fresh("A");
    check(seq_is(d, "news", "r1", "content.blocks"), "the field should be a sequence");
    check(!seq_is(d, "news", "r1", "glyph"), "a scalar field is not a sequence");
    check(seq_read(d, "news", "r1", "glyph") == nullptr,
          "reading a scalar as a sequence must return nothing, not an empty list");
}

/* --- the headline -------------------------------------------------------- */

bool contiguous(const std::string& text, const std::string& run) {
    return text.find(run) != std::string::npos;
}

void test_the_fixture_is_actually_adversarial() {
    /* Guard the guard. If the two peers' ids did not interleave in sort order,
     * the non-interleaving test below could not fail however broken the algorithm
     * was - so assert the fixture is hostile before trusting what it proves. */
    AlternatingMint a(0), b(1);
    std::vector<std::string> ids;
    for (int i = 0; i < 3; ++i) { ids.push_back(a.next()); ids.push_back(b.next()); }
    std::vector<std::string> sorted_ids = ids;
    std::sort(sorted_ids.begin(), sorted_ids.end());
    check(sorted_ids == ids,
          "the mints must produce ids that already alternate when sorted");
}

void test_concurrent_runs_never_interleave() {
    /* THE test. Two editors type a run at the same position, concurrently. Both
     * runs must survive INTACT and CONTIGUOUS - "abcxyz" or "xyzabc", never
     * "axbycz".
     *
     * The mints are chosen so that sorting the element ids alone would produce
     * exactly "axbycz". Fugue's tree has to actively prevent that; a list CRDT
     * that merely converges would report the interleaved answer and be wrong in
     * precisely the way a user notices. */
    Doc a = fresh("A"), b = fresh("B");
    AlternatingMint ma(0), mb(1);
    type(a, ma, 0, "abc");
    type(b, mb, 0, "xyz");

    std::string text = text_of(join(a, b));
    check(text.size() == 6, "both runs should survive: got " + text);
    check(text != "axbycz" && text != "xaybzc",
          "the runs interleaved - the exact failure Fugue exists to stop");
    check(contiguous(text, "abc"), "run abc was broken up: got " + text);
    check(contiguous(text, "xyz"), "run xyz was broken up: got " + text);
    std::printf("     ids that sort to axbycz merged to %s\n", text.c_str());
}

void test_interleaving_is_prevented_mid_document() {
    /* The same property away from position 0, where the insertion rule takes its
     * other branch (attaching as a left child rather than extending a chain). */
    Doc base = fresh("A");
    CounterMint mb0("Be");
    type(base, mb0, 0, "START-END");

    Doc a = fresh("A"), b = fresh("B");
    AlternatingMint ma(0), mb(1);
    type(a, ma, 0, "START-END");
    type(b, mb, 0, "START-END");
    /* Both now hold the same text but with DIFFERENT ids, which is the harder
     * case: they are genuinely different elements that happen to read alike. */
    type(a, ma, 6, "111");
    type(b, mb, 6, "222");
    std::string text = text_of(join(a, b));
    check(contiguous(text, "111"), "run '111' was broken up: got '" + text + "'");
    check(contiguous(text, "222"), "run '222' was broken up: got '" + text + "'");
}

void test_long_runs_stay_contiguous() {
    /* Interleaving gets likelier as runs get longer, so this is the version a
     * short fixture could pass by luck. */
    Doc a = fresh("A"), b = fresh("B");
    AlternatingMint ma(0), mb(1);
    type(a, ma, 0, "aaaaaaaaaaaaaaaaaaaa");
    type(b, mb, 0, "bbbbbbbbbbbbbbbbbbbb");
    std::string text = text_of(join(a, b));
    check(text.size() == 40, "both runs should survive whole: got " +
                                 std::to_string(text.size()) + " chars");
    check(contiguous(text, std::string(20, 'a')), "the 'a' run was broken up");
    check(contiguous(text, std::string(20, 'b')), "the 'b' run was broken up");
}

/* --- the laws, inherited ------------------------------------------------- */

void test_sequence_converges_under_any_merge_order() {
    Doc a = fresh("A"), b = fresh("B"), c = fresh("C");
    CounterMint ma("Ae"), mb("Be"), mc("Ce");
    type(a, ma, 0, "aa");
    type(b, mb, 0, "bb");
    type(c, mc, 0, "cc");
    std::string ref = text_of(join(join(a, b), c));
    check(text_of(join(join(a, c), b)) == ref, "merge order (a,c,b) diverged");
    check(text_of(join(join(b, a), c)) == ref, "merge order (b,a,c) diverged");
    check(text_of(join(join(c, b), a)) == ref, "merge order (c,b,a) diverged");
    check(text_of(join(join(b, c), a)) == ref, "merge order (b,c,a) diverged");
}

void test_sequence_is_idempotent_and_replay_safe() {
    Doc a = fresh("A"), b = fresh("B");
    CounterMint ma("Ae"), mb("Be");
    type(a, ma, 0, "hello");
    type(b, mb, 0, "world");
    Doc once = join(a, b);
    Doc twice = join(join(join(a, b), a), b);
    check(canon_doc(once) == canon_doc(twice),
          "replaying a peer's state changed the sequence");
    check(text_of(once) == text_of(twice), "replay changed the visible text");
}

void test_concurrent_erase_of_the_same_element_agrees() {
    Doc a = fresh("A");
    CounterMint m("Ae");
    type(a, m, 0, "abc");
    Doc b = join(a, a);                                  // B has seen the same adds
    seq_erase(a, "news", "r1", "content.blocks", 1);     // both remove 'b'
    seq_erase(b, "news", "r1", "content.blocks", 1);
    check(text_of(join(a, b)) == "ac",
          "two peers erasing the same element: got '" + text_of(join(a, b)) + "'");
}

void test_erase_on_one_peer_survives_a_merge() {
    Doc a = fresh("A");
    CounterMint m("Ae");
    type(a, m, 0, "abc");
    Doc b = join(a, a);
    seq_erase(a, "news", "r1", "content.blocks", 1);
    /* B never removed anything and has no concurrent re-add, so the removal must
     * win — this is the property a grow-only set cannot have. */
    check(text_of(join(a, b)) == "ac", "the erase did not propagate: got '" +
                                           text_of(join(a, b)) + "'");
}

void test_sequence_survives_the_document_round_trip() {
    Doc a = fresh("A");
    CounterMint m("Ae");
    type(a, m, 0, "abc");
    std::string before = canon_doc(a);
    /* The sequence rides in the enriched document, so it must canonicalize
     * deterministically like everything else. */
    check(canon_doc(a) == before, "canon_doc is not stable over a sequence");
    Doc merged = join(a, a);
    check(canon_doc(merged) == before, "a ⊔ a changed a document with a sequence");
}

/* --- runner -------------------------------------------------------------- */

struct Test { const char* name; void (*fn)(); };

const Test kTests[] = {
    {"insert_and_read_in_order", test_insert_and_read_in_order},
    {"insert_in_the_middle", test_insert_in_the_middle},
    {"erase_and_tombstones", test_erase_and_tombstones},
    {"out_of_range_is_refused", test_out_of_range_is_refused},
    {"a_field_knows_it_is_a_sequence", test_a_field_knows_it_is_a_sequence},
    {"the_fixture_is_actually_adversarial", test_the_fixture_is_actually_adversarial},
    {"concurrent_runs_never_interleave", test_concurrent_runs_never_interleave},
    {"interleaving_is_prevented_mid_document", test_interleaving_is_prevented_mid_document},
    {"long_runs_stay_contiguous", test_long_runs_stay_contiguous},
    {"sequence_converges_under_any_merge_order", test_sequence_converges_under_any_merge_order},
    {"sequence_is_idempotent_and_replay_safe", test_sequence_is_idempotent_and_replay_safe},
    {"concurrent_erase_of_the_same_element_agrees", test_concurrent_erase_of_the_same_element_agrees},
    {"erase_on_one_peer_survives_a_merge", test_erase_on_one_peer_survives_a_merge},
    {"sequence_survives_the_document_round_trip", test_sequence_survives_the_document_round_trip},
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
