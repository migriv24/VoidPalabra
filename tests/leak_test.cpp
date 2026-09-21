/* leak_test.cpp — allocation balance across every public entry point.
 *
 * Why this exists rather than a sanitizer: the toolchain this project builds with
 * (MinGW/ucrt64) has no working AddressSanitizer, so `-fsanitize=address` silently
 * produces nothing. A check that quietly does not run is worse than no check —
 * it was reported clean here once before anyone noticed the binaries had never
 * been built.
 *
 * cJSON routes every allocation through hooks, and cJSON is the only allocator
 * Palabra's data structures use, so counting through the hooks catches the leaks
 * that matter: trees built and dropped inside `join`, `flatten`, `enrich`,
 * `decode`, and the archive's index. It does not catch leaks of std:: containers,
 * which RAII already handles.
 *
 * The rule this enforces: **a function that returns nothing owned must leave the
 * allocation count exactly where it found it.**
 */
#include "voidpalabra/archive.hpp"
#include "voidpalabra/canonical.hpp"
#include "voidpalabra/join.hpp"
#include "voidpalabra/store.hpp"
#include "voidpalabra/utterance.hpp"
#include "voidpalabra/replica.hpp"
#include "voidpalabra/references.hpp"
#include "voidpalabra/sync.hpp"
#include "voidpalabra/links.hpp"
#include "voidpalabra/crdt/conflict.hpp"

#include "cJSON.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace voidpalabra;

namespace {

long g_live = 0;
long g_total = 0;

void* counting_malloc(size_t n) {
    ++g_live;
    ++g_total;
    return std::malloc(n);
}
void counting_free(void* p) {
    if (p) --g_live;
    std::free(p);
}

int g_checks = 0;
std::vector<std::string> g_failures;

/* Run `body`, then assert the allocation count came back to where it started. */
void balanced(const char* what, void (*body)()) {
    long before = g_live;
    body();
    ++g_checks;
    if (g_live != before) {
        g_failures.push_back(std::string(what) + ": leaked " +
                             std::to_string(g_live - before) + " cJSON allocation(s)");
    }
}

const char* kState =
    "{\"mantles\":[{\"id\":\"m1\",\"name\":\"demo\",\"runes\":["
    "{\"spirit\":{\"id\":\"rune_1\",\"name\":\"intro\"},\"glyph\":\"text\","
    "\"tags\":[\"draft\",\"science\"],\"content\":{\"value\":\"hello\",\"n\":3}},"
    "{\"spirit\":{\"id\":\"rune_2\",\"name\":\"body\"},\"glyph\":\"richtext\","
    "\"tags\":[\"draft\"],\"content\":{\"value\":\"world\"}}],"
    "\"layout\":{\"edges\":[{\"from\":\"intro\",\"to\":\"body\"}]}}]}";

/* --- the bodies ---------------------------------------------------------- */

void body_canonical() {
    cJSON* s = cJSON_Parse(kState);
    (void)version_name(s);
    (void)canon_slice(s);
    (void)slice_hash(s);
    cJSON_Delete(s);
}

void body_encode_decode() {
    cJSON* s = cJSON_Parse(kState);
    std::string bytes = encode(s);
    cJSON* back = decode(bytes);
    cJSON_Delete(back);
    cJSON_Delete(s);
}

void body_decode_garbage() {
    /* The refusal path allocates partial trees before it discovers the input is
     * bad; those must not escape. */
    for (const char* bad : {"\x08\x05", "\x07\xff", "\x05\xff\xff", "\x99"}) {
        cJSON* v = decode(std::string(bad));
        if (v) cJSON_Delete(v);
    }
}

void body_enrich_flatten() {
    cJSON* s = cJSON_Parse(kState);
    CounterMint mint("P");
    Doc d = enrich(s, mint);
    Doc f = flatten(d);
    (void)canon_doc(d);
    cJSON_Delete(s);
}

void body_join_and_conflicts() {
    cJSON* s = cJSON_Parse(kState);
    CounterMint ma("A"), mb("B");
    Doc a = enrich(s, ma);
    Doc b = enrich(s, mb);
    cJSON* v = cJSON_CreateString("changed");
    set_field(a, "demo", "rune_1", "content.value", v, ma);
    cJSON_Delete(v);
    Doc merged = join(a, b);
    for (const Conflict& c : conflicts(merged)) {
        (void)c.hash();
        cJSON* j = conflict_to_json(c);
        cJSON_Delete(j);
    }
    Doc flat = flatten(merged);
    cJSON_Delete(s);
}

void body_tags() {
    cJSON* s = cJSON_Parse(kState);
    CounterMint mint("P");
    Doc d = enrich(s, mint);
    add_tag(d, "demo", "rune_1", "extra", mint);
    remove_tag(d, "demo", "rune_1", "draft");
    add_tag(d, "demo", "nonexistent", "x", mint);   // failure path
    remove_tag(d, "nope", "rune_1", "draft");       // failure path
    cJSON_Delete(s);
}

void body_archive() {
    cJSON* s = cJSON_Parse(kState);
    Archive a;
    a.save(s, "one");
    a.put_asset("x.bin", std::string(30000, 'q'));
    cJSON* back = a.load_latest();
    if (back) cJSON_Delete(back);
    cJSON* missing = a.load("v:nope");
    if (missing) cJSON_Delete(missing);
    std::string bytes = a.to_bytes();
    Archive b;
    Archive::from_bytes(bytes, b);
    Archive c;
    Archive::from_bytes("garbage", c);           // failure path
    Archive::from_bytes(bytes.substr(0, 50), c); // truncation path
    cJSON_Delete(s);
}

void body_import_miga() {
    std::string good = std::string("{\"magic\":\"MIGA\",\"version\":3,\"state\":") +
                       kState + ",\"assets\":{\"a\":\"aGk=\"}}";
    Archive a;
    import_miga(good, a);
    Archive b;
    import_miga("{\"magic\":\"MIGA\",\"version\":2}", b);          // refusal
    Archive c;
    import_miga(std::string("{\"magic\":\"MIGA\",\"version\":3,\"state\":") + kState +
                ",\"assets\":{\"a\":\"!!!\"}}", c);                 // bad base64
    Archive d;
    import_miga("not json", d);                                     // unparseable
}

void body_refusals() {
    /* Every throwing path allocates before it throws. */
    const char* bad[] = {
        "{\"mantles\":[{\"name\":\"a\",\"runes\":[{}]}]}",            // no spirit.id
        "{\"mantles\":[{\"id\":\"x\"}]}",                             // no name
        "{\"mantles\":[{\"id\":\"a\",\"name\":\"d\"},{\"id\":\"b\",\"name\":\"d\"}]}",
    };
    for (const char* text : bad) {
        cJSON* s = cJSON_Parse(text);
        try { (void)version_name(s); } catch (const CanonicalError&) {}
        cJSON_Delete(s);
    }
}


void body_field_policies() {
    /* The Max path decodes every candidate value to compare them, so it allocates
     * inside a loop and discards most of what it makes — exactly the shape that
     * leaks. Pick and the Conflict fallback go through the same resolver. */
    cJSON* s = cJSON_Parse(kState);
    CounterMint ma("A"), mb("B");
    Doc a = enrich(s, ma), b = enrich(s, mb);
    cJSON* n1 = cJSON_CreateNumber(7);
    cJSON* n2 = cJSON_CreateNumber(3);
    cJSON* bad = cJSON_CreateString("not-a-number");
    set_field(a, "demo", "rune_1", "content.n", n1, ma);
    set_field(b, "demo", "rune_1", "content.n", n2, mb);
    set_field(a, "demo", "rune_2", "content.v", bad, ma);
    set_field(b, "demo", "rune_2", "content.v", n1, mb);
    cJSON_Delete(n1); cJSON_Delete(n2); cJSON_Delete(bad);

    Doc merged = join(a, b);
    JoinPolicy maxp; maxp.fields["content.*"] = FieldJoin::Max;
    JoinPolicy pick = JoinPolicy::core_defaults();
    for (const JoinPolicy& p : {JoinPolicy{}, maxp, pick}) {
        Doc f = flatten(merged, p);
        for (const Conflict& c : conflicts(merged, p)) {
            (void)c.hash();
            cJSON* j = conflict_to_json(c);
            cJSON_Delete(j);
        }
    }
    cJSON_Delete(s);
}


void body_sequence() {
    /* seq_order decodes every node on every call and seq_read decodes payloads
     * into a fresh array - both allocate in loops and discard most of it. */
    cJSON* s = cJSON_Parse(kState);
    CounterMint ma("A"), mb("B");
    Doc a = enrich(s, ma), b = enrich(s, mb);
    cJSON* empty = cJSON_CreateArray();
    seq_init(a, "demo", "rune_1", "content.blocks", empty, ma);
    seq_init(b, "demo", "rune_1", "content.blocks", empty, mb);
    cJSON_Delete(empty);

    for (int i = 0; i < 6; ++i) {
        cJSON* v = cJSON_CreateString(i % 2 ? "x" : "y");
        seq_insert(a, "demo", "rune_1", "content.blocks", i, v, ma);
        seq_insert(b, "demo", "rune_1", "content.blocks", i, v, mb);
        cJSON_Delete(v);
    }
    seq_erase(a, "demo", "rune_1", "content.blocks", 2);
    seq_erase(a, "demo", "rune_1", "content.blocks", 99);   // refusal path
    cJSON* v = cJSON_CreateString("z");
    seq_insert(a, "demo", "rune_1", "content.blocks", 99, v, ma);  // refusal path
    seq_insert(a, "nope", "rune_1", "content.blocks", 0, v, ma);   // missing mantle
    cJSON_Delete(v);

    Doc merged = join(a, b);
    cJSON* read = seq_read(merged, "demo", "rune_1", "content.blocks");
    if (read) cJSON_Delete(read);
    cJSON* none = seq_read(merged, "demo", "rune_1", "glyph");
    if (none) cJSON_Delete(none);
    (void)seq_is(merged, "demo", "rune_1", "content.blocks");
    (void)canon_doc(merged);
    Doc flat = flatten(merged);
    cJSON_Delete(s);
}

/* The utterance layer allocates cJSON in three places: parsing a journal export,
 * writing a history out, and reading one back. Each returns either nothing owned
 * or a tree the caller frees, so the count must come home. */
void body_utterance() {
    const char* journal =
        "[{\"seq\":1,\"command\":\"rune new text a\",\"verb\":\"rune\","
        "\"who\":\"ada\",\"pure\":true,\"slice\":\"undo\",\"minted\":[\"rune_a\"]},"
        "{\"seq\":2,\"command\":\"save\",\"verb\":\"save\",\"who\":null,"
        "\"pure\":false,\"slice\":\"host\",\"minted\":[]},"
        "{\"seq\":3,\"command\":\"tag a +x\",\"verb\":\"tag\",\"who\":null,"
        "\"pure\":true,\"slice\":\"undo\",\"minted\":[]}]";

    std::vector<JournalEntry> entries;
    parse_journal(std::string(journal), entries, nullptr);

    History h;
    h.record(entries, {"leak-check"});

    cJSON* out = h.to_json();
    History back;
    History::from_json(out, back, nullptr);
    cJSON_Delete(out);

    cJSON* one = utterance_to_json(*h.get(h.heads()[0]));
    Utterance u;
    utterance_from_json(one, u, nullptr);
    cJSON_Delete(one);

    /* And the failure paths, which are the ones that leak: an early return that
     * forgets the tree it parsed. */
    std::vector<JournalEntry> junk;
    parse_journal(std::string("{\"not\":\"an array\"}"), junk, nullptr);
    parse_journal(std::string("[{\"seq\":1}]"), junk, nullptr);
    parse_journal(std::string("not json at all"), junk, nullptr);
}

/* The glyph path allocates in three places: enrich building a register per
 * declaration, flatten decoding one back, and conflict rendering. */
void body_glyphs() {
    const char* a = "{\"mantles\":[],\"glyphs\":{\"stat\":{\"glyph\":\"stat\","
                    "\"kind\":\"measure\",\"fields\":[\"note\"]}}}";
    const char* b = "{\"mantles\":[],\"glyphs\":{\"stat\":{\"glyph\":\"stat\","
                    "\"kind\":\"entity\",\"fields\":[\"body\"]}}}";
    cJSON* pa = cJSON_Parse(a);
    cJSON* pb = cJSON_Parse(b);
    CounterMint ma("A"), mb("B");
    {
        Doc da = enrich(pa, ma), db = enrich(pb, mb);
        Doc merged = join(da, db);
        for (const Conflict& c : conflicts(merged)) {
            cJSON* j = conflict_to_json(c);
            cJSON_Delete(j);
        }
        Doc flat = flatten(merged);
        (void)canon_doc(merged);
    }
    (void)version_name(pa);
    /* The refusal paths, which are where an early return forgets a tree. */
    cJSON* bad = cJSON_Parse("{\"mantles\":[],\"glyphs\":[\"nope\"]}");
    try { (void)version_name(bad); } catch (const CanonicalError&) {}
    cJSON_Delete(bad);
    cJSON_Delete(pa);
    cJSON_Delete(pb);
}

/* The replica allocates on every path that matters to a long-running sync loop:
 * observe (a working copy and a delta, discarded on failure), merge (validation,
 * two tag scans, the join), resolve, and persistence. A leak here is per tick. */
void body_replica() {
    const char* s1 =
        "{\"mantles\":[{\"id\":\"m1\",\"name\":\"n\",\"tags\":{\"t\":{\"near\":{}}},\"rules\":[\"r\"],"
        "\"runes\":[{\"spirit\":{\"id\":\"rune_1\",\"name\":\"a\"},\"glyph\":\"text\","
        "\"content\":{\"body\":\"one\"},\"tags\":[\"x\"],\"relations\":[1]}]}],"
        "\"glyphs\":{\"g\":{\"glyph\":\"g\",\"source\":\"host\"}}}";
    const char* s2 =
        "{\"mantles\":[{\"id\":\"m1\",\"name\":\"n\",\"runes\":[{\"spirit\":{\"id\":\"rune_1\","
        "\"name\":\"a\"},\"glyph\":\"text\",\"content\":{\"body\":\"two\"}}]}]}";
    const char* s3 = "{\"mantles\":[{\"id\":\"m1\",\"name\":\"n\",\"runes\":[]}]}";

    Replica a, b;
    Replica::create("leak-replica-A-000", a, nullptr);
    Replica::create("leak-replica-B-000", b, nullptr);
    cJSON* j1 = cJSON_Parse(s1);
    cJSON* j2 = cJSON_Parse(s2);
    cJSON* j3 = cJSON_Parse(s3);
    {
        Replica::Observed o1 = a.observe(j1);
        b.merge(o1.delta, nullptr);
        Replica::Observed o2 = a.observe(j2);
        Replica::Observed o3 = b.observe(j3);
        a.merge(b.doc(), nullptr);
        b.merge(a.doc(), nullptr);
        for (const Conflict& c : a.conflicts()) {
            Doc delta;
            a.resolve(c, 0, &delta);
            break;
        }
        Doc f = a.flatten();
        Replica::Observed idle = a.observe(f.root);

        Replica back;
        Replica::from_bytes(a.to_bytes(), back, nullptr);
        Replica forked;
        back.fork("leak-replica-C-000", forked, nullptr);
        /* The refusal paths: a copy that collides, a document that fails the door,
         * a state that cannot be encoded, bytes that are not a replica. */
        Replica clone;
        Replica::from_bytes(a.to_bytes(), clone, nullptr);
        clone.observe(j1);
        a.observe(j3);
        a.merge(clone.doc(), nullptr);
        cJSON* bad = cJSON_Parse("{\"palabra\":1,\"mantles\":{\"n\":{\"present\":7}}}");
        a.merge(bad, nullptr);
        cJSON_Delete(bad);
        cJSON* dup = cJSON_Parse("{\"mantles\":[{\"name\":\"x\"},{\"name\":\"x\"}]}");
        a.observe(dup);
        cJSON_Delete(dup);
        Replica junk;
        Replica::from_bytes("{\"palabra_replica\":1,\"id\":\"x\"}", junk, nullptr);
    }
    cJSON_Delete(j1);
    cJSON_Delete(j2);
    cJSON_Delete(j3);

    /* The decoder's refusals, which used to be where an early return forgot a tree. */
    cJSON* v = decode(std::string("\x07\x03\x07\x01\x02", 5));
    if (v) cJSON_Delete(v);
    v = decode(std::string("\x08\x01\x05\x01k\x06", 6));
    if (v) cJSON_Delete(v);
    (void)is_canonical(std::string("\x08\x01\x05\x01k\x02", 6));
}

/* Anomalies decode every edge and name; references walk every declared field. */
void body_merge_rules() {
    Replica a, b;
    Replica::create("leak-rules-A-000001", a, nullptr);
    Replica::create("leak-rules-B-000001", b, nullptr);
    const char* base = "{\"mantles\":[{\"name\":\"m\",\"runes\":[{\"spirit\":{\"id\":\"r1\",\"name\":\"x\"},\"glyph\":\"stat\",\"content\":{}}],"
                       "\"layout\":{\"edges\":[]}}],\"glyphs\":{\"stat\":{\"glyph\":\"stat\"}}}";
    const char* sa = "{\"mantles\":[{\"name\":\"m\",\"runes\":[],\"layout\":{\"edges\":[]}}]}";
    const char* sb = "{\"mantles\":[{\"name\":\"m\",\"runes\":[{\"spirit\":{\"id\":\"r1\",\"name\":\"x\"},\"glyph\":\"stat\",\"content\":{}},"
                     "{\"spirit\":{\"id\":\"r2\",\"name\":\"x\"},\"glyph\":\"stat\",\"content\":{\"photo\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\"}}],"
                     "\"layout\":{\"edges\":[{\"from\":\"x\",\"to\":{\"mantle\":\"m\",\"rune\":\"x\"}}]}}],"
                     "\"glyphs\":{\"stat\":{\"glyph\":\"stat\"}}}";
    cJSON* jb = cJSON_Parse(base);
    cJSON* ja = cJSON_Parse(sa);
    cJSON* jbb = cJSON_Parse(sb);
    {
        a.observe(jb);
        b.merge(a.doc(), nullptr);
        a.observe(ja);
        b.observe(jbb);
        a.merge(b.doc(), nullptr);
        for (const Anomaly& x : a.anomalies()) {
            cJSON* j = anomaly_to_json(x);
            cJSON_Delete(j);
            (void)x.hash();
        }
        ReferencePolicy policy;
        policy.fields["content.*"] = sha256_hex_anywhere();
        policy.fields["descriptor"] = sha256_hex_anywhere();
        Doc f = a.flatten();
        BlockStore store;
        std::vector<Reference> refs = references(f.root, policy);
        (void)missing(refs, held_by(store));
        (void)content_matches("aa", "x");
    }
    cJSON_Delete(jb);
    cJSON_Delete(ja);
    cJSON_Delete(jbb);
}

/* A sync session allocates per frame: parsing a header, exporting and printing the
 * shareable state, parsing a peer's state. A leak here is per message, forever. */
void body_sync() {
    using namespace voidpalabra::sync;
    Replica a, b;
    Replica::create("leak-sync-A-0000001", a, nullptr);
    Replica::create("leak-sync-B-0000001", b, nullptr);
    cJSON* s = cJSON_Parse("{\"mantles\":[{\"name\":\"m\",\"runes\":[{\"spirit\":{\"id\":\"r1\",\"name\":\"x\"},"
                           "\"glyph\":\"t\",\"content\":{\"photo\":\"2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824\"}},"
                           "{\"spirit\":{\"id\":\"secret\",\"name\":\"y\"},\"glyph\":\"t\",\"content\":{}}],"
                           "\"layout\":{\"edges\":[{\"from\":\"x\",\"to\":\"y\"}]}}]}");
    a.observe(s);
    cJSON_Delete(s);
    {
        Host ha, hb;
        ha.share = [](const std::string&, const std::string& r) { return r != "secret"; };
        ha.references.fields["content.*"] = sha256_hex_anywhere();
        hb.references = ha.references;
        hb.have = [](const std::string&) { return false; };
        ha.read = [](const std::string&, std::string& out) { out = "x"; return true; };
        Session sa(a, ha), sb(b, hb);
        std::vector<std::string> to_b = sa.start(0).send, to_a = sb.start(0).send;
        for (int round = 0; round < 6; ++round) {
            std::vector<std::string> nb, na;
            for (const auto& f : to_b) { Step x = sb.receive(f, round * 100); nb.insert(nb.end(), x.send.begin(), x.send.end()); }
            for (const auto& f : to_a) { Step x = sa.receive(f, round * 100); na.insert(na.end(), x.send.begin(), x.send.end()); }
            Step ta = sa.tick(round * 100 + 50), tb = sb.tick(round * 100 + 50);
            na.insert(na.end(), ta.send.begin(), ta.send.end());
            nb.insert(nb.end(), tb.send.begin(), tb.send.end());
            to_b = na;
            to_a = nb;
        }
        sa.publish_presence("{}", 700);
        /* The refusal paths: junk frames, a bad state, an oversize presence. */
        sb.receive("VPS1\x05\x00\x00\x00{bad}", 800);
        sb.receive("garbage", 800);
        Message bad;
        bad.kind = Kind::doc;
        bad.from = a.id();
        bad.session = "x";
        bad.payload = "{\"palabra\":1,\"mantles\":{\"m\":{\"present\":7}}}";
        sb.receive(encode_frame(bad), 800);
        sa.publish_presence(std::string(20000, 'p'), 900);
        sa.close("done", 1000);
        Doc e = exportable(a.doc(), ha.share);
    }
}

void body_concurrent_structure() {
    using namespace voidpalabra::sync;
    cJSON* s = cJSON_Parse(
        "{\"mantles\":[{\"name\":\"net\",\"runes\":["
        "{\"spirit\":{\"id\":\"w\",\"name\":\"w\"}},{\"spirit\":{\"id\":\"w1\",\"name\":\"w1\"}},"
        "{\"spirit\":{\"id\":\"a\",\"name\":\"a\"}},{\"spirit\":{\"id\":\"b\",\"name\":\"b\"}}],"
        "\"layout\":{\"edges\":[{\"from\":\"w1\",\"to\":\"w\",\"relation\":\"=\"},"
        "{\"from\":\"a\",\"to\":\"w1\",\"relation\":\"1:0\"},{\"from\":\"b\",\"to\":\"w\",\"relation\":\"1:0\"},"
        "{\"from\":\"a\",\"to\":\"b\",\"relation\":\"in\"},{\"from\":\"b\",\"to\":\"a\",\"relation\":\"in\"},"
        "{\"from\":\"a\",\"to\":\"nobody\"}]}}]}");
    LinkRules rules;
    rules.equivalence = {"="};
    rules.acyclic = {"in"};
    Capacity c;
    c.name = "one";
    c.slot = Slot::ports;
    rules.capacity.push_back(c);
    Capacity e;
    e.name = "ends";
    e.through_equivalence = true;
    rules.capacity.push_back(e);
    Quotient q = quotient(s, rules);
    for (const Violation& v : check_links(s, rules)) {
        cJSON* j = violation_to_json(v);
        cJSON_Delete(j);
        (void)v.hash();
    }

    Replica a, b;
    Replica::create("leak-cs-A-00000001", a, nullptr);
    Replica::create("leak-cs-B-00000001", b, nullptr);
    a.observe(s);
    b.merge(a.doc());
    JoinPolicy pol;
    pol.fields["placement"] = FieldJoin::Latest;
    b.set_policy(pol);
    Doc f = b.flatten();
    (void)b.writers({"net", "a", "", "present"});
    (void)b.writers({"net", "", "", "edges"});
    cJSON_Delete(s);

    StreamReader r;
    Message m;
    m.kind = Kind::hello;
    r.feed(stream_frame(encode_frame(m)) + std::string("\x04\x00\x00\x00", 4));
    std::string frame;
    while (r.next(frame)) {}
}

struct Case { const char* name; void (*fn)(); };

const Case kCases[] = {
    {"canonical", body_canonical},
    {"encode/decode", body_encode_decode},
    {"decode refusals", body_decode_garbage},
    {"enrich/flatten", body_enrich_flatten},
    {"join + conflicts", body_join_and_conflicts},
    {"tag edits", body_tags},
    {"archive", body_archive},
    {"import_miga", body_import_miga},
    {"canonical refusals", body_refusals},
    {"field policies", body_field_policies},
    {"sequence", body_sequence},
    {"utterance/history", body_utterance},
    {"glyph declarations", body_glyphs},
    {"replica + validation", body_replica},
    {"anomalies + references", body_merge_rules},
    {"sync session", body_sync},
    {"concurrent structure", body_concurrent_structure},
};

}  // namespace

int main() {
    cJSON_Hooks hooks;
    hooks.malloc_fn = counting_malloc;
    hooks.free_fn = counting_free;
    cJSON_InitHooks(&hooks);

    for (const Case& c : kCases) {
        long before = g_live;
        balanced(c.name, c.fn);
        bool ok = g_live == before;
        std::printf("%s %-22s (%+ld live)\n", ok ? "ok  " : "LEAK", c.name,
                    g_live - before);
    }

    /* Run everything a second time: a leak that only shows up under repetition —
     * a cache, a static, a container that grows — would otherwise hide. */
    long before_all = g_live;
    for (int i = 0; i < 20; ++i)
        for (const Case& c : kCases) c.fn();
    ++g_checks;
    if (g_live != before_all)
        g_failures.push_back("20 repetitions leaked " +
                             std::to_string(g_live - before_all) + " allocation(s)");
    std::printf("%s 20x repetition       (%+ld live)\n",
                g_live == before_all ? "ok  " : "LEAK", g_live - before_all);

    for (const auto& f : g_failures) std::printf("  - %s\n", f.c_str());
    std::printf("\n%d/%d balanced, %ld total allocations exercised\n",
                g_checks - static_cast<int>(g_failures.size()), g_checks, g_total);
    return g_failures.empty() ? 0 : 1;
}
