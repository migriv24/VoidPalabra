/* gen_linear_vectors.cpp — regenerates conformance/cases/14-linear-extension.json.
 *
 * WHY A GENERATOR EXISTS FOR ONE VECTOR FILE, when every other one is hand-written.
 *
 * The utterances in that file name each other BY HASH: a child carries its
 * parents' addresses, so the diamond cannot be typed out. `--regen` cannot fix it
 * either — that only rewrites each case's `out`, and here the hashes are in the
 * `in`. Without this the file is unmaintainable the first time CANON_VERSION
 * moves, which it did on 2026-09-03 and will again.
 *
 * Not part of the default build or of ctest: it WRITES a contract rather than
 * checking one, and a regenerator that runs by accident is how a conformance
 * suite quietly starts agreeing with whatever the implementation currently does.
 *
 *   cmake --build build --target gen_linear_vectors
 *   ./build/bin/gen_linear_vectors          # from the repo root
 *   ./build/bin/voidpalabra_conformance --regen
 *
 * The second command fills in the `out` values; this one only fixes the inputs.
 * Read the diff before committing it — that is the whole safeguard.
 */
#include "voidpalabra/utterance.hpp"
#include "cJSON.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <random>
#include <string>
#include <vector>

using namespace voidpalabra;

namespace {

Utterance make(const char* command, const char* verb,
               std::vector<std::string> parents, const char* who,
               std::vector<std::string> minted, int seq) {
    Utterance u;
    u.command = command;
    u.verb = verb;
    u.parents = std::move(parents);
    if (who) { u.who = who; u.has_who = true; }
    u.minted = std::move(minted);
    u.seq = seq;
    u.seal();
    return u;
}

cJSON* array_of(const std::vector<Utterance>& us) {
    cJSON* a = cJSON_CreateArray();
    for (const Utterance& u : us) cJSON_AddItemToArray(a, utterance_to_json(u));
    return a;
}

void add_case(cJSON* cases, const char* name, const std::vector<Utterance>& us) {
    cJSON* c = cJSON_CreateObject();
    cJSON_AddStringToObject(c, "name", name);
    cJSON_AddItemToObject(c, "in", array_of(us));
    /* Left empty on purpose: `--regen` fills it from the implementation, and the
     * two steps stay separate so nobody regenerates an expectation by running a
     * tool that was only meant to fix an input. */
    cJSON_AddStringToObject(c, "out", "");
    cJSON_AddItemToArray(cases, c);
}

}  // namespace

int main() {
    /* The diamond: one root, two concurrent children, one merge naming both, and
     * a tail after the merge. Width 2 — the smallest graph whose linear extension
     * involves an actual choice, which is the only kind that can pin a tiebreak. */
    Utterance root  = make("rune new text shared", "rune", {}, "ada", {"rune_s"}, 1);
    Utterance left  = make("tag shared +ada", "tag", {root.hash}, "ada", {}, 2);
    Utterance right = make("tag shared +grace", "tag", {root.hash}, "grace", {}, 2);
    Utterance merge = make("tag shared +both", "tag", {left.hash, right.hash}, "ada", {}, 3);
    Utterance tail  = make("rune new text after", "rune", {merge.hash}, "ada", {"rune_t"}, 4);

    std::vector<Utterance> diamond = {root, left, right, merge, tail};

    std::vector<Utterance> reversed = diamond;
    std::reverse(reversed.begin(), reversed.end());

    std::vector<Utterance> shuffled = diamond;
    std::mt19937 rng(20260827);   // fixed, so the file is reproducible
    std::shuffle(shuffled.begin(), shuffled.end(), rng);

    /* A pure chain: nothing concurrent, so the tiebreak never fires and the
     * reading is forced. Included so the diamond's answer is visibly a choice. */
    Utterance c1 = make("rune new text a", "rune", {}, nullptr, {"rune_a"}, 1);
    Utterance c2 = make("rune new text b", "rune", {c1.hash}, nullptr, {"rune_b"}, 2);
    Utterance c3 = make("rune new text c", "rune", {c2.hash}, nullptr, {"rune_c"}, 3);
    std::vector<Utterance> chain = {c3, c1, c2};

    std::vector<Utterance> incomplete = {root, merge, tail};   // branches withheld

    /* Two roots and nothing joining them: an antichain of width 2 with no common
     * ancestor, which is what two peers that never met look like. */
    Utterance r1 = make("rune new text one", "rune", {}, "ada", {"rune_1"}, 1);
    Utterance r2 = make("rune new text two", "rune", {}, "grace", {"rune_2"}, 1);
    std::vector<Utterance> forest = {r2, r1};

    cJSON* root_obj = cJSON_CreateObject();
    cJSON_AddStringToObject(root_obj, "kind", "linear");
    cJSON_AddStringToObject(
        root_obj, "about",
        "§8.4 — the linear extension: one reading of a partial order, "
        "made a function of the graph by a canonical tiebreak");
    cJSON* cases = cJSON_AddArrayToObject(root_obj, "cases");

    add_case(cases, "a diamond, delivered in dependency order", diamond);
    add_case(cases, "the same diamond, delivered reversed — must equal", reversed);
    add_case(cases, "the same diamond, shuffled — must equal", shuffled);
    add_case(cases, "a chain, delivered out of order — must DIFFER", chain);
    add_case(cases, "two roots that never met — must DIFFER", forest);
    add_case(cases, "a delivery missing the middle — must DIFFER (incomplete)", incomplete);

    const char* path = "conformance/cases/14-linear-extension.json";
    char* txt = cJSON_Print(root_obj);
    std::ofstream f(path, std::ios::binary);
    if (!f) {
        std::printf("cannot write %s — run from the repo root\n", path);
        cJSON_free(txt);
        cJSON_Delete(root_obj);
        return 1;
    }
    f << txt << "\n";
    cJSON_free(txt);
    cJSON_Delete(root_obj);
    std::printf("wrote %s — now run: voidpalabra_conformance --regen\n", path);
    return 0;
}
