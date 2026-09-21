/* sync_test.cpp — the sync protocol against a transport that is allowed to be terrible.
 *
 * Since founding, okf/concepts/history-graph.md has claimed that Palabra survives a
 * transport that "may drop, reorder and duplicate". The join suite proved convergence
 * under any MERGE order, which is not the same thing as any MESSAGE order. This file is
 * where the claim becomes a measurement: a simulated network drops, duplicates, delays
 * (and so reorders) and partitions frames between n peers editing concurrently, over
 * many schedules, and every schedule must converge.
 *
 * Nothing here opens a socket. A Session is handed frames and the time; the harness
 * is the network, and the clock is a number.
 */
#include "voidpalabra/sync.hpp"
#include "voidpalabra/canonical.hpp"

#include "cJSON.h"

#include <algorithm>
#include <cstdio>
#include <deque>
#include <map>
#include <memory>
#include <random>
#include <set>
#include <string>
#include <vector>

using namespace voidpalabra;
using namespace voidpalabra::sync;

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

std::string printed(const cJSON* v) {
    char* s = cJSON_PrintUnformatted(const_cast<cJSON*>(v));
    std::string out = s ? s : "";
    if (s) cJSON_free(s);
    return out;
}

/* ── a device: a replica, its files, and what it has been told ───────────── */

struct Device {
    std::string name;
    Replica replica;
    std::map<std::string, std::string> files;  // address -> bytes
    std::vector<std::string> presence;          // payloads received, in order
    std::vector<Event> events;
    int next_rune = 0;

    explicit Device(const std::string& id) : name(id) {
        std::string why;
        if (!Replica::create(id, replica, &why)) g_failures.push_back("create " + id + ": " + why);
    }

    void observe(const std::string& text) {
        Json s(text);
        Replica::Observed o = replica.observe(s.p);
        if (!o.ok) g_failures.push_back(name + " observe: " + o.error);
    }

    Host host(ExportSet share = share_everything(), FetchPolicy fetch = FetchPolicy::automatic) {
        Host h;
        h.share = share;
        h.references.fields["content.photo"] = sha256_hex_anywhere();
        h.have = [this](const std::string& a) { return files.count(a) > 0; };
        h.read = [this](const std::string& a, std::string& out) {
            auto it = files.find(a);
            if (it == files.end()) return false;
            out = it->second;
            return true;
        };
        h.fetch = fetch;
        return h;
    }

    void absorb(const Step& s) {
        for (const Event& e : s.events) {
            events.push_back(e);
            if (e.type == Event::Type::content_arrived) files[e.address] = e.bytes;
            if (e.type == Event::Type::presence) presence.push_back(e.bytes);
        }
    }

    std::string shown() const {
        Doc f = replica.flatten();
        return version_name(f.root);
    }

    std::size_t count(Event::Type t) const {
        return std::count_if(events.begin(), events.end(), [t](const Event& e) { return e.type == t; });
    }
};

/* ── the network ─────────────────────────────────────────────────────────── */

struct Conditions {
    double drop = 0.0;
    double duplicate = 0.0;
    Millis max_delay = 0;  // uniform in [0, max_delay]; non-zero means reordering
};

struct Packet {
    Millis at;
    std::uint64_t order;
    std::string frame;
};

/* Two sessions — one on each device — and the wire between them. */
struct Link {
    Device* a;
    Device* b;
    std::unique_ptr<Session> at_a, at_b;
    std::vector<Packet> to_a, to_b;
    bool up = true;
    std::uint64_t sent_frames = 0;
    std::map<Kind, int> sent_by_kind;
};

struct Net {
    std::mt19937 rng;
    Conditions c;
    std::vector<std::unique_ptr<Link>> links;
    Millis now = 0;
    std::uint64_t order = 0;

    explicit Net(unsigned seed, Conditions cond = {}) : rng(seed), c(cond) {}

    Link& connect(Device& a, Device& b, Host ha, Host hb, Timing t = {}, bool up = true) {
        auto l = std::make_unique<Link>();
        l->a = &a;
        l->b = &b;
        l->up = up;
        l->at_a = std::make_unique<Session>(a.replica, ha, t);
        l->at_b = std::make_unique<Session>(b.replica, hb, t);
        links.push_back(std::move(l));
        Link& ref = *links.back();
        route(ref, true, ref.at_a->start(now));
        route(ref, false, ref.at_b->start(now));
        return ref;
    }

    double roll() { return std::uniform_real_distribution<double>(0, 1)(rng); }

    /* `from_a`: the step came from the session on device a. */
    void route(Link& l, bool from_a, const Step& s) {
        (from_a ? l.a : l.b)->absorb(s);
        for (const std::string& f : s.send) {
            ++l.sent_frames;
            Message m;
            if (decode_frame(f, m, Limits{})) ++l.sent_by_kind[m.kind];
            if (!l.up || roll() < c.drop) continue;
            int copies = roll() < c.duplicate ? 2 : 1;
            for (int k = 0; k < copies; ++k) {
                Millis delay = c.max_delay ? std::uniform_int_distribution<Millis>(0, c.max_delay)(rng) : 0;
                (from_a ? l.to_b : l.to_a).push_back({now + delay, order++, f});
            }
        }
    }

    void deliver(Link& l, bool to_a) {
        std::vector<Packet>& q = to_a ? l.to_a : l.to_b;
        std::stable_sort(q.begin(), q.end(), [](const Packet& x, const Packet& y) {
            return x.at != y.at ? x.at < y.at : x.order < y.order;
        });
        std::vector<Packet> due;
        auto split = std::find_if(q.begin(), q.end(), [&](const Packet& p) { return p.at > now; });
        due.assign(q.begin(), split);
        q.erase(q.begin(), split);
        for (const Packet& p : due) {
            Session& s = to_a ? *l.at_a : *l.at_b;
            route(l, to_a, s.receive(p.frame, now));
        }
    }

    void step(Millis dt) {
        now += dt;
        for (auto& l : links) {
            deliver(*l, true);
            deliver(*l, false);
            route(*l, true, l->at_a->tick(now));
            route(*l, false, l->at_b->tick(now));
        }
    }

    void run(Millis duration, Millis dt = 100) {
        for (Millis end = now + duration; now < end;) step(dt);
    }

    bool quiet() const {
        for (const auto& l : links)
            if (!l->to_a.empty() || !l->to_b.empty() || !l->at_a->in_sync() || !l->at_b->in_sync())
                return false;
        return true;
    }

    /* Run until every session has its current state acknowledged and nothing is in
     * flight — or the budget runs out, which a test then reports. */
    bool settle(Millis budget, Millis dt = 100) {
        for (Millis end = now + budget; now < end;) {
            step(dt);
            if (quiet()) return true;
        }
        return quiet();
    }
};

/* ── documents ───────────────────────────────────────────────────────────── */

std::string rune_json(const std::string& id, const std::string& name, const std::string& content) {
    return "{\"spirit\":{\"id\":\"" + id + "\",\"name\":\"" + name + "\"},\"glyph\":\"text\",\"content\":" +
           content + "}";
}

std::string doc_json(const std::vector<std::string>& runes, const std::string& edges = "") {
    std::string rs;
    for (const auto& r : runes) rs += (rs.empty() ? "" : ",") + r;
    return "{\"mantles\":[{\"id\":\"m\",\"name\":\"notes\",\"runes\":[" + rs +
           "],\"layout\":{\"edges\":[" + edges + "]}}]}";
}

/* A random local edit against what the device currently shows: add, change or
 * delete a rune. */
void random_edit(Device& d, std::mt19937& rng) {
    Doc f = d.replica.flatten();
    cJSON* state = cJSON_Duplicate(f.root, 1);
    cJSON* mantles = cJSON_GetObjectItem(state, "mantles");
    if (!mantles || cJSON_GetArraySize(mantles) == 0) {
        cJSON_Delete(state);
        d.observe(doc_json({}));
        return;
    }
    cJSON* runes = cJSON_GetObjectItem(mantles->child, "runes");
    int n = cJSON_GetArraySize(runes);
    int what = std::uniform_int_distribution<int>(0, 2)(rng);
    if (what == 0 || n == 0) {
        std::string id = d.name + "_r" + std::to_string(d.next_rune++);
        cJSON_AddItemToArray(runes, cJSON_Parse(rune_json(id, id, "{\"body\":\"new\"}").c_str()));
    } else if (what == 1) {
        cJSON* r = cJSON_GetArrayItem(runes, std::uniform_int_distribution<int>(0, n - 1)(rng));
        cJSON* content = cJSON_GetObjectItem(r, "content");
        cJSON_DeleteItemFromObject(content, "body");
        cJSON_AddStringToObject(content, "body", (d.name + " edit " + std::to_string(rng() % 1000)).c_str());
    } else {
        cJSON_DeleteItemFromArray(runes, std::uniform_int_distribution<int>(0, n - 1)(rng));
    }
    Replica::Observed o = d.replica.observe(state);
    if (!o.ok) g_failures.push_back(d.name + " edit: " + o.error);
    cJSON_Delete(state);
}

std::string digest_of(const std::string& bytes) { return to_hex(sha256(bytes)); }

/* ── the frame ───────────────────────────────────────────────────────────── */

void a_frame_round_trips() {
    g_current = "a_frame_round_trips";
    Message m;
    m.kind = Kind::want;
    m.from = "replica-x-0123456789";
    m.session = "s1";
    m.seq = 7;
    m.addresses = {"a", "b"};
    m.auth = std::string("\x01\x02\xff", 3);
    Message back;
    std::string why;
    check(decode_frame(encode_frame(m), back, Limits{}, &why), why);
    check(back.kind == Kind::want && back.from == m.from && back.seq == 7 &&
              back.addresses == m.addresses && back.auth == m.auth,
          "every field survives");

    Message c;
    c.kind = Kind::content;
    c.from = "x";
    c.session = "s";
    c.address = "addr";
    c.payload = std::string("\x00\x01binary\xff", 9);
    check(decode_frame(encode_frame(c), back, Limits{}, &why) && back.payload == c.payload,
          "a binary payload rides raw");
}

void a_bad_frame_is_refused_before_it_is_believed() {
    g_current = "a_bad_frame_is_refused_before_it_is_believed";
    Message ok;
    ok.kind = Kind::hello;
    ok.from = "x";
    ok.session = "s";
    std::string good = encode_frame(ok);
    Message out;
    Limits l;

    auto refused = [&](const std::string& f, const char* what) {
        check(!decode_frame(f, out, l), std::string("refused: ") + what);
    };
    refused("", "nothing");
    refused("VPS", "shorter than a header");
    refused("XXXX" + good.substr(4), "wrong magic");
    std::string long_header = good;
    long_header[4] = '\xff';
    long_header[5] = '\xff';
    long_header[6] = '\xff';
    long_header[7] = '\x7f';
    refused(long_header, "a header length beyond the frame");
    refused(good + "payload", "a payload on a hello");

    auto with_header = [](const std::string& h, const std::string& payload = "") {
        std::string f = "VPS1";
        std::uint32_t n = static_cast<std::uint32_t>(h.size());
        for (int k = 0; k < 4; ++k) f.push_back(static_cast<char>((n >> (8 * k)) & 0xFF));
        return f + h + payload;
    };
    refused(with_header("not json"), "a header that is not JSON");
    refused(with_header("{\"kind\":\"steal\",\"from\":\"x\",\"session\":\"s\",\"seq\":0}"), "an unknown kind");
    refused(with_header("{\"kind\":\"hello\",\"from\":7,\"session\":\"s\",\"seq\":0}"), "a sender that is not text");
    refused(with_header("{\"kind\":\"hello\",\"from\":\"x\",\"session\":\"s\",\"seq\":-1}"), "a negative sequence");
    refused(with_header("{\"kind\":\"hello\",\"from\":\"x\",\"session\":\"s\",\"seq\":0,\"auth\":\"zz\"}"),
            "an auth slot that is not hex");
    refused(with_header("{\"kind\":\"presence\",\"from\":\"x\",\"session\":\"s\",\"seq\":1}",
                        std::string(l.max_presence + 1, 'p')),
            "presence larger than max_presence");
    std::string many = "[";
    for (std::size_t k = 0; k <= l.max_addresses; ++k) many += (k ? ",\"a\"" : "\"a\"");
    refused(with_header("{\"kind\":\"want\",\"from\":\"x\",\"session\":\"s\",\"seq\":0,\"addresses\":" + many + "]}"),
            "more addresses than max_addresses");
    Limits tiny;
    tiny.max_frame = 16;
    check(!decode_frame(good, out, tiny), "refused: a frame larger than max_frame");
}

/* ── two peers ───────────────────────────────────────────────────────────── */

void two_peers_converge_and_then_go_quiet() {
    g_current = "two_peers_converge_and_then_go_quiet";
    Device a("sync-device-A-0001"), b("sync-device-B-0001");
    a.observe(doc_json({rune_json("a1", "from-a", "{\"body\":\"one\"}")}));
    b.observe(doc_json({rune_json("b1", "from-b", "{\"body\":\"two\"}")}));
    Net net(1);
    Link& l = net.connect(a, b, a.host(), b.host());
    check(net.settle(10000), "settled");
    check(a.shown() == b.shown(), "both show the same state");
    check(a.count(Event::Type::peer_identified) == 1 && b.count(Event::Type::merged) >= 1,
          "each identified the other and merged");

    int docs_before = l.sent_by_kind[Kind::doc];
    net.run(60000);
    check(l.sent_by_kind[Kind::doc] == docs_before,
          "a minute of idleness sends no state — only keepalives");
    check(l.at_a->is_open() && l.at_b->is_open(), "and the keepalives kept the session open");

    a.observe(doc_json({rune_json("a1", "from-a", "{\"body\":\"one, edited\"}"),
                        rune_json("b1", "from-b", "{\"body\":\"two\"}")}));
    net.step(100);
    check(l.sent_by_kind[Kind::doc] == docs_before + 1, "a local change goes out on the next tick");
    check(net.settle(5000) && a.shown() == b.shown(), "and arrives");
}

void a_deletion_travels_through_the_protocol() {
    g_current = "a_deletion_travels_through_the_protocol";
    Device a("sync-device-A-0002"), b("sync-device-B-0002");
    a.observe(doc_json({rune_json("x", "x", "{}"), rune_json("y", "y", "{}")}));
    Net net(2);
    net.connect(a, b, a.host(), b.host());
    net.settle(10000);
    a.observe(doc_json({rune_json("y", "y", "{}")}));
    check(net.settle(10000), "settled");
    Doc f = b.replica.flatten();
    check(printed(f.root).find("\"id\":\"x\"") == std::string::npos, "the deleted rune is gone on b");
    check(a.shown() == b.shown(), "and both agree");
}

/* ── the headline: a terrible network ────────────────────────────────────── */

void a_mesh_converges_under_loss_duplication_reordering_and_partition() {
    g_current = "a_mesh_converges_under_loss_duplication_reordering_and_partition";
    int converged = 0, schedules = 30;
    for (unsigned seed = 100; seed < 100 + static_cast<unsigned>(schedules); ++seed) {
        std::vector<std::unique_ptr<Device>> ds;
        for (int k = 0; k < 4; ++k)
            ds.push_back(std::make_unique<Device>("mesh-schedule-" + std::to_string(seed) + "-peer-" + std::to_string(k)));
        Conditions bad;
        bad.drop = 0.3;
        bad.duplicate = 0.2;
        bad.max_delay = 1500;
        Net net(seed, bad);
        Timing t;
        t.silence_timeout = 600000;  // a partition must not end the session
        for (std::size_t i = 0; i < ds.size(); ++i)
            for (std::size_t j = i + 1; j < ds.size(); ++j)
                net.connect(*ds[i], *ds[j], ds[i]->host(), ds[j]->host(), t);

        std::mt19937 edits(seed * 7);
        for (int round = 0; round < 30; ++round) {
            random_edit(*ds[edits() % ds.size()], edits);
            /* Partition {0,1} from {2,3} for a stretch in the middle, then heal. */
            bool cut = round >= 10 && round < 20;
            for (auto& l : net.links) {
                bool across = (l->a == ds[0].get() || l->a == ds[1].get()) !=
                              (l->b == ds[0].get() || l->b == ds[1].get());
                l->up = !(cut && across);
            }
            net.run(700);
        }
        for (auto& l : net.links) l->up = true;
        bool settled = net.settle(300000);
        bool same = true;
        for (auto& d : ds) same = same && d->shown() == ds[0]->shown();
        if (settled && same) ++converged;
        else g_failures.push_back(g_current + std::string(": seed ") + std::to_string(seed) +
                                  (settled ? " settled but diverged" : " did not settle"));
    }
    check(converged == schedules, std::to_string(converged) + " of " + std::to_string(schedules) +
                                      " schedules converged");
}

/* ── what must not leave ─────────────────────────────────────────────────── */

void a_private_rune_never_leaves_in_any_form() {
    g_current = "a_private_rune_never_leaves_in_any_form";
    const std::string secret_file = "a private photo";
    const std::string secret_addr = digest_of(secret_file);
    Device a("sync-device-A-0003"), b("sync-device-B-0003");
    a.files[secret_addr] = secret_file;

    ExportSet share = [](const std::string&, const std::string& rune) { return rune.rfind("secret", 0) != 0; };
    a.observe(doc_json(
        {rune_json("public_1", "shared-note", "{\"body\":\"hello\"}"),
         rune_json("secret_1", "diary", "{\"body\":\"TOP-SECRET-TEXT\",\"photo\":\"" + secret_addr + "\"}")},
        "{\"from\":\"shared-note\",\"to\":\"diary\"}"));

    Net net(3, Conditions{0.2, 0.2, 500});
    net.connect(a, b, a.host(share), b.host());
    check(net.settle(30000), "settled");
    std::string seen = printed(b.replica.doc().root);
    check(seen.find("secret_1") == std::string::npos, "the private rune's id never arrived");
    check(seen.find("TOP-SECRET") == std::string::npos, "nor its content");
    check(seen.find("diary") == std::string::npos, "nor its name, which a link would have carried");
    check(seen.find("public_1") != std::string::npos, "while the shared rune did arrive");

    /* Asking for the private file directly — as a curious peer could, knowing the
     * hash from elsewhere — gets nothing. */
    Message want;
    want.kind = Kind::want;
    want.from = b.replica.id();
    want.addresses = {secret_addr};
    Link& l = *net.links[0];
    want.session = "probe";
    Step s = l.at_a->receive(encode_frame(want), net.now);
    bool served = false;
    for (const auto& f : s.send) {
        Message m;
        if (decode_frame(f, m, Limits{}) && m.kind == Kind::content) served = true;
    }
    check(!served, "a file only a private rune names is not served, even when asked for by hash");
}

void a_name_shared_by_a_private_rune_is_not_linked() {
    g_current = "a_name_shared_by_a_private_rune_is_not_linked";
    Device a("sync-device-A-0004"), b("sync-device-B-0004");
    ExportSet share = [](const std::string&, const std::string& rune) { return rune != "hidden"; };
    a.observe(doc_json({rune_json("shown", "note", "{}"), rune_json("hidden", "note", "{}"),
                        rune_json("other", "other", "{}")},
                       "{\"from\":\"other\",\"to\":\"note\"}"));
    Net net(4);
    net.connect(a, b, a.host(share), b.host());
    net.settle(10000);
    Doc f = b.replica.flatten();
    cJSON* edges = cJSON_GetObjectItem(cJSON_GetObjectItem(
                                           cJSON_GetObjectItem(f.root, "mantles")->child, "layout"),
                                       "edges");
    check(cJSON_GetArraySize(edges) == 0,
          "a link to a name a private rune also holds is withheld: it cannot say which one it means");
}

void a_deletion_of_a_now_private_rune_still_travels() {
    g_current = "a_deletion_of_a_now_private_rune_still_travels";
    /* Shared, then made private, then deleted. The deletion must still reach the
     * peers that received it — or it lives there forever — and nothing but the fact
     * of the removal goes with it. */
    Device a("sync-device-A-0005"), b("sync-device-B-0005");
    bool is_private = false;
    ExportSet share = [&](const std::string&, const std::string& rune) { return !(is_private && rune == "r"); };
    a.observe(doc_json({rune_json("r", "note", "{\"body\":\"v1\"}")}));
    Net net(5);
    Link& l = net.connect(a, b, a.host(share), b.host());
    net.settle(10000);
    check(b.shown() == a.shown(), "shared first");

    is_private = true;
    a.observe(doc_json({rune_json("r", "note", "{\"body\":\"v2 PRIVATE EDIT\"}")}));
    a.observe(doc_json({}));
    (void)l;
    net.settle(10000);
    Doc f = b.replica.flatten();
    std::string seen = printed(f.root);
    check(seen.find("\"id\":\"r\"") == std::string::npos, "the deletion reached b");
    check(printed(b.replica.doc().root).find("PRIVATE EDIT") == std::string::npos,
          "and the private edit made before it did not");
}

/* ── files ───────────────────────────────────────────────────────────────── */

void a_file_follows_its_rune_across_a_lossy_link() {
    g_current = "a_file_follows_its_rune_across_a_lossy_link";
    const std::string photo = std::string(200000, 'x') + "the photo";
    const std::string addr = digest_of(photo);
    Device a("sync-device-A-0006"), b("sync-device-B-0006");
    a.files[addr] = photo;
    a.observe(doc_json({rune_json("p", "ada", "{\"photo\":\"assets/" + addr + ".png\"}")}));
    Net net(6, Conditions{0.3, 0.1, 800});
    Link& l = net.connect(a, b, a.host(), b.host());
    net.settle(60000);
    net.run(60000);
    check(b.files.count(addr) && b.files[addr] == photo, "the picture arrived, intact");
    check(l.at_b->content_state(addr) == ContentState::held, "and is known to be held");
}

void wrong_bytes_are_refused_and_the_file_is_asked_for_again() {
    g_current = "wrong_bytes_are_refused_and_the_file_is_asked_for_again";
    const std::string photo = "the real photo";
    const std::string addr = digest_of(photo);
    Device a("sync-device-A-0007"), b("sync-device-B-0007");
    a.files[addr] = photo;
    a.observe(doc_json({rune_json("p", "ada", "{\"photo\":\"" + addr + "\"}")}));
    Host lying = a.host();
    int lies = 2;
    lying.read = [&](const std::string& h, std::string& out) {
        if (h != addr) return false;
        out = lies-- > 0 ? std::string("not the photo") : photo;
        return true;
    };
    Net net(7);
    Timing t;
    t.content_timeout = 1000;
    net.connect(a, b, lying, b.host(), t);
    net.settle(10000);
    net.run(30000);
    check(b.count(Event::Type::content_refused) >= 1, "the wrong bytes were refused");
    check(b.files.count(addr) && b.files[addr] == photo, "and the right ones arrived later");
}

void cautious_mode_knows_about_a_file_without_fetching_it() {
    g_current = "cautious_mode_knows_about_a_file_without_fetching_it";
    const std::string photo = "someone else's photo";
    const std::string addr = digest_of(photo);
    Device a("sync-device-A-0008"), b("sync-device-B-0008");
    a.files[addr] = photo;
    a.observe(doc_json({rune_json("p", "ada", "{\"photo\":\"" + addr + "\"}")}));
    Net net(8);
    Link& l = net.connect(a, b, a.host(), b.host(share_everything(), FetchPolicy::cautious));
    net.settle(10000);
    net.run(20000);
    check(l.sent_by_kind[Kind::want] == 0, "nothing was asked for");
    check(l.at_b->content_state(addr) == ContentState::deferred,
          "the file is known and deferred — a view draws the placeholder from this");
    check(!b.files.count(addr), "and not fetched");

    net.route(l, false, l.at_b->fetch(addr, net.now));
    net.run(5000);
    check(b.files.count(addr) && b.files[addr] == photo, "until the host says so");
}

void a_file_nobody_asked_for_is_refused() {
    g_current = "a_file_nobody_asked_for_is_refused";
    Device a("sync-device-A-0009"), b("sync-device-B-0009");
    Net net(9);
    Link& l = net.connect(a, b, a.host(), b.host());
    net.settle(5000);
    Message push;
    push.kind = Kind::content;
    push.from = a.replica.id();
    push.session = "x";
    push.address = digest_of("junk");
    push.payload = "junk";
    net.route(l, false, l.at_b->receive(encode_frame(push), net.now));
    check(b.files.empty(), "a peer cannot fill this device's storage with files it did not ask for");
    check(b.count(Event::Type::message_refused) >= 1, "and the attempt is reported");
}

/* ── presence ────────────────────────────────────────────────────────────── */

void presence_is_newest_first_expires_and_never_versions() {
    g_current = "presence_is_newest_first_expires_and_never_versions";
    Device a("sync-device-A-0010"), b("sync-device-B-0010");
    Net net(10);
    Timing t;
    t.presence_ttl = 3000;
    Link& l = net.connect(a, b, a.host(), b.host(), t);
    net.settle(5000);
    std::uint64_t rev = b.replica.revision();

    Step s1 = l.at_a->publish_presence("{\"selected\":\"one\"}", net.now);
    Step s2 = l.at_a->publish_presence("{\"selected\":\"two\"}", net.now);
    /* Deliver them out of order, the second twice. */
    Session& at_b = *l.at_b;
    b.absorb(at_b.receive(s2.send[0], net.now));
    b.absorb(at_b.receive(s1.send[0], net.now));
    b.absorb(at_b.receive(s2.send[0], net.now));
    check(b.presence == std::vector<std::string>({"{\"selected\":\"two\"}"}),
          "the newest one, once — a late older one and a duplicate are dropped");
    check(b.replica.revision() == rev, "and the replica never moved");

    net.links[0]->up = true;
    net.run(4000);
    check(b.count(Event::Type::presence_expired) == 1, "unrefreshed, it expires on the receiver's clock");

    Step big = l.at_a->publish_presence(std::string(20000, 'p'), net.now);
    check(big.send.empty(), "a payload over the ceiling is refused at the sender");
}

/* ── identity, time, trust hooks ─────────────────────────────────────────── */

void a_peer_with_this_replicas_id_is_refused() {
    g_current = "a_peer_with_this_replicas_id_is_refused";
    Device a("sync-device-SAME-0011");
    Device clone("sync-device-SAME-0011");
    Net net(11);
    Link& l = net.connect(a, clone, a.host(), clone.host());
    net.run(2000);
    check(l.at_a->is_closed() && l.at_b->is_closed(), "both ends close");
    bool said = false;
    for (const Event& e : a.events)
        if (e.type == Event::Type::closed && e.detail.find("fork") != std::string::npos) said = true;
    check(said, "saying why, and what to do");
}

void every_wait_ends() {
    g_current = "every_wait_ends";
    Device a("sync-device-A-0012"), b("sync-device-B-0012");
    Net net(12);
    Timing t;
    Link& l = net.connect(a, b, a.host(), b.host(), t, false);  // down from the start
    net.run(t.hello_timeout + 1000);
    check(l.at_a->is_closed(), "a hello that is never answered ends the session");

    Net net2(13);
    Link& l2 = net2.connect(a, b, a.host(), b.host(), t);
    net2.settle(5000);
    l2.up = false;  // then silence
    net2.run(t.silence_timeout + 1000);
    check(l2.at_a->is_closed() && l2.at_b->is_closed(), "a peer heard from and then silent ends it");
}

void a_restarted_peer_is_sent_the_state_again() {
    g_current = "a_restarted_peer_is_sent_the_state_again";
    Device a("sync-device-A-0014"), b("sync-device-B-0014");
    a.observe(doc_json({rune_json("x", "x", "{}")}));
    Net net(14);
    Link& l = net.connect(a, b, a.host(), b.host());
    net.settle(5000);
    /* b's process restarts: a new session, and — to prove the resend — a replica
     * that lost the state. */
    b.replica = Replica();
    Replica::create("sync-device-B-0014", b.replica);
    l.at_b = std::make_unique<Session>(b.replica, b.host());
    net.route(l, false, l.at_b->start(net.now));
    check(net.settle(10000), "settled");
    check(a.shown() == b.shown(), "the new session received the whole state");
}

void signatures_plug_in_through_the_hooks() {
    g_current = "signatures_plug_in_through_the_hooks";
    /* A stand-in scheme, NOT cryptography: the point is that the slot and the hook
     * carry whatever an application plugs in, and that a failing check stops the
     * message before it can do anything. */
    auto signer = [](const std::string& key) {
        return [key](const std::string& frame) { return to_hex(sha256(key + frame)).substr(0, 16); };
    };
    auto verifier = [](const std::string& key) {
        return [key](const std::string& frame, const std::string& auth) {
            return auth == to_hex(sha256(key + frame)).substr(0, 16);
        };
    };
    Device a("sync-device-A-0015"), b("sync-device-B-0015"), m("sync-device-M-0015");
    a.observe(doc_json({rune_json("x", "x", "{}")}));
    Host ha = a.host(), hb = b.host();
    ha.sign = signer("team");
    ha.verify = verifier("team");
    hb.sign = signer("team");
    hb.verify = verifier("team");
    Net net(15);
    net.connect(a, b, ha, hb);
    check(net.settle(10000) && a.shown() == b.shown(), "members who share the scheme sync");

    Host hm = m.host();
    hm.sign = signer("outsider");
    m.observe(doc_json({rune_json("evil", "evil", "{}")}));
    Host hb2 = b.host();
    hb2.verify = verifier("team");
    hb2.sign = signer("team");
    Net net2(16);
    net2.connect(m, b, hm, hb2);
    net2.run(10000);
    check(printed(b.replica.doc().root).find("evil") == std::string::npos,
          "an outsider's state is never merged");
    check(b.count(Event::Type::message_refused) >= 1, "and its messages are reported as refused");
}

void a_refused_state_is_not_resent_until_it_changes() {
    g_current = "a_refused_state_is_not_resent_until_it_changes";
    Device a("sync-device-A-0017"), b("sync-device-B-0017");
    Net net(17);
    Link& l = net.connect(a, b, a.host(), b.host());
    net.settle(5000);
    Message bad;
    bad.kind = Kind::doc;
    bad.from = a.replica.id();
    bad.session = "x";
    bad.digest = "0000";
    bad.payload = "{\"palabra\":1,\"mantles\":{\"m\":{\"present\":7}}}";
    Step s = l.at_b->receive(encode_frame(bad), net.now);
    b.absorb(s);
    bool refused = false;
    for (const auto& f : s.send) {
        Message m;
        if (decode_frame(f, m, Limits{}) && m.kind == Kind::refuse && m.digest == "0000") refused = true;
    }
    check(refused, "the peer is told its state was refused, with the digest");
    check(b.count(Event::Type::merge_refused) == 1, "and the host is told why");
}

}  // namespace

/* ── on a byte stream ────────────────────────────────────────────────────── */

void a_stream_is_reassembled_however_the_socket_splits_it() {
    g_current = "a_stream_is_reassembled_however_the_socket_splits_it";
    std::vector<std::string> frames;
    for (int i = 0; i < 20; ++i) {
        Message m;
        m.kind = i % 3 == 0 ? Kind::presence : Kind::hello;
        m.from = "replica-A-0123456789";
        m.session = "s";
        m.seq = static_cast<std::uint64_t>(i);
        if (m.kind == Kind::presence) m.payload = std::string(static_cast<std::size_t>(i * 37), 'x');
        frames.push_back(encode_frame(m));
    }
    std::string stream;
    for (const auto& f : frames) stream += stream_frame(f);

    /* Every way a socket can hand the bytes over: one at a time, all at once, and
     * random pieces that cut frames, lengths and magics anywhere. */
    std::mt19937 rng(77);
    for (int trial = 0; trial < 50; ++trial) {
        StreamReader r;
        std::vector<std::string> got;
        std::size_t at = 0;
        while (at < stream.size()) {
            std::size_t n = trial == 0 ? 1
                          : trial == 1 ? stream.size()
                          : std::uniform_int_distribution<std::size_t>(0, 64)(rng);
            n = std::min(n, stream.size() - at);
            r.feed(stream.data() + at, n);
            at += n;
            std::string f;
            while (r.next(f)) got.push_back(f);
        }
        check(!r.broken(), "a well-formed stream never breaks the reader: " + r.why());
        check(got == frames, "every frame, whole, in order, trial " + std::to_string(trial));
        check(r.pending() == 0, "and nothing left over");
    }
}

void a_bad_stream_breaks_at_once_and_stays_broken() {
    g_current = "a_bad_stream_breaks_at_once_and_stays_broken";
    Limits small;
    small.max_frame = 1024;
    {
        StreamReader r(small);
        r.feed(std::string("\xff\xff\x00\x00", 4));  // 65535 > max_frame
        std::string f;
        check(!r.next(f) && r.broken(), "a length over the limit is refused from four bytes");
    }
    {
        StreamReader r;
        r.feed(std::string("\x10\x00\x00\x00HTTP", 8));
        std::string f;
        check(!r.next(f) && r.broken(), "another protocol is refused on its first bytes, not its last");
        r.feed(stream_frame(encode_frame(Message{})));
        check(!r.next(f) && r.broken(), "and a broken stream stays broken: it cannot resynchronize");
    }
    {
        StreamReader r;
        r.feed(std::string("\x03\x00\x00\x00VPS", 7));
        std::string f;
        check(!r.next(f) && r.broken(), "a length shorter than a frame's own prefix");
    }
    {
        StreamReader r;
        r.feed(std::string("\x10\x00\x00\x00VP", 6));
        std::string f;
        check(!r.next(f) && !r.broken(), "a magic arriving in pieces is not judged before it is here");
    }
}

/* ── lifecycle ───────────────────────────────────────────────────────────── */

/* A phone app is paused and its sockets die; a laptop sleeps; Wi-Fi hands over.
 * None of them is an error. The transport opens a new connection and a new session
 * over the SAME replica, and whatever changed meanwhile, on either side, arrives. */
void a_link_that_dies_is_replaced_and_nothing_is_lost() {
    g_current = "a_link_that_dies_is_replaced_and_nothing_is_lost";
    Device a("sync-device-A-0020"), b("sync-device-B-0020");
    a.observe(doc_json({rune_json("x", "x", "{\"body\":\"one\"}")}));
    Net net(20);
    Link& first = net.connect(a, b, a.host(), b.host());
    check(net.settle(10000), "the first link settles");

    first.up = false;  // paused: nothing moves, and the sessions will time out
    a.observe(doc_json({rune_json("x", "x", "{\"body\":\"edited while paused\"}")}));
    net.run(Timing{}.silence_timeout + 5000);
    check(first.at_a->is_closed() && first.at_b->is_closed(), "the dead link ends on elapsed time");

    net.links.clear();
    net.connect(a, b, a.host(), b.host());  // resumed: a new connection, new sessions
    check(net.settle(10000), "the new link settles");
    check(a.shown() == b.shown(), "the edit made while the link was dead arrived");
}

/* ── coalescing ──────────────────────────────────────────────────────────── */

void a_burst_of_changes_leaves_as_few_states() {
    g_current = "a_burst_of_changes_leaves_as_few_states";
    Device a("sync-device-A-0021"), b("sync-device-B-0021");
    Timing t;
    t.coalesce = 100;
    Net net(21);
    Link& l = net.connect(a, b, a.host(), b.host(), t);
    check(net.settle(10000), "settles");
    int before = l.sent_by_kind[Kind::doc];

    /* A reducer stepping every 10 ms for two seconds: 200 changes. */
    for (int i = 0; i < 200; ++i) {
        a.observe(doc_json({rune_json("x", "x", "{\"step\":" + std::to_string(i) + "}")}));
        net.step(10);
    }
    check(net.settle(10000), "settles after the burst");
    int sent = l.sent_by_kind[Kind::doc] - before;
    check(sent <= 25, "about one state per 100 ms, not one per change (sent " + std::to_string(sent) + ")");
    check(a.shown() == b.shown(), "and the last state is the one that arrived");
}

void cautious_mode_can_be_turned_off_on_a_live_link() {
    g_current = "cautious_mode_can_be_turned_off_on_a_live_link";
    const std::string photo = "a photo";
    const std::string addr = digest_of(photo);
    Device a("sync-device-A-0022"), b("sync-device-B-0022");
    a.files[addr] = photo;
    a.observe(doc_json({rune_json("p", "p", "{\"photo\":\"" + addr + "\"}")}));
    Net net(22);
    Link& l = net.connect(a, b, a.host(), b.host(share_everything(), FetchPolicy::cautious));
    net.settle(10000);
    check(l.at_b->content_state(addr) == ContentState::deferred, "deferred while cautious");

    net.route(l, false, l.at_b->set_fetch(FetchPolicy::automatic, net.now));
    net.run(5000);
    check(b.files.count(addr) == 1, "turning cautious mode off fetches what it deferred, without reconnecting");

    const std::string photo2 = "a second photo";
    const std::string addr2 = digest_of(photo2);
    a.files[addr2] = photo2;
    net.route(l, false, l.at_b->set_fetch(FetchPolicy::cautious, net.now));
    a.observe(doc_json({rune_json("p", "p", "{\"photo\":\"" + addr + "\"}"),
                        rune_json("q", "q", "{\"photo\":\"" + addr2 + "\"}")}));
    net.settle(10000);
    net.run(5000);
    check(l.at_b->content_state(addr2) == ContentState::deferred, "and turning it back on defers the next one");
}

int main() {
    a_frame_round_trips();
    a_bad_frame_is_refused_before_it_is_believed();
    two_peers_converge_and_then_go_quiet();
    a_deletion_travels_through_the_protocol();
    a_mesh_converges_under_loss_duplication_reordering_and_partition();
    a_private_rune_never_leaves_in_any_form();
    a_name_shared_by_a_private_rune_is_not_linked();
    a_deletion_of_a_now_private_rune_still_travels();
    a_file_follows_its_rune_across_a_lossy_link();
    wrong_bytes_are_refused_and_the_file_is_asked_for_again();
    cautious_mode_knows_about_a_file_without_fetching_it();
    a_file_nobody_asked_for_is_refused();
    presence_is_newest_first_expires_and_never_versions();
    a_peer_with_this_replicas_id_is_refused();
    every_wait_ends();
    a_restarted_peer_is_sent_the_state_again();
    signatures_plug_in_through_the_hooks();
    a_refused_state_is_not_resent_until_it_changes();
    a_stream_is_reassembled_however_the_socket_splits_it();
    a_bad_stream_breaks_at_once_and_stays_broken();
    a_link_that_dies_is_replaced_and_nothing_is_lost();
    a_burst_of_changes_leaves_as_few_states();
    cautious_mode_can_be_turned_off_on_a_live_link();

    std::printf("sync: %d checks", g_checks);
    if (g_failures.empty()) {
        std::printf(", all passed\n");
        return 0;
    }
    std::printf(", %d FAILED\n", static_cast<int>(g_failures.size()));
    for (const std::string& f : g_failures) std::printf("  - %s\n", f.c_str());
    return 1;
}
