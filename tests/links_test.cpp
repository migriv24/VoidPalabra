/* links_test.cpp — concurrent structural edits, and the answers that need no one to
 * coordinate.
 *
 * Three questions a multi-user application asks the moment two people can change a
 * structure at once, each answered by a pure read of the merged document:
 *
 *   - two rewrites that share a boundary each rewrote half of it: does the merge
 *     hold the whole? (equivalence: a wire is a class of segments, fused);
 *   - two members each used the one thing only one may use: who is told?
 *     (capacity: one wire per port, one booking per seat);
 *   - two members each moved a folder into the other: (acyclic).
 *
 * And the two seams that make "last one wins" honest and "who did this" answerable:
 * the Lamport counter behind `FieldJoin::Latest`, and `writers`.
 *
 * Every merged answer is checked in BOTH merge orders. An answer that depended on
 * which peer merged first would be a divergence wearing a rule's clothes.
 */
#include "voidpalabra/replica.hpp"
#include "voidpalabra/links.hpp"
#include "voidpalabra/canonical.hpp"
#include "voidpalabra/join.hpp"

#include "cJSON.h"

#include <algorithm>
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
    check(Replica::create(id, r, &why), std::string("create ") + id + ": " + why);
    return r;
}

std::string rune(const std::string& id, const std::string& glyph = "agent",
                 const std::string& content = "{}") {
    return "{\"spirit\":{\"id\":\"" + id + "\",\"name\":\"" + id + "\"},\"glyph\":\"" + glyph +
           "\",\"content\":" + content + "}";
}

std::string edge(const std::string& from, const std::string& to, const std::string& relation) {
    return "{\"from\":\"" + from + "\",\"to\":\"" + to + "\",\"relation\":\"" + relation + "\"}";
}

std::string state(const std::vector<std::string>& runes, const std::vector<std::string>& edges) {
    std::string rs, es;
    for (const auto& r : runes) rs += (rs.empty() ? "" : ",") + r;
    for (const auto& e : edges) es += (es.empty() ? "" : ",") + e;
    return "{\"mantles\":[{\"id\":\"m\",\"name\":\"net\",\"runes\":[" + rs +
           "],\"layout\":{\"edges\":[" + es + "]}}]}";
}

void observe(Replica& r, const std::string& text) {
    Json s(text);
    Replica::Observed o = r.observe(s.p);
    check(o.ok, "observe: " + o.error);
}

void merge(Replica& into, const Replica& from) {
    std::string why;
    check(into.merge(from.doc(), &why) == MergeResult::ok, "merge: " + why);
}

std::string name_of(const Replica& r) {
    Doc f = r.flatten();
    return version_name(f.root);
}

std::string printed(const cJSON* v) {
    char* s = cJSON_PrintUnformatted(const_cast<cJSON*>(v));
    std::string out = s ? s : "";
    if (s) cJSON_free(s);
    return out;
}

std::string violations_of(const Replica& r, const LinkRules& rules) {
    Doc f = r.flatten();
    std::string out;
    for (const Violation& v : check_links(f.root, rules)) {
        cJSON* j = violation_to_json(v);
        out += printed(j) + " #" + to_hex(v.hash()) + "\n";
        cJSON_Delete(j);
    }
    return out;
}

/* Two replicas that exchanged A→B and B→A in opposite orders must agree on
 * everything a reader derives. */
void same_everywhere(Replica& a, Replica& b, const LinkRules& rules, const std::string& what) {
    merge(a, b);
    merge(b, a);
    check(name_of(a) == name_of(b), what + ": both peers hold one state");
    check(violations_of(a, rules) == violations_of(b, rules), what + ": and see the same violations");
}

/* The wire rules of an interaction net written as runes (the encoding Void Maiz
 * proposed as option (a)): an agent's port attaches to a WIRE rune with a link
 * "port:0"; a rewrite that joins two wires FUSES them with a link "=". */
LinkRules wire_rules() {
    LinkRules r;
    r.equivalence = {"="};
    Capacity port;
    port.name = "one wire per port";
    port.slot = Slot::from_port;
    port.max = 1;
    r.capacity.push_back(port);
    Capacity ends;
    ends.name = "a wire has two ends";
    ends.slot = Slot::to;
    ends.max = 2;
    ends.through_equivalence = true;
    r.capacity.push_back(ends);
    return r;
}

/* ── the shared boundary wire ────────────────────────────────────────────── */

/* Maiz's §3.1, measured. Two disjoint active pairs (a,b) and (c,d), an auxiliary
 * wire w from a.1 to c.2. Peer 1 steps (a,b); peer 2 steps (c,d), concurrently.
 * The mathematics says the result wires a'.1 to c'.1. */
void two_rewrites_sharing_a_wire_commute_when_the_wire_is_a_class() {
    g_current = "two_rewrites_sharing_a_wire_commute_when_the_wire_is_a_class";
    const std::vector<std::string> agents = {rune("a"), rune("b"), rune("c"), rune("d"),
                                             rune("p", "wire"), rune("q", "wire"),
                                             rune("w", "wire")};
    const std::vector<std::string> wiring = {
        edge("a", "p", "0:0"), edge("b", "p", "0:0"),  // (a,b) meet on principal ports
        edge("c", "q", "0:0"), edge("d", "q", "0:0"),  // (c,d) too
        edge("a", "w", "1:0"), edge("c", "w", "2:0"),  // the shared auxiliary wire
    };
    Replica one = make("replica-one-0123456789");
    observe(one, state(agents, wiring));
    Replica two;
    check(one.fork("replica-two-0123456789", two), "fork");

    /* Peer 1 consumes a and b and mints a', whose port 1 takes over a's end of w:
     * a new segment w1, fused to w. It writes NOTHING about c's end. */
    observe(one, state({rune("c"), rune("d"), rune("q", "wire"), rune("w", "wire"),
                        rune("a2"), rune("w1", "wire")},
                       {edge("c", "q", "0:0"), edge("d", "q", "0:0"), edge("c", "w", "2:0"),
                        edge("a2", "w1", "1:0"), edge("w1", "w", "=")}));
    /* Peer 2, concurrently, consumes c and d and mints c', symmetric. */
    observe(two, state({rune("a"), rune("b"), rune("p", "wire"), rune("w", "wire"),
                        rune("c2"), rune("w2", "wire")},
                       {edge("a", "p", "0:0"), edge("b", "p", "0:0"), edge("a", "w", "1:0"),
                        edge("c2", "w2", "1:0"), edge("w2", "w", "=")}));

    LinkRules rules = wire_rules();
    same_everywhere(one, two, rules, "after both steps");

    Doc f = one.flatten();
    Quotient q = quotient(f.root, rules);
    check(q.classes.size() == 1 && q.classes[0].size() == 3,
          "w, w1 and w2 are one wire");
    RuneRef wire = q.representative({"net", "w1"});
    std::vector<std::string> ends;
    for (const Link& l : resolved_links(f.root))
        if (l.relation != "=" && q.representative(l.to) == wire) ends.push_back(l.from.id);
    std::sort(ends.begin(), ends.end());
    check(ends == std::vector<std::string>({"a2", "c2"}),
          "and its two ends are a'.1 and c'.1 — the wire nobody wrote, read from what both wrote");
    check(check_links(f.root, rules).empty(), "one wire per port, two ends per wire: nothing to report");
    check(one.anomalies().empty(), "and no link was broken: every link names a live rune");
}

/* The same two steps with a wire written as ONE link between its endpoints — the
 * encoding that fails. Kept as a test so the failure stays measured. */
void the_same_rewrites_lose_the_wire_when_a_wire_is_one_link() {
    g_current = "the_same_rewrites_lose_the_wire_when_a_wire_is_one_link";
    Replica one = make("replica-one-0123456789");
    observe(one, state({rune("a"), rune("b"), rune("c"), rune("d")},
                       {edge("a", "b", "0:0"), edge("c", "d", "0:0"), edge("a", "c", "1:2")}));
    Replica two;
    check(one.fork("replica-two-0123456789", two), "fork");
    observe(one, state({rune("c"), rune("d"), rune("a2")},
                       {edge("c", "d", "0:0"), edge("a2", "c", "1:2")}));
    observe(two, state({rune("a"), rune("b"), rune("c2")},
                       {edge("a", "b", "0:0"), edge("a", "c2", "1:1")}));
    merge(one, two);
    Doc f = one.flatten();
    bool joined = false;
    for (const Link& l : resolved_links(f.root))
        if ((l.from.id == "a2" && l.to.id == "c2") || (l.from.id == "c2" && l.to.id == "a2")) joined = true;
    check(!joined, "no link joins a' to c': each peer wrote half, and halves of a link are not a link");
    check(one.anomalies().size() == 2, "and the merge reports two broken links, one per half");
}

/* ── capacity ────────────────────────────────────────────────────────────── */

void two_wires_on_one_port_are_reported_the_same_everywhere() {
    g_current = "two_wires_on_one_port_are_reported_the_same_everywhere";
    Replica a = make("replica-A-0123456789");
    observe(a, state({rune("x"), rune("y"), rune("z"), rune("u", "wire"), rune("v", "wire")},
                     {}));
    Replica b;
    check(a.fork("replica-B-0123456789", b), "fork");
    /* Both people see x's port 2 free, and both wire it. */
    observe(a, state({rune("x"), rune("y"), rune("z"), rune("u", "wire"), rune("v", "wire")},
                     {edge("x", "u", "2:0"), edge("y", "u", "0:0")}));
    observe(b, state({rune("x"), rune("y"), rune("z"), rune("u", "wire"), rune("v", "wire")},
                     {edge("x", "v", "2:0"), edge("z", "v", "0:0")}));
    LinkRules rules = wire_rules();
    check(violations_of(a, rules).empty(), "each device kept the rule");
    same_everywhere(a, b, rules, "after the merge");

    Doc f = a.flatten();
    std::vector<Violation> v = check_links(f.root, rules);
    check(v.size() == 1, "one violation");
    if (v.size() == 1) {
        check(v[0].kind == ViolationKind::over_capacity && v[0].rule == "one wire per port",
              "named by the host's rule");
        check(v[0].slot == "x:2", "at the port, not merely the rune");
        check(v[0].links.size() == 2, "with both competing links, neither chosen");
    }
}

void a_seat_booked_twice_is_one_violation_listing_both() {
    g_current = "a_seat_booked_twice_is_one_violation_listing_both";
    LinkRules rules;
    Capacity seat;
    seat.name = "one booking per seat";
    seat.relation = "booked";
    seat.slot = Slot::to;
    rules.capacity.push_back(seat);

    Replica a = make("replica-A-0123456789");
    observe(a, state({rune("seat-12", "seat"), rune("ana", "person"), rune("bo", "person")}, {}));
    Replica b;
    check(a.fork("replica-B-0123456789", b), "fork");
    observe(a, state({rune("seat-12", "seat"), rune("ana", "person"), rune("bo", "person")},
                     {edge("ana", "seat-12", "booked")}));
    observe(b, state({rune("seat-12", "seat"), rune("ana", "person"), rune("bo", "person")},
                     {edge("bo", "seat-12", "booked")}));
    same_everywhere(a, b, rules, "a double booking");
    Doc f = a.flatten();
    std::vector<Violation> v = check_links(f.root, rules);
    check(v.size() == 1 && v[0].slot == "seat-12" &&
              v[0].runes == std::vector<std::string>({"ana", "bo", "seat-12"}),
          "one violation at the seat, naming everyone involved");

    /* Resolving it is an ordinary edit, and the violation is gone when the state no
     * longer breaks the rule. */
    observe(a, state({rune("seat-12", "seat"), rune("ana", "person"), rune("bo", "person")},
                     {edge("ana", "seat-12", "booked")}));
    merge(b, a);
    check(violations_of(a, rules).empty() && violations_of(b, rules).empty(),
          "an edit that unbooks one clears it everywhere");
}

/* ── equivalence ─────────────────────────────────────────────────────────── */

void sameness_asserted_on_two_devices_is_one_class() {
    g_current = "sameness_asserted_on_two_devices_is_one_class";
    LinkRules rules;
    rules.equivalence = {"same-as"};
    Replica a = make("replica-A-0123456789");
    observe(a, state({rune("x", "contact"), rune("y", "contact"), rune("z", "contact"),
                      rune("k", "contact")}, {}));
    Replica b;
    check(a.fork("replica-B-0123456789", b), "fork");
    /* A says x is y. B, not having seen that, says z is y. */
    observe(a, state({rune("x", "contact"), rune("y", "contact"), rune("z", "contact"),
                      rune("k", "contact")}, {edge("x", "y", "same-as")}));
    observe(b, state({rune("x", "contact"), rune("y", "contact"), rune("z", "contact"),
                      rune("k", "contact")}, {edge("z", "y", "same-as")}));
    same_everywhere(a, b, rules, "two merges of contacts");
    Doc f = a.flatten();
    Quotient q = quotient(f.root, rules);
    check(q.classes.size() == 1 && q.classes[0].size() == 3, "x, y and z are one person");
    check(q.representative({"net", "z"}).id == "x", "represented by the least id, on every peer");
    check(q.representative({"net", "k"}).id == "k", "and k is only itself");
}

/* ── acyclic ─────────────────────────────────────────────────────────────── */

void two_moves_that_make_a_cycle_are_reported() {
    g_current = "two_moves_that_make_a_cycle_are_reported";
    LinkRules rules;
    rules.acyclic = {"in"};
    Capacity parent;
    parent.name = "one parent";
    parent.relation = "in";
    parent.slot = Slot::from;
    rules.capacity.push_back(parent);

    const std::vector<std::string> folders = {rune("root", "folder"), rune("f1", "folder"),
                                              rune("f2", "folder")};
    Replica a = make("replica-A-0123456789");
    observe(a, state(folders, {edge("f1", "root", "in"), edge("f2", "root", "in")}));
    Replica b;
    check(a.fork("replica-B-0123456789", b), "fork");
    /* A moves f1 into f2; B moves f2 into f1. Each is a valid move where it was made. */
    observe(a, state(folders, {edge("f1", "f2", "in"), edge("f2", "root", "in")}));
    observe(b, state(folders, {edge("f1", "root", "in"), edge("f2", "f1", "in")}));
    check(violations_of(a, rules).empty() && violations_of(b, rules).empty(), "valid on each device");
    same_everywhere(a, b, rules, "the two moves");

    Doc f = a.flatten();
    std::vector<Violation> v = check_links(f.root, rules);
    check(v.size() == 1 && v[0].kind == ViolationKind::cycle &&
              v[0].runes == std::vector<std::string>({"f1", "f2"}) && v[0].links.size() == 2,
          "one cycle, f1 and f2, with the two links that close it; each still has one parent");
}

void a_long_chain_and_a_self_loop() {
    g_current = "a_long_chain_and_a_self_loop";
    LinkRules rules;
    rules.acyclic = {"*"};
    std::string es;
    std::string rs;
    const int n = 20000;
    for (int i = 0; i < n; ++i) {
        rs += (i ? "," : "") + rune("n" + std::to_string(i));
        if (i) es += (i > 1 ? "," : "") + edge("n" + std::to_string(i - 1), "n" + std::to_string(i), "next");
    }
    std::string text = "{\"mantles\":[{\"id\":\"m\",\"name\":\"net\",\"runes\":[" + rs +
                       "],\"layout\":{\"edges\":[" + es + "]}}]}";
    Json s(text);
    check(check_links(s.p, rules).empty(), "a 20,000-link chain is acyclic, and walking it does not overflow the stack");

    Json loop(state({rune("a")}, {edge("a", "a", "next")}));
    std::vector<Violation> v = check_links(loop.p, rules);
    check(v.size() == 1 && v[0].kind == ViolationKind::cycle, "a rune linked to itself is a cycle");
}

void unresolvable_links_are_left_to_anomalies() {
    g_current = "unresolvable_links_are_left_to_anomalies";
    LinkRules rules;
    Capacity c;
    c.name = "one";
    rules.capacity.push_back(c);
    /* Two runes named "t" (ids t1, t2), one dangling link. Neither counts here. */
    Json s("{\"mantles\":[{\"id\":\"m\",\"name\":\"net\",\"runes\":["
           "{\"spirit\":{\"id\":\"t1\",\"name\":\"t\"}},{\"spirit\":{\"id\":\"t2\",\"name\":\"t\"}},"
           "{\"spirit\":{\"id\":\"s\",\"name\":\"s\"}}],\"layout\":{\"edges\":["
           "{\"from\":\"s\",\"to\":\"t\"},{\"from\":\"s\",\"to\":\"nobody\"},"
           "{\"from\":{\"mantle\":\"net\",\"rune\":\"s\"},\"to\":\"s\"}]}}]}");
    std::vector<Link> links = resolved_links(s.p);
    check(links.size() == 1, "only the link whose ends each name exactly one rune resolves");
    check(check_links(s.p, rules).empty(), "so the ambiguous and the dangling are not counted twice");
}

/* ── Lamport stamps, Latest, and writers ─────────────────────────────────── */

void a_new_device_is_not_outranked_forever_by_a_busy_one() {
    g_current = "a_new_device_is_not_outranked_forever_by_a_busy_one";
    JoinPolicy policy;
    policy.fields["content.pos"] = FieldJoin::Latest;

    Replica busy = make("replica-Z-busy-0123456789");
    for (int i = 0; i < 300; ++i)
        observe(busy, state({rune("n", "agent", "{\"pos\":" + std::to_string(i) + "}")}, {}));
    Replica fresh = make("replica-A-fresh-0123456789");
    merge(fresh, busy);
    check(fresh.issued() >= busy.issued(), "merging moves the counter past everything seen");

    /* Now both move n at once. The fresh device saw all 300 of the busy one's moves
     * before making its own, so its move ranks with the busy one's next. Without the
     * Lamport counter it would rank at ~1 and lose every concurrent move forever. */
    observe(fresh, state({rune("n", "agent", "{\"pos\":\"fresh\"}")}, {}));
    observe(busy, state({rune("n", "agent", "{\"pos\":\"busy\"}")}, {}));
    fresh.set_policy(policy);
    busy.set_policy(policy);
    merge(fresh, busy);
    merge(busy, fresh);
    check(name_of(fresh) == name_of(busy), "both show the same winner");
    check(fresh.conflicts().empty(), "and no conflict is raised for a Latest field");

    std::vector<Written> w = fresh.writers({"net", "n", "", "content.pos"});
    check(w.size() == 2, "both writes are still in the document; Latest only chose which to show");
    std::uint64_t fresh_stamp = 0, busy_stamp = 0;
    for (const auto& x : w) {
        if (x.writers == std::vector<std::string>({"replica-A-fresh-0123456789"})) fresh_stamp = x.stamp;
        if (x.writers == std::vector<std::string>({"replica-Z-busy-0123456789"})) busy_stamp = x.stamp;
    }
    check(fresh_stamp > 300 && busy_stamp > 300, "both stamps are past the 300 moves both saw");

    /* The next write after seeing everything outranks everything, from either side. */
    observe(fresh, state({rune("n", "agent", "{\"pos\":\"after\"}")}, {}));
    merge(busy, fresh);
    Doc f = busy.flatten();
    check(printed(f.root).find("\"pos\":\"after\"") != std::string::npos,
          "a write made after seeing the others is the one shown");
}

void the_counter_does_not_follow_an_absurd_stamp() {
    g_current = "the_counter_does_not_follow_an_absurd_stamp";
    Replica a = make("replica-A-0123456789");
    observe(a, state({rune("x")}, {}));
    /* A peer claiming 2^50 writes: a corrupt or hostile counter. */
    std::string doc = printed(a.doc().root);
    std::size_t at = doc.find("replica-A-0123456789_1");
    check(at != std::string::npos, "found a tag to rewrite");
    doc.replace(at, std::string("replica-A-0123456789_1").size(), "replica-E-0123456789_1125899906842624");
    Json evil(doc);
    Replica b = make("replica-B-0123456789");
    std::string why;
    check(b.merge(evil.p, &why) == MergeResult::ok, "merged: " + why);
    check(b.issued() < (std::uint64_t(1) << 41), "the counter did not jump to it, so it cannot be run out of room");
}

void a_fork_starts_past_the_counters_it_holds() {
    g_current = "a_fork_starts_past_the_counters_it_holds";
    Replica a = make("replica-A-0123456789");
    for (int i = 0; i < 10; ++i) observe(a, state({rune("x", "agent", "{\"i\":" + std::to_string(i) + "}")}, {}));
    Replica b;
    check(a.fork("replica-B-0123456789", b), "fork");
    check(b.issued() >= a.issued(), "the fork's first write outranks everything it inherited");
    std::string why;
    std::string bytes = b.to_bytes();
    Replica back;
    check(Replica::from_bytes(bytes, back, &why), "and it still saves and restores: " + why);
}

void writers_names_who_holds_each_value_up() {
    g_current = "writers_names_who_holds_each_value_up";
    Replica a = make("replica-A-0123456789");
    observe(a, state({rune("x", "agent", "{\"body\":\"hi\"}")}, {}));
    Replica b;
    check(a.fork("replica-B-0123456789", b), "fork");
    observe(a, state({rune("x", "agent", "{\"body\":\"from A\"}")}, {edge("x", "x", "self")}));
    observe(b, state({rune("x", "agent", "{\"body\":\"from B\"}")}, {}));
    merge(a, b);

    std::vector<Written> w = a.writers({"net", "x", "", "content.body"});
    check(w.size() == 2, "two live values");
    std::vector<Conflict> cs = a.conflicts();
    check(cs.size() == 1, "one conflict");
    if (w.size() == 2 && cs.size() == 1) {
        for (std::size_t i = 0; i < 2; ++i)
            check(w[i].value == cs[0].sides[i], "writers are in the conflict's side order, so a row can say whose");
        check(w[0].writers.size() == 1 && w[1].writers.size() == 1 && w[0].writers != w[1].writers,
              "each side has its own writer");
    }
    std::vector<Written> created = a.writers({"net", "x", "", "present"});
    check(created.size() == 1 && created[0].writers == std::vector<std::string>({"replica-A-0123456789"}),
          "who created the rune");
    std::vector<Written> links = a.writers({"net", "", "", "edges"});
    check(links.size() == 1 && links[0].writers == std::vector<std::string>({"replica-A-0123456789"}),
          "who drew the link");
    check(a.writers({"net", "nobody", "", "content.body"}).empty(), "nothing where nothing is");
    check(a.writers({"", "", "no-glyph", "descriptor"}).empty(), "nor for an undeclared glyph");

    /* One value written independently by both: one entry, two writers. */
    Replica c = make("replica-C-0123456789");
    Replica d = make("replica-D-0123456789");
    observe(c, state({rune("y", "agent", "{\"body\":\"same\"}")}, {}));
    observe(d, state({rune("y", "agent", "{\"body\":\"same\"}")}, {}));
    merge(c, d);
    std::vector<Written> same = c.writers({"net", "y", "", "content.body"});
    check(same.size() == 1 && same[0].writers.size() == 2, "the same value from two devices lists both");
}

}  // namespace

int main() {
    two_rewrites_sharing_a_wire_commute_when_the_wire_is_a_class();
    the_same_rewrites_lose_the_wire_when_a_wire_is_one_link();
    two_wires_on_one_port_are_reported_the_same_everywhere();
    a_seat_booked_twice_is_one_violation_listing_both();
    sameness_asserted_on_two_devices_is_one_class();
    two_moves_that_make_a_cycle_are_reported();
    a_long_chain_and_a_self_loop();
    unresolvable_links_are_left_to_anomalies();
    a_new_device_is_not_outranked_forever_by_a_busy_one();
    the_counter_does_not_follow_an_absurd_stamp();
    a_fork_starts_past_the_counters_it_holds();
    writers_names_who_holds_each_value_up();

    std::printf("links: %d checks", g_checks);
    if (g_failures.empty()) {
        std::printf(", all passed\n");
        return 0;
    }
    std::printf(", %d FAILED\n", static_cast<int>(g_failures.size()));
    for (const std::string& f : g_failures) std::printf("  - %s\n", f.c_str());
    return 1;
}
