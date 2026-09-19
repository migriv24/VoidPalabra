/* merge_rules_test.cpp — what a merge breaks that no device broke, and what a
 * document names that it does not hold.
 *
 * Two questions from two machines testing a shared database produced these. One:
 * two members each made `note-1`, and after the merge every command naming `note-1`
 * was ambiguous. Two: a picture added after one member joined never reached the
 * other. Answered here as the classes they belong to, not as those two cases —
 * every scenario below runs through real replicas merging, because every one of
 * them exists only BECAUSE a merge joined two devices' valid work.
 */
#include "voidpalabra/replica.hpp"
#include "voidpalabra/references.hpp"
#include "voidpalabra/store.hpp"
#include "voidpalabra/canonical.hpp"

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

struct Json {
    cJSON* p;
    explicit Json(const std::string& text) : p(cJSON_Parse(text.c_str())) {}
    ~Json() { if (p) cJSON_Delete(p); }
    Json(const Json&) = delete;
    Json& operator=(const Json&) = delete;
};

Replica make(const char* id) {
    Replica r;
    std::string why;
    check(Replica::create(id, r, &why), why);
    return r;
}

void observe(Replica& r, const std::string& text) {
    Json s(text);
    Replica::Observed o = r.observe(s.p);
    check(o.ok, "observe: " + o.error);
}

void exchange(Replica& a, Replica& b) {
    std::string why;
    Doc da;
    da.root = cJSON_Duplicate(a.doc().root, 1);
    check(b.merge(da, &why) == MergeResult::ok, why);
    check(a.merge(b.doc(), &why) == MergeResult::ok, why);
}

std::string rune(const std::string& id, const std::string& name,
                 const std::string& glyph = "text", const std::string& content = "{}") {
    return "{\"spirit\":{\"id\":\"" + id + "\",\"name\":\"" + name + "\"},\"glyph\":\"" +
           glyph + "\",\"content\":" + content + "}";
}

std::string mantle(const std::string& name, const std::string& runes,
                   const std::string& edges = "") {
    return "{\"id\":\"" + name + "-id\",\"name\":\"" + name + "\",\"runes\":[" + runes +
           "],\"layout\":{\"edges\":[" + edges + "]}}";
}

std::string doc(const std::string& mantles, const std::string& glyphs = "") {
    return "{\"mantles\":[" + mantles + "]" + (glyphs.empty() ? "" : ",\"glyphs\":" + glyphs) + "}";
}

std::string edge(const std::string& from, const std::string& to) {
    return "{\"from\":\"" + from + "\",\"to\":\"" + to + "\",\"relation\":\"supports\"}";
}

const char* kA = "merge-rules-A-00001";
const char* kB = "merge-rules-B-00001";

/* ── rules a merge can break ──────────────────────────────────────────────── */

void two_devices_that_each_mint_one_name_share_it_after_the_merge() {
    g_current = "two_devices_that_each_mint_one_name_share_it_after_the_merge";
    Replica a = make(kA), b = make(kB);
    observe(a, doc(mantle("notes", rune("rune_a1", "note-1"))));
    observe(b, doc(mantle("notes", rune("rune_b1", "note-1"))));
    check(a.anomalies().empty() && b.anomalies().empty(), "each device alone is fine");
    exchange(a, b);

    std::vector<Anomaly> xa = a.anomalies(), xb = b.anomalies();
    check(xa.size() == 1 && xa[0].kind == AnomalyKind::duplicate_name, "one duplicate name");
    check(!xa.empty() && xa[0].subject == "note-1" && xa[0].mantle == "notes", "located");
    check(!xa.empty() && xa[0].runes == std::vector<std::string>({"rune_a1", "rune_b1"}),
          "naming both runes, by id — the one thing that still tells them apart");
    check(xb.size() == 1 && !xa.empty() && to_hex(xa[0].hash()) == to_hex(xb[0].hash()),
          "one problem with one name on both devices");
    check(a.conflicts().empty(), "and it is not a conflict: no value is in dispute");

    /* The fix is an ordinary edit, and the anomaly goes away on its own. */
    observe(a, doc(mantle("notes", rune("rune_a1", "note-1") + "," + rune("rune_b1", "note-1b"))));
    exchange(a, b);
    check(a.anomalies().empty() && b.anomalies().empty(), "renaming one clears it everywhere");
}

void a_removed_rune_cannot_collide() {
    g_current = "a_removed_rune_cannot_collide";
    Replica a = make(kA), b = make(kB);
    observe(a, doc(mantle("notes", rune("rune_a1", "note-1"))));
    exchange(a, b);
    observe(a, doc(mantle("notes", "")));
    observe(b, doc(mantle("notes", rune("rune_a1", "note-1") + "," + rune("rune_b2", "note-2"))));
    exchange(a, b);
    observe(b, doc(mantle("notes", rune("rune_b2", "note-1"))));  // b reuses the name
    exchange(a, b);
    std::size_t duplicates = 0;
    for (const Anomaly& x : a.anomalies())
        if (x.kind == AnomalyKind::duplicate_name) ++duplicates;
    check(duplicates == 0, "a name held only by something removed is free to reuse");
    Doc f = a.flatten();
    char* text = cJSON_PrintUnformatted(f.root);
    check(text && std::string(text).find("\"name\":\"note-1\"") != std::string::npos,
          "and the reuse is really there to have collided");
    cJSON_free(text);
}

void a_link_to_something_deleted_elsewhere_is_reported() {
    g_current = "a_link_to_something_deleted_elsewhere_is_reported";
    Replica a = make(kA), b = make(kB);
    std::string both = rune("rune_1", "intro") + "," + rune("rune_2", "methods");
    observe(a, doc(mantle("paper", both)));
    exchange(a, b);
    observe(a, doc(mantle("paper", rune("rune_2", "methods"))));                // a deletes intro
    observe(b, doc(mantle("paper", both, edge("methods", "intro"))));           // b links to it
    exchange(a, b);
    std::vector<Anomaly> xs = a.anomalies();
    check(xs.size() == 1 && xs[0].kind == AnomalyKind::link_broken && xs[0].cause == "removed",
          "the link a concurrent removal broke");
    check(!xs.empty() && xs[0].subject == "intro" && xs[0].runes == std::vector<std::string>({"rune_1"}),
          "naming the endpoint and the rune that held it");
}

void a_link_to_something_renamed_elsewhere_is_reported() {
    g_current = "a_link_to_something_renamed_elsewhere_is_reported";
    Replica a = make(kA), b = make(kB);
    std::string both = rune("rune_1", "intro") + "," + rune("rune_2", "methods");
    observe(a, doc(mantle("paper", both)));
    exchange(a, b);
    observe(a, doc(mantle("paper", rune("rune_1", "overview") + "," + rune("rune_2", "methods"))));
    observe(b, doc(mantle("paper", both, edge("methods", "intro"))));
    exchange(a, b);
    std::vector<Anomaly> xs = b.anomalies();
    check(xs.size() == 1 && xs[0].cause == "renamed",
          "a link drawn to a name while the name was changed elsewhere");
}

void a_link_that_was_always_dangling_is_not_an_anomaly() {
    g_current = "a_link_that_was_always_dangling_is_not_an_anomaly";
    /* Void Core allows a link to something not written yet, on purpose. The merge
     * did not break it, so it is not reported — that is what keeps this from being a
     * second copy of Core's `validate`. */
    Replica a = make(kA), b = make(kB);
    observe(a, doc(mantle("paper", rune("rune_1", "intro"), edge("intro", "future-work"))));
    exchange(a, b);
    check(a.anomalies().empty(), "an intentional forward link is left alone");
}

void a_link_into_another_mantle_is_checked_there() {
    g_current = "a_link_into_another_mantle_is_checked_there";
    Replica a = make(kA), b = make(kB);
    std::string lib = mantle("library", rune("rune_9", "source"));
    std::string link = "{\"from\":\"cite\",\"to\":{\"mantle\":\"library\",\"rune\":\"source\"}}";
    observe(a, doc(mantle("paper", rune("rune_1", "cite")) + "," + lib));
    exchange(a, b);
    observe(a, doc(mantle("paper", rune("rune_1", "cite")) + "," + mantle("library", "")));
    observe(b, doc(mantle("paper", rune("rune_1", "cite"), link) + "," + lib));
    exchange(a, b);
    std::vector<Anomaly> xs = a.anomalies();
    check(xs.size() == 1 && xs[0].subject == "library/source" && xs[0].mantle == "paper",
          "reported on the mantle holding the link, naming where it pointed");
}

void a_type_removed_while_in_use_elsewhere_is_reported() {
    g_current = "a_type_removed_while_in_use_elsewhere_is_reported";
    /* Core refuses `glyph undeclare` while runes carry the glyph — on one device.
     * Across two, one member undeclares an unused type while another starts using
     * it. Core raised exactly this case when it moved declarations into the
     * document. */
    Replica a = make(kA), b = make(kB);
    std::string decl = "{\"stat\":{\"glyph\":\"stat\",\"kind\":\"measure\"}}";
    observe(a, doc(mantle("game", ""), decl));
    exchange(a, b);
    observe(a, doc(mantle("game", "")));                                     // a undeclares
    observe(b, doc(mantle("game", rune("rune_s", "speed", "stat")), decl));  // b uses it
    exchange(a, b);
    bool found = false;
    for (const Anomaly& x : a.anomalies())
        if (x.kind == AnomalyKind::type_removed && x.subject == "stat" &&
            x.runes == std::vector<std::string>({"rune_s"}))
            found = true;
    check(found, "the rune left holding a type that no longer exists");
}

void anomalies_render_and_order_deterministically() {
    g_current = "anomalies_render_and_order_deterministically";
    Replica a = make(kA), b = make(kB);
    observe(a, doc(mantle("m1", rune("r_a", "x")) + "," + mantle("m2", rune("r_c", "y"))));
    observe(b, doc(mantle("m1", rune("r_b", "x")) + "," + mantle("m2", rune("r_d", "y"))));
    exchange(a, b);
    std::vector<Anomaly> xa = a.anomalies(), xb = b.anomalies();
    check(xa.size() == 2 && xb.size() == 2, "two anomalies");
    for (std::size_t i = 0; i < xa.size() && i < xb.size(); ++i)
        check(to_hex(xa[i].hash()) == to_hex(xb[i].hash()), "listed identically on both");
    if (!xa.empty()) {
        cJSON* j = anomaly_to_json(xa[0]);
        char* s = cJSON_PrintUnformatted(j);
        std::string text = s;
        cJSON_free(s);
        cJSON_Delete(j);
        check(text.find("\"kind\":\"duplicate_name\"") != std::string::npos, "rendered: " + text);
    }
}

/* ── what a document names and does not hold ──────────────────────────────── */

std::string digest_of(const std::string& bytes) { return to_hex(sha256(bytes)); }

void a_file_referenced_after_joining_is_on_the_want_list() {
    g_current = "a_file_referenced_after_joining_is_on_the_want_list";
    /* A picture added on one device after the other joined. The document syncs; the
     * file does not. The receiver must be able to say what it lacks, the sender that
     * it lacks nothing — without either fact entering the shared document. */
    const std::string picture = "\x89PNG pretend bytes";
    const std::string addr = digest_of(picture);
    BlockStore sender_files, receiver_files;
    sender_files.put(picture);

    Replica a = make(kA), b = make(kB);
    observe(a, doc(mantle("people", "")));
    exchange(a, b);
    observe(a, doc(mantle("people", rune("rune_p", "ada", "person",
                                         "{\"photo\":\"assets/" + addr + ".png\"}"))));
    exchange(a, b);

    ReferencePolicy policy;
    policy.fields["content.photo"] = sha256_hex_anywhere();
    Doc fa = a.flatten(), fb = b.flatten();
    std::vector<Reference> ra = references(fa.root, policy), rb = references(fb.root, policy);
    check(ra.size() == 1 && rb.size() == 1, "both devices see one reference");
    check(!rb.empty() && rb[0].address == addr && rb[0].rune == "rune_p" && rb[0].field == "content.photo",
          "to the right file, from the right rune");
    check(missing(ra, held_by(sender_files)).empty(), "the sender lacks nothing");
    std::vector<std::string> want = missing(rb, held_by(receiver_files));
    check(want == std::vector<std::string>({addr}), "the receiver's want-list is that one file");

    /* A peer answers with bytes. They are checked before they are stored. */
    check(!content_matches(addr, picture + "tampered"), "the wrong bytes are refused");
    check(content_matches(addr, picture), "the right ones accepted");
    receiver_files.put(picture);
    check(missing(rb, held_by(receiver_files)).empty(), "and the want-list empties");
}

void only_declared_fields_are_read() {
    g_current = "only_declared_fields_are_read";
    /* Content means what the application says it means. A 64-character hex string in
     * a field nobody declared is somebody's data, not a file to go and fetch. */
    std::string h = digest_of("x");
    Json s(doc(mantle("m", rune("r", "n", "text",
                                "{\"photo\":\"" + h + "\",\"checksum\":\"" + h + "\"}"))));
    ReferencePolicy policy;
    policy.fields["content.photo"] = sha256_hex_anywhere();
    std::vector<Reference> rs = references(s.p, policy);
    check(rs.size() == 1 && rs[0].field == "content.photo", "the undeclared field is ignored");

    ReferencePolicy everything;
    everything.fields["content.*"] = sha256_hex_anywhere();
    check(references(s.p, everything).size() == 2, "a prefix rule covers both");
}

void the_default_finder_finds_addresses_and_only_addresses() {
    g_current = "the_default_finder_finds_addresses_and_only_addresses";
    std::string h1 = digest_of("one"), h2 = digest_of("two");
    std::string upper = h1;
    for (char& c : upper) if (c >= 'a' && c <= 'f') c = static_cast<char>(c - 32);
    Json v("{\"gallery\":[\"a/" + h1 + ".jpg\",{\"thumb\":\"" + h2 + "\"}],"
           "\"long\":\"" + h1 + "0\",\"upper\":\"" + upper + "\"}");
    std::set<std::string> found;
    sha256_hex_anywhere()(v.p, found);
    check(found == std::set<std::string>({h1, h2}),
          "inside paths, at any depth — and not a 65-character run or uppercase hex");
}

void a_type_can_name_files_too() {
    g_current = "a_type_can_name_files_too";
    /* A glyph declaration's `presentations` may name a sprite or a font. Declarations
     * sync; their files would not. */
    std::string sprite = digest_of("sprite");
    Json s(doc(mantle("m", ""), "{\"hero\":{\"glyph\":\"hero\",\"presentations\":"
                               "{\"canvas\":{\"sprite\":\"sprites/" + sprite + ".png\"}}}}"));
    ReferencePolicy policy;
    policy.fields["descriptor"] = sha256_hex_anywhere();
    std::vector<Reference> rs = references(s.p, policy);
    check(rs.size() == 1 && rs[0].glyph == "hero" && rs[0].mantle.empty() && rs[0].address == sprite,
          "located at the declaration, not in any mantle");
}

void every_kept_version_decides_what_may_be_deleted() {
    g_current = "every_kept_version_decides_what_may_be_deleted";
    /* A file the current version no longer names may still be needed: an archive keeps
     * old versions, and opening one needs its files. The set a device must keep is
     * the union over the versions it keeps. */
    std::string old_photo = digest_of("old"), new_photo = digest_of("new");
    Json v1(doc(mantle("p", rune("r", "ada", "person", "{\"photo\":\"" + old_photo + "\"}"))));
    Json v2(doc(mantle("p", rune("r", "ada", "person", "{\"photo\":\"" + new_photo + "\"}"))));
    ReferencePolicy policy;
    policy.fields["content.photo"] = sha256_hex_anywhere();
    std::set<std::string> keep;
    for (const cJSON* v : {v1.p, v2.p})
        for (const Reference& r : references(v, policy)) keep.insert(r.address);
    check(keep.count(old_photo) && keep.count(new_photo),
          "the replaced photo is still needed while the version that used it is kept");
}

}  // namespace

int main() {
    two_devices_that_each_mint_one_name_share_it_after_the_merge();
    a_removed_rune_cannot_collide();
    a_link_to_something_deleted_elsewhere_is_reported();
    a_link_to_something_renamed_elsewhere_is_reported();
    a_link_that_was_always_dangling_is_not_an_anomaly();
    a_link_into_another_mantle_is_checked_there();
    a_type_removed_while_in_use_elsewhere_is_reported();
    anomalies_render_and_order_deterministically();

    a_file_referenced_after_joining_is_on_the_want_list();
    only_declared_fields_are_read();
    the_default_finder_finds_addresses_and_only_addresses();
    a_type_can_name_files_too();
    every_kept_version_decides_what_may_be_deleted();

    std::printf("merge rules: %d checks", g_checks);
    if (g_failures.empty()) {
        std::printf(", all passed\n");
        return 0;
    }
    std::printf(", %d FAILED\n", static_cast<int>(g_failures.size()));
    for (const std::string& f : g_failures) std::printf("  - %s\n", f.c_str());
    return 1;
}
