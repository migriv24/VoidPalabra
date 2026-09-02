/* utterance_test.cpp — Phase 3, rung 1.
 *
 * Structured the way canonical_test.cpp is, because the shape carries the
 * argument: first *change something meaningless and assert the name does not
 * move*, then *change something meaningful and assert it does*. The second half
 * is what makes the first half mean anything — a hash function that ignored
 * everything would pass every test in the first half.
 *
 * The headline tests, the ones the rest is scaffolding for:
 *
 *   - `effectful_entries_do_not_reach_the_history` — VoidCore:SPEC §6.2's
 *     obligation on consumers, as an assertion.
 *   - `two_peers_name_the_same_cut` — the whole point of the layer: the same
 *     utterances received in opposite orders produce one name.
 *   - `absorb_survives_arbitrary_delivery_order` — the transport is allowed to be
 *     terrible, and this is where that claim stops being a sentence.
 */
#include "voidpalabra/utterance.hpp"
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

/* A journal as Core would export it, built by hand so the tests state their own
 * input. The field names and the shape are VoidCore:SPEC.md §6.2's. */
std::string journal_json(const std::string& entries) {
    return "[" + entries + "]";
}

std::string entry(int seq, const char* command, const char* verb, bool pure,
                  const char* slice, const std::string& minted = "",
                  const char* who = nullptr) {
    std::string s = "{\"seq\":" + std::to_string(seq);
    s += ",\"command\":\"" + std::string(command) + "\"";
    s += ",\"verb\":\"" + std::string(verb) + "\"";
    s += ",\"who\":";
    s += who ? "\"" + std::string(who) + "\"" : "null";
    s += ",\"pure\":";
    s += pure ? "true" : "false";
    s += ",\"slice\":\"" + std::string(slice) + "\"";
    s += ",\"minted\":[" + minted + "]}";
    return s;
}

std::vector<JournalEntry> parse(const std::string& text) {
    std::vector<JournalEntry> out;
    std::string err;
    if (!parse_journal(text, out, &err)) {
        g_failures.push_back(std::string(g_current) + ": journal did not parse: " + err);
    }
    return out;
}

/* The running example: two runes created and one tagged, with one `save` in the
 * middle that must not survive into the history. */
std::vector<JournalEntry> sample_journal() {
    return parse(journal_json(
        entry(1, "rune new text title", "rune", true, "undo", "\"rune_aaa\"", "ada") + "," +
        entry(2, "rune new text body", "rune", true, "undo", "\"rune_bbb\"", "ada") + "," +
        entry(3, "save", "save", false, "host", "", "ada") + "," +
        entry(4, "tag title +draft", "tag", true, "undo", "", "ada")));
}

/* ── the address: what must NOT move it ──────────────────────────────────── */

void names_are_stable_under_parent_order() {
    g_current = "names_are_stable_under_parent_order";
    Utterance a;
    a.command = "rune new text x";
    a.verb = "rune";
    a.parents = {"u:bbb", "u:aaa"};
    Utterance b = a;
    b.parents = {"u:aaa", "u:bbb"};
    check(a.name() == b.name(), "parents are a SET; their listing order is not information");

    /* And a duplicate parent is the same parent. Idempotence, one level up from
     * the OrSet's. */
    Utterance c = a;
    c.parents = {"u:aaa", "u:bbb", "u:aaa"};
    check(a.name() == c.name(), "a repeated parent does not change the name");
}

void names_are_stable_under_minted_order() {
    g_current = "names_are_stable_under_minted_order";
    Utterance a;
    a.command = "batch";
    a.verb = "batch";
    a.minted = {"rune_z", "rune_a"};
    Utterance b = a;
    b.minted = {"rune_a", "rune_z"};
    check(a.name() == b.name(), "minted ids arrive from a merge-diff; their order is an artifact");
}

void seq_does_not_move_the_name() {
    g_current = "seq_does_not_move_the_name";
    /* The load-bearing one. `seq` is a fact about one manager's dispatch counter;
     * two peers' counters are unrelated, so hashing it would make the same change
     * name itself differently on two machines. */
    Utterance a;
    a.command = "rune new text x";
    a.verb = "rune";
    a.seq = 1;
    Utterance b = a;
    b.seq = 9999;
    check(a.name() == b.name(), "seq is local provenance, never part of the address");
}

/* ── the address: what MUST move it ──────────────────────────────────────── */

void meaningful_differences_move_the_name() {
    g_current = "meaningful_differences_move_the_name";
    Utterance base;
    base.command = "rune new text x";
    base.verb = "rune";
    base.parents = {"u:aaa"};
    base.minted = {"rune_1"};
    base.who = "ada";
    base.has_who = true;
    const std::string b = base.name();

    Utterance different_command = base;
    different_command.command = "rune new text y";
    check(different_command.name() != b, "a different command is a different change");

    Utterance different_parent = base;
    different_parent.parents = {"u:bbb"};
    check(different_parent.name() != b, "a different history is a different change");

    Utterance different_mint = base;
    different_mint.minted = {"rune_2"};
    check(different_mint.name() != b, "a different identity is a different change");

    Utterance different_who = base;
    different_who.who = "grace";
    check(different_who.name() != b, "attribution is part of the record");

    Utterance grouped = base;
    grouped.group = "batch_1";
    check(grouped.name() != b, "a grouping label is recorded, so it is addressed");

    Utterance no_who = base;
    no_who.has_who = false;
    no_who.who = "";
    check(no_who.name() != b, "unattributed is not the same as attributed to ''");

    Utterance empty_who = base;
    empty_who.who = "";
    check(empty_who.name() != no_who.name(),
          "null and the empty string are distinguishable in the address");
}

void an_utterance_with_no_honest_bytes_is_refused() {
    g_current = "an_utterance_with_no_honest_bytes_is_refused";
    Utterance u;
    u.verb = "rune";
    u.command = std::string("rune new text \xC3\x28");  // invalid UTF-8
    bool threw = false;
    try {
        u.name();
    } catch (const CanonicalError&) {
        threw = true;
    }
    check(threw, "SPEC §2.2 refuses bytes it cannot read as text, here as everywhere");
}

/* ── the filter ──────────────────────────────────────────────────────────── */

void effectful_entries_do_not_reach_the_history() {
    g_current = "effectful_entries_do_not_reach_the_history";
    History h;
    IngestReport r = h.record(sample_journal());
    check(r.recorded.size() == 3, "three pure mantle-slice commands recorded");
    check(r.skipped.size() == 1, "the save was skipped, not dropped");
    check(!r.skipped.empty() && r.skipped[0].why == Skip::effectful,
          "and it was skipped for being effectful");
    check(!r.skipped.empty() && r.skipped[0].entry.verb == "save",
          "the skipped entry is returned whole, so a reader can see the gap");
    check(h.size() == 3, "the graph holds exactly the recorded utterances");
    check(!r.skipped.empty() &&
              std::string(skip_reason(r.skipped[0].why)).find("holiday") != std::string::npos,
          "and the reason is sayable, so a host can show it rather than guess");
}

void a_view_mutation_is_not_versioned_content() {
    g_current = "a_view_mutation_is_not_versioned_content";
    std::vector<JournalEntry> j = parse(journal_json(
        entry(1, "rune new text a", "rune", true, "undo", "\"rune_a\"") + "," +
        entry(2, "place a 10 20", "place", true, "view")));
    History h;
    IngestReport r = h.record(j);
    check(r.recorded.size() == 1, "placement is real state but not versioned content");
    check(r.skipped.size() == 1 && r.skipped[0].why == Skip::view_slice,
          "and the reason says which rule excluded it");
}

void a_pure_host_slice_command_is_reported_as_such() {
    g_current = "a_pure_host_slice_command_is_reported_as_such";
    /* `journal` is pure AND lands nowhere in the document. Both facts are true;
     * the report must name the one that actually excluded it. */
    std::vector<JournalEntry> j =
        parse(journal_json(entry(1, "journal on", "journal", true, "host")));
    History h;
    IngestReport r = h.record(j);
    check(r.recorded.empty(), "nothing in the document changed, so nothing is versioned");
    check(r.skipped.size() == 1 && r.skipped[0].why == Skip::host_slice,
          "reported as host-slice rather than as effectful");
}

void an_effectful_entry_is_reported_as_effectful_not_as_host() {
    g_current = "an_effectful_entry_is_reported_as_effectful_not_as_host";
    /* `save` is both effectful and host-slice. Purity is the rule with authority
     * — it is Core's, and it is the one a replay consumer must obey — so it is
     * the reason reported. */
    History h;
    IngestReport r = h.record(parse(journal_json(entry(1, "save", "save", false, "host"))));
    check(r.skipped.size() == 1 && r.skipped[0].why == Skip::effectful,
          "the more informative of the two true statements");
}

void undo_is_recorded_because_omitting_it_would_lie() {
    g_current = "undo_is_recorded_because_omitting_it_would_lie";
    std::vector<JournalEntry> j = parse(journal_json(
        entry(1, "rune new text c", "rune", true, "undo", "\"rune_c\"") + "," +
        entry(2, "undo", "undo", true, "undo", "\"rune_c\"")));
    History h;
    IngestReport r = h.record(j);
    check(r.recorded.size() == 2,
          "a record that omits taking a change back replays into a state nobody had");
    check(r.recorded[1].verb == "undo",
          "the verb says the ids in `minted` were RESTORED, not freshly minted");
}

void the_canonical_line_is_what_records() {
    g_current = "the_canonical_line_is_what_records";
    /* Core desugars aliases before journaling, so `rm b` arrives as `rune rm b`.
     * The test asserts the consequence Palabra cares about: two peers, one typing
     * the alias and one the canonical form, produce ONE utterance rather than two
     * for the same edit. */
    History a, b;
    a.record(parse(journal_json(entry(1, "rune rm b", "rune", true, "undo"))));
    b.record(parse(journal_json(entry(7, "rune rm b", "rune", true, "undo"))));
    check(a.heads() == b.heads(), "same change, same name, different seq");
    check(a.cut_name() == b.cut_name(), "and therefore the same cut");
}

/* ── the graph ───────────────────────────────────────────────────────────── */

void a_journal_becomes_a_chain() {
    g_current = "a_journal_becomes_a_chain";
    History h;
    IngestReport r = h.record(sample_journal());
    check(h.heads().size() == 1, "one dispatcher, one head");
    check(r.recorded[0].parents.empty(), "the first utterance is a root");
    check(r.recorded[1].parents.size() == 1 &&
              r.recorded[1].parents[0] == r.recorded[0].hash,
          "each command observed the state its predecessor left");
    check(h.is_ancestor(r.recorded[0].hash, r.recorded[2].hash),
          "ancestry is transitive over the chain");
    check(!h.concurrent(r.recorded[0].hash, r.recorded[1].hash),
          "nothing in one journal is concurrent with anything else in it");
}

void a_missing_parent_is_refused() {
    g_current = "a_missing_parent_is_refused";
    Utterance u;
    u.command = "rune new text x";
    u.verb = "rune";
    u.parents = {"u:0000000000000000000000000000000000000000000000000000000000000000"};
    u.seal();
    History h;
    check(h.add(u) == History::Add::missing_parent,
          "a node whose ancestry is absent has no place in the partial order");
    check(h.size() == 0, "and it is not held anyway");
}

void a_mis_sealed_utterance_is_refused() {
    g_current = "a_mis_sealed_utterance_is_refused";
    Utterance u;
    u.command = "rune new text x";
    u.verb = "rune";
    u.seal();
    Utterance tampered = u;
    tampered.command = "rune rm x";  // content changed, hash left alone
    History h;
    check(h.add(tampered) == History::Add::malformed,
          "a name that does not prove its content is worse than no name");
    Utterance unsealed;
    unsealed.command = "rune new text y";
    unsealed.verb = "rune";
    check(h.add(unsealed) == History::Add::malformed, "and an unsealed one is refused too");
}

void receiving_the_same_utterance_twice_is_free() {
    g_current = "receiving_the_same_utterance_twice_is_free";
    History h;
    IngestReport r = h.record(sample_journal());
    check(h.add(r.recorded[0]) == History::Add::duplicate, "idempotence at the graph level");
    check(h.size() == 3, "and it costs nothing");
}

void concurrency_is_recorded_as_concurrency() {
    g_current = "concurrency_is_recorded_as_concurrency";
    /* Two peers edit from the same root. Neither reaches the other, and the graph
     * says so rather than picking a winner. */
    History root;
    IngestReport r0 = root.record(parse(journal_json(
        entry(1, "rune new text shared", "rune", true, "undo", "\"rune_s\""))));

    History a = root, b = root;
    IngestReport ra = a.record(parse(journal_json(
        entry(2, "tag shared +ada", "tag", true, "undo"))));
    IngestReport rb = b.record(parse(journal_json(
        entry(2, "tag shared +grace", "tag", true, "undo"))));

    History merged = a;
    merged.absorb(b);
    check(merged.size() == 3, "both branches are held");
    check(merged.heads().size() == 2, "two heads is a normal resting state, not a problem");
    check(merged.concurrent(ra.recorded[0].hash, rb.recorded[0].hash),
          "neither reaches the other: genuinely unordered");
    check(merged.is_ancestor(r0.recorded[0].hash, ra.recorded[0].hash) &&
              merged.is_ancestor(r0.recorded[0].hash, rb.recorded[0].hash),
          "and both descend from the shared root");

    /* The next command after a merge names both heads — that is the merge point,
     * and it needs no special API. */
    IngestReport after = merged.record(parse(journal_json(
        entry(3, "tag shared +both", "tag", true, "undo"))));
    check(after.recorded[0].parents.size() == 2, "one utterance, two parents");
    check(merged.heads().size() == 1, "which closes the fork");
}

void two_peers_name_the_same_cut() {
    g_current = "two_peers_name_the_same_cut";
    History source;
    source.record(sample_journal());
    source.record(parse(journal_json(entry(9, "tag body +final", "tag", true, "undo"))));

    /* Deliver every utterance in REVERSE. The receiver must reach the same graph
     * and therefore the same name, or the claim that a peer can be handed its
     * history over a bad transport is false. */
    std::vector<Utterance> inbox;
    std::vector<std::string> order = source.linear_extension();
    std::reverse(order.begin(), order.end());
    for (const std::string& h : order) inbox.push_back(*source.get(h));

    History receiver;
    std::vector<Utterance> unplaceable;
    std::size_t added = receiver.receive(inbox, &unplaceable);
    check(added == source.size(), "everything arrived");
    check(unplaceable.empty(), "and everything was placeable, given retries");
    check(receiver.cut_name() == source.cut_name(), "same utterances, same cut name");
    check(receiver.linear_extension() == source.linear_extension(),
          "and the same rendered timeline");

    /* A cut name is not a state version name, and the two prefixes exist so that
     * a caller who confuses them notices. */
    check(source.cut_name().rfind("c:", 0) == 0, "cuts are named c:…");
    check(source.heads()[0].rfind("u:", 0) == 0, "utterances are named u:…");
}

void absorb_survives_arbitrary_delivery_order() {
    g_current = "absorb_survives_arbitrary_delivery_order";
    History source;
    std::string entries;
    for (int i = 1; i <= 12; ++i) {
        if (i > 1) entries += ",";
        entries += entry(i, ("rune new text r" + std::to_string(i)).c_str(), "rune",
                         true, "undo", "\"rune_" + std::to_string(i) + "\"");
    }
    source.record(parse(journal_json(entries)));
    check(source.size() == 12, "twelve utterances to shuffle");

    const std::string expected = source.cut_name();
    const std::vector<std::string> timeline = source.linear_extension();

    std::mt19937 rng(20260827);
    for (int trial = 0; trial < 40; ++trial) {
        std::vector<std::string> order = timeline;
        std::shuffle(order.begin(), order.end(), rng);
        std::vector<Utterance> inbox;
        for (const std::string& h : order) inbox.push_back(*source.get(h));

        History received;
        std::vector<Utterance> unplaceable;
        std::size_t added = received.receive(inbox, &unplaceable);
        check(added == 12 && unplaceable.empty(), "no drops, whatever the order");
        check(received.cut_name() == expected, "the name does not depend on arrival order");
        check(received.linear_extension() == timeline,
              "nor does the rendered timeline — that is what the tiebreak buys");
    }

    /* Duplicates are free, which is what lets a transport retry blindly. */
    std::vector<Utterance> doubled;
    for (const std::string& h : timeline) {
        doubled.push_back(*source.get(h));
        doubled.push_back(*source.get(h));
    }
    History twice;
    twice.receive(doubled);
    check(twice.size() == 12, "receiving everything twice holds it once");
    check(twice.cut_name() == expected, "and names the same cut");
}

void an_unplaceable_utterance_is_handed_back_not_held() {
    g_current = "an_unplaceable_utterance_is_handed_back_not_held";
    History source;
    IngestReport r = source.record(sample_journal());

    /* A peer sends only the tail of its history — the region the receiver was
     * missing, minus the root it needed. Holding that under a complete-looking
     * history is the failure a Merkle clock exists to prevent. */
    History receiver;
    std::vector<Utterance> unplaceable;
    std::size_t added = receiver.receive({r.recorded[2]}, &unplaceable);
    check(added == 0, "nothing could be placed");
    check(receiver.size() == 0, "and nothing was held anyway");
    check(unplaceable.size() == 1 && unplaceable[0].hash == r.recorded[2].hash,
          "it is handed back, so the caller can ask for the missing region");

    /* Send the region it was missing, and the held-back one goes in. */
    receiver.receive({r.recorded[0], r.recorded[1]});
    std::vector<Utterance> still;
    check(receiver.receive(unplaceable, &still) == 1 && still.empty(),
          "the retry succeeds once the ancestry arrives");
    check(receiver.cut_name() == source.cut_name(), "and the two peers agree");
}

void absorbing_a_whole_graph_cannot_partially_fail() {
    g_current = "absorbing_a_whole_graph_cannot_partially_fail";
    History a;
    a.record(sample_journal());
    History b;
    check(b.absorb(a) == a.size(), "a dependency-closed graph merges whole");
    check(b.cut_name() == a.cut_name(), "and lands on the same cut");
    check(b.absorb(a) == 0, "absorbing it again adds nothing");
}

/* ── round trips ─────────────────────────────────────────────────────────── */

void a_history_round_trips_through_json() {
    g_current = "a_history_round_trips_through_json";
    History h;
    h.record(sample_journal(), {"session-1"});
    History extra = h;
    extra.record(parse(journal_json(entry(20, "tag body +x", "tag", true, "undo", "", "grace"))));

    cJSON* json = extra.to_json();
    History back;
    std::string err;
    bool ok = History::from_json(json, back, &err);
    check(ok, "the file loads: " + err);
    check(back.size() == extra.size(), "same count");
    check(back.cut_name() == extra.cut_name(), "same cut");
    check(back.linear_extension() == extra.linear_extension(), "same reading");

    const Utterance* u = back.get(extra.heads()[0]);
    check(u && u->who == "grace" && u->has_who, "attribution survives");
    check(u && u->group.empty(), "and so does its absence");

    const Utterance* first = back.get(back.linear_extension()[0]);
    check(first && first->group == "session-1", "the grouping label survives");

    cJSON_Delete(json);
}

void a_tampered_file_is_refused() {
    g_current = "a_tampered_file_is_refused";
    History h;
    h.record(sample_journal());
    cJSON* json = h.to_json();

    cJSON* arr = cJSON_GetObjectItemCaseSensitive(json, "utterances");
    cJSON* first = cJSON_GetArrayItem(arr, 0);
    cJSON_DeleteItemFromObjectCaseSensitive(first, "command");
    cJSON_AddStringToObject(first, "command", "rune rm everything");

    History back;
    std::string err;
    bool ok = History::from_json(json, back, &err);
    check(!ok, "content that does not match its name is refused");
    check(err.find("mismatch") != std::string::npos, "and the error says why: " + err);
    cJSON_Delete(json);
}

void an_incomplete_file_is_refused() {
    g_current = "an_incomplete_file_is_refused";
    History h;
    h.record(sample_journal());
    cJSON* json = h.to_json();
    cJSON* arr = cJSON_GetObjectItemCaseSensitive(json, "utterances");
    cJSON_DeleteItemFromArray(arr, 0);   // remove the root

    History back;
    std::string err;
    check(!History::from_json(json, back, &err), "a history with a hole is not a history");
    check(err.find("incomplete") != std::string::npos, "and it says so: " + err);
    cJSON_Delete(json);
}

/* ── the journal parser ──────────────────────────────────────────────────── */

void a_malformed_journal_is_refused_rather_than_defaulted() {
    g_current = "a_malformed_journal_is_refused_rather_than_defaulted";
    std::vector<JournalEntry> out;
    std::string err;

    check(!parse_journal(std::string("{}"), out, &err), "an object is not a journal");
    check(!parse_journal(std::string("[{\"seq\":1,\"command\":\"x\",\"verb\":\"x\","
                                     "\"pure\":true,\"slice\":\"undo\",\"minted\":[]}]"),
                         out, &err),
          "an absent `who` is a failure: Core emits an explicit null");
    check(!parse_journal(std::string("[{\"seq\":1,\"command\":\"x\",\"verb\":\"x\","
                                     "\"who\":null,\"pure\":true,\"slice\":\"elsewhere\","
                                     "\"minted\":[]}]"),
                         out, &err),
          "an unknown slice is a failure, not a default");
    check(err.find("elsewhere") != std::string::npos, "and the error names it: " + err);

    check(parse_journal(std::string("[]"), out, &err) && out.empty(),
          "an empty journal is a valid journal");
}

void an_empty_history_still_has_a_name() {
    g_current = "an_empty_history_still_has_a_name";
    History h;
    check(h.heads().empty(), "no heads");
    check(!h.cut_name().empty() && h.cut_name().rfind("c:", 0) == 0,
          "the empty cut is still a cut, and is sayable");
    History other;
    check(h.cut_name() == other.cut_name(), "and every peer says the same word for it");
}

}  // namespace

int main() {
    names_are_stable_under_parent_order();
    names_are_stable_under_minted_order();
    seq_does_not_move_the_name();
    meaningful_differences_move_the_name();
    an_utterance_with_no_honest_bytes_is_refused();

    effectful_entries_do_not_reach_the_history();
    a_view_mutation_is_not_versioned_content();
    a_pure_host_slice_command_is_reported_as_such();
    an_effectful_entry_is_reported_as_effectful_not_as_host();
    undo_is_recorded_because_omitting_it_would_lie();
    the_canonical_line_is_what_records();

    a_journal_becomes_a_chain();
    a_missing_parent_is_refused();
    a_mis_sealed_utterance_is_refused();
    receiving_the_same_utterance_twice_is_free();
    concurrency_is_recorded_as_concurrency();
    two_peers_name_the_same_cut();
    absorb_survives_arbitrary_delivery_order();
    an_unplaceable_utterance_is_handed_back_not_held();
    absorbing_a_whole_graph_cannot_partially_fail();

    a_history_round_trips_through_json();
    a_tampered_file_is_refused();
    an_incomplete_file_is_refused();

    a_malformed_journal_is_refused_rather_than_defaulted();
    an_empty_history_still_has_a_name();

    std::printf("utterance: %d checks", g_checks);
    if (g_failures.empty()) {
        std::printf(", all passed\n");
        return 0;
    }
    std::printf(", %d FAILED\n", static_cast<int>(g_failures.size()));
    for (const std::string& f : g_failures) std::printf("  - %s\n", f.c_str());
    return 1;
}
