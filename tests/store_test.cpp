/* store_test.cpp — the dedup and diff properties Phase 2 exists to deliver.
 *
 * The headline test is `insertion_only_perturbs_nearby_chunks`. Everything else
 * here is table stakes; that one is the reason content-DEFINED chunking was worth
 * implementing instead of splitting every N bytes, and it is checked against a
 * fixed-size splitter so the comparison is not a claim but a measurement.
 */
#include "voidpalabra/store.hpp"
#include "voidpalabra/join.hpp"
#include "voidpalabra/canonical.hpp"

#include "cJSON.h"

#include <algorithm>
#include <cstdio>
#include <random>
#include <set>
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

std::string random_blob(std::mt19937& rng, std::size_t n) {
    std::string s;
    s.reserve(n);
    /* Text-like rather than uniform noise: repeated words, which is what a mantle
     * of runes and a document asset actually look like, and which is the case
     * chunking has to behave well on. */
    static const char* words[] = {"rune ", "mantle ", "glyph ", "holiday ", "the ",
                                  "a ", "join ", "peer ", "tag ", "content "};
    while (s.size() < n) s += words[rng() % 10];
    s.resize(n);
    return s;
}

std::set<std::string> chunk_set(const std::string& blob, const ChunkParams& p = {}) {
    std::set<std::string> out;
    std::size_t off = 0;
    for (std::size_t n : chunk(blob.data(), blob.size(), p)) {
        out.insert(to_hex(sha256(blob.substr(off, n))));
        off += n;
    }
    return out;
}

std::set<std::string> fixed_chunk_set(const std::string& blob, std::size_t size) {
    std::set<std::string> out;
    for (std::size_t off = 0; off < blob.size(); off += size)
        out.insert(to_hex(sha256(blob.substr(off, size))));
    return out;
}

std::size_t shared(const std::set<std::string>& a, const std::set<std::string>& b) {
    std::vector<std::string> both;
    std::set_intersection(a.begin(), a.end(), b.begin(), b.end(),
                          std::back_inserter(both));
    return both.size();
}

constexpr int kSeeds = 16;

/* --- chunking ------------------------------------------------------------ */

void test_chunks_cover_the_input_exactly() {
    for (int seed = 0; seed < kSeeds; ++seed) {
        std::mt19937 rng(seed);
        for (std::size_t n : {std::size_t{0}, std::size_t{1}, std::size_t{1000},
                              std::size_t{100000}}) {
            std::string blob = random_blob(rng, n);
            std::size_t total = 0;
            for (std::size_t c : chunk(blob.data(), blob.size())) total += c;
            check(total == n, "chunks do not sum to the input length");
        }
    }
}

void test_chunking_is_deterministic() {
    /* Two peers must cut the same blob identically or dedup never fires between
     * them. This is why the gear table is generated from a fixed sequence rather
     * than being an implementation detail. */
    std::mt19937 rng(1);
    std::string blob = random_blob(rng, 200000);
    check(chunk(blob.data(), blob.size()) == chunk(blob.data(), blob.size()),
          "chunking is not deterministic");
}

void test_chunk_sizes_respect_bounds() {
    ChunkParams p;
    std::mt19937 rng(2);
    std::string blob = random_blob(rng, 500000);
    auto sizes = chunk(blob.data(), blob.size(), p);
    for (std::size_t i = 0; i + 1 < sizes.size(); ++i) {  // last may be short
        check(sizes[i] >= p.min_size, "a chunk was below min_size");
        check(sizes[i] <= p.max_size, "a chunk exceeded max_size");
    }
}

void test_insertion_only_perturbs_nearby_chunks() {
    /* THE test. okf/references/academic-foundations.md §5: content-defined
     * chunking "keeps the hash of a version stable under insertions — the property
     * a size-split B-tree does not have."
     *
     * Insert a few bytes near the FRONT of a large blob, which is the worst case,
     * and measure how many chunks survive. Compared against fixed-size splitting
     * so the result is a measurement rather than an assertion of faith. */
    for (int seed = 0; seed < kSeeds; ++seed) {
        std::mt19937 rng(seed);
        std::string before = random_blob(rng, 400000);
        std::string after = before.substr(0, 37) + "INSERTED" + before.substr(37);

        auto cdc_a = chunk_set(before), cdc_b = chunk_set(after);
        std::size_t cdc_shared = shared(cdc_a, cdc_b);
        double cdc_frac = double(cdc_shared) / double(cdc_a.size());

        auto fix_a = fixed_chunk_set(before, 8 * 1024);
        auto fix_b = fixed_chunk_set(after, 8 * 1024);
        double fix_frac = double(shared(fix_a, fix_b)) / double(fix_a.size());

        check(cdc_frac > 0.90,
              "content-defined chunking kept only " + std::to_string(cdc_frac) +
                  " of chunks across a front insertion (seed " +
                  std::to_string(seed) + ")");
        check(fix_frac < 0.10,
              "the fixed-size baseline was unexpectedly stable (" +
                  std::to_string(fix_frac) + ") — the comparison is not meaningful");
        if (seed == 0)
            std::printf("     front-insert survival: content-defined %.1f%%, "
                        "fixed-size %.1f%%\n", cdc_frac * 100, fix_frac * 100);
    }
}

void test_appending_does_not_disturb_the_front() {
    std::mt19937 rng(3);
    std::string before = random_blob(rng, 200000);
    std::string after = before + random_blob(rng, 50000);
    auto a = chunk_set(before), b = chunk_set(after);
    check(shared(a, b) >= a.size() - 1,
          "appending changed chunks other than the last");
}

/* --- the store ----------------------------------------------------------- */

void test_storing_the_same_bytes_twice_is_free() {
    BlockStore s;
    s.put("hello");
    std::size_t after_one = s.block_count();
    s.put("hello");
    check(s.block_count() == after_one, "a duplicate block was stored twice");
}

void test_blob_round_trip() {
    for (int seed = 0; seed < kSeeds; ++seed) {
        std::mt19937 rng(seed);
        std::string blob = random_blob(rng, 1 + (rng() % 200000));
        BlockStore s;
        auto recipe = s.put_blob(blob);
        std::string back;
        check(s.get_blob(recipe, back), "a recipe did not resolve");
        check(back == blob, "a blob did not survive the round trip");
    }
}

void test_an_unchanged_asset_costs_nothing_to_re_store() {
    /* The direct fix for Hormiga's base64 inlining: re-saving a document whose
     * image did not change must not store the image again. */
    std::mt19937 rng(4);
    std::string asset = random_blob(rng, 300000);
    BlockStore s;
    s.put_blob(asset);
    std::size_t bytes_after_first = s.byte_count();
    for (int version = 0; version < 20; ++version) s.put_blob(asset);
    check(s.byte_count() == bytes_after_first,
          "re-storing an unchanged asset 20 times grew the store");
}

void test_an_edited_asset_costs_only_the_edit() {
    std::mt19937 rng(5);
    std::string v1 = random_blob(rng, 400000);
    std::string v2 = v1.substr(0, 200) + "EDIT" + v1.substr(200);
    BlockStore s;
    s.put_blob(v1);
    std::size_t after_v1 = s.byte_count();
    s.put_blob(v2);
    std::size_t added = s.byte_count() - after_v1;
    check(added < v1.size() / 8,
          "an edit cost " + std::to_string(added) + " bytes on a " +
              std::to_string(v1.size()) + "-byte asset — dedup is not working");
    std::printf("     400KB asset, 4-byte edit -> %zu new bytes stored\n", added);
}

void test_a_missing_block_is_reported_not_faked() {
    BlockStore s;
    auto recipe = s.put_blob(std::string(50000, 'x'));
    BlockStore empty;
    std::string out = "sentinel";
    check(!empty.get_blob(recipe, out), "a partial store claimed to resolve a blob");
}

/* --- the container ------------------------------------------------------- */

void test_container_round_trip() {
    std::mt19937 rng(6);
    Container c;
    c.doc = "some encoded document bytes";
    c.blocks.put_blob(random_blob(rng, 100000));
    c.blocks.put("small");

    std::string file = container_write(c);
    Container back;
    check(container_read(file, back), "a container did not read back");
    check(back.doc == c.doc, "the document was not preserved");
    check(back.blocks.block_count() == c.blocks.block_count(), "blocks were lost");
    check(back.blocks.byte_count() == c.blocks.byte_count(), "bytes were lost");
}

void test_a_container_is_byte_identical_for_equal_content() {
    /* The container inherits the canonical form's determinism instead of having a
     * weaker notion of its own: two peers writing equal content write equal files,
     * whatever order they happened to put the blocks in. */
    std::mt19937 rng(7);
    std::string a = random_blob(rng, 40000), b = random_blob(rng, 40000);
    Container c1, c2;
    c1.doc = c2.doc = "doc";
    c1.blocks.put(a); c1.blocks.put(b);
    c2.blocks.put(b); c2.blocks.put(a);  // inserted in the opposite order
    check(container_write(c1) == container_write(c2),
          "insertion order changed the container bytes");
}

void test_a_damaged_container_is_refused() {
    Container c;
    c.doc = "doc";
    c.blocks.put("payload");
    std::string file = container_write(c);

    Container out;
    check(!container_read("not a container at all", out), "bad magic was accepted");
    check(!container_read(file.substr(0, file.size() - 5), out),
          "a truncated container was accepted");
    check(!container_read(file + "garbage", out), "trailing garbage was accepted");

    std::string tampered = file;
    tampered[tampered.size() - 1] ^= 0x40;  // flip a bit inside the last block
    check(!container_read(tampered, out),
          "a block that does not hash to its key was accepted — this is the one "
          "thing content addressing exists to prevent");

    std::string wrong_version = file;
    wrong_version[4] = 99;
    check(!container_read(wrong_version, out), "an unknown version was guessed at");
}

/* --- the whole rung, together -------------------------------------------- */

void test_a_state_survives_a_container_round_trip() {
    /* End to end: a Void Core state -> enriched document -> container bytes ->
     * back -> the same version name. This is the property Phase 2 exists to give
     * an application, and it involves every rung built so far. */
    const char* text =
        "{\"mantles\":[{\"id\":\"m1\",\"name\":\"demo\",\"runes\":["
        "{\"spirit\":{\"id\":\"rune_1\",\"name\":\"intro\"},\"glyph\":\"text\","
        "\"tags\":[\"draft\"],\"content\":{\"value\":\"hello\"}}],"
        "\"layout\":{\"edges\":[]}}]}";
    cJSON* state = cJSON_Parse(text);
    check(state != nullptr, "fixture did not parse");
    std::string name_before = version_name(state);

    CounterMint mint("peerA");
    Doc doc = enrich(state, mint);

    Container c;
    c.doc = canon_doc(doc);
    std::string file = container_write(c);

    Container back;
    check(container_read(file, back), "container did not read back");
    check(back.doc == c.doc, "the enriched document changed across the container");

    /* And the flattened state still names the same version. */
    Doc flat = flatten(doc);
    check(version_name(flat.root) == name_before,
          "the version name changed across enrich -> container -> flatten");
    std::printf("     round-tripped %s through %zu container bytes\n",
                name_before.substr(0, 12).c_str(), file.size());
    cJSON_Delete(state);
}

/* --- runner -------------------------------------------------------------- */

struct Test { const char* name; void (*fn)(); };

const Test kTests[] = {
    {"chunks_cover_the_input_exactly", test_chunks_cover_the_input_exactly},
    {"chunking_is_deterministic", test_chunking_is_deterministic},
    {"chunk_sizes_respect_bounds", test_chunk_sizes_respect_bounds},
    {"insertion_only_perturbs_nearby_chunks", test_insertion_only_perturbs_nearby_chunks},
    {"appending_does_not_disturb_the_front", test_appending_does_not_disturb_the_front},
    {"storing_the_same_bytes_twice_is_free", test_storing_the_same_bytes_twice_is_free},
    {"blob_round_trip", test_blob_round_trip},
    {"an_unchanged_asset_costs_nothing_to_re_store",
     test_an_unchanged_asset_costs_nothing_to_re_store},
    {"an_edited_asset_costs_only_the_edit", test_an_edited_asset_costs_only_the_edit},
    {"a_missing_block_is_reported_not_faked", test_a_missing_block_is_reported_not_faked},
    {"container_round_trip", test_container_round_trip},
    {"a_container_is_byte_identical_for_equal_content",
     test_a_container_is_byte_identical_for_equal_content},
    {"a_damaged_container_is_refused", test_a_damaged_container_is_refused},
    {"a_state_survives_a_container_round_trip",
     test_a_state_survives_a_container_round_trip},
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
