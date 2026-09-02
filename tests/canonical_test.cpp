/* canonical_test.cpp — property tests for the canonical form.
 *
 * The suite has two halves, and the second is the one people forget:
 *
 *  1. INVARIANCE — permute everything meaningless and assert the bytes do not
 *     move. This is the whole point of Rung 0.
 *  2. SENSITIVITY — change anything meaningful and assert the bytes DO move. A
 *     canonicalizer that hashes everything to one value passes half 1 perfectly
 *     and is worthless. Half 2 is what makes half 1 mean something.
 *
 * Hand-rolled with seeded std::mt19937 rather than a test framework, so Palabra
 * stays zero-dependency like Void Core's core.
 */
#include "voidpalabra/canonical.hpp"

#include "cJSON.h"

#include <algorithm>
#include <cstdio>
#include <functional>
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

/* --- a tiny cJSON helper so the fixtures read like the state document ----- */

struct Json {
    cJSON* p = nullptr;
    explicit Json(const std::string& text) : p(cJSON_Parse(text.c_str())) {}
    ~Json() { if (p) cJSON_Delete(p); }
    Json(const Json&) = delete;
    Json& operator=(const Json&) = delete;
    const cJSON* get() const { return p; }
};

std::string hex_of(const Digest& d) { return to_hex(d); }

/* --- fixtures ------------------------------------------------------------ */

std::string quote(const std::string& s) { return "\"" + s + "\""; }

std::string join(const std::vector<std::string>& v, const char* sep = ",") {
    std::string out;
    for (std::size_t i = 0; i < v.size(); ++i) { if (i) out += sep; out += v[i]; }
    return out;
}

struct Fixture {
    std::vector<std::string> runes;   // JSON text per rune
    std::vector<std::string> edges;   // JSON text per edge
};

Fixture make_fixture(std::mt19937& rng, int n_runes) {
    static const char* glyphs[] = {"text", "image", "richtext", "group"};
    static const char* all_tags[] = {"science", "draft", "month:june", "event"};
    Fixture f;
    std::vector<std::string> names;
    for (int i = 0; i < n_runes; ++i) {
        std::string name = "r" + std::to_string(i);
        names.push_back(name);
        std::vector<std::string> tags;
        for (int t = 0; t < 4; ++t)
            if (rng() % 2) tags.push_back(quote(all_tags[t]));
        f.runes.push_back(
            "{\"spirit\":{\"id\":\"rune_" + std::to_string(1000 + i) +
            "\",\"name\":" + quote(name) + "},\"glyph\":" +
            quote(glyphs[rng() % 4]) + ",\"facets\":{\"who\":\"m\",\"what\":\"w" +
            std::to_string(i) + "\"},\"tags\":[" + join(tags) +
            "],\"content\":{\"value\":\"body" + std::to_string(i) +
            "\",\"images\":[\"a.png\",\"b.png\"],\"n\":" +
            std::to_string(rng() % 100) + "},\"placement\":" +
            (rng() % 2 ? "null" : "{\"x\":1,\"y\":2}") + "}");
    }
    int n_edges = static_cast<int>(rng() % 5);
    for (int e = 0; e < n_edges && names.size() >= 2; ++e) {
        const std::string& a = names[rng() % names.size()];
        const std::string& b = names[rng() % names.size()];
        f.edges.push_back("{\"from\":" + quote(a) + ",\"to\":" + quote(b) +
                          ",\"relation\":\"supports\",\"weight\":1.0,\"directed\":" +
                          (rng() % 2 ? "true" : "false") + "}");
    }
    return f;
}

std::string mantle_text(const Fixture& f, const std::string& name = "demo") {
    return "{\"id\":\"mantle_1\",\"name\":" + quote(name) +
           ",\"domain\":null,\"runes\":[" + join(f.runes) +
           "],\"tags\":{},\"layout\":{\"edges\":[" + join(f.edges) +
           "]},\"rules\":[]}";
}

Digest hash_mantle_text(const std::string& text, const Policy& p = {}) {
    Json j(text);
    if (!j.get()) throw CanonicalError("fixture did not parse: " + text);
    return mantle_hash(j.get(), p);
}

Digest hash_rune_text(const std::string& text, const Policy& p = {}) {
    Json j(text);
    if (!j.get()) throw CanonicalError("fixture did not parse: " + text);
    return rune_hash(j.get(), p);
}

Digest hash_slice_text(const std::string& text, const Policy& p = {}) {
    Json j(text);
    if (!j.get()) throw CanonicalError("fixture did not parse: " + text);
    return slice_hash(j.get(), p);
}

/* "cafe" with an acute: U+00E9 composed, vs "e" + U+0301 decomposed. */
std::string unicodedata_nfc() { return "caf\xc3\xa9"; }
std::string unicodedata_nfd() { return "cafe\xcc\x81"; }

bool throws_canonical(const std::function<void()>& fn) {
    try { fn(); } catch (const CanonicalError&) { return true; } catch (...) {}
    return false;
}

constexpr int kSeeds = 64;

/* --- 1. invariance ------------------------------------------------------- */

void test_rune_order_is_not_information() {
    for (int seed = 0; seed < kSeeds; ++seed) {
        std::mt19937 rng(seed);
        Fixture f = make_fixture(rng, 6);
        Digest base = hash_mantle_text(mantle_text(f));
        for (int k = 0; k < 5; ++k) {
            Fixture g = f;
            std::shuffle(g.runes.begin(), g.runes.end(), rng);
            check(hash_mantle_text(mantle_text(g)) == base,
                  "shuffling runes moved the hash (seed " + std::to_string(seed) + ")");
        }
    }
}

void test_edge_order_is_not_information() {
    for (int seed = 0; seed < kSeeds; ++seed) {
        std::mt19937 rng(seed);
        Fixture f = make_fixture(rng, 6);
        Digest base = hash_mantle_text(mantle_text(f));
        for (int k = 0; k < 5; ++k) {
            Fixture g = f;
            std::shuffle(g.edges.begin(), g.edges.end(), rng);
            check(hash_mantle_text(mantle_text(g)) == base, "shuffling edges moved it");
        }
    }
}

void test_tag_order_is_not_information() {
    const char* a = "{\"spirit\":{\"id\":\"r\",\"name\":\"a\"},"
                    "\"tags\":[\"science\",\"draft\",\"event\"]}";
    const char* b = "{\"spirit\":{\"id\":\"r\",\"name\":\"a\"},"
                    "\"tags\":[\"event\",\"science\",\"draft\"]}";
    check(hash_rune_text(a) == hash_rune_text(b), "tag order changed the hash");
}

void test_key_order_is_not_information() {
    const char* a = "{\"spirit\":{\"id\":\"r\",\"name\":\"a\"},\"glyph\":\"text\","
                    "\"content\":{\"x\":1,\"y\":2}}";
    const char* b = "{\"content\":{\"y\":2,\"x\":1},\"glyph\":\"text\","
                    "\"spirit\":{\"name\":\"a\",\"id\":\"r\"}}";
    check(hash_rune_text(a) == hash_rune_text(b), "key insertion order changed it");
}

void test_mantle_order_is_not_information() {
    std::string m1 = "{\"id\":\"a\",\"name\":\"one\"}";
    std::string m2 = "{\"id\":\"b\",\"name\":\"two\"}";
    check(hash_slice_text("{\"mantles\":[" + m1 + "," + m2 + "]}") ==
              hash_slice_text("{\"mantles\":[" + m2 + "," + m1 + "]}"),
          "mantle order changed the version name");
}

void test_arrival_order_is_not_information() {
    /* The real scenario: two peers receive the same runes in opposite orders.
     * This is the property the whole architecture rests on
     * (okf/concepts/version-as-cut.md, "order-independent"). */
    for (int seed = 0; seed < kSeeds; ++seed) {
        std::mt19937 rng(seed);
        Fixture source = make_fixture(rng, 8);
        Fixture a, b;
        for (const auto& r : source.runes) a.runes.push_back(r);
        for (auto it = source.runes.rbegin(); it != source.runes.rend(); ++it)
            b.runes.push_back(*it);
        a.edges = source.edges;
        b.edges = source.edges;
        check(hash_mantle_text(mantle_text(a)) == hash_mantle_text(mantle_text(b)),
              "two peers with opposite arrival orders disagreed (seed " +
                  std::to_string(seed) + ")");
    }
}

void test_hydration_is_invisible() {
    const char* partial = "{\"spirit\":{\"id\":\"rune_1\",\"name\":\"a\"},"
                          "\"glyph\":\"text\"}";
    const char* full =
        "{\"spirit\":{\"id\":\"rune_1\",\"name\":\"a\"},\"glyph\":\"text\","
        "\"facets\":{\"who\":\"\",\"what\":\"\",\"when\":\"\",\"where\":\"\","
        "\"why\":\"\",\"how\":\"\"},\"tags\":[],\"content\":{},"
        "\"placement\":null,\"relations\":[]}";
    check(hash_rune_text(partial) == hash_rune_text(full),
          "SPEC §3.2 defaults are not invisible");
}

void test_edge_defaults_are_invisible() {
    std::string bare = "{\"id\":\"m\",\"name\":\"n\",\"layout\":{\"edges\":"
                       "[{\"from\":\"a\",\"to\":\"b\"}]}}";
    std::string full = "{\"id\":\"m\",\"name\":\"n\",\"layout\":{\"edges\":"
                       "[{\"from\":\"a\",\"to\":\"b\",\"relation\":\"\","
                       "\"weight\":1.0,\"directed\":true}]}}";
    check(hash_mantle_text(bare) == hash_mantle_text(full),
          "SPEC §3.7 edge defaults are not invisible");
}

void test_integral_floats_equal_ints() {
    /* 1 and 1.0 survive a JSON round-trip as the same value, so they must hash
     * the same. cJSON stores every number as a double, so this also protects
     * against a host that wrote the literal differently. */
    auto m = [](const char* w) {
        return std::string("{\"id\":\"m\",\"name\":\"n\",\"layout\":{\"edges\":"
                           "[{\"from\":\"a\",\"to\":\"b\",\"weight\":") + w + "}]}}";
    };
    check(hash_mantle_text(m("1")) == hash_mantle_text(m("1.0")), "1 != 1.0");
    check(hash_mantle_text(m("0")) == hash_mantle_text(m("-0.0")), "0 != -0.0");
    check(hash_mantle_text(m("1")) != hash_mantle_text(m("1.5")), "1 == 1.5");
}

void test_undirected_edges_are_symmetric() {
    auto m = [](const char* from, const char* to) {
        return std::string("{\"id\":\"m\",\"name\":\"n\",\"layout\":{\"edges\":[{"
                           "\"from\":\"") + from + "\",\"to\":\"" + to +
               "\",\"directed\":false}]}}";
    };
    check(hash_mantle_text(m("a", "b")) == hash_mantle_text(m("b", "a")),
          "an undirected wire is not the same read from either end");
}

void test_peer_local_fields_do_not_reach_the_hash() {
    /* The domains ruling, enforced. okf/concepts/utterance.md: a domain carries
     * real deploy commands, so it is peer-local and never syncs. If it reached
     * the version name, two peers with identical content but different deploy
     * targets would fail to converge. */
    std::string mantles = "\"mantles\":[{\"id\":\"m\",\"name\":\"n\"}]";
    std::string a = "{" + mantles +
                    ",\"domains\":{\"d\":{\"name\":\"d\",\"deploy\":\"rm -rf /\"}},"
                    "\"bindings\":[],\"config\":{\"actor\":\"miguel\"},"
                    "\"active\":{\"mantle\":\"n\"},\"scripts\":{},\"_baseline\":[]}";
    std::string b = "{" + mantles +
                    ",\"domains\":{\"d\":{\"name\":\"d\",\"deploy\":\"echo other\"}},"
                    "\"bindings\":[{\"id\":\"bind_1\"}],"
                    "\"config\":{\"actor\":\"someone-else\"},"
                    "\"active\":{\"mantle\":null},\"scripts\":{\"x\":\"ls\"},"
                    "\"_baseline\":[{\"id\":\"z\",\"name\":\"z\"}]}";
    check(hash_slice_text(a) == hash_slice_text(b),
          "peer-local state reached the version name");
}

/* --- 2. sensitivity ------------------------------------------------------ */
/* Without these, everything above is satisfied by `return ""`. */

void test_every_meaningful_field_moves_the_hash() {
    const std::string base =
        "{\"spirit\":{\"id\":\"rune_1\",\"name\":\"a\"},\"glyph\":\"text\","
        "\"facets\":{\"who\":\"m\"},\"tags\":[\"science\"],"
        "\"content\":{\"value\":\"hello\"},\"placement\":{\"x\":1,\"y\":2}}";
    Digest b = hash_rune_text(base);
    struct { const char* label; const char* text; } muts[] = {
        {"spirit.id", "{\"spirit\":{\"id\":\"rune_2\",\"name\":\"a\"},\"glyph\":\"text\",\"facets\":{\"who\":\"m\"},\"tags\":[\"science\"],\"content\":{\"value\":\"hello\"},\"placement\":{\"x\":1,\"y\":2}}"},
        {"spirit.name", "{\"spirit\":{\"id\":\"rune_1\",\"name\":\"b\"},\"glyph\":\"text\",\"facets\":{\"who\":\"m\"},\"tags\":[\"science\"],\"content\":{\"value\":\"hello\"},\"placement\":{\"x\":1,\"y\":2}}"},
        {"glyph", "{\"spirit\":{\"id\":\"rune_1\",\"name\":\"a\"},\"glyph\":\"image\",\"facets\":{\"who\":\"m\"},\"tags\":[\"science\"],\"content\":{\"value\":\"hello\"},\"placement\":{\"x\":1,\"y\":2}}"},
        {"facet", "{\"spirit\":{\"id\":\"rune_1\",\"name\":\"a\"},\"glyph\":\"text\",\"facets\":{\"who\":\"other\"},\"tags\":[\"science\"],\"content\":{\"value\":\"hello\"},\"placement\":{\"x\":1,\"y\":2}}"},
        {"tag added", "{\"spirit\":{\"id\":\"rune_1\",\"name\":\"a\"},\"glyph\":\"text\",\"facets\":{\"who\":\"m\"},\"tags\":[\"science\",\"draft\"],\"content\":{\"value\":\"hello\"},\"placement\":{\"x\":1,\"y\":2}}"},
        {"tag removed", "{\"spirit\":{\"id\":\"rune_1\",\"name\":\"a\"},\"glyph\":\"text\",\"facets\":{\"who\":\"m\"},\"tags\":[],\"content\":{\"value\":\"hello\"},\"placement\":{\"x\":1,\"y\":2}}"},
        {"content value", "{\"spirit\":{\"id\":\"rune_1\",\"name\":\"a\"},\"glyph\":\"text\",\"facets\":{\"who\":\"m\"},\"tags\":[\"science\"],\"content\":{\"value\":\"goodbye\"},\"placement\":{\"x\":1,\"y\":2}}"},
        {"content key", "{\"spirit\":{\"id\":\"rune_1\",\"name\":\"a\"},\"glyph\":\"text\",\"facets\":{\"who\":\"m\"},\"tags\":[\"science\"],\"content\":{\"value\":\"hello\",\"extra\":1},\"placement\":{\"x\":1,\"y\":2}}"},
        {"placement", "{\"spirit\":{\"id\":\"rune_1\",\"name\":\"a\"},\"glyph\":\"text\",\"facets\":{\"who\":\"m\"},\"tags\":[\"science\"],\"content\":{\"value\":\"hello\"},\"placement\":{\"x\":9,\"y\":2}}"},
        {"relations", "{\"spirit\":{\"id\":\"rune_1\",\"name\":\"a\"},\"glyph\":\"text\",\"facets\":{\"who\":\"m\"},\"tags\":[\"science\"],\"content\":{\"value\":\"hello\"},\"placement\":{\"x\":1,\"y\":2},\"relations\":[\"x\"]}"},
    };
    for (const auto& m : muts)
        check(hash_rune_text(m.text) != b,
              std::string("changing ") + m.label + " did not move the hash");
}

void test_content_list_order_is_information() {
    /* Void Core does not interpret content, so Palabra may not declare it
     * unordered. */
    const char* a = "{\"spirit\":{\"id\":\"r\",\"name\":\"a\"},"
                    "\"content\":{\"images\":[\"x\",\"y\"]}}";
    const char* b = "{\"spirit\":{\"id\":\"r\",\"name\":\"a\"},"
                    "\"content\":{\"images\":[\"y\",\"x\"]}}";
    check(hash_rune_text(a) != hash_rune_text(b), "content list order was discarded");
}

void test_directed_edges_are_not_symmetric() {
    auto m = [](const char* f, const char* t, const char* d) {
        return std::string("{\"id\":\"m\",\"name\":\"n\",\"layout\":{\"edges\":[{"
                           "\"from\":\"") + f + "\",\"to\":\"" + t +
               "\",\"directed\":" + d + "}]}}";
    };
    check(hash_mantle_text(m("a", "b", "true")) != hash_mantle_text(m("b", "a", "true")),
          "a directed edge was treated as symmetric");
    check(hash_mantle_text(m("a", "b", "true")) != hash_mantle_text(m("a", "b", "false")),
          "directedness itself was not information");
}

void test_duplicate_edges_collapse() {
    /* layout.edges is an OR-Set: a wire exists or it does not. Idempotence
     * (a ⊔ a = a) requires a duplicate to be free. */
    const char* one = "{\"id\":\"m\",\"name\":\"n\",\"layout\":{\"edges\":"
                      "[{\"from\":\"a\",\"to\":\"b\",\"relation\":\"supports\"}]}}";
    const char* twice = "{\"id\":\"m\",\"name\":\"n\",\"layout\":{\"edges\":"
                        "[{\"from\":\"a\",\"to\":\"b\",\"relation\":\"supports\"},"
                        "{\"from\":\"a\",\"to\":\"b\",\"relation\":\"supports\"}]}}";
    const char* distinct = "{\"id\":\"m\",\"name\":\"n\",\"layout\":{\"edges\":"
                           "[{\"from\":\"a\",\"to\":\"b\",\"relation\":\"supports\"},"
                           "{\"from\":\"a\",\"to\":\"b\",\"relation\":\"refutes\"}]}}";
    check(hash_mantle_text(one) == hash_mantle_text(twice), "a duplicate edge was not free");
    check(hash_mantle_text(one) != hash_mantle_text(distinct), "distinct edges collapsed");
}

void test_duplicate_tags_collapse() {
    const char* one = "{\"spirit\":{\"id\":\"r\",\"name\":\"a\"},\"tags\":[\"x\"]}";
    const char* twice = "{\"spirit\":{\"id\":\"r\",\"name\":\"a\"},"
                        "\"tags\":[\"x\",\"x\"]}";
    check(hash_rune_text(one) == hash_rune_text(twice), "a duplicate tag was not free");
}

void test_domain_separation() {
    Json r("{\"spirit\":{\"id\":\"x\",\"name\":\"x\"},\"glyph\":\"\"}");
    Json m("{\"id\":\"x\",\"name\":\"x\"}");
    Json s("{\"mantles\":[{\"id\":\"x\",\"name\":\"x\"}]}");
    check(rune_hash(r.get()) != mantle_hash(m.get()), "a rune collided with a mantle");
    check(slice_hash(s.get()) != mantle_hash(m.get()), "a slice collided with a mantle");
}

void test_type_confusion_is_impossible() {
    const char* vals[] = {"1", "\"1\"", "true", "null", "[]", "{}", "[1]", "{\"1\":1}"};
    std::vector<std::string> seen;
    for (const char* v : vals) {
        std::string t = std::string("{\"spirit\":{\"id\":\"r\",\"name\":\"a\"},"
                                    "\"content\":{\"v\":") + v + "}}";
        seen.push_back(hex_of(hash_rune_text(t)));
    }
    std::sort(seen.begin(), seen.end());
    seen.erase(std::unique(seen.begin(), seen.end()), seen.end());
    check(seen.size() == 8, "distinct JSON types produced colliding hashes");
}

void test_string_boundaries_cannot_be_forged() {
    /* Length prefixes, not delimiters: ["ab","c"] must not equal ["a","bc"]. */
    const char* a = "{\"spirit\":{\"id\":\"r\",\"name\":\"a\"},"
                    "\"content\":{\"v\":[\"ab\",\"c\"]}}";
    const char* b = "{\"spirit\":{\"id\":\"r\",\"name\":\"a\"},"
                    "\"content\":{\"v\":[\"a\",\"bc\"]}}";
    check(hash_rune_text(a) != hash_rune_text(b), "string boundaries were forgeable");
}

/* --- 3. policy, and the SPEC §4 ruling ----------------------------------- */

void test_order_insensitivity_is_not_optional() {
    /* SPEC §4, normative since 2026-07-27: "an order-sensitive consumer (a
     * canonical form, a content hash, a sync join) MUST be order-insensitive at
     * the Core level, because two peers who create the same runes in different
     * orders hold EQUAL state."
     *
     * There is deliberately no policy knob to turn this off. The test that used
     * to prove a `Seq` policy worked is gone with it — what replaced it is the
     * assertion that NO configuration makes rune order matter. */
    for (int seed = 0; seed < kSeeds; ++seed) {
        std::mt19937 rng(seed);
        Fixture f = make_fixture(rng, 5);
        Fixture rotated = f;
        std::rotate(rotated.runes.begin(), rotated.runes.begin() + 1,
                    rotated.runes.end());
        Policy with_view;
        Policy no_view; no_view.include_view = false;
        for (const Policy& p : {with_view, no_view})
            check(hash_mantle_text(mantle_text(f), p) ==
                      hash_mantle_text(mantle_text(rotated), p),
                  "some policy made rune order matter (seed " +
                      std::to_string(seed) + ")");
    }
}

void test_agrees_with_core_on_sha256() {
    /* The one point of byte-level agreement Palabra already has with Void Core,
     * reported by them 2026-07-27: `materialize --stamp` writes a `provenance`
     * value computed as canonical JSON -> SHA-256 -> first 16 hex, and
     * provenance({}) is pinned at 44136fa355b3678a in conformance/scry/ case 07.
     *
     * That is sha256("{}") truncated, so it is a two-line check that this
     * implementation's hash agrees with Core's before anything harder is
     * debugged. Core's encoding is not Palabra's — theirs is canonical JSON text
     * and does not normalize numbers, which is exactly the {"n":1} vs {"n":1.0}
     * hazard this encoder folds away — but the HASH underneath must be the same
     * function or nothing downstream can be compared at all. */
    check(to_hex(sha256(std::string("{}"))).substr(0, 16) == "44136fa355b3678a",
          "disagrees with VoidCore conformance/scry/07 on sha256(\"{}\")");
}

void test_without_view_ignores_placement_and_nothing_else() {
    Policy no_view; no_view.include_view = false;
    const char* at11 = "{\"spirit\":{\"id\":\"r\",\"name\":\"a\"},"
                       "\"content\":{\"v\":1},\"placement\":{\"x\":1,\"y\":1}}";
    const char* at99 = "{\"spirit\":{\"id\":\"r\",\"name\":\"a\"},"
                       "\"content\":{\"v\":1},\"placement\":{\"x\":9,\"y\":9}}";
    const char* edited = "{\"spirit\":{\"id\":\"r\",\"name\":\"a\"},"
                         "\"content\":{\"v\":2},\"placement\":{\"x\":1,\"y\":1}}";
    check(hash_rune_text(at11, no_view) == hash_rune_text(at99, no_view),
          "without_view still saw a move");
    check(hash_rune_text(at11) != hash_rune_text(at99),
          "the default policy ignored a move");
    check(hash_rune_text(at11, no_view) != hash_rune_text(edited, no_view),
          "without_view also ignored an edit");
}

/* --- 4. refusals --------------------------------------------------------- */

void test_a_rune_without_identity_is_refused() {
    const char* bad[] = {"{}", "{\"spirit\":{}}", "{\"spirit\":{\"name\":\"a\"}}"};
    for (const char* t : bad)
        check(throws_canonical([&] { hash_rune_text(t); }),
              "SPEC §3.2 requires rejecting a rune with no spirit.id");
}

void test_duplicate_mantle_names_are_refused() {
    check(throws_canonical([] {
              hash_slice_text("{\"mantles\":[{\"id\":\"a\",\"name\":\"dup\"},"
                              "{\"id\":\"b\",\"name\":\"dup\"}]}");
          }),
          "SPEC §3.4 requires mantle names unique in state.mantles");
}

void test_invalid_utf8_is_refused() {
    std::string bad = "{\"spirit\":{\"id\":\"r\",\"name\":\"x\"},\"glyph\":\"\"}";
    Json j(bad);
    // Splice an invalid byte directly past the parser.
    cJSON* glyph = cJSON_GetObjectItemCaseSensitive(j.p, "glyph");
    cJSON_SetValuestring(glyph, "\xC3\x28");  // truncated 2-byte sequence
    check(throws_canonical([&] { rune_hash(j.get()); }),
          "invalid UTF-8 was hashed instead of refused");
}

/* --- 5. SHA-256 correctness ---------------------------------------------- */

void test_sha256_matches_fips_vectors() {
    /* A hand-rolled hash must be proven, not assumed. FIPS 180-4 / NIST. */
    check(to_hex(sha256(std::string(""))) ==
              "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
          "SHA-256 of the empty string is wrong");
    check(to_hex(sha256(std::string("abc"))) ==
              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
          "SHA-256(\"abc\") is wrong");
    check(to_hex(sha256(std::string("abcdbcdecdefdefgefghfghighijhijkijkljklmklmn"
                                    "lmnomnopnopq"))) ==
              "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1",
          "SHA-256 of the 56-byte vector is wrong (padding boundary)");
    check(to_hex(sha256(std::string(1000000, 'a'))) ==
              "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0",
          "SHA-256 of a million 'a' is wrong (multi-block)");
    /* 64 bytes exactly — the block boundary the padding logic is easiest to get
     * wrong on. */
    check(to_hex(sha256(std::string(64, 'a'))) ==
              "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb",
          "SHA-256 of 64 bytes is wrong (exact block)");
}

/* --- 6. stability -------------------------------------------------------- */

void test_bytes_are_stable() {
    /* A pinned vector. If this changes, kCanonVersion must change with it.
     * Not decoration: it is the only check that catches an encoder change nobody
     * meant to make, which is exactly the failure that would split a live mesh. */
    const char* state =
        "{\"mantles\":[{\"id\":\"mantle_0001\",\"name\":\"demo\",\"domain\":null,"
        "\"runes\":[{\"spirit\":{\"id\":\"rune_0001\",\"name\":\"intro\"},"
        "\"glyph\":\"text\",\"tags\":[\"draft\",\"science\"],"
        "\"content\":{\"value\":\"hello\"}}],"
        "\"layout\":{\"edges\":[{\"from\":\"intro\",\"to\":\"methods\"}]}}]}";
    Json j(state);
    check(j.get() != nullptr, "the pinned fixture did not parse");
    std::string name = version_name(j.get());
    check(name.size() == 2 + 64, "a version name is 'v:' plus 64 hex chars");
    check(name.rfind("v:", 0) == 0, "a version name starts with v:");
    check(canon_slice(j.get()) == canon_slice(j.get()), "canon_slice is not stable");
    std::printf("     pinned version name: %s\n", name.c_str());
}


void test_nfc_is_a_precondition_with_a_detector() {
    /* Palabra does not normalize (SPEC §6.1) and the failure mode is silent
     * divergence, so the precondition ships with a diagnostic a host can run over
     * its own corpus. Hormiga's bilingual database is the case that hits this. */
    std::string nfc = unicodedata_nfc();
    std::string nfd = unicodedata_nfd();
    check(nfc != nfd, "the fixture must actually differ");
    check(!has_combining_marks(nfc), "composed text should not be flagged");
    check(has_combining_marks(nfd), "decomposed text should be flagged");

    /* Plain ASCII and composed Spanish are both clean. */
    check(!has_combining_marks("Campana de Regreso a Clases"), "ASCII flagged");
    check(!has_combining_marks("ni\xc3\xb1os del condado"), "composed n-tilde flagged");
    check(!has_combining_marks(""), "empty string flagged");

    /* And the reason it matters, demonstrated: the two forms hash differently. */
    auto r = [](const std::string& n) {
        return std::string("{\"spirit\":{\"id\":\"r1\",\"name\":\"") + n + "\"}}";
    };
    check(hash_rune_text(r(nfc)) != hash_rune_text(r(nfd)),
          "NFC and NFD must hash differently - that IS the hazard");
}

/* --- runner -------------------------------------------------------------- */

struct Test { const char* name; void (*fn)(); };

const Test kTests[] = {
    {"rune_order_is_not_information", test_rune_order_is_not_information},
    {"edge_order_is_not_information", test_edge_order_is_not_information},
    {"tag_order_is_not_information", test_tag_order_is_not_information},
    {"key_order_is_not_information", test_key_order_is_not_information},
    {"mantle_order_is_not_information", test_mantle_order_is_not_information},
    {"arrival_order_is_not_information", test_arrival_order_is_not_information},
    {"hydration_is_invisible", test_hydration_is_invisible},
    {"edge_defaults_are_invisible", test_edge_defaults_are_invisible},
    {"integral_floats_equal_ints", test_integral_floats_equal_ints},
    {"undirected_edges_are_symmetric", test_undirected_edges_are_symmetric},
    {"peer_local_fields_do_not_reach_the_hash",
     test_peer_local_fields_do_not_reach_the_hash},
    {"every_meaningful_field_moves_the_hash",
     test_every_meaningful_field_moves_the_hash},
    {"content_list_order_is_information", test_content_list_order_is_information},
    {"directed_edges_are_not_symmetric", test_directed_edges_are_not_symmetric},
    {"duplicate_edges_collapse", test_duplicate_edges_collapse},
    {"duplicate_tags_collapse", test_duplicate_tags_collapse},
    {"domain_separation", test_domain_separation},
    {"type_confusion_is_impossible", test_type_confusion_is_impossible},
    {"string_boundaries_cannot_be_forged", test_string_boundaries_cannot_be_forged},
    {"order_insensitivity_is_not_optional", test_order_insensitivity_is_not_optional},
    {"agrees_with_core_on_sha256", test_agrees_with_core_on_sha256},
    {"without_view_ignores_placement_and_nothing_else",
     test_without_view_ignores_placement_and_nothing_else},
    {"a_rune_without_identity_is_refused", test_a_rune_without_identity_is_refused},
    {"duplicate_mantle_names_are_refused", test_duplicate_mantle_names_are_refused},
    {"invalid_utf8_is_refused", test_invalid_utf8_is_refused},
    {"sha256_matches_fips_vectors", test_sha256_matches_fips_vectors},
    {"nfc_is_a_precondition_with_a_detector", test_nfc_is_a_precondition_with_a_detector},
    {"bytes_are_stable", test_bytes_are_stable},
};

}  // namespace

int main() {
    int failed_tests = 0;
    for (const Test& t : kTests) {
        g_current = t.name;
        std::size_t before = g_failures.size();
        try {
            t.fn();
        } catch (const std::exception& e) {
            g_failures.push_back(std::string(t.name) + ": threw: " + e.what());
        }
        bool ok = g_failures.size() == before;
        if (!ok) ++failed_tests;
        std::printf("%s %s\n", ok ? "ok  " : "FAIL", t.name);
    }
    for (const auto& f : g_failures) std::printf("  - %s\n", f.c_str());
    std::printf("\n%d/%zu tests passed (%d checks)\n",
                static_cast<int>(sizeof(kTests) / sizeof(kTests[0])) - failed_tests,
                sizeof(kTests) / sizeof(kTests[0]), g_checks);
    return failed_tests ? 1 : 0;
}
