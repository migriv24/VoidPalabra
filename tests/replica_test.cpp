/* replica_test.cpp — a sync loop that runs on a timer, and everything it can get wrong.
 *
 * A client asked for automatic sync "as long as it WORKS correctly". These tests
 * are what "correctly" turned out to mean, and most of them are cases a first
 * client does not meet on day one:
 *
 *   - a deletion that comes back on the next merge;
 *   - a conflict that quietly resolves itself because a timer observed it;
 *   - metadata that grows on every tick with nothing changing;
 *   - a colleague's edit that vanishes because someone else deleted the thing;
 *   - a backup restore, a crash, or a cloned device reusing a replica's tags;
 *   - deltas that arrive out of order, twice, or never.
 *
 * The headline is `a_deletion_propagates_instead_of_coming_back`. The one most
 * likely to bite an application that does everything else right is
 * `observing_does_not_resolve_a_conflict`.
 */
#include "voidpalabra/replica.hpp"
#include "voidpalabra/canonical.hpp"
#include "voidpalabra/join.hpp"

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
    cJSON* p;
    explicit Json(const std::string& text) : p(cJSON_Parse(text.c_str())) {}
    ~Json() { if (p) cJSON_Delete(p); }
    Json(const Json&) = delete;
    Json& operator=(const Json&) = delete;
};

const char* kIdA = "replica-A-0123456789";
const char* kIdB = "replica-B-0123456789";
const char* kIdC = "replica-C-0123456789";

Replica make(const char* id) {
    Replica r;
    std::string why;
    bool ok = Replica::create(id, r, &why);
    check(ok, std::string("create ") + id + ": " + why);
    return r;
}

std::string rune(const std::string& id, const std::string& body,
                 const std::string& extra = "") {
    return "{\"spirit\":{\"id\":\"" + id + "\",\"name\":\"" + id + "\"},\"glyph\":\"text\","
           "\"content\":{\"body\":\"" + body + "\"}" + extra + "}";
}

std::string state(const std::string& runes, const std::string& mantle_extra = "",
                  const std::string& doc_extra = "") {
    return "{\"mantles\":[{\"id\":\"m1\",\"name\":\"notes\",\"runes\":[" + runes + "]" +
           mantle_extra + "}]" + doc_extra + "}";
}

std::size_t observe(Replica& r, const std::string& text) {
    Json s(text);
    Replica::Observed o = r.observe(s.p);
    check(o.ok, "observe failed: " + o.error);
    return o.changes;
}

/* Merge both ways, the way two peers exchanging full documents do. */
void exchange(Replica& a, Replica& b) {
    std::string why;
    Doc da;
    da.root = cJSON_Duplicate(a.doc().root, 1);
    check(b.merge(da, &why) == MergeResult::ok, "b <- a: " + why);
    check(a.merge(b.doc(), &why) == MergeResult::ok, "a <- b: " + why);
}

std::string shown(const Replica& r) {
    Doc f = r.flatten();
    return version_name(f.root);
}

bool shows_rune(const Replica& r, const std::string& id) {
    Doc f = r.flatten();
    cJSON* ms = cJSON_GetObjectItem(f.root, "mantles");
    for (cJSON* m = ms ? ms->child : nullptr; m; m = m->next)
        for (cJSON* x = cJSON_GetObjectItem(m, "runes")->child; x; x = x->next)
            if (id == cJSON_GetObjectItem(cJSON_GetObjectItem(x, "spirit"), "id")->valuestring)
                return true;
    return false;
}

std::string body_of(const Replica& r, const std::string& id) {
    Doc f = r.flatten();
    cJSON* ms = cJSON_GetObjectItem(f.root, "mantles");
    for (cJSON* m = ms ? ms->child : nullptr; m; m = m->next)
        for (cJSON* x = cJSON_GetObjectItem(m, "runes")->child; x; x = x->next)
            if (id == cJSON_GetObjectItem(cJSON_GetObjectItem(x, "spirit"), "id")->valuestring) {
                cJSON* b = cJSON_GetObjectItem(cJSON_GetObjectItem(x, "content"), "body");
                return b && cJSON_IsString(b) ? b->valuestring : std::string("<absent>");
            }
    return "<no rune>";
}

/* ── deletions ─────────────────────────────────────────────────────────────── */

void a_deletion_propagates_instead_of_coming_back() {
    g_current = "a_deletion_propagates_instead_of_coming_back";
    Replica a = make(kIdA), b = make(kIdB);
    observe(a, state(rune("rune_1", "one") + "," + rune("rune_2", "two")));
    exchange(a, b);
    check(shows_rune(b, "rune_1"), "b received rune_1");

    observe(a, state(rune("rune_2", "two")));  // a deletes rune_1
    for (int tick = 0; tick < 5; ++tick) {
        exchange(a, b);
        /* b's application splices what it was shown and observes it again, as an
         * automatic loop would. */
        Doc fb = b.flatten();
        b.observe(fb.root);
    }
    check(!shows_rune(a, "rune_1"), "a does not get it back");
    check(!shows_rune(b, "rune_1"), "b lost it too, and keeps it lost across five ticks");
    check(shows_rune(b, "rune_2"), "and nothing else went with it");
    check(shown(a) == shown(b), "both show the same version");
}

void re_enriching_every_exchange_resurrects_deletions() {
    g_current = "re_enriching_every_exchange_resurrects_deletions";
    /* Kept as a test because it is the mistake a first client makes, and the reason
     * `enrich` must not sit inside a sync loop. B builds a fresh document from its
     * old state before each exchange, and in doing so forgets it ever saw rune_1 —
     * so A's deletion loses to "a rune A never saw". */
    Replica a = make(kIdA);
    std::string both = state(rune("rune_1", "one") + "," + rune("rune_2", "two"));
    observe(a, both);
    observe(a, state(rune("rune_2", "two")));  // a deletes rune_1

    Json b_state(both);
    CounterMint fresh("stale-enrich");
    Doc b_doc = enrich(b_state.p, fresh);
    std::string why;
    check(a.merge(b_doc, &why) == MergeResult::ok, why);
    check(shows_rune(a, "rune_1"),
          "demonstrated: a peer that re-enriches brings the deleted rune back");
}

void a_removed_content_key_is_absent_not_null() {
    g_current = "a_removed_content_key_is_absent_not_null";
    Replica a = make(kIdA), b = make(kIdB);
    observe(a, state(rune("rune_1", "one", ",\"tags\":[\"x\",\"y\"]")));
    exchange(a, b);
    observe(a, state("{\"spirit\":{\"id\":\"rune_1\",\"name\":\"rune_1\"},\"glyph\":\"text\","
                     "\"content\":{},\"tags\":[\"x\"]}"));
    exchange(a, b);
    check(body_of(b, "rune_1") == "<absent>",
          "a deleted key is absent — `null` would be a value the user never wrote");
    Json expect(state("{\"spirit\":{\"id\":\"rune_1\",\"name\":\"rune_1\"},\"glyph\":\"text\","
                      "\"content\":{},\"tags\":[\"x\"]}"));
    check(shown(b) == version_name(expect.p), "and the removed tag went with it");
}

void undo_brings_back_the_same_rune_after_a_synced_delete() {
    g_current = "undo_brings_back_the_same_rune_after_a_synced_delete";
    /* Core's undo restores a deleted rune under its ORIGINAL spirit.id. The deletion
     * has already been retired on every peer, and the restoration must still win —
     * it is a new act, with a new tag. */
    Replica a = make(kIdA), b = make(kIdB);
    std::string with = state(rune("rune_1", "one"));
    observe(a, with);
    exchange(a, b);
    observe(a, state(""));
    exchange(a, b);
    check(!shows_rune(b, "rune_1"), "deleted everywhere first");
    observe(a, with);
    exchange(a, b);
    check(shows_rune(b, "rune_1") && body_of(b, "rune_1") == "one",
          "restored everywhere, with its content");
    check(a.conflicts().empty() && b.conflicts().empty(), "and it is not a conflict");
}

void mantle_and_glyph_removals_propagate() {
    g_current = "mantle_and_glyph_removals_propagate";
    Replica a = make(kIdA), b = make(kIdB);
    observe(a, "{\"mantles\":[{\"name\":\"keep\",\"runes\":[]},{\"name\":\"drop\",\"runes\":[]}],"
               "\"glyphs\":{\"stat\":{\"glyph\":\"stat\",\"kind\":\"measure\"}}}");
    exchange(a, b);
    observe(a, "{\"mantles\":[{\"name\":\"keep\",\"runes\":[]}]}");
    exchange(a, b);
    Doc f = b.flatten();
    check(cJSON_GetArraySize(cJSON_GetObjectItem(f.root, "mantles")) == 1, "the mantle is gone");
    check(!cJSON_GetObjectItem(f.root, "glyphs"), "and so is the declaration");
}

void mantle_tags_rules_and_relations_survive_the_loop() {
    g_current = "mantle_tags_rules_and_relations_survive_the_loop";
    /* All three were lost on every merge until 2026-09-16: hashed by the canonical
     * form, carried by nothing. */
    Replica a = make(kIdA), b = make(kIdB);
    std::string s = state(rune("rune_1", "one", ",\"relations\":[{\"to\":\"rune_2\"}]"),
                          ",\"tags\":{\"urgent\":{\"near\":{\"soon\":0.8}}},"
                          "\"rules\":[\"when x then y\"]");
    observe(a, s);
    exchange(a, b);
    Json expect(s);
    check(shown(b) == version_name(expect.p), "the peer holds exactly the slice a observed");

    std::string fewer = state(rune("rune_1", "one"), ",\"tags\":{}");
    observe(a, fewer);
    exchange(a, b);
    Json expect2(fewer);
    check(shown(b) == version_name(expect2.p), "and removing them propagates too");
}

/* ── the automatic loop ────────────────────────────────────────────────────── */

void observing_its_own_output_records_nothing() {
    g_current = "observing_its_own_output_records_nothing";
    Replica a = make(kIdA), b = make(kIdB);
    observe(a, state(rune("rune_1", "one", ",\"tags\":[\"x\"],\"placement\":{\"x\":3}"),
                     ",\"layout\":{\"edges\":[{\"from\":\"rune_1\",\"to\":\"rune_1\"}]}",
                     ",\"glyphs\":{\"stat\":{\"glyph\":\"stat\",\"source\":\"host\"}}"));
    observe(b, state(rune("rune_2", "two")));
    exchange(a, b);
    std::uint64_t before = a.issued();
    for (int tick = 0; tick < 10; ++tick) {
        Doc f = a.flatten();
        Replica::Observed o = a.observe(f.root);
        check(o.ok && o.changes == 0, "tick " + std::to_string(tick) + " recorded a change");
    }
    check(a.issued() == before,
          "no tags minted in ten idle ticks — otherwise metadata grows forever and two "
          "peers trade phantom changes");
}

void observing_does_not_resolve_a_conflict() {
    g_current = "observing_does_not_resolve_a_conflict";
    /* Two devices edit one field. Each is shown ONE of the two values. If observing
     * that shown value counted as an edit, the next timer tick would resolve every
     * conflict in the document in favour of whatever `flatten` happened to pick —
     * and nobody would have been asked. */
    Replica a = make(kIdA), b = make(kIdB);
    observe(a, state(rune("rune_1", "base")));
    exchange(a, b);
    observe(a, state(rune("rune_1", "from a")));
    observe(b, state(rune("rune_1", "from b")));
    exchange(a, b);
    check(a.conflicts().size() == 1, "one conflict");

    for (int tick = 0; tick < 3; ++tick) {
        Doc fa = a.flatten();
        a.observe(fa.root);
        Doc fb = b.flatten();
        b.observe(fb.root);
        exchange(a, b);
    }
    check(a.conflicts().size() == 1 && b.conflicts().size() == 1,
          "still one conflict on both after three automatic ticks");
}

void choosing_the_other_value_is_an_edit() {
    g_current = "choosing_the_other_value_is_an_edit";
    Replica a = make(kIdA), b = make(kIdB);
    observe(a, state(rune("rune_1", "base")));
    exchange(a, b);
    observe(a, state(rune("rune_1", "from a")));
    observe(b, state(rune("rune_1", "from b")));
    exchange(a, b);
    std::string displayed = body_of(a, "rune_1");
    std::string other = displayed == "from a" ? "from b" : "from a";
    observe(a, state(rune("rune_1", other)));
    exchange(a, b);
    check(a.conflicts().empty() && b.conflicts().empty(), "picking the value NOT shown settles it");
    check(body_of(b, "rune_1") == other, "to that value");
}

void resolve_chooses_the_shown_value_on_purpose() {
    g_current = "resolve_chooses_the_shown_value_on_purpose";
    Replica a = make(kIdA), b = make(kIdB);
    observe(a, state(rune("rune_1", "base")));
    exchange(a, b);
    observe(a, state(rune("rune_1", "from a")));
    observe(b, state(rune("rune_1", "from b")));
    exchange(a, b);
    std::vector<Conflict> cs = a.conflicts();
    check(cs.size() == 1, "one conflict");
    if (cs.empty()) return;
    Doc delta;
    check(a.resolve(cs[0], 0, &delta), "resolved");
    std::string why;
    check(b.merge(delta, &why) == MergeResult::ok, "the resolution travels as a delta: " + why);
    check(a.conflicts().empty() && b.conflicts().empty(), "settled on both");
}

void a_stale_resolution_is_refused() {
    g_current = "a_stale_resolution_is_refused";
    /* Read a conflict, then a third device writes, then resolve what was read. The
     * resolution would overwrite a value the user never saw. */
    Replica a = make(kIdA), b = make(kIdB), c = make(kIdC);
    observe(a, state(rune("rune_1", "base")));
    exchange(a, b);
    exchange(a, c);
    observe(a, state(rune("rune_1", "from a")));
    observe(b, state(rune("rune_1", "from b")));
    exchange(a, b);
    std::vector<Conflict> read = a.conflicts();

    observe(c, state(rune("rune_1", "from c")));
    exchange(a, c);
    check(!read.empty() && !a.resolve(read[0], 0), "a conflict that has changed is not resolved");
    check(!a.conflicts().empty(), "and nothing was written");
}

void a_declared_join_is_not_collapsed_by_observing() {
    g_current = "a_declared_join_is_not_collapsed_by_observing";
    /* A policy is read-time only. Observing the value it showed must not write that
     * value back and turn a read-time answer into a stored one. */
    Replica a = make(kIdA), b = make(kIdB);
    JoinPolicy max;
    max.fields["content.body"] = FieldJoin::Max;
    a.set_policy(max);
    b.set_policy(max);
    const std::string num = "{\"spirit\":{\"id\":\"rune_1\",\"name\":\"n\"},\"glyph\":\"n\","
                            "\"content\":{\"body\":";
    observe(a, state(num + "1}}"));
    exchange(a, b);
    observe(a, state(num + "5}}"));
    observe(b, state(num + "9}}"));
    exchange(a, b);
    std::uint64_t before = a.issued();
    Doc f = a.flatten();
    Replica::Observed o = a.observe(f.root);
    check(o.ok && o.changes == 0 && a.issued() == before, "observing max(5, 9) writes nothing");
    Replica plain = make(kIdC);
    std::string why;
    plain.merge(a.doc(), &why);
    check(plain.conflicts().size() == 1,
          "and a replica WITHOUT the policy still sees both values — nothing was stored");
}

/* ── delete racing an edit ─────────────────────────────────────────────────── */

void a_delete_that_raced_an_edit_is_a_conflict() {
    g_current = "a_delete_that_raced_an_edit_is_a_conflict";
    Replica a = make(kIdA), b = make(kIdB);
    observe(a, state(rune("rune_1", "one") + "," + rune("rune_2", "two")));
    exchange(a, b);
    observe(a, state(rune("rune_2", "two")));            // a deletes rune_1
    observe(b, state(rune("rune_1", "edited on b") + "," + rune("rune_2", "two")));
    exchange(a, b);

    std::vector<Conflict> ca = a.conflicts(), cb = b.conflicts();
    check(ca.size() == 1 && ca[0].kind == ConflictKind::deleted_while_edited,
          "reported on the deleting device");
    check(cb.size() == 1 && cb[0].kind == ConflictKind::deleted_while_edited,
          "and on the editing device, where the rune just vanished from view");
    check(!ca.empty() && !cb.empty() && to_hex(ca[0].hash()) == to_hex(cb[0].hash()),
          "as one conflict with one name");
    check(!ca.empty() && ca[0].rune == "rune_1" && ca[0].field == "present", "located at the rune");
    check(!shows_rune(a, "rune_1") && !shows_rune(b, "rune_1"), "the deletion stands until asked");

    /* The deleting device observes its own output — the rune is absent there, as it
     * was. That is not a decision, and must not close the question. */
    Doc fa = a.flatten();
    a.observe(fa.root);
    check(a.conflicts().size() == 1, "observing the absence does not settle it");
}

void keeping_restores_it_with_the_edit() {
    g_current = "keeping_restores_it_with_the_edit";
    Replica a = make(kIdA), b = make(kIdB);
    observe(a, state(rune("rune_1", "one")));
    exchange(a, b);
    observe(a, state(""));
    observe(b, state(rune("rune_1", "edited on b")));
    exchange(a, b);
    std::vector<Conflict> cs = a.conflicts();
    std::size_t kept = 0;
    for (std::size_t i = 0; !cs.empty() && i < cs[0].sides.size(); ++i) {
        cJSON* v = decode(cs[0].sides[i]);
        if (v && std::string(v->valuestring) == "kept") kept = i;
        cJSON_Delete(v);
    }
    check(!cs.empty() && a.resolve(cs[0], kept), "kept");
    exchange(a, b);
    check(body_of(a, "rune_1") == "edited on b" && body_of(b, "rune_1") == "edited on b",
          "back on both devices, with the edit that raced the delete");
    check(a.conflicts().empty() && b.conflicts().empty(), "and settled");
}

void deleting_again_closes_it() {
    g_current = "deleting_again_closes_it";
    Replica a = make(kIdA), b = make(kIdB);
    observe(a, state(rune("rune_1", "one")));
    exchange(a, b);
    observe(a, state(""));
    observe(b, state(rune("rune_1", "edited on b")));
    exchange(a, b);
    std::vector<Conflict> cs = b.conflicts();
    std::size_t deleted = 0;
    for (std::size_t i = 0; !cs.empty() && i < cs[0].sides.size(); ++i) {
        cJSON* v = decode(cs[0].sides[i]);
        if (v && std::string(v->valuestring) == "deleted") deleted = i;
        cJSON_Delete(v);
    }
    check(!cs.empty() && b.resolve(cs[0], deleted), "deleted");
    exchange(a, b);
    check(!shows_rune(a, "rune_1") && !shows_rune(b, "rune_1"), "gone on both");
    check(a.conflicts().empty() && b.conflicts().empty(), "and the edit is now recorded as seen");
}

void a_clean_delete_is_not_a_conflict() {
    g_current = "a_clean_delete_is_not_a_conflict";
    Replica a = make(kIdA), b = make(kIdB);
    observe(a, state(rune("rune_1", "one")));
    exchange(a, b);
    observe(a, state(""));
    exchange(a, b);
    check(a.conflicts().empty() && b.conflicts().empty(), "nobody edited it, nobody is asked");
}

void a_rune_added_inside_a_deleted_mantle_is_one_question() {
    g_current = "a_rune_added_inside_a_deleted_mantle_is_one_question";
    Replica a = make(kIdA), b = make(kIdB);
    observe(a, state(rune("rune_1", "one") + "," + rune("rune_2", "two")));
    exchange(a, b);
    observe(a, "{\"mantles\":[]}");
    observe(b, state(rune("rune_1", "one") + "," + rune("rune_2", "two") + "," +
                     rune("rune_3", "added on b")));
    exchange(a, b);
    std::vector<Conflict> cs = a.conflicts();
    check(cs.size() == 1, "one question, not one per rune");
    check(!cs.empty() && cs[0].mantle == "notes" && cs[0].rune.empty(),
          "asked about the mantle that was deleted");
}

void an_edit_that_arrives_before_its_creation_is_not_a_deletion() {
    g_current = "an_edit_that_arrives_before_its_creation_is_not_a_deletion";
    /* Deltas are merged in whatever order they arrive. A rune never present HERE is
     * not a rune somebody deleted. */
    Replica a = make(kIdA), c = make(kIdC);
    Json s1(state(rune("rune_1", "one")));
    Replica::Observed created = a.observe(s1.p);
    Json s2(state(rune("rune_1", "edited")));
    Replica::Observed edited = a.observe(s2.p);
    std::string why;
    check(c.merge(edited.delta, &why) == MergeResult::ok, "the edit arrives first: " + why);
    check(c.conflicts().empty(), "no conflict for a rune that was never deleted");
    check(!shows_rune(c, "rune_1"), "and nothing shown yet");
    check(c.merge(created.delta, &why) == MergeResult::ok, "then the creation: " + why);
    check(body_of(c, "rune_1") == "edited", "and it appears with the later value");
}

void a_redeclaration_racing_an_undeclaration_is_a_conflict() {
    g_current = "a_redeclaration_racing_an_undeclaration_is_a_conflict";
    Replica a = make(kIdA), b = make(kIdB);
    observe(a, "{\"mantles\":[],\"glyphs\":{\"stat\":{\"glyph\":\"stat\",\"kind\":\"measure\"}}}");
    exchange(a, b);
    observe(a, "{\"mantles\":[]}");
    observe(b, "{\"mantles\":[],\"glyphs\":{\"stat\":{\"glyph\":\"stat\",\"kind\":\"act\"}}}");
    exchange(a, b);
    std::vector<Conflict> cs = a.conflicts();
    check(cs.size() == 1 && cs[0].glyph == "stat" &&
              cs[0].kind == ConflictKind::deleted_while_edited,
          "a type redefined while it was being removed is asked about");
}

/* ── identity ──────────────────────────────────────────────────────────────── */

void a_restored_backup_is_caught_before_it_reuses_a_tag() {
    g_current = "a_restored_backup_is_caught_before_it_reuses_a_tag";
    /* The same failure a crash between "send" and "save" produces: this replica's
     * counter is behind tags that already exist elsewhere, and it would mint them
     * again for different values. */
    Replica a = make(kIdA), b = make(kIdB);
    observe(a, state(rune("rune_1", "one")));
    std::string backup = a.to_bytes();
    observe(a, state(rune("rune_1", "after the backup")));
    exchange(a, b);

    Replica restored;
    std::string why;
    check(Replica::from_bytes(backup, restored, &why), "the backup loads: " + why);
    std::string before = canon_doc(restored.doc());
    check(restored.merge(b.doc(), &why) == MergeResult::identity_collision,
          "the restored replica refuses: " + why);
    check(canon_doc(restored.doc()) == before, "and merged nothing");

    Replica fresh;
    check(restored.fork("replica-A-restored-01", fresh, &why), "fork: " + why);
    check(fresh.merge(b.doc(), &why) == MergeResult::ok, "a fork continues safely: " + why);
    check(body_of(fresh, "rune_1") == "after the backup", "and catches up");
}

void a_device_provisioned_by_copying_a_replica_is_caught() {
    g_current = "a_device_provisioned_by_copying_a_replica_is_caught";
    /* Setting up a second device by handing it the first device's replica bytes
     * gives both the same id. Each then mints "<id>_N" for different things. */
    Replica a = make(kIdA);
    observe(a, state(rune("rune_1", "one")));
    Replica copy;
    std::string why;
    check(Replica::from_bytes(a.to_bytes(), copy, &why), "copied: " + why);
    observe(a, state(rune("rune_1", "one") + "," + rune("rune_2", "made on the original")));
    observe(copy, state(rune("rune_1", "one") + "," + rune("rune_3", "made on the copy")));
    check(a.merge(copy.doc(), &why) == MergeResult::identity_collision,
          "the two devices recognise they share an id: " + why);

    Replica provisioned;
    check(a.fork("replica-D-provisioned", provisioned, &why), "the right way is a fork: " + why);
    observe(provisioned, state(rune("rune_1", "one") + "," + rune("rune_4", "made on the new device")));
    check(a.merge(provisioned.doc(), &why) == MergeResult::ok, "which merges cleanly: " + why);
}

void ids_that_do_not_qualify_are_refused() {
    g_current = "ids_that_do_not_qualify_are_refused";
    Replica r;
    std::string why;
    check(!Replica::create("laptop", r, &why), "a name two people would both choose");
    check(!Replica::create("replica_with_underscore", r, &why), "the tag separator");
    check(!Replica::create(std::string(200, 'a'), r, &why), "absurdly long");
    Replica a = make(kIdA), f;
    check(!a.fork(kIdA, f, &why), "a fork to the same id is the failure it prevents");
}

void persistence_keeps_the_document_and_the_counter_together() {
    g_current = "persistence_keeps_the_document_and_the_counter_together";
    Replica a = make(kIdA);
    observe(a, state(rune("rune_1", "one") + "," + rune("rune_2", "two")));
    Replica back;
    std::string why;
    check(Replica::from_bytes(a.to_bytes(), back, &why), "round trip: " + why);
    check(back.id() == a.id() && back.issued() == a.issued(), "id and counter");
    check(canon_doc(back.doc()) == canon_doc(a.doc()), "document");
    observe(back, state(rune("rune_1", "one") + "," + rune("rune_2", "two") + "," +
                        rune("rune_3", "three")));
    check(back.issued() > a.issued(), "and it keeps counting from where it was");

    /* A document saved with a counter from an older save. */
    std::string bytes = a.to_bytes();
    std::string needle = "\"issued\":" + std::to_string(a.issued());
    std::size_t at = bytes.find(needle);
    check(at != std::string::npos, "found the counter");
    if (at != std::string::npos) {
        bytes.replace(at, needle.size(), "\"issued\":1");
        Replica stale;
        check(!Replica::from_bytes(bytes, stale, &why), "a counter behind its document is refused");
    }
    Replica junk;
    check(!Replica::from_bytes("{\"palabra_replica\":2}", junk, &why), "a newer shape is refused");
    check(!Replica::from_bytes("not json", junk, &why), "garbage is refused");
}

/* ── deltas ────────────────────────────────────────────────────────────────── */

void deltas_converge_in_any_order_and_repeated() {
    g_current = "deltas_converge_in_any_order_and_repeated";
    Replica a = make(kIdA);
    std::vector<std::string> history = {
        state(rune("rune_1", "one")),
        state(rune("rune_1", "one") + "," + rune("rune_2", "two")),
        state(rune("rune_2", "two, edited")),
        state(rune("rune_2", "two, edited") + "," + rune("rune_1", "one")),
        state(rune("rune_2", "two, edited", ",\"tags\":[\"done\"]") + "," + rune("rune_1", "one")),
    };
    std::vector<std::string> deltas;
    for (const auto& h : history) {
        Json s(h);
        Replica::Observed o = a.observe(s.p);
        check(o.ok, o.error);
        char* t = cJSON_PrintUnformatted(o.delta.root);
        deltas.push_back(t);
        cJSON_free(t);
    }
    std::mt19937 rng(20260916);
    for (int trial = 0; trial < 25; ++trial) {
        std::vector<std::string> inbox = deltas;
        inbox.insert(inbox.end(), deltas.begin(), deltas.begin() + 2);  // duplicates
        std::shuffle(inbox.begin(), inbox.end(), rng);
        Replica c = make(kIdC);
        std::string why;
        for (const auto& d : inbox) {
            Json dj(d);
            check(c.merge(dj.p, &why) == MergeResult::ok, why);
        }
        check(shown(c) == shown(a), "trial " + std::to_string(trial) + " diverged");
    }
}

void a_lost_delta_is_repaired_by_a_full_exchange() {
    g_current = "a_lost_delta_is_repaired_by_a_full_exchange";
    Replica a = make(kIdA), c = make(kIdC);
    Json s1(state(rune("rune_1", "one")));
    Replica::Observed d1 = a.observe(s1.p);
    Json s2(state(rune("rune_1", "one") + "," + rune("rune_2", "lost")));
    Replica::Observed d2 = a.observe(s2.p);
    Json s3(state(rune("rune_1", "one, edited") + "," + rune("rune_2", "lost")));
    Replica::Observed d3 = a.observe(s3.p);
    std::string why;
    c.merge(d1.delta, &why);
    c.merge(d3.delta, &why);  // d2 never arrives
    check(!shows_rune(c, "rune_2"), "the loss is silent — nothing in d3 mentions d2");
    c.merge(a.doc(), &why);
    check(shown(c) == shown(a), "one full exchange repairs it");
}

/* ── observe is atomic ─────────────────────────────────────────────────────── */

void a_state_that_cannot_be_recorded_changes_nothing() {
    g_current = "a_state_that_cannot_be_recorded_changes_nothing";
    Replica a = make(kIdA);
    observe(a, state(rune("rune_1", "one")));
    std::string before = canon_doc(a.doc());
    std::uint64_t issued = a.issued();

    Json dup("{\"mantles\":[{\"name\":\"x\",\"runes\":[]},{\"name\":\"x\",\"runes\":[]}]}");
    Replica::Observed o = a.observe(dup.p);
    check(!o.ok && !o.error.empty(), "duplicate mantle names are refused");
    check(canon_doc(a.doc()) == before, "and the document is untouched");

    cJSON* bad = cJSON_Parse(state(rune("rune_1", "one")).c_str());
    cJSON* first_mantle = cJSON_GetObjectItem(bad, "mantles")->child;
    cJSON* first_rune = cJSON_GetObjectItem(first_mantle, "runes")->child;
    cJSON* body = cJSON_GetObjectItem(cJSON_GetObjectItem(first_rune, "content"), "body");
    check(body != nullptr, "found the body to damage");
    if (body) cJSON_SetValuestring(body, "\xC3\x28");  // invalid UTF-8
    Replica::Observed o2 = a.observe(bad);
    cJSON_Delete(bad);
    check(!o2.ok, "text with no honest byte form is refused");
    check(canon_doc(a.doc()) == before, "and still nothing changed");
    check(a.issued() >= issued, "the counter never moves backwards");
    observe(a, state(rune("rune_1", "two")));
    check(body_of(a, "rune_1") == "two", "and the replica carries on");
}

}  // namespace

int main() {
    a_deletion_propagates_instead_of_coming_back();
    re_enriching_every_exchange_resurrects_deletions();
    a_removed_content_key_is_absent_not_null();
    undo_brings_back_the_same_rune_after_a_synced_delete();
    mantle_and_glyph_removals_propagate();
    mantle_tags_rules_and_relations_survive_the_loop();

    observing_its_own_output_records_nothing();
    observing_does_not_resolve_a_conflict();
    choosing_the_other_value_is_an_edit();
    resolve_chooses_the_shown_value_on_purpose();
    a_stale_resolution_is_refused();
    a_declared_join_is_not_collapsed_by_observing();

    a_delete_that_raced_an_edit_is_a_conflict();
    keeping_restores_it_with_the_edit();
    deleting_again_closes_it();
    a_clean_delete_is_not_a_conflict();
    a_rune_added_inside_a_deleted_mantle_is_one_question();
    an_edit_that_arrives_before_its_creation_is_not_a_deletion();
    a_redeclaration_racing_an_undeclaration_is_a_conflict();

    a_restored_backup_is_caught_before_it_reuses_a_tag();
    a_device_provisioned_by_copying_a_replica_is_caught();
    ids_that_do_not_qualify_are_refused();
    persistence_keeps_the_document_and_the_counter_together();

    deltas_converge_in_any_order_and_repeated();
    a_lost_delta_is_repaired_by_a_full_exchange();

    a_state_that_cannot_be_recorded_changes_nothing();

    std::printf("replica: %d checks", g_checks);
    if (g_failures.empty()) {
        std::printf(", all passed\n");
        return 0;
    }
    std::printf(", %d FAILED\n", static_cast<int>(g_failures.size()));
    for (const std::string& f : g_failures) std::printf("  - %s\n", f.c_str());
    return 1;
}
