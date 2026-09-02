/* archive_test.cpp — save/load and local version tracking, tested against the
 * workload Void Hormiga actually has: one device, one user, many saves of a slowly
 * changing document with a few large assets.
 *
* The headline test is `a_hundred_saves_cost_what_changed`. Everything else is
 * correctness; that one is whether the feature is usable at all, because "keep
 * every version" is only a good idea if keeping every version is cheap.
 */
#include "voidpalabra/archive.hpp"
#include "voidpalabra/canonical.hpp"

#include "cJSON.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
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

/* A document shaped like a Hormiga newsletter: many text runes in one mantle. */
std::string doc_text(int n_runes, int revision = 0) {
    std::string runes;
    for (int i = 0; i < n_runes; ++i) {
        if (i) runes += ",";
        runes += "{\"spirit\":{\"id\":\"rune_" + std::to_string(i) +
                 "\",\"name\":\"block" + std::to_string(i) +
                 "\"},\"glyph\":\"richtext\",\"tags\":[\"draft\"],"
                 "\"content\":{\"body\":\"Paragraph " + std::to_string(i) +
                 " of the newsletter, revision " +
                 std::to_string(i == 0 ? revision : 0) + ". " +
                 std::string(200, 'x') + "\"}}";
    }
    return "{\"mantles\":[{\"id\":\"m1\",\"name\":\"newsletter\",\"runes\":[" +
           runes + "],\"layout\":{\"edges\":[]}}]}";
}

bool file_exists(const std::string& p) {
    struct stat st;
    return ::stat(p.c_str(), &st) == 0;
}

std::string read_whole(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    std::ostringstream ss; ss << f.rdbuf();
    return ss.str();
}

std::string big_asset(std::mt19937& rng, std::size_t n) {
    std::string s;
    static const char* w[] = {"jpeg ", "data ", "pixel ", "chunk ", "image "};
    while (s.size() < n) s += w[rng() % 5];
    s.resize(n);
    return s;
}

/* --- saving and loading -------------------------------------------------- */

void test_save_then_load_returns_the_same_state() {
    Json state(doc_text(8));
    Archive a;
    std::string v = a.save(state.p, "first");
    check(v.rfind("v:", 0) == 0, "a save did not return a version name");
    cJSON* back = a.load(v);
    check(back != nullptr, "load returned nothing");
    if (back) {
        check(version_name(back) == v, "the loaded state has a different name");
        cJSON_Delete(back);
    }
}

void test_saving_the_same_state_twice_is_free() {
    /* Ctrl+S twice must cost nothing. Because the name is a hash of the content,
     * this is a comparison rather than a heuristic. */
    Json state(doc_text(8));
    Archive a;
    a.save(state.p, "one");
    std::size_t bytes = a.stored_bytes();
    std::size_t saves = a.saves().size();
    a.save(state.p, "two");
    check(a.stored_bytes() == bytes, "a redundant save stored bytes");
    check(a.saves().size() == saves, "a redundant save added an entry");
}

void test_every_past_version_is_reachable() {
    Archive a;
    std::vector<std::string> versions;
    for (int rev = 0; rev < 12; ++rev) {
        Json s(doc_text(10, rev));
        versions.push_back(a.save(s.p, "rev " + std::to_string(rev)));
    }
    check(a.saves().size() == 12, "not every save was recorded");
    for (std::size_t i = 0; i < versions.size(); ++i) {
        cJSON* back = a.load(versions[i]);
        check(back != nullptr, "an old version could not be loaded");
        if (back) {
            Json expect(doc_text(10, static_cast<int>(i)));
            check(version_name(back) == version_name(expect.p),
                  "version " + std::to_string(i) + " loaded the wrong state");
            cJSON_Delete(back);
        }
    }
}

void test_load_latest_is_the_most_recent_save() {
    Archive a;
    Json s0(doc_text(4, 0)), s1(doc_text(4, 1));
    a.save(s0.p, "a");
    std::string v1 = a.save(s1.p, "b");
    cJSON* back = a.load_latest();
    check(back && version_name(back) == v1, "load_latest is not the last save");
    if (back) cJSON_Delete(back);
}

void test_an_unknown_version_is_reported_not_faked() {
    Archive a;
    Json s(doc_text(3));
    a.save(s.p, "x");
    check(a.load("v:0000000000000000000000000000000000000000000000000000000000000000")
              == nullptr, "an unknown version returned something");
    check(!a.has("v:deadbeef"), "has() claimed an unknown version");
}

/* --- the cost properties ------------------------------------------------- */

void test_a_hundred_saves_cost_what_changed() {
    /* THE test. "Keep every version" is only usable if keeping every version is
     * cheap, so this measures the thing Void Hormiga would actually feel.
     *
     * The comparison that matters is against what it replaces. `.miga` v3 is a
     * whole-database bundle with assets inlined as base64, so N saves cost N x
     * (document + every asset). Palabra pays once for the shared parts. */
    Archive a;
    Json first(doc_text(60, 0));
    a.save(first.p, "rev 0");
    std::size_t one_save = a.stored_bytes();

    std::mt19937 rng(9);
    std::string image = big_asset(rng, 400000);
    a.put_asset("assets/header.png", image);

    for (int rev = 1; rev < 100; ++rev) {
        Json s(doc_text(60, rev));
        a.save(s.p, "rev " + std::to_string(rev));
        a.put_asset("assets/header.png", image);  // unchanged, as assets usually are
    }
    check(a.saves().size() == 100, "not all 100 saves were recorded");

    std::size_t palabra = a.to_bytes().size();
    std::size_t miga = 100 * (one_save + image.size() * 4 / 3);  // base64 inflates 4/3
    check(palabra * 20 < miga,
          "Palabra stored " + std::to_string(palabra) + " bytes where .miga would "
          "store " + std::to_string(miga) + " — the dedup win is not there");
    std::printf("     100 saves + a 400KB asset:  .miga ~%zu KB  ->  Palabra %zu KB "
                "(%.0fx smaller)\n",
                miga / 1024, palabra / 1024, double(miga) / double(palabra));

    /* State growth, asserted as a ceiling so it cannot silently regress.
     *
     * Structural chunking (one block per rune) took this from 11.3x to ~5x: an
     * edit to one rune now dirties that rune's block plus one small manifest
     * chunk, and CANNOT dirty another rune whatever the byte offsets do. The
     * remaining per-save cost is the manifest, which changes every time because
     * one key inside it moves. */
    double growth = double(a.stored_bytes() - image.size()) / double(one_save);
    check(growth < 7.0, "state growth regressed past the structural-chunking floor");
    std::size_t total = a.to_bytes().size();
    std::printf("     state alone: %.1fx for 100 revisions "
                "(one rune block + one manifest chunk per save)\n", growth);
    std::printf("     archive index: %zu KB of %zu KB total (recipes stored as hex)\n",
                (total - a.stored_bytes()) / 1024, total / 1024);
}

void test_an_asset_shared_across_saves_is_stored_once() {
    /* The direct replacement for `.miga`'s base64 inlining: an image that does not
     * change must not be re-stored on every save. */
    std::mt19937 rng(1);
    std::string image = big_asset(rng, 400000);
    Archive a;
    for (int rev = 0; rev < 25; ++rev) {
        Json s(doc_text(10, rev));
        a.save(s.p, "rev");
        a.put_asset("assets/header.png", image);  // same bytes every time
    }
    check(a.stored_bytes() < image.size() * 2,
          "25 saves with one unchanged asset cost " +
              std::to_string(a.stored_bytes()) + " bytes for a " +
              std::to_string(image.size()) + "-byte image");
    std::string back;
    check(a.get_asset("assets/header.png", back) && back == image,
          "the asset did not survive");
    std::printf("     400KB image across 25 saves -> %zu bytes stored\n",
                a.stored_bytes());
}

void test_two_names_for_the_same_bytes_cost_one_copy() {
    std::mt19937 rng(2);
    std::string bytes = big_asset(rng, 200000);
    Archive a;
    a.put_asset("assets/a.png", bytes);
    std::size_t after_one = a.stored_bytes();
    a.put_asset("assets/copy-of-a.png", bytes);
    check(a.stored_bytes() == after_one, "a duplicate asset was stored twice");
    check(a.asset_names().size() == 2, "both names should still exist");
}

/* --- persistence --------------------------------------------------------- */

void test_archive_round_trips_through_bytes() {
    std::mt19937 rng(3);
    Archive a;
    std::vector<std::string> versions;
    for (int rev = 0; rev < 5; ++rev) {
        Json s(doc_text(12, rev));
        versions.push_back(a.save(s.p, "rev " + std::to_string(rev)));
    }
    a.put_asset("assets/img.png", big_asset(rng, 150000));

    std::string file = a.to_bytes();
    Archive b;
    check(Archive::from_bytes(file, b), "an archive did not read back");
    check(b.saves().size() == a.saves().size(), "saves were lost");
    check(b.stored_bytes() == a.stored_bytes(), "blocks were lost");

    for (const std::string& v : versions) {
        cJSON* back = b.load(v);
        check(back != nullptr, "a version was unreachable after a round trip");
        if (back) { check(version_name(back) == v, "wrong state after reload"); cJSON_Delete(back); }
    }
    std::string img;
    check(b.get_asset("assets/img.png", img), "an asset was lost");
    check(b.saves()[2].label == "rev 2", "labels were lost");
}

void test_equal_archives_produce_equal_files() {
    Archive a, b;
    for (int rev = 0; rev < 4; ++rev) {
        Json s(doc_text(6, rev));
        a.save(s.p, "r");
        b.save(s.p, "r");
    }
    check(a.to_bytes() == b.to_bytes(), "equal archives wrote different files");
}

void test_a_damaged_archive_is_refused() {
    Archive a;
    Json s(doc_text(5));
    a.save(s.p, "only");
    std::string file = a.to_bytes();
    Archive out;
    check(!Archive::from_bytes("garbage", out), "garbage was accepted");
    std::string tampered = file;
    tampered[tampered.size() - 3] ^= 0x20;
    check(!Archive::from_bytes(tampered, out),
          "a tampered archive was accepted — blocks must verify against their keys");
}



/* --- the whole document, not just the versioned slice -------------------- */
/*
 * Reported by Void Hormiga 2026-08-21, measured against a real state document.
 * `save` stored `mantles` and nothing else, so `config` / `scripts` / `domains` /
 * `bindings` / `active` vanished on a round trip — and because the dedup guard
 * compared the version NAME, which is derived from `mantles` alone, a
 * config-only edit produced no save AND no error.
 *
 * `site.base_url` lives in `config`. A user changing their site's address and
 * pressing Save lost it silently. These tests exist so that cannot come back.
 */

std::string full_doc(const char* base_url, int rune_rev = 0) {
    return std::string(
        "{\"version\":1,"
        "\"mantles\":[{\"id\":\"m1\",\"name\":\"org\",\"runes\":["
        "{\"spirit\":{\"id\":\"r1\",\"name\":\"a\"},"
        "\"content\":{\"v\":") + std::to_string(rune_rev) + "}}]}],"
        "\"domains\":{\"site\":{\"name\":\"site\",\"deploy\":\"rsync -a\"}},"
        "\"bindings\":[{\"id\":\"b1\"}],"
        "\"scripts\":{\"allomone\":\"rule: month:june -> season:summer\"},"
        "\"config\":{\"site.base_url\":\"" + base_url + "\",\"theme.accent\":\"#f60\"},"
        "\"active\":{\"mantle\":\"org\"},"
        "\"_baseline\":[]}";
}

std::vector<std::string> keys_of(const cJSON* o) {
    std::vector<std::string> k;
    for (const cJSON* it = o ? o->child : nullptr; it; it = it->next)
        if (it->string) k.push_back(it->string);
    std::sort(k.begin(), k.end());
    return k;
}

void test_every_top_level_key_survives() {
    Json original(full_doc("https://example.org"));
    Archive a;
    std::string v = a.save(original.p, "one");
    cJSON* back = a.load(v);
    check(back != nullptr, "the save did not load");
    if (!back) return;
    check(keys_of(back) == keys_of(original.p),
          "a top-level key was lost on the round trip");
    /* And the value that motivated the report, specifically. */
    const cJSON* cfg = cJSON_GetObjectItemCaseSensitive(back, "config");
    const cJSON* url = cJSON_GetObjectItemCaseSensitive(const_cast<cJSON*>(cfg), "site.base_url");
    check(url && url->valuestring && std::string(url->valuestring) == "https://example.org",
          "config.site.base_url did not survive");
    const cJSON* sc = cJSON_GetObjectItemCaseSensitive(back, "scripts");
    check(sc && cJSON_GetObjectItemCaseSensitive(const_cast<cJSON*>(sc), "allomone"),
          "scripts (authored Allomone rules) did not survive");
    cJSON_Delete(back);
}

void test_a_config_only_change_is_saved() {
    /* THE regression. Same mantles, different config: the version name is
     * unchanged — correctly, it is the same cut — but the DOCUMENT differs, so a
     * save must happen. */
    Archive a;
    Json before(full_doc("https://before.example.org"));
    Json after(full_doc("https://AFTER.example.org"));
    std::string v1 = a.save(before.p, "one");
    std::string v2 = a.save(after.p, "two");

    check(v1 == v2, "the cut did not change, so the version name should not have");
    check(a.saves().size() == 2,
          "a config-only change produced no save — this is the silent data loss");
    check(!(a.saves()[0].content == a.saves()[1].content),
          "two different documents must have different content digests");

    cJSON* back = a.load_latest();
    check(back != nullptr, "the second save did not load");
    if (back) {
        const cJSON* cfg = cJSON_GetObjectItemCaseSensitive(back, "config");
        const cJSON* url = cJSON_GetObjectItemCaseSensitive(const_cast<cJSON*>(cfg), "site.base_url");
        check(url && url->valuestring &&
                  std::string(url->valuestring) == "https://AFTER.example.org",
              "load_latest returned the older config");
        cJSON_Delete(back);
    }
}

void test_identical_documents_still_dedup() {
    /* The guard must still be free for a genuine no-op — Hormiga autosaves
     * aggressively, so this is not a nicety. */
    Archive a;
    Json d(full_doc("https://example.org"));
    a.save(d.p, "one");
    std::size_t bytes = a.stored_bytes(), n = a.saves().size();
    a.save(d.p, "two");
    a.save(d.p, "three");
    check(a.stored_bytes() == bytes, "a redundant save stored bytes");
    check(a.saves().size() == n, "a redundant save added an entry");
}

void test_two_saves_may_share_a_version() {
    /* Honest rather than awkward: they ARE the same cut of the versioned slice,
     * and they are different documents. */
    Archive a;
    Json x(full_doc("https://a.example.org"));
    Json y(full_doc("https://b.example.org"));
    a.save(x.p, "x");
    a.save(y.p, "y");
    check(a.saves()[0].version == a.saves()[1].version, "same mantles, same version");
    check(a.has(a.saves()[0].version), "the shared version must still resolve");
}

void test_the_remainder_is_stored_once_when_unchanged() {
    /* The claim is specifically that the NON-MANTLE remainder is not re-stored
     * per save. Measuring it against a tiny document does not show that — the
     * per-save manifest churn dominates the ratio and drowns the signal.
     *
     * So make the remainder large and the mantles small: if the remainder were
     * being re-stored, twenty saves would cost twenty copies of it. */
    const std::size_t kBig = 200000;
    std::string big_script(kBig, 's');
    auto doc_with = [&](int rev) {
        return std::string(
            "{\"mantles\":[{\"id\":\"m1\",\"name\":\"org\",\"runes\":["
            "{\"spirit\":{\"id\":\"r1\",\"name\":\"a\"},"
            "\"content\":{\"v\":") + std::to_string(rev) + "}}]}],"
            "\"scripts\":{\"allomone\":\"" + big_script + "\"}}";
    };

    Archive a;
    Json first(doc_with(0));
    a.save(first.p, "rev 0");
    std::size_t after_one = a.stored_bytes();
    check(after_one > kBig, "the remainder should be stored at least once");

    for (int rev = 1; rev < 20; ++rev) {
        Json s(doc_with(rev));
        a.save(s.p, "rev " + std::to_string(rev));
    }
    check(a.saves().size() == 20, "all 20 rune edits should be saved");

    std::size_t added = a.stored_bytes() - after_one;
    check(added < kBig / 4,
          "19 further saves added " + std::to_string(added) + " bytes against a " +
              std::to_string(kBig) + "-byte remainder — it is being re-stored");
    std::printf("     200KB remainder, 19 rune-only edits -> %zu bytes added\n", added);

    /* And it really is still there, on the newest save. */
    cJSON* back = a.load_latest();
    check(back != nullptr, "the last save did not load");
    if (back) {
        const cJSON* sc = cJSON_GetObjectItemCaseSensitive(back, "scripts");
        const cJSON* al = sc ? cJSON_GetObjectItemCaseSensitive(const_cast<cJSON*>(sc), "allomone") : nullptr;
        check(al && al->valuestring && std::strlen(al->valuestring) == kBig,
              "the remainder did not survive to the newest save");
        cJSON_Delete(back);
    }
}

/* --- files --------------------------------------------------------------- */

void test_file_round_trip() {
    Archive a;
    Json s(doc_text(8));
    std::string v = a.save(s.p, "on disk");
    a.put_asset("assets/x.bin", std::string(40000, 'z'));

    const std::string path = "test-archive.vpal";
    std::string err;
    check(a.write_file(path, &err), "write_file failed: " + err);

    Archive b;
    check(Archive::read_file(path, b, &err), "read_file failed: " + err);
    check(b.saves().size() == 1, "saves did not survive the file");
    cJSON* back = b.load(v);
    check(back != nullptr, "the save was unreachable after reading the file");
    if (back) { check(version_name(back) == v, "wrong state from file"); cJSON_Delete(back); }
    std::string asset;
    check(b.get_asset("assets/x.bin", asset) && asset.size() == 40000, "asset lost");
    std::remove(path.c_str());
}

void test_write_is_atomic() {
    /* The property that matters: an existing archive is never left damaged. We
     * cannot crash mid-write from a test, but we can check the two observable
     * halves — a successful write leaves no temporary behind, and a failed write
     * leaves the previous file intact and unmodified. */
    const std::string path = "test-atomic.vpal";
    Archive first;
    Json s1(doc_text(4, 1));
    first.save(s1.p, "one");
    check(first.write_file(path), "first write failed");
    std::string before = read_whole(path);

    check(!file_exists(path + ".tmp"), "a temporary file was left behind");

    /* A write that cannot create its temporary must not touch the target. */
    Archive second;
    Json s2(doc_text(4, 2));
    second.save(s2.p, "two");
    std::string err;
    bool ok = second.write_file("no-such-directory/nested/deep.vpal", &err);
    check(!ok, "writing into a missing directory should fail");
    check(!err.empty(), "a failure must explain itself");
    check(read_whole(path) == before, "a failed write elsewhere disturbed the archive");

    /* And a successful overwrite fully replaces it. */
    check(second.write_file(path), "overwrite failed");
    check(read_whole(path) != before, "the overwrite did not take");
    check(!file_exists(path + ".tmp"), "overwrite left a temporary behind");
    std::remove(path.c_str());
}

void test_file_errors_are_distinguished() {
    std::string err;
    Archive out;
    check(!Archive::read_file("definitely-not-here.vpal", out, &err),
          "reading a missing file should fail");
    check(err.find("cannot open") != std::string::npos,
          "a missing file should say so, not report corruption: " + err);

    /* A file that exists and is not an archive is a DIFFERENT failure, and the
     * message must not blame the filesystem for it. */
    const std::string path = "test-garbage.vpal";
    { std::ofstream f(path, std::ios::binary); f << "this is not a container"; }
    err.clear();
    check(!Archive::read_file(path, out, &err), "garbage should not read as an archive");
    check(err.find("not a valid") != std::string::npos,
          "a corrupt file should say so, not report an I/O error: " + err);
    std::remove(path.c_str());
}

/* --- the Hormiga importer ------------------------------------------------ */

void test_import_miga_v3() {
    /* `.miga` v3 is {magic, version, meta, state, assets:{path: base64}}. The
     * assets map is the thing being migrated away from. */
    std::string state = doc_text(6);
    // "hello world" and "PALABRA" in base64
    std::string miga =
        "{\"magic\":\"MIGA\",\"version\":3,"
        "\"meta\":{\"name\":\"demo\",\"app\":\"hormiga\"},"
        "\"state\":" + state + ","
        "\"assets\":{\"assets/a.txt\":\"aGVsbG8gd29ybGQ=\","
        "\"assets/b.txt\":\"UEFMQUJSQQ==\"}}";

    Archive a;
    check(import_miga(miga, a, "imported from .miga"), "a valid v3 bundle failed to import");
    check(a.saves().size() == 1, "import should produce exactly one save");
    check(a.saves()[0].label == "imported from .miga", "the label was lost");

    Json expect(state);
    cJSON* back = a.load_latest();
    check(back && version_name(back) == version_name(expect.p),
          "the imported state does not match the bundle's");
    if (back) cJSON_Delete(back);

    std::string asset;
    check(a.get_asset("assets/a.txt", asset) && asset == "hello world",
          "a base64 asset did not decode");
    check(a.get_asset("assets/b.txt", asset) && asset == "PALABRA",
          "a padded base64 asset did not decode");
}

void test_import_refuses_what_it_does_not_understand() {
    Archive a;
    check(!import_miga("{\"magic\":\"MIGA\",\"version\":2,\"state\":{}}", a),
          "a v2 vault was imported as a v3 bundle");
    check(!import_miga("{\"version\":3,\"state\":{}}", a), "a bundle with no magic was accepted");
    check(!import_miga("not json", a), "unparseable input was accepted");
    check(!import_miga("{\"magic\":\"MIGA\",\"version\":3,\"state\":{\"mantles\":[]},"
                       "\"assets\":{\"x\":\"!!!not base64!!!\"}}", a),
          "a bundle with an undecodable asset was accepted — a partial import that "
          "looks complete is worse than a refusal");
}

/* --- runner -------------------------------------------------------------- */

struct Test { const char* name; void (*fn)(); };

const Test kTests[] = {
    {"save_then_load_returns_the_same_state", test_save_then_load_returns_the_same_state},
    {"saving_the_same_state_twice_is_free", test_saving_the_same_state_twice_is_free},
    {"every_past_version_is_reachable", test_every_past_version_is_reachable},
    {"load_latest_is_the_most_recent_save", test_load_latest_is_the_most_recent_save},
    {"an_unknown_version_is_reported_not_faked", test_an_unknown_version_is_reported_not_faked},
    {"a_hundred_saves_cost_what_changed", test_a_hundred_saves_cost_what_changed},
    {"an_asset_shared_across_saves_is_stored_once",
     test_an_asset_shared_across_saves_is_stored_once},
    {"two_names_for_the_same_bytes_cost_one_copy", test_two_names_for_the_same_bytes_cost_one_copy},
    {"archive_round_trips_through_bytes", test_archive_round_trips_through_bytes},
    {"equal_archives_produce_equal_files", test_equal_archives_produce_equal_files},
    {"a_damaged_archive_is_refused", test_a_damaged_archive_is_refused},
    {"every_top_level_key_survives", test_every_top_level_key_survives},
    {"a_config_only_change_is_saved", test_a_config_only_change_is_saved},
    {"identical_documents_still_dedup", test_identical_documents_still_dedup},
    {"two_saves_may_share_a_version", test_two_saves_may_share_a_version},
    {"the_remainder_is_stored_once_when_unchanged", test_the_remainder_is_stored_once_when_unchanged},
    {"file_round_trip", test_file_round_trip},
    {"write_is_atomic", test_write_is_atomic},
    {"file_errors_are_distinguished", test_file_errors_are_distinguished},
    {"import_miga_v3", test_import_miga_v3},
    {"import_refuses_what_it_does_not_understand", test_import_refuses_what_it_does_not_understand},
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
