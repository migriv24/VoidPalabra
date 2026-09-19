/* replica.cpp — see include/voidpalabra/replica.hpp for the contract.
 *
 * Every change goes through one small editor that loads an OrSet, changes it, and
 * stores it back — recording the difference into a delta as it goes. Nothing here
 * writes CRDT metadata by hand, which is the rule crdt/internal.hpp states for the
 * whole layer: hand-assembled metadata is how invariants get broken quietly.
 */
#include "internal.hpp"

#include "encoding/internal.hpp"

#include "voidpalabra/replica.hpp"
#include "voidpalabra/canonical.hpp"
#include "cJSON.h"

#include <cstdint>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace voidpalabra {

using namespace crdt;

namespace {

constexpr std::size_t kMinIdLength = 16;
constexpr std::size_t kMaxIdLength = 128;
constexpr double kMaxCounter = 9007199254740992.0;  // 2^53: exact in a JSON number

bool valid_id(const std::string& id, std::string* why) {
    if (id.size() < kMinIdLength || id.size() > kMaxIdLength) {
        if (why) *why = "a replica id is 16 to 128 characters";
        return false;
    }
    for (char c : id) {
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || c == '-';
        /* No `_`: a tag is "<id>_<n>", and an id containing the separator would let
         * one replica's tags be read as another's. */
        if (!ok) {
            if (why) *why = "a replica id is [A-Za-z0-9-] only";
            return false;
        }
    }
    return true;
}

cJSON* empty_root() {
    cJSON* r = cJSON_CreateObject();
    cJSON_AddNumberToObject(r, "palabra", 1);
    cJSON_AddItemToObject(r, "mantles", cJSON_CreateObject());
    return r;
}

/* Every tag anywhere beneath `node` — adds and removes both. */
void collect_all_tags(const cJSON* node, std::set<std::string>& out) {
    if (!node) return;
    if (is_orset(node)) {
        OrSet s = orset_from_json(node);
        for (const auto& kv : s.adds) out.insert(kv.first);
        for (const auto& t : s.removes) out.insert(t);
        return;
    }
    if (!cJSON_IsObject(node)) return;
    for (const cJSON* it = node->child; it; it = it->next) collect_all_tags(it, out);
}

/* For every ADD beneath `node`: the tag, and what it names — the path of the set it
 * was added to, and the value. Two adds under one tag that name different things
 * are the signature of a reused tag. */
void collect_adds(const cJSON* node, const std::string& path,
                  std::map<std::string, std::string>& out) {
    if (!node) return;
    if (is_orset(node)) {
        OrSet s = orset_from_json(node);
        for (const auto& kv : s.adds) {
            std::string named = path;
            named.push_back('\0');
            named += kv.second;
            out[kv.first] = named;
        }
        return;
    }
    if (!cJSON_IsObject(node)) return;
    for (const cJSON* it = node->child; it; it = it->next)
        collect_adds(it, path + '/' + (it->string ? it->string : ""), out);
}

/* The counter inside "<id>_<n>", or false if `tag` was not minted under `id`. */
bool own_counter(const std::string& tag, const std::string& id, std::uint64_t& n) {
    if (tag.size() <= id.size() + 1 || tag.compare(0, id.size(), id) != 0 ||
        tag[id.size()] != '_')
        return false;
    n = 0;
    for (std::size_t i = id.size() + 1; i < tag.size(); ++i) {
        char c = tag[i];
        if (c < '0' || c > '9') return false;
        if (n > (UINT64_MAX - 9) / 10) return false;
        n = n * 10 + static_cast<std::uint64_t>(c - '0');
    }
    return true;
}

using Path = std::vector<std::string>;

Path join_path(Path p, std::initializer_list<std::string> more) {
    for (const auto& k : more) p.push_back(k);
    return p;
}

cJSON* at_path(cJSON* root, const Path& path, bool create) {
    cJSON* n = root;
    for (const auto& k : path) {
        if (!n) return nullptr;
        cJSON* c = cJSON_GetObjectItemCaseSensitive(n, k.c_str());
        if (!c && create) {
            c = cJSON_CreateObject();
            cJSON_AddItemToObject(n, k.c_str(), c);
        }
        n = c;
    }
    return n;
}

bool same(const OrSet& a, const OrSet& b) { return a.adds == b.adds && a.removes == b.removes; }

class Editor {
public:
    Editor(cJSON* work, cJSON* delta, const std::string& id, std::uint64_t& issued,
           const JoinPolicy& policy)
        : work_(work), delta_(delta), id_(id), issued_(issued), policy_(policy) {}

    std::size_t changes() const { return changes_; }

    const cJSON* node(const Path& p) const { return at_path(work_, p, false); }

    std::string mint() { return id_ + "_" + std::to_string(++issued_); }

    OrSet load(const Path& p) const { return orset_from_json(node(p)); }

    void store(const Path& p, const OrSet& before, const OrSet& after) {
        if (same(before, after)) return;
        Path parent(p.begin(), p.end() - 1);
        cJSON* holder = at_path(work_, parent, true);
        cJSON_DeleteItemFromObjectCaseSensitive(holder, p.back().c_str());
        cJSON_AddItemToObject(holder, p.back().c_str(), orset_to_json(after));

        /* The delta holds only what changed. Joined into any document, it has the
         * same effect as this store — that is what makes it a delta. */
        OrSet diff;
        for (const auto& kv : after.adds)
            if (!before.adds.count(kv.first)) diff.adds.insert(kv);
        for (const auto& t : after.removes)
            if (!before.removes.count(t)) diff.removes.insert(t);
        cJSON* dholder = at_path(delta_, parent, true);
        if (cJSON* existing = cJSON_GetObjectItemCaseSensitive(dholder, p.back().c_str())) {
            diff = join(orset_from_json(existing), diff);
            cJSON_DeleteItemFromObjectCaseSensitive(dholder, p.back().c_str());
        }
        cJSON_AddItemToObject(dholder, p.back().c_str(), orset_to_json(diff));
        ++changes_;
    }

    /* Rule 1 of replica.hpp: a value equal to what `flatten` showed is not an edit,
     * even when the register holds more than one value. */
    void sync_register(const Path& p, const std::string& field, const std::string* value) {
        Live l = resolve(node(p), policy_.lookup(field));
        bool had = !l.values.empty();
        if (value) {
            if (had && l.values[0] == *value) return;
            OrSet before = load(p), after = before;
            write(after, mint(), *value);
            store(p, before, after);
        } else {
            if (!had) return;
            OrSet before = load(p), after = before;
            after.remove_all();
            store(p, before, after);
        }
    }

    void sync_set(const Path& p, const std::set<std::string>& want) {
        OrSet before = load(p), after = before;
        std::vector<std::string> live = before.values();
        std::set<std::string> have(live.begin(), live.end());
        for (const auto& v : want)
            if (!have.count(v)) after.add(mint(), v);
        for (const auto& v : have)
            if (!want.count(v)) after.remove_value(v);
        store(p, before, after);
    }

    bool live(const Path& p) const { return !load(join_path(p, {"present"})).values().empty(); }

    void make_present(const Path& p, const char* value) {
        Path pp = join_path(p, {"present"});
        OrSet before = load(pp);
        if (!before.values().empty()) return;
        OrSet after = before;
        after.add(mint(), value);
        store(pp, before, after);
    }

    /* Remove, recording every live tag the removal SAW beneath it (SPEC §5.7).
     * Those tags go into the object's own `present` removes, where OrSet semantics
     * ignore them and conflict detection reads them. */
    void remove(const Path& p, bool even_if_dead) {
        const cJSON* n = node(p);
        if (!n) return;
        Path pp = join_path(p, {"present"});
        OrSet before = load(pp);
        /* Observing that something is absent when it is already absent is not an
         * act. In particular it must not settle a pending deleted-while-edited
         * conflict: the user of this device was never shown the edit. */
        if (before.values().empty() && !even_if_dead) return;
        OrSet after = before;
        after.remove_all();
        std::set<std::string> seen;
        collect_live_tags(n, true, seen);
        for (const auto& t : seen) after.removes.insert(t);
        store(pp, before, after);
    }

private:
    cJSON* work_;
    cJSON* delta_;
    const std::string& id_;
    std::uint64_t& issued_;
    const JoinPolicy& policy_;
    std::size_t changes_ = 0;
};

/* Names of the fields beneath `fields` that start with `prefix` and still show a
 * value. Used to find keys the state no longer has. */
std::vector<std::string> shown_with_prefix(const Editor& e, const Path& fields,
                                           const char* prefix) {
    std::vector<std::string> out;
    const cJSON* f = e.node(fields);
    std::size_t n = std::strlen(prefix);
    for (const cJSON* it = f ? f->child : nullptr; it; it = it->next) {
        if (!it->string || std::strncmp(it->string, prefix, n) != 0) continue;
        if (!live_of(it).values.empty()) out.push_back(it->string);
    }
    return out;
}

/* A value that `enrich` writes only when it is not its default. Absent, and an
 * empty array, are the default. */
const cJSON* non_default(const cJSON* v) {
    if (!v) return nullptr;
    if (cJSON_IsArray(v) && cJSON_GetArraySize(v) == 0) return nullptr;
    return v;
}

void keyed_fields(Editor& e, const Path& fields, const char* prefix, const cJSON* object) {
    std::set<std::string> present;
    for (const cJSON* c = object ? object->child : nullptr; c; c = c->next) {
        if (!c->string || !c->string[0]) continue;
        std::string name = std::string(prefix) + c->string;
        present.insert(name);
        std::string bytes = as_text(c);
        e.sync_register(join_path(fields, {name}), name, &bytes);
    }
    for (const auto& name : shown_with_prefix(e, fields, prefix))
        if (!present.count(name)) e.sync_register(join_path(fields, {name}), name, nullptr);
}

void optional_field(Editor& e, const Path& fields, const char* name, const cJSON* v) {
    const cJSON* nd = non_default(v);
    if (nd) {
        std::string bytes = as_text(nd);
        e.sync_register(join_path(fields, {name}), name, &bytes);
    } else {
        e.sync_register(join_path(fields, {name}), name, nullptr);
    }
}

void fixed_field(Editor& e, const Path& fields, const std::string& name, const cJSON* v) {
    std::string bytes = as_text(v);  // absent encodes as null, as `enrich` writes it
    e.sync_register(join_path(fields, {name}), name, &bytes);
}

void observe_rune(Editor& e, const Path& base, const cJSON* r) {
    e.make_present(base, "1");
    Path fields = join_path(base, {"fields"});
    fixed_field(e, fields, "spirit.name", get(get(r, "spirit"), "name"));
    fixed_field(e, fields, "glyph", get(r, "glyph"));
    for (const char* k : kFacets)
        fixed_field(e, fields, std::string("facets.") + k, get(get(r, "facets"), k));
    fixed_field(e, fields, "placement", get(r, "placement"));
    optional_field(e, fields, "relations", get(r, "relations"));

    const cJSON* content = get(r, "content");
    if (content && !cJSON_IsObject(content))
        throw CanonicalError("rune content is not an object");
    keyed_fields(e, fields, "content.", content);

    std::set<std::string> want;
    const cJSON* tags = get(r, "tags");
    for (const cJSON* t = tags ? tags->child : nullptr; t; t = t->next) want.insert(as_text(t));
    e.sync_set(join_path(base, {"tags"}), want);
}

void observe_mantle(Editor& e, const Path& base, const cJSON* m) {
    e.make_present(base, "1");
    Path fields = join_path(base, {"fields"});
    fixed_field(e, fields, "id", get(m, "id"));
    fixed_field(e, fields, "domain", get(m, "domain"));
    const cJSON* mtags = get(m, "tags");
    if (mtags && !cJSON_IsObject(mtags) && !cJSON_IsNull(mtags))
        throw CanonicalError("mantle tags are not an object");
    keyed_fields(e, fields, "tags.", cJSON_IsObject(mtags) ? mtags : nullptr);
    optional_field(e, fields, "rules", get(m, "rules"));

    std::map<std::string, const cJSON*> runes;
    const cJSON* rarr = get(m, "runes");
    for (const cJSON* r = rarr ? rarr->child : nullptr; r; r = r->next) {
        const cJSON* id = get(get(r, "spirit"), "id");
        if (!id || !cJSON_IsString(id) || !id->valuestring || !id->valuestring[0])
            throw CanonicalError("a rune has no spirit.id");
        if (!runes.emplace(id->valuestring, r).second)
            throw CanonicalError(std::string("duplicate rune id ") + id->valuestring);
    }
    for (const auto& kv : runes) observe_rune(e, join_path(base, {"runes", kv.first}), kv.second);

    /* Runes this replica showed and the state no longer has. */
    const cJSON* existing = e.node(join_path(base, {"runes"}));
    std::vector<std::string> gone;
    for (const cJSON* r = existing ? existing->child : nullptr; r; r = r->next)
        if (r->string && !runes.count(r->string) && e.live(join_path(base, {"runes", r->string})))
            gone.push_back(r->string);
    for (const auto& id : gone) e.remove(join_path(base, {"runes", id}), false);

    std::set<std::string> edges;
    const cJSON* earr = get(get(m, "layout"), "edges");
    for (const cJSON* ed = earr ? earr->child : nullptr; ed; ed = ed->next) edges.insert(as_text(ed));
    e.sync_set(join_path(base, {"edges"}), edges);
}

std::size_t observe_into(Editor& e, const cJSON* state) {
    if (!state || !cJSON_IsObject(state)) throw CanonicalError("a state document is an object");

    std::map<std::string, const cJSON*> mantles;
    const cJSON* marr = get(state, "mantles");
    if (marr && !cJSON_IsArray(marr)) throw CanonicalError("`mantles` is not an array");
    for (const cJSON* m = marr ? marr->child : nullptr; m; m = m->next) {
        const cJSON* name = get(m, "name");
        if (!name || !cJSON_IsString(name) || !name->valuestring || !name->valuestring[0])
            throw CanonicalError("a mantle has no name");
        if (!mantles.emplace(name->valuestring, m).second)
            throw CanonicalError(std::string("duplicate mantle name ") + name->valuestring);
    }
    for (const auto& kv : mantles) observe_mantle(e, {"mantles", kv.first}, kv.second);

    const cJSON* existing = e.node({"mantles"});
    std::vector<std::string> gone;
    for (const cJSON* m = existing ? existing->child : nullptr; m; m = m->next)
        if (m->string && !mantles.count(m->string) && e.live({"mantles", m->string}))
            gone.push_back(m->string);
    for (const auto& name : gone) e.remove({"mantles", name}, false);

    std::map<std::string, std::string> glyphs;
    const cJSON* gobj = get(state, "glyphs");
    if (gobj && !cJSON_IsObject(gobj) && !cJSON_IsNull(gobj))
        throw CanonicalError("`glyphs` is present but is not an object");
    for (const cJSON* g = cJSON_IsObject(gobj) ? gobj->child : nullptr; g; g = g->next) {
        if (!g->string || !g->string[0]) throw CanonicalError("a glyph declaration has no name");
        glyphs[g->string] = enc::canon_glyph_descriptor(g);  // `source` excluded, SPEC §4.5
    }
    for (const auto& kv : glyphs) {
        Path base{"glyphs", kv.first};
        e.make_present(base, "");
        e.sync_register(join_path(base, {"fields", "descriptor"}), "descriptor", &kv.second);
    }
    const cJSON* gexisting = e.node({"glyphs"});
    std::vector<std::string> ggone;
    for (const cJSON* g = gexisting ? gexisting->child : nullptr; g; g = g->next)
        if (g->string && !glyphs.count(g->string) && e.live({"glyphs", g->string}))
            ggone.push_back(g->string);
    for (const auto& name : ggone) e.remove({"glyphs", name}, false);

    return e.changes();
}

Path locate(const Conflict& c) {
    if (!c.glyph.empty()) return {"glyphs", c.glyph};
    if (!c.rune.empty()) return {"mantles", c.mantle, "runes", c.rune};
    return {"mantles", c.mantle};
}

}  // namespace

/* ── identity ──────────────────────────────────────────────────────────────── */

bool Replica::create(const std::string& id, Replica& out, std::string* why) {
    if (!valid_id(id, why)) return false;
    Replica r;
    r.id_ = id;
    r.issued_ = 0;
    r.doc_.root = empty_root();
    out = std::move(r);
    return true;
}

bool Replica::fork(const std::string& new_id, Replica& out, std::string* why) const {
    if (!valid_id(new_id, why)) return false;
    if (new_id == id_) {
        if (why) *why = "a fork needs a NEW id; reusing this one is the failure it prevents";
        return false;
    }
    Replica r;
    r.id_ = new_id;
    r.issued_ = 0;
    r.doc_.root = cJSON_Duplicate(doc_.root, 1);
    r.policy_ = policy_;
    out = std::move(r);
    return true;
}

/* ── local changes ─────────────────────────────────────────────────────────── */

Replica::Observed Replica::observe(const cJSON* state) {
    Observed out;
    cJSON* work = cJSON_Duplicate(doc_.root, 1);
    cJSON* delta = cJSON_CreateObject();
    cJSON_AddNumberToObject(delta, "palabra", 1);
    Editor e(work, delta, id_, issued_, policy_);
    try {
        out.changes = observe_into(e, state);
    } catch (const CanonicalError& ex) {
        /* Atomic: the document is untouched. The counter is NOT rolled back — the
         * tags minted before the failure never left this call, so reusing them
         * would be safe today, but a counter that only moves forward is a property
         * that stays true without anyone re-checking why. */
        cJSON_Delete(work);
        cJSON_Delete(delta);
        out.error = ex.what();
        return out;
    }
    cJSON_Delete(doc_.root);
    doc_.root = work;
    out.delta.root = delta;
    out.ok = true;
    return out;
}

/* ── remote changes ────────────────────────────────────────────────────────── */

MergeResult Replica::merge(const cJSON* remote, std::string* why) {
    if (!validate(remote, why)) return MergeResult::invalid;

    /* Two ways a tag minted under this id can betray a reused identity:
     *
     *   UNKNOWN — the tag is not in this replica's document at all. This replica
     *   was restored from an older copy, or crashed after sending and before saving.
     *
     *   REUSED — the tag IS here, but names something else: a different set, or a
     *   different value. Two devices share this id and each minted "<id>_N" for
     *   its own change. Checking only for unknown tags misses this completely,
     *   because every tag the copy minted is a string the original also minted —
     *   the first version of this check did miss it, and a test caught it.
     *
     * The same tag naming the same add in the same set is not a collision: it is
     * the same change, arriving again. */
    std::set<std::string> theirs;
    collect_all_tags(remote, theirs);
    std::map<std::string, std::string> their_adds;
    std::set<std::string> mine;
    std::map<std::string, std::string> my_adds;
    bool gathered = false;
    std::uint64_t n = 0;
    for (const auto& tag : theirs) {
        if (!own_counter(tag, id_, n)) continue;
        if (!gathered) {
            collect_all_tags(doc_.root, mine);
            collect_adds(doc_.root, "", my_adds);
            collect_adds(remote, "", their_adds);
            gathered = true;
        }
        std::string problem;
        if (!mine.count(tag)) {
            problem = "was minted under this replica's id and this replica has no record of it "
                      "(restored from an older copy, or crashed after sending)";
        } else {
            auto theirs_add = their_adds.find(tag);
            auto mine_add = my_adds.find(tag);
            if (theirs_add != their_adds.end() &&
                (mine_add == my_adds.end() || mine_add->second != theirs_add->second))
                problem = "names something different here and there — another device is "
                          "minting under this replica's id";
        }
        if (!problem.empty()) {
            if (why) *why = "tag " + tag + " " + problem + ". Fork to a new id.";
            return MergeResult::identity_collision;
        }
    }

    cJSON* joined = crdt::join_node(doc_.root, remote);
    cJSON_Delete(doc_.root);
    doc_.root = joined;
    return MergeResult::ok;
}

MergeResult Replica::merge(const Doc& remote, std::string* why) {
    return merge(remote.root, why);
}

/* ── reading ───────────────────────────────────────────────────────────────── */

Doc Replica::flatten() const { return voidpalabra::flatten(doc_, policy_); }

std::vector<Conflict> Replica::conflicts() const {
    return voidpalabra::conflicts(doc_, policy_);
}

std::vector<Anomaly> Replica::anomalies() const {
    return voidpalabra::anomalies(doc_, policy_);
}

bool Replica::resolve(const Conflict& conflict, std::size_t side, Doc* delta_out) {
    if (side >= conflict.sides.size()) return false;
    const std::string wanted = to_hex(conflict.hash());
    bool current = false;
    for (const Conflict& c : conflicts())
        if (to_hex(c.hash()) == wanted) { current = true; break; }
    if (!current) return false;

    cJSON* work = cJSON_Duplicate(doc_.root, 1);
    cJSON* delta = cJSON_CreateObject();
    cJSON_AddNumberToObject(delta, "palabra", 1);
    Editor e(work, delta, id_, issued_, policy_);
    Path base = locate(conflict);

    if (conflict.kind == ConflictKind::values) {
        Path p = join_path(base, {"fields", conflict.field});
        OrSet before = e.load(p), after = before;
        write(after, e.mint(), conflict.sides[side]);
        e.store(p, before, after);
    } else {
        cJSON* choice = decode(conflict.sides[side]);
        std::string what = choice && cJSON_IsString(choice) ? choice->valuestring : "";
        cJSON_Delete(choice);
        if (what == "kept") {
            e.make_present(base, conflict.glyph.empty() ? "1" : "");
        } else if (what == "deleted") {
            /* Delete again, and this time the edit is SEEN, so the conflict closes. */
            e.remove(base, true);
        } else {
            cJSON_Delete(work);
            cJSON_Delete(delta);
            return false;
        }
    }

    cJSON_Delete(doc_.root);
    doc_.root = work;
    if (delta_out) {
        Doc d;
        d.root = delta;
        *delta_out = std::move(d);
    } else {
        cJSON_Delete(delta);
    }
    return true;
}

/* ── persistence ───────────────────────────────────────────────────────────── */

std::string Replica::to_bytes() const {
    cJSON* o = cJSON_CreateObject();
    cJSON_AddNumberToObject(o, "palabra_replica", 1);
    cJSON_AddStringToObject(o, "id", id_.c_str());
    cJSON_AddNumberToObject(o, "issued", static_cast<double>(issued_));
    cJSON_AddItemToObject(o, "doc", cJSON_Duplicate(doc_.root, 1));
    char* text = cJSON_PrintUnformatted(o);
    std::string out = text ? text : "";
    if (text) cJSON_free(text);
    cJSON_Delete(o);
    return out;
}

bool Replica::from_bytes(const std::string& bytes, Replica& out, std::string* why) {
    auto fail = [&](const std::string& w) {
        if (why) *why = w;
        return false;
    };
    cJSON* o = cJSON_ParseWithLength(bytes.data(), bytes.size());
    if (!o) return fail("not JSON");

    bool ok = false;
    do {
        const cJSON* v = get(o, "palabra_replica");
        if (!v || !cJSON_IsNumber(v) || v->valuedouble != 1.0) { fail("not a replica, or a newer shape"); break; }
        const cJSON* id = get(o, "id");
        if (!id || !cJSON_IsString(id) || !valid_id(id->valuestring, why)) break;
        const cJSON* issued = get(o, "issued");
        if (!issued || !cJSON_IsNumber(issued) || issued->valuedouble < 0 ||
            issued->valuedouble > kMaxCounter ||
            issued->valuedouble != static_cast<double>(static_cast<std::uint64_t>(issued->valuedouble))) {
            fail("the counter is not a whole number in range");
            break;
        }
        const cJSON* doc = get(o, "doc");
        if (!validate(doc, why)) break;

        /* The document and the counter must agree. A document holding a tag this
         * replica minted beyond its own counter means the two were saved separately
         * and one of them is stale — and continuing would mint that tag again. */
        std::set<std::string> tags;
        collect_all_tags(doc, tags);
        std::uint64_t counter = static_cast<std::uint64_t>(issued->valuedouble);
        std::uint64_t n = 0;
        bool behind = false;
        for (const auto& t : tags)
            if (own_counter(t, id->valuestring, n) && n > counter) { behind = true; break; }
        if (behind) { fail("the counter is behind the document; fork to a new id"); break; }

        Replica r;
        r.id_ = id->valuestring;
        r.issued_ = counter;
        r.doc_.root = cJSON_Duplicate(doc, 1);
        out = std::move(r);
        ok = true;
    } while (false);

    cJSON_Delete(o);
    return ok;
}

}  // namespace voidpalabra
