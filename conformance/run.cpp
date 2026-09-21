/* run.cpp — the conformance runner.
 *
 * Void Core taught this lesson to us and we wrote it in a message before we had
 * taken it ourselves: *a layer with a contract gets PORTED; a layer without one
 * gets REINVENTED.* Palabra's SPEC.md was a document until this directory existed;
 * now it is a contract.
 *
 * The cases are pure JSON and the semantics are stated language-neutrally in
 * README.md, so an implementation in C, Rust, or on an ESP32 can be proven to
 * agree byte-for-byte without sharing a line of code with this one. That matters
 * more here than in most projects, because Palabra's entire claim is that two
 * independent peers compute the same name for the same state — a claim exactly one
 * implementation cannot test.
 *
 *   voidpalabra_conformance            run every case
 *   voidpalabra_conformance --regen    rewrite expectations from this implementation
 *
 * --regen is how the vectors were first produced and how they are updated when
 * CANON_VERSION legitimately changes. Regenerating to make a failure go away is
 * how a contract becomes worthless; the version bump is the honest move.
 */
#include "voidpalabra/archive.hpp"
#include "voidpalabra/canonical.hpp"
#include "voidpalabra/join.hpp"
#include "voidpalabra/utterance.hpp"
#include "voidpalabra/replica.hpp"
#include "voidpalabra/references.hpp"
#include "voidpalabra/links.hpp"
#include "voidpalabra/sync.hpp"

#include "cJSON.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace voidpalabra;

namespace {

std::string to_hex_bytes(const std::string& s) {
    static const char* d = "0123456789abcdef";
    std::string out;
    for (unsigned char c : s) { out += d[c >> 4]; out += d[c & 0xF]; }
    return out;
}

std::string read_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

bool write_file(const std::string& path, const std::string& text) {
    std::ofstream f(path, std::ios::binary);
    f << text;
    return f.good();
}

/* Evaluate one case against this implementation. `kind` selects which part of the
 * SPEC is under test; the result is always a hex string so the case format needs
 * exactly one comparison rule. Returns false if the case is malformed or the
 * implementation refused input the case did not mark as a refusal. */
bool evaluate(const std::string& kind, const cJSON* c, std::string& out,
              std::string& why) {
    const cJSON* input = cJSON_GetObjectItemCaseSensitive(const_cast<cJSON*>(c), "in");
    if (!input) { why = "case has no 'in'"; return false; }

    Policy policy;
    const cJSON* pol = cJSON_GetObjectItemCaseSensitive(const_cast<cJSON*>(c), "policy");
    if (pol) {
        const cJSON* iv = cJSON_GetObjectItemCaseSensitive(const_cast<cJSON*>(pol),
                                                           "include_view");
        if (iv) policy.include_view = cJSON_IsTrue(iv);
    }

    try {
        if (kind == "encode") { out = to_hex_bytes(encode(input)); return true; }
        if (kind == "rune") { out = to_hex(rune_hash(input, policy)); return true; }
        if (kind == "mantle") { out = to_hex(mantle_hash(input, policy)); return true; }
        if (kind == "slice") { out = version_name(input, policy); return true; }
        if (kind == "canon_slice") { out = to_hex_bytes(canon_slice(input, policy)); return true; }
        if (kind == "enrich") {
            /* A fixed mint, so the vector is reproducible. SPEC §5.1 requires tags
             * to be unique, not random — a deterministic source is conforming and
             * is the only kind that can be pinned. */
            CounterMint mint("t");
            Doc d = enrich(input, mint);
            out = to_hex_bytes(canon_doc(d));
            return true;
        }
        if (kind == "join") {
            /* `in` is [state_a, state_b]; each is enriched with its own mint, then
             * merged. This pins the §5.3 merge, not just the encoding. */
            const cJSON* a = cJSON_GetArrayItem(const_cast<cJSON*>(input), 0);
            const cJSON* b = cJSON_GetArrayItem(const_cast<cJSON*>(input), 1);
            if (!a || !b) { why = "join case needs a two-element 'in'"; return false; }
            CounterMint ma("A"), mb("B");
            Doc da = enrich(a, ma), db = enrich(b, mb);
            Doc merged = join(da, db);
            out = to_hex_bytes(canon_doc(merged));
            return true;
        }
        if (kind == "conflicts") {
            const cJSON* a = cJSON_GetArrayItem(const_cast<cJSON*>(input), 0);
            const cJSON* b = cJSON_GetArrayItem(const_cast<cJSON*>(input), 1);
            if (!a || !b) { why = "conflicts case needs a two-element 'in'"; return false; }
            CounterMint ma("A"), mb("B");
            Doc da = enrich(a, ma), db = enrich(b, mb);
            Doc merged = join(da, db);
            std::string acc;
            for (const Conflict& k : conflicts(merged)) acc += to_hex(k.hash());
            out = acc.empty() ? "none" : acc;
            return true;
        }
        if (kind == "validate") {
            /* `in` is an enriched document or a delta, exactly as a peer would send
             * it. `out` is "valid" or "refused". Pins the door (SPEC §5.6), so an
             * implementation that lets through what this one refuses — or refuses
             * what this one accepts — is caught before two such peers meet. */
            out = validate(input) ? "valid" : "refused";
            return true;
        }
        if (kind == "merged_slice") {
            /* `in` is [state_a, state_b]. `out` is the version name of the merged
             * slice — what a user of either peer would actually see. The join kind
             * pins the metadata; this pins the result, which is where the
             * mantles-named-a-and-r defect showed (zero mantles out of three). */
            const cJSON* a = cJSON_GetArrayItem(const_cast<cJSON*>(input), 0);
            const cJSON* b = cJSON_GetArrayItem(const_cast<cJSON*>(input), 1);
            if (!a || !b) { why = "merged_slice needs a two-element 'in'"; return false; }
            CounterMint ma("A"), mb("B");
            Doc merged = flatten(join(enrich(a, ma), enrich(b, mb)));
            out = version_name(merged.root);
            return true;
        }
        if (kind == "replica_doc") {
            /* `in` is {"id": "...", "observe": [state, state, ...]}: one replica
             * observing a sequence of states. `out` is the hex of its enriched
             * document's canonical bytes.
             *
             * This pins what an implementation MUST agree on for deltas to be
             * interoperable: the tag format "<id>_<n>", the order tags are minted
             * in, that a removal retires presence and records every live tag it
             * saw (§5.7), and that observing an unchanged state mints nothing. */
            const cJSON* id = cJSON_GetObjectItemCaseSensitive(const_cast<cJSON*>(input), "id");
            const cJSON* steps = cJSON_GetObjectItemCaseSensitive(const_cast<cJSON*>(input), "observe");
            if (!id || !cJSON_IsString(id) || !steps || !cJSON_IsArray(steps)) {
                why = "replica_doc needs {id, observe:[...]}";
                return false;
            }
            Replica r;
            std::string err;
            if (!Replica::create(id->valuestring, r, &err)) { out = "refused"; return true; }
            for (const cJSON* s = steps->child; s; s = s->next) {
                Replica::Observed o = r.observe(s);
                if (!o.ok) { out = "refused"; return true; }
            }
            out = to_hex_bytes(canon_doc(r.doc()));
            return true;
        }
        if (kind == "merge_anomalies") {
            /* `in` is {"base", "a", "b"}: replica A observes base; B takes it; A then
             * observes `a` while B observes `b`; they exchange. `out` is the
             * concatenated anomaly hashes on A, or "none". The ids are fixed so the
             * vector is reproducible, and both sides are checked to agree, because an
             * anomaly that two peers name differently is not a shared fact. */
            const cJSON* base = cJSON_GetObjectItemCaseSensitive(const_cast<cJSON*>(input), "base");
            const cJSON* sa = cJSON_GetObjectItemCaseSensitive(const_cast<cJSON*>(input), "a");
            const cJSON* sb = cJSON_GetObjectItemCaseSensitive(const_cast<cJSON*>(input), "b");
            if (!base || !sa || !sb) { why = "merge_anomalies needs {base, a, b}"; return false; }
            Replica ra, rb;
            Replica::create("vector-merge-A-000001", ra);
            Replica::create("vector-merge-B-000001", rb);
            if (!ra.observe(base).ok) { out = "refused"; return true; }
            rb.merge(ra.doc());
            if (!ra.observe(sa).ok || !rb.observe(sb).ok) { out = "refused"; return true; }
            ra.merge(rb.doc());
            rb.merge(ra.doc());
            std::string acc, other;
            for (const Anomaly& x : ra.anomalies()) acc += to_hex(x.hash());
            for (const Anomaly& x : rb.anomalies()) other += to_hex(x.hash());
            if (acc != other) { why = "the two replicas disagree about the anomalies"; return false; }
            out = acc.empty() ? "none" : acc;
            return true;
        }
        if (kind == "references") {
            /* `in` is {"fields": [declared field keys], "state": ...}, every declared
             * field read with the default SHA-256 finder. `out` is each reference as
             * "address mantle/rune/glyph/field", joined by ";", or "none". */
            const cJSON* fields = cJSON_GetObjectItemCaseSensitive(const_cast<cJSON*>(input), "fields");
            const cJSON* state = cJSON_GetObjectItemCaseSensitive(const_cast<cJSON*>(input), "state");
            if (!fields || !state) { why = "references needs {fields, state}"; return false; }
            ReferencePolicy policy;
            for (const cJSON* f = fields->child; f; f = f->next)
                if (cJSON_IsString(f)) policy.fields[f->valuestring] = sha256_hex_anywhere();
            std::string acc;
            for (const Reference& r : references(state, policy)) {
                if (!acc.empty()) acc += ";";
                acc += r.address + " " + r.mantle + "/" + r.rune + "/" + r.glyph + "/" + r.field;
            }
            out = acc.empty() ? "none" : acc;
            return true;
        }
        if (kind == "frame") {
            /* `in` is a message as JSON — {kind, from, session, seq, digest?, reason?,
             * addresses?, address?, payload?} with payload as text — and `out` is the
             * hex of its frame (SPEC §11). Or `in` is {"hex": "..."}, a frame as
             * bytes, and `out` is "valid" or "refused": what any implementation must
             * refuse before believing it. */
            const cJSON* hexin = cJSON_GetObjectItemCaseSensitive(const_cast<cJSON*>(input), "hex");
            if (hexin && cJSON_IsString(hexin)) {
                std::string bytes, h = hexin->valuestring;
                for (std::size_t i = 0; i + 1 < h.size(); i += 2)
                    bytes.push_back(static_cast<char>(std::stoi(h.substr(i, 2), nullptr, 16)));
                sync::Message m;
                out = sync::decode_frame(bytes, m, sync::Limits{}) ? "valid" : "refused";
                return true;
            }
            auto str = [&](const char* k) {
                const cJSON* v = cJSON_GetObjectItemCaseSensitive(const_cast<cJSON*>(input), k);
                return v && cJSON_IsString(v) ? std::string(v->valuestring) : std::string();
            };
            static const std::pair<const char*, sync::Kind> kinds[] = {
                {"hello", sync::Kind::hello}, {"doc", sync::Kind::doc}, {"ack", sync::Kind::ack},
                {"refuse", sync::Kind::refuse}, {"want", sync::Kind::want}, {"content", sync::Kind::content},
                {"absent", sync::Kind::absent}, {"presence", sync::Kind::presence}, {"bye", sync::Kind::bye}};
            sync::Message m;
            bool known = false;
            for (const auto& k : kinds) if (str("kind") == k.first) { m.kind = k.second; known = true; }
            if (!known) { why = "unknown kind in case"; return false; }
            m.from = str("from");
            m.session = str("session");
            const cJSON* seq = cJSON_GetObjectItemCaseSensitive(const_cast<cJSON*>(input), "seq");
            m.seq = seq && cJSON_IsNumber(seq) ? static_cast<std::uint64_t>(seq->valuedouble) : 0;
            m.digest = str("digest");
            m.reason = str("reason");
            m.address = str("address");
            m.payload = str("payload");
            const cJSON* as = cJSON_GetObjectItemCaseSensitive(const_cast<cJSON*>(input), "addresses");
            for (const cJSON* a = as ? as->child : nullptr; a; a = a->next)
                if (cJSON_IsString(a)) m.addresses.push_back(a->valuestring);
            out = to_hex_bytes(sync::encode_frame(m));
            return true;
        }
        if (kind == "links") {
            /* `in` is {"rules": {equivalence, capacity:[{name, relation, slot, max,
             * through_equivalence}], acyclic}, "state": ...}. `out` is every
             * non-trivial class as "rep=member,member" joined by ";", then "|", then
             * the violation hashes (SPEC §5.11), or "none" for either part. */
            const cJSON* rj = cJSON_GetObjectItemCaseSensitive(const_cast<cJSON*>(input), "rules");
            const cJSON* state = cJSON_GetObjectItemCaseSensitive(const_cast<cJSON*>(input), "state");
            if (!rj || !state) { why = "links needs {rules, state}"; return false; }
            LinkRules rules;
            auto strings = [&](const char* k, std::vector<std::string>& into) {
                const cJSON* a = cJSON_GetObjectItemCaseSensitive(const_cast<cJSON*>(rj), k);
                for (const cJSON* s = a ? a->child : nullptr; s; s = s->next)
                    if (cJSON_IsString(s)) into.push_back(s->valuestring);
            };
            strings("equivalence", rules.equivalence);
            strings("acyclic", rules.acyclic);
            const cJSON* caps = cJSON_GetObjectItemCaseSensitive(const_cast<cJSON*>(rj), "capacity");
            for (const cJSON* c2 = caps ? caps->child : nullptr; c2; c2 = c2->next) {
                Capacity cap;
                auto s = [&](const char* k) {
                    const cJSON* v = cJSON_GetObjectItemCaseSensitive(const_cast<cJSON*>(c2), k);
                    return v && cJSON_IsString(v) ? std::string(v->valuestring) : std::string();
                };
                cap.name = s("name");
                if (!s("relation").empty()) cap.relation = s("relation");
                static const std::pair<const char*, Slot> slots[] = {
                    {"to", Slot::to}, {"from", Slot::from}, {"ends", Slot::ends}, {"ports", Slot::ports},
                    {"from_port", Slot::from_port}, {"to_port", Slot::to_port}};
                bool known = false;
                for (const auto& sl : slots) if (s("slot") == sl.first) { cap.slot = sl.second; known = true; }
                if (!known) { why = "unknown slot"; return false; }
                const cJSON* mx = cJSON_GetObjectItemCaseSensitive(const_cast<cJSON*>(c2), "max");
                if (mx && cJSON_IsNumber(mx)) cap.max = static_cast<std::size_t>(mx->valuedouble);
                cap.through_equivalence = cJSON_IsTrue(
                    cJSON_GetObjectItemCaseSensitive(const_cast<cJSON*>(c2), "through_equivalence"));
                rules.capacity.push_back(cap);
            }
            std::string classes;
            for (const auto& cls : quotient(state, rules).classes) {
                if (!classes.empty()) classes += ";";
                classes += cls.front().mantle + "/" + cls.front().id + "=";
                for (std::size_t i = 0; i < cls.size(); ++i)
                    classes += (i ? "," : "") + cls[i].mantle + "/" + cls[i].id;
            }
            std::string vs;
            for (const Violation& v : check_links(state, rules)) vs += to_hex(v.hash());
            out = (classes.empty() ? "none" : classes) + "|" + (vs.empty() ? "none" : vs);
            return true;
        }
        if (kind == "stream") {
            /* `in` is {"hex": bytes as they arrived on a stream}. `out` is each frame
             * the stream envelope delivers, as hex, joined by ","; then "+partial" if
             * bytes are left over; "none" if nothing whole arrived; or "refused" if
             * the stream is broken (SPEC §11.9). */
            const cJSON* hexin = cJSON_GetObjectItemCaseSensitive(const_cast<cJSON*>(input), "hex");
            if (!hexin || !cJSON_IsString(hexin)) { why = "stream needs {hex}"; return false; }
            std::string bytes, h = hexin->valuestring;
            for (std::size_t i = 0; i + 1 < h.size(); i += 2)
                bytes.push_back(static_cast<char>(std::stoi(h.substr(i, 2), nullptr, 16)));
            sync::StreamReader r;
            r.feed(bytes);
            std::string acc, f;
            while (r.next(f)) acc += (acc.empty() ? "" : ",") + to_hex_bytes(f);
            if (r.broken()) { out = "refused"; return true; }
            if (acc.empty()) acc = "none";
            if (r.pending()) acc += "+partial";
            out = acc;
            return true;
        }
        if (kind == "script") {
            /* `in` is {"steps": [{"on": "A"|"B", "observe": state} |
             * {"on": "A"|"B", "merge": true}], "latest": [field keys]}. Two replicas
             * with fixed ids; "merge" takes the other's document. At the end each takes
             * the other's once more. `out` is the SHA-256 of the canonical enriched
             * document — which pins the tag each write was minted under, and so the
             * Lamport rule (SPEC §5.7) — then " ", then the version name of what is
             * shown with the listed fields under FieldJoin::Latest (§5.10). Both
             * replicas must agree on both. */
            const cJSON* steps = cJSON_GetObjectItemCaseSensitive(const_cast<cJSON*>(input), "steps");
            if (!steps || !cJSON_IsArray(steps)) { why = "script needs {steps}"; return false; }
            Replica ra, rb;
            Replica::create("vector-script-A-000001", ra);
            Replica::create("vector-script-B-000001", rb);
            for (const cJSON* s = steps->child; s; s = s->next) {
                const cJSON* on = cJSON_GetObjectItemCaseSensitive(const_cast<cJSON*>(s), "on");
                bool is_a = on && cJSON_IsString(on) && std::strcmp(on->valuestring, "A") == 0;
                Replica& me = is_a ? ra : rb;
                Replica& other = is_a ? rb : ra;
                const cJSON* st = cJSON_GetObjectItemCaseSensitive(const_cast<cJSON*>(s), "observe");
                if (st) {
                    if (!me.observe(st).ok) { out = "refused"; return true; }
                } else if (me.merge(other.doc()) != MergeResult::ok) {
                    out = "refused";
                    return true;
                }
            }
            ra.merge(rb.doc());
            rb.merge(ra.doc());
            JoinPolicy pol;
            const cJSON* latest = cJSON_GetObjectItemCaseSensitive(const_cast<cJSON*>(input), "latest");
            for (const cJSON* f = latest ? latest->child : nullptr; f; f = f->next)
                if (cJSON_IsString(f)) pol.fields[f->valuestring] = FieldJoin::Latest;
            ra.set_policy(pol);
            rb.set_policy(pol);
            std::string da = to_hex(sha256(canon_doc(ra.doc())));
            std::string db = to_hex(sha256(canon_doc(rb.doc())));
            Doc fa = ra.flatten(), fb = rb.flatten();
            std::string sa = version_name(fa.root), sb = version_name(fb.root);
            if (da != db || sa != sb) { why = "the two replicas disagree"; return false; }
            out = da + " " + sa;
            return true;
        }
        if (kind == "utterance") {
            /* `in` is an utterance object. `out` is its content address, which is
             * the one thing two implementations must agree on before anything
             * else in Phase 3 can be shared. */
            Utterance u;
            std::string err;
            if (!utterance_from_json(input, u, &err)) { why = err; return false; }
            out = u.hash;
            return true;
        }
        if (kind == "ingest") {
            /* `in` is a journal export, exactly as `vc_export_journal` emits it.
             * `out` is the resulting cut name — which pins the FILTER as well as
             * the naming, because an implementation that keeps an effectful entry
             * holds an extra head and therefore names a different cut. That is
             * what makes "with a save appended — must equal" a real assertion
             * about VoidCore:SPEC §6.2 rather than a recorded number. */
            std::vector<JournalEntry> entries;
            std::string err;
            if (!parse_journal(input, entries, &err)) { out = "refused"; return true; }
            History h;
            h.record(entries);
            out = h.cut_name();
            return true;
        }
        if (kind == "ingest_report") {
            /* The same input, reporting what was REFUSED. Separate from `ingest`
             * because a skip must be visible and a cut name cannot show one: the
             * whole argument for returning skips is that a gap you cannot see is
             * worse than an entry you must step over. */
            std::vector<JournalEntry> entries;
            std::string err;
            if (!parse_journal(input, entries, &err)) { out = "refused"; return true; }
            History h;
            IngestReport r = h.record(entries);
            out = std::to_string(r.recorded.size()) + " recorded";
            for (const Skipped& s : r.skipped) {
                out += "; ";
                switch (s.why) {
                    case Skip::effectful:  out += "effectful"; break;
                    case Skip::view_slice: out += "view"; break;
                    case Skip::host_slice: out += "host"; break;
                }
                out += " " + s.entry.verb;
            }
            return true;
        }
        if (kind == "linear") {
            /* `in` is an array of utterance objects, delivered in the order given.
             * `out` is the linear extension, so the canonical tiebreak is pinned
             * rather than assumed — two implementations must render one timeline
             * from one graph, or the "three faces are three projections" claim
             * is false. */
            std::vector<Utterance> inbox;
            for (const cJSON* e = input->child; e; e = e->next) {
                Utterance u;
                std::string err;
                if (!utterance_from_json(e, u, &err)) { why = err; return false; }
                inbox.push_back(u);
            }
            History h;
            std::vector<Utterance> unplaceable;
            h.receive(inbox, &unplaceable);
            if (!unplaceable.empty()) { out = "incomplete"; return true; }
            std::string acc;
            for (const std::string& hash : h.linear_extension()) {
                if (!acc.empty()) acc += " ";
                acc += hash.substr(2, 8);   // short, so a vector stays readable
            }
            out = acc.empty() ? "empty" : acc;
            return true;
        }
        why = "unknown kind '" + kind + "'";
        return false;
    } catch (const CanonicalError& e) {
        out = "refused";
        (void)e;
        return true;
    }
}

struct Totals { int pass = 0; int fail = 0; };

void run_file(const std::string& path, bool regen, Totals& tot,
              std::vector<std::string>& failures) {
    std::string text = read_file(path);
    if (text.empty()) { std::printf("  !! cannot read %s\n", path.c_str()); ++tot.fail; return; }
    cJSON* root = cJSON_Parse(text.c_str());
    if (!root) { std::printf("  !! %s is not JSON\n", path.c_str()); ++tot.fail; return; }

    const cJSON* kind_j = cJSON_GetObjectItemCaseSensitive(root, "kind");
    const cJSON* about = cJSON_GetObjectItemCaseSensitive(root, "about");
    cJSON* cases = cJSON_GetObjectItemCaseSensitive(root, "cases");
    std::string kind = kind_j && kind_j->valuestring ? kind_j->valuestring : "";

    std::printf("  %s — %s\n", path.substr(path.find_last_of("/\\") + 1).c_str(),
                about && about->valuestring ? about->valuestring : "");

    bool changed = false;
    std::string previous_out;
    for (cJSON* c = cases ? cases->child : nullptr; c; c = c->next) {
        const cJSON* name = cJSON_GetObjectItemCaseSensitive(c, "name");
        std::string label = name && name->valuestring ? name->valuestring : "(unnamed)";
        std::string got, why;
        if (!evaluate(kind, c, got, why)) {
            std::printf("    !! %s: %s\n", label.c_str(), why.c_str());
            failures.push_back(path + " / " + label + ": " + why);
            ++tot.fail;
            continue;
        }
        cJSON* expect = cJSON_GetObjectItemCaseSensitive(c, "out");
        if (regen) {
            if (!expect || !expect->valuestring || got != expect->valuestring) changed = true;
            cJSON_DeleteItemFromObjectCaseSensitive(c, "out");
            cJSON_AddStringToObject(c, "out", got.c_str());
            ++tot.pass;
            continue;
        }
        if (!expect || !expect->valuestring) {
            std::printf("    !! %s: no expected value (run --regen once)\n", label.c_str());
            failures.push_back(path + " / " + label + ": missing expectation");
            ++tot.fail;
        } else if (got != expect->valuestring) {
            std::printf("    FAIL %s\n      expected %s\n      got      %s\n",
                        label.c_str(), expect->valuestring, got.c_str());
            failures.push_back(path + " / " + label);
            ++tot.fail;
        } else {
            ++tot.pass;
        }

        /* Enforce the RELATIONSHIP a case name claims, not just its recorded
         * value. This is what keeps the suite from degenerating into "whatever
         * the implementation printed, blessed": a vector file regenerated against
         * a broken implementation would still record self-consistent bytes, but
         * "tags ba — must equal tags ab" would stop holding and say so.
         *
         * These are the SPEC's actual invariants — set semantics, hydration,
         * undirected symmetry, order sensitivity — expressed as relations between
         * adjacent cases, so they survive any regeneration. */
        /* A name that states its own outcome — "... — refused", "... — valid",
         * "... — none" — is held to it, the same way "must equal" is. Added after a
         * vector named "a well-formed hello — valid" was regenerated to "refused":
         * its hand-written header length was one byte short, the decoder was right,
         * and --regen recorded the wrong expectation without a murmur. The name knew
         * better than the bytes. */
        for (const char* claim : {"refused", "valid", "none"}) {
            std::string marker = std::string(" — ") + claim;  // " — claim" (an em dash, UTF-8)
            std::size_t at = label.find(marker);
            if (at == std::string::npos) continue;
            std::size_t end = at + marker.size();
            if (end < label.size() && label[end] != ' ' && label[end] != '(') continue;
            if (!regen && got != claim) {
                std::printf("    FAIL %s: named '%s', produced '%s'\n", label.c_str(), claim, got.c_str());
                failures.push_back(path + " / " + label + ": outcome named in the case differs");
                ++tot.fail;
            } else if (!regen) {
                ++tot.pass;
            }
        }

        bool claims_equal = label.find("must equal") != std::string::npos;
        bool claims_differ = label.find("must DIFFER") != std::string::npos;
        if ((claims_equal || claims_differ) && !previous_out.empty()) {
            bool equal = (got == previous_out);
            if (claims_equal && !equal) {
                std::printf("    FAIL %s: differs from the preceding case\n", label.c_str());
                failures.push_back(path + " / " + label + ": relation 'must equal' broken");
                ++tot.fail;
            } else if (claims_differ && equal) {
                std::printf("    FAIL %s: equals the preceding case\n", label.c_str());
                failures.push_back(path + " / " + label + ": relation 'must DIFFER' broken");
                ++tot.fail;
            } else {
                ++tot.pass;  // the relation is its own assertion
            }
        }
        previous_out = got;
    }

    if (regen && changed) {
        char* txt = cJSON_Print(root);
        if (txt) { write_file(path, std::string(txt) + "\n"); cJSON_free(txt); }
        std::printf("    (regenerated)\n");
    }
    cJSON_Delete(root);
}

}  // namespace

int main(int argc, char** argv) {
    bool regen = argc > 1 && std::strcmp(argv[1], "--regen") == 0;

    /* Listed explicitly rather than globbed: a case file that silently stops being
     * run is the failure mode a conformance suite must not have. */
    const char* files[] = {
        "conformance/cases/01-encoding.json",
        "conformance/cases/02-numbers-and-strings.json",
        "conformance/cases/03-sets-and-order.json",
        "conformance/cases/04-rune-hydration.json",
        "conformance/cases/05-mantle-and-edges.json",
        "conformance/cases/06-slice-and-exclusions.json",
        "conformance/cases/07-refusals.json",
        "conformance/cases/08-enriched-document.json",
        "conformance/cases/09-join.json",
        "conformance/cases/10-conflicts.json",
        "conformance/cases/11-utterance.json",
        "conformance/cases/12-journal-ingest.json",
        "conformance/cases/13-ingest-report.json",
        "conformance/cases/14-linear-extension.json",
        "conformance/cases/15-glyph-declarations.json",
        "conformance/cases/16-glyph-merge.json",
        "conformance/cases/17-validation.json",
        "conformance/cases/18-merged-slice.json",
        "conformance/cases/19-replica.json",
        "conformance/cases/20-merge-anomalies.json",
        "conformance/cases/21-references.json",
        "conformance/cases/22-frames.json",
        "conformance/cases/23-links.json",
        "conformance/cases/24-stream.json",
        "conformance/cases/25-lamport-and-latest.json",
    };

    std::printf("Void Palabra conformance — SPEC.md v%d%s\n\n", kCanonVersion,
                regen ? "  [REGENERATING]" : "");
    Totals tot;
    std::vector<std::string> failures;
    for (const char* f : files) run_file(f, regen, tot, failures);

    std::printf("\n%d passed, %d failed\n", tot.pass, tot.fail);
    if (!failures.empty()) {
        std::printf("\nFailures:\n");
        for (const auto& f : failures) std::printf("  - %s\n", f.c_str());
        std::printf("\nIf this implementation is right and the vectors are stale,\n"
                    "bump kCanonVersion and --regen. Regenerating to silence a\n"
                    "failure is how a contract stops being one.\n");
    }
    return tot.fail ? 1 : 0;
}
