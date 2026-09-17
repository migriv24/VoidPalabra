/* hostile_test.cpp — what a peer can send, and what it must not be able to do.
 *
 * Every test here began as a demonstration that the library failed (2026-09-16),
 * and each one is phrased as the failure it prevents. None of these payloads is
 * large. That is the point worth keeping: a per-message size ceiling — which a
 * client builds first, and should — stops none of them. The damage came from
 * AMPLIFICATION (a nine-byte count that demands exabytes), from TYPE CONFUSION (a
 * node of the wrong kind that splits a mesh for good), and from an honest user's
 * NAMES colliding with the library's own structure.
 */
#include "voidpalabra/canonical.hpp"
#include "voidpalabra/join.hpp"

#include "cJSON.h"

#include <cstdio>
#include <functional>
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
    cJSON* p;
    explicit Json(const std::string& text) : p(cJSON_Parse(text.c_str())) {}
    ~Json() { if (p) cJSON_Delete(p); }
    Json(const Json&) = delete;
    Json& operator=(const Json&) = delete;
};

Doc enriched(const char* state, const char* prefix) {
    Json s(state);
    CounterMint m(prefix);
    return enrich(s.p, m);
}

const char* kState =
    "{\"mantles\":[{\"id\":\"m1\",\"name\":\"m\",\"runes\":["
    "{\"spirit\":{\"id\":\"rune_1\",\"name\":\"t\"},\"glyph\":\"text\","
    "\"tags\":[\"x\"],\"content\":{\"body\":\"hi\"}}]}]}";

/* ── the decoder: amplification ───────────────────────────────────────────── */

void a_count_larger_than_the_input_is_refused() {
    g_current = "a_count_larger_than_the_input_is_refused";
    /* Nine bytes declaring 2^63 elements. The old decoder allocated until the
     * process died. */
    check(decode(std::string("\x07\xff\xff\xff\xff\xff\xff\xff\x7f", 9)) == nullptr,
          "a sequence count beyond the bytes present");
    check(decode(std::string("\x08\xff\xff\xff\xff\x0f", 6)) == nullptr,
          "a map count beyond the bytes present");
    check(decode(std::string("\x05\xff\xff\xff\xff\x0f" "ab", 8)) == nullptr,
          "a string length beyond the bytes present");
}

void running_out_of_input_is_a_failure_not_a_null() {
    g_current = "running_out_of_input_is_a_failure_not_a_null";
    /* Three elements declared, one present. It used to decode to [true, null, null]
     * — two values nobody wrote. */
    check(decode(std::string("\x07\x03\x02", 3)) == nullptr, "a truncated sequence");
    check(decode(std::string("\x04\x3f\xf0", 3)) == nullptr, "a truncated float");
    check(decode(std::string("\x03", 1)) == nullptr, "an integer with no varint");
    check(decode(std::string()) == nullptr, "no bytes at all");
}

void nesting_is_bounded() {
    g_current = "nesting_is_bounded";
    /* Two million levels overflowed the stack. */
    std::string deep;
    for (int k = 0; k < 200000; ++k) deep += std::string("\x07\x01", 2);
    deep += std::string(1, '\0');
    check(decode(deep) == nullptr, "nesting past CJSON_NESTING_LIMIT is refused");

    std::string shallow;
    for (int k = 0; k < 50; ++k) shallow += std::string("\x07\x01", 2);
    shallow += std::string(1, '\0');
    cJSON* v = decode(shallow);
    check(v != nullptr, "and ordinary nesting still decodes");
    cJSON_Delete(v);
}

void malformed_bytes_are_refused_rather_than_guessed() {
    g_current = "malformed_bytes_are_refused_rather_than_guessed";
    check(decode(std::string("\x06", 1)) == nullptr, "an unknown tag used to decode to null");
    check(decode(std::string("\x03\xff\xff\xff\xff\xff\xff\xff\xff\xff\x7f", 11)) == nullptr,
          "an eleven-byte varint");
    check(decode(std::string("\x08\x01\x02\x02", 4)) == nullptr,
          "a map key that is not a string used to be filed under \"\"");
    check(decode(std::string("\x02\x02", 2)) == nullptr, "trailing bytes");
}

void two_spellings_of_one_value_are_not_both_canonical() {
    g_current = "two_spellings_of_one_value_are_not_both_canonical";
    /* 1.0 as a float and 1 as an integer are one value (SPEC §2.1). Only the integer
     * spelling is canonical; accepting both would let a register read one value as
     * two, and call it a conflict. */
    check(is_canonical(std::string("\x03\x02", 2)), "1 as an integer is canonical");
    check(!is_canonical(std::string("\x04\x3f\xf0\x00\x00\x00\x00\x00\x00", 9)),
          "1.0 as a float is not");
    check(!is_canonical(std::string("\x09\x02\x05\x01" "b" "\x05\x01" "a", 8)),
          "an unsorted set is not");
}

/* ── the join: convergence on input that is wrong ─────────────────────────── */

void a_node_of_the_wrong_kind_does_not_split_the_mesh() {
    g_current = "a_node_of_the_wrong_kind_does_not_split_the_mesh";
    /* A malformed `present` on one peer used to make join(a,b) keep a and
     * join(b,a) keep b — two peers that never converged again. */
    Doc a = enriched(kState, "A");
    Doc b = enriched(kState, "B");
    cJSON* m = cJSON_GetObjectItem(cJSON_GetObjectItem(a.root, "mantles"), "m");
    cJSON_ReplaceItemInObject(m, "present", cJSON_CreateNumber(7));
    check(canon_doc(join(a, b)) == canon_doc(join(b, a)), "commutative on a kind mismatch");

    Doc c = enriched(kState, "C");
    cJSON* mc = cJSON_GetObjectItem(cJSON_GetObjectItem(c.root, "mantles"), "m");
    cJSON_ReplaceItemInObject(mc, "present", cJSON_CreateString("nonsense"));
    check(canon_doc(join(join(a, b), c)) == canon_doc(join(a, join(b, c))),
          "associative across three kinds");
    check(canon_doc(join(a, a)) == canon_doc(join(join(a, a), a)), "idempotent");
}

void an_orset_of_the_wrong_shape_does_not_crash_the_reader() {
    g_current = "an_orset_of_the_wrong_shape_does_not_crash_the_reader";
    Doc d;
    d.root = cJSON_Parse(
        "{\"palabra\":1,\"mantles\":{\"m\":{\"present\":{\"a\":[1,2],\"r\":[3]},"
        "\"fields\":{\"id\":{\"a\":{\"t1\":\"ZZ\",\"t2\":\"0\"},\"r\":[]}},"
        "\"runes\":{},\"edges\":{\"a\":{},\"r\":[]}}}}");
    Doc flat = flatten(d);  // used to crash: a std::string built from a null key
    check(flat.root != nullptr, "flatten returned");
}

/* ── an honest user's names ───────────────────────────────────────────────── */

void mantles_named_a_and_r_survive_a_merge() {
    g_current = "mantles_named_a_and_r_survive_a_merge";
    /* The one here that needed no attacker. `a` and `r` are the member names of an
     * OrSet, and the `mantles` map of a user who named two mantles that way was
     * read as one — the merge produced ZERO mantles. */
    Doc d1 = enriched("{\"mantles\":[{\"name\":\"a\",\"runes\":[]},{\"name\":\"r\",\"runes\":[]}]}", "A");
    Doc d2 = enriched("{\"mantles\":[{\"name\":\"a\",\"runes\":[]},{\"name\":\"r\",\"runes\":[]},"
                      "{\"name\":\"z\",\"runes\":[]}]}", "B");
    Doc merged = flatten(join(d1, d2));
    check(cJSON_GetArraySize(cJSON_GetObjectItem(merged.root, "mantles")) == 3,
          "three mantles in, three mantles out");
}

void glyphs_named_a_and_r_survive_a_merge() {
    g_current = "glyphs_named_a_and_r_survive_a_merge";
    Doc d1 = enriched("{\"mantles\":[],\"glyphs\":{\"a\":{\"glyph\":\"a\"},\"r\":{\"glyph\":\"r\"}}}", "A");
    Doc d2 = enriched("{\"mantles\":[],\"glyphs\":{\"a\":{\"glyph\":\"a\"},\"r\":{\"glyph\":\"r\"},"
                      "\"z\":{\"glyph\":\"z\"}}}", "B");
    Doc merged = flatten(join(d1, d2));
    check(cJSON_GetArraySize(cJSON_GetObjectItem(merged.root, "glyphs")) == 3,
          "three declarations in, three out");
}

/* ── the door ─────────────────────────────────────────────────────────────── */

/* Take a valid enriched document, damage it one way, and require refusal. */
void refused_after(const char* what, const std::function<void(cJSON*)>& damage) {
    Doc d = enriched(kState, "P");
    std::string why;
    check(validate(d, &why), std::string("the undamaged document is valid for: ") + what + " " + why);
    damage(d.root);
    check(!validate(d, &why), std::string("refused: ") + what);
}

cJSON* mantle_m(cJSON* root) {
    return cJSON_GetObjectItem(cJSON_GetObjectItem(root, "mantles"), "m");
}
cJSON* rune_1(cJSON* root) {
    return cJSON_GetObjectItem(cJSON_GetObjectItem(mantle_m(root), "runes"), "rune_1");
}

void a_bad_document_is_refused_at_the_door() {
    g_current = "a_bad_document_is_refused_at_the_door";
    refused_after("a node of the wrong kind", [](cJSON* r) {
        cJSON_ReplaceItemInObject(mantle_m(r), "present", cJSON_CreateNumber(7));
    });
    refused_after("an OrSet whose adds are an array", [](cJSON* r) {
        cJSON* p = cJSON_GetObjectItem(mantle_m(r), "present");
        cJSON_ReplaceItemInObject(p, "a", cJSON_CreateArray());
    });
    refused_after("an OrSet with a third member", [](cJSON* r) {
        cJSON_AddNumberToObject(cJSON_GetObjectItem(mantle_m(r), "edges"), "x", 1);
    });
    refused_after("uppercase hex", [](cJSON* r) {
        cJSON* body = cJSON_GetObjectItem(cJSON_GetObjectItem(rune_1(r), "fields"), "content.body");
        cJSON* a = cJSON_GetObjectItem(body, "a");
        cJSON_ReplaceItemInObject(a, a->child->string, cJSON_CreateString("05024A4B"));
    });
    refused_after("a value spelled non-canonically", [](cJSON* r) {
        cJSON* body = cJSON_GetObjectItem(cJSON_GetObjectItem(rune_1(r), "fields"), "content.body");
        cJSON* a = cJSON_GetObjectItem(body, "a");
        cJSON_ReplaceItemInObject(a, a->child->string, cJSON_CreateString("043ff0000000000000"));
    });
    refused_after("a field that is not in the shape", [](cJSON* r) {
        cJSON_AddItemToObject(cJSON_GetObjectItem(rune_1(r), "fields"), "secret",
                              cJSON_Parse("{\"a\":{},\"r\":[]}"));
    });
    refused_after("an unknown root member", [](cJSON* r) {
        cJSON_AddNumberToObject(r, "extra", 1);
    });
    refused_after("a newer document shape", [](cJSON* r) {
        cJSON_ReplaceItemInObject(r, "palabra", cJSON_CreateNumber(2));
    });
    refused_after("an empty tag", [](cJSON* r) {
        cJSON* p = cJSON_GetObjectItem(mantle_m(r), "present");
        cJSON_AddItemToArray(cJSON_GetObjectItem(p, "r"), cJSON_CreateString(""));
    });
    refused_after("a glyph declaration that is not a map", [](cJSON* r) {
        cJSON* g = cJSON_CreateObject();
        cJSON* fields = cJSON_CreateObject();
        cJSON_AddItemToObject(fields, "descriptor", cJSON_Parse("{\"a\":{\"t\":\"05026869\"},\"r\":[]}"));
        cJSON_AddItemToObject(g, "fields", fields);
        cJSON* gs = cJSON_CreateObject();
        cJSON_AddItemToObject(gs, "stat", g);
        cJSON_ReplaceItemInObject(r, "glyphs", gs);
    });
    refused_after("a member named twice", [](cJSON* r) {
        /* cJSON reads the first, other parsers read the last: two peers holding this
         * document would disagree about what is in it. */
        cJSON_AddItemToObject(r, "mantles", cJSON_CreateObject());
    });
}

void a_delta_is_a_valid_document() {
    g_current = "a_delta_is_a_valid_document";
    /* A delta has most of its parts missing. Refusing it would make the door refuse
     * the thing peers most often send. */
    Json partial("{\"palabra\":1,\"mantles\":{\"m\":{\"runes\":{\"rune_1\":{\"fields\":"
                 "{\"content.body\":{\"a\":{\"B_9\":\"05026869\"},\"r\":[\"P_7\"]}}}}}}}");
    std::string why;
    check(validate(partial.p, &why), "a one-field delta passes: " + why);
}

}  // namespace

int main() {
    a_count_larger_than_the_input_is_refused();
    running_out_of_input_is_a_failure_not_a_null();
    nesting_is_bounded();
    malformed_bytes_are_refused_rather_than_guessed();
    two_spellings_of_one_value_are_not_both_canonical();

    a_node_of_the_wrong_kind_does_not_split_the_mesh();
    an_orset_of_the_wrong_shape_does_not_crash_the_reader();

    mantles_named_a_and_r_survive_a_merge();
    glyphs_named_a_and_r_survive_a_merge();

    a_bad_document_is_refused_at_the_door();
    a_delta_is_a_valid_document();

    std::printf("hostile: %d checks", g_checks);
    if (g_failures.empty()) {
        std::printf(", all passed\n");
        return 0;
    }
    std::printf(", %d FAILED\n", static_cast<int>(g_failures.size()));
    for (const std::string& f : g_failures) std::printf("  - %s\n", f.c_str());
    return 1;
}
