/* links.cpp — see include/voidpalabra/links.hpp for what and why.
 *
 * Three passes over one resolved link list: union-find for the equivalence, a count
 * per slot for capacity, strongly connected components for cycles. Every output is
 * sorted before it leaves, so the result is a function of the document and not of
 * the order a map or a JSON array happened to iterate in.
 */
#include "voidpalabra/links.hpp"

#include "voidpalabra/canonical.hpp"
#include "internal.hpp"

#include "cJSON.h"

#include <algorithm>
#include <functional>
#include <map>
#include <set>
#include <tuple>

namespace voidpalabra {

using namespace enc;

namespace {

/* name -> spirit.ids holding it, per mantle. */
using Names = std::map<std::string, std::map<std::string, std::vector<std::string>>>;

Names index_names(const cJSON* state) {
    Names out;
    const cJSON* ms = get(state, "mantles");
    for (const cJSON* m = ms && cJSON_IsArray(ms) ? ms->child : nullptr; m; m = m->next) {
        std::string mname = str_or(get(m, "name"), "");
        auto& names = out[mname];
        const cJSON* rs = get(m, "runes");
        for (const cJSON* r = rs && cJSON_IsArray(rs) ? rs->child : nullptr; r; r = r->next) {
            const cJSON* spirit = get(r, "spirit");
            std::string id = str_or(get(spirit, "id"), "");
            if (id.empty()) continue;
            names[str_or(get(spirit, "name"), "")].push_back(id);
        }
    }
    return out;
}

/* A name resolves when exactly one rune in the named mantle holds it. */
bool resolve_end(const cJSON* e, const std::string& own, const Names& names, RuneRef& out) {
    std::string mantle, rune;
    if (e && cJSON_IsString(e) && e->valuestring) {
        mantle = own;
        rune = e->valuestring;
    } else if (e && cJSON_IsObject(e)) {
        const cJSON* rm = get(e, "rune");
        if (!rm || !cJSON_IsString(rm) || !rm->valuestring) return false;
        mantle = str_or(get(e, "mantle"), own.c_str());
        rune = rm->valuestring;
    } else {
        return false;
    }
    auto m = names.find(mantle);
    if (m == names.end()) return false;
    auto n = m->second.find(rune);
    if (n == m->second.end() || n->second.size() != 1) return false;
    out.mantle = mantle;
    out.id = n->second.front();
    return true;
}

bool parse_ports(const std::string& relation, std::string& i, std::string& j) {
    std::size_t colon = relation.find(':');
    if (colon == std::string::npos || colon == 0 || colon + 1 >= relation.size()) return false;
    i = relation.substr(0, colon);
    j = relation.substr(colon + 1);
    auto digits = [](const std::string& s) {
        return std::all_of(s.begin(), s.end(), [](char c) { return c >= '0' && c <= '9'; });
    };
    return digits(i) && digits(j);
}

bool any_matches(const std::vector<std::string>& patterns, const std::string& relation) {
    for (const auto& p : patterns)
        if (label_matches(p, relation)) return true;
    return false;
}

struct UnionFind {
    std::map<RuneRef, RuneRef> parent;
    RuneRef find(const RuneRef& x) {
        auto it = parent.find(x);
        if (it == parent.end()) return x;
        RuneRef root = find(it->second);
        it->second = root;
        return root;
    }
    void unite(const RuneRef& a, const RuneRef& b) {
        RuneRef ra = find(a), rb = find(b);
        if (ra == rb) return;
        /* The least member becomes the root, so the representative is the class
         * minimum however the links were visited. */
        if (rb < ra) std::swap(ra, rb);
        parent[rb] = ra;
    }
};

std::vector<std::string> sorted_unique(std::vector<std::string> v) {
    std::sort(v.begin(), v.end());
    v.erase(std::unique(v.begin(), v.end()), v.end());
    return v;
}

const char* kind_name(ViolationKind k) {
    return k == ViolationKind::cycle ? "cycle" : "over_capacity";
}

}  // namespace

bool label_matches(const std::string& pattern, const std::string& relation) {
    if (!pattern.empty() && pattern.back() == '*')
        return relation.compare(0, pattern.size() - 1, pattern, 0, pattern.size() - 1) == 0;
    return pattern == relation;
}

std::vector<Link> resolved_links(const cJSON* state) {
    std::vector<Link> out;
    Names names = index_names(state);
    const cJSON* ms = get(state, "mantles");
    for (const cJSON* m = ms && cJSON_IsArray(ms) ? ms->child : nullptr; m; m = m->next) {
        std::string mname = str_or(get(m, "name"), "");
        const cJSON* es = get(get(m, "layout"), "edges");
        for (const cJSON* e = es && cJSON_IsArray(es) ? es->child : nullptr; e; e = e->next) {
            Link l;
            l.mantle = mname;
            if (!resolve_end(get(e, "from"), mname, names, l.from)) continue;
            if (!resolve_end(get(e, "to"), mname, names, l.to)) continue;
            l.relation = str_or(get(e, "relation"), "");
            l.bytes = encode(e);
            out.push_back(std::move(l));
        }
    }
    std::sort(out.begin(), out.end(), [](const Link& a, const Link& b) {
        return std::tie(a.mantle, a.bytes) < std::tie(b.mantle, b.bytes);
    });
    /* The same link twice in one mantle is one link: an OR-set of edges cannot hold
     * a duplicate, and a hand-written document that does must not count it twice. */
    out.erase(std::unique(out.begin(), out.end(), [](const Link& a, const Link& b) {
                  return a.mantle == b.mantle && a.bytes == b.bytes;
              }), out.end());
    return out;
}

RuneRef Quotient::representative(const RuneRef& r) const {
    auto it = rep.find(r);
    return it == rep.end() ? r : it->second;
}

namespace {

Quotient quotient_of(const std::vector<Link>& links, const LinkRules& rules) {
    UnionFind uf;
    std::set<RuneRef> touched;
    for (const Link& l : links) {
        if (!any_matches(rules.equivalence, l.relation)) continue;
        uf.unite(l.from, l.to);
        touched.insert(l.from);
        touched.insert(l.to);
    }
    Quotient q;
    std::map<RuneRef, std::vector<RuneRef>> by_root;
    for (const RuneRef& r : touched) by_root[uf.find(r)].push_back(r);
    for (auto& [root, members] : by_root) {
        if (members.size() < 2) continue;
        for (const RuneRef& r : members) q.rep[r] = root;
        q.classes.push_back(members);  // already sorted: `touched` is ordered
    }
    return q;
}

}  // namespace

Quotient quotient(const cJSON* state, const LinkRules& rules) {
    return quotient_of(resolved_links(state), rules);
}

std::vector<Violation> check_links(const cJSON* state, const LinkRules& rules) {
    std::vector<Violation> out;
    std::vector<Link> links = resolved_links(state);
    Quotient q = quotient_of(links, rules);

    /* ── capacity ── */
    for (const Capacity& cap : rules.capacity) {
        struct Held { std::vector<std::string> links, runes; };
        std::map<std::tuple<std::string, std::string, std::string>, Held> slots;
        for (const Link& l : links) {
            if (!label_matches(cap.relation, l.relation)) continue;
            if (any_matches(rules.equivalence, l.relation)) continue;  // fuses, never occupies
            auto occupy = [&](RuneRef r, const std::string& port) {
                if (cap.through_equivalence) r = q.representative(r);
                Held& h = slots[{r.mantle, r.id, port}];
                h.links.push_back(l.bytes);
                h.runes.push_back(l.from.id);
                h.runes.push_back(l.to.id);
            };
            switch (cap.slot) {
                case Slot::to:   occupy(l.to, ""); break;
                case Slot::from: occupy(l.from, ""); break;
                case Slot::ends: occupy(l.from, ""); occupy(l.to, ""); break;
                case Slot::ports:
                case Slot::from_port:
                case Slot::to_port: {
                    std::string i, j;
                    if (!parse_ports(l.relation, i, j)) break;
                    if (cap.slot != Slot::to_port) occupy(l.from, i);
                    if (cap.slot != Slot::from_port) occupy(l.to, j);
                    break;
                }
            }
        }
        for (auto& [key, held] : slots) {
            /* Counted per END, so a self-link on `ends` or `ports` occupies its slot
             * twice when both ends land on it — a wire from a port to itself is a
             * port holding two wire ends. The list of links is reported distinct. */
            if (held.links.size() <= cap.max) continue;
            Violation v;
            v.kind = ViolationKind::over_capacity;
            v.rule = cap.name;
            v.mantle = std::get<0>(key);
            v.slot = std::get<1>(key);
            if (!std::get<2>(key).empty()) v.slot += ":" + std::get<2>(key);
            v.runes = sorted_unique(held.runes);
            v.links = sorted_unique(held.links);
            out.push_back(std::move(v));
        }
    }

    /* ── acyclic ── Tarjan's strongly connected components, iterative so a long
     * chain cannot exhaust the stack. A component of two or more, or a rune linked
     * to itself, is a cycle. */
    for (const std::string& pattern : rules.acyclic) {
        std::map<RuneRef, std::vector<std::pair<RuneRef, const Link*>>> adj;
        std::set<RuneRef> nodes;
        for (const Link& l : links) {
            if (!label_matches(pattern, l.relation)) continue;
            adj[l.from].push_back({l.to, &l});
            nodes.insert(l.from);
            nodes.insert(l.to);
        }
        std::map<RuneRef, int> index, low;
        std::map<RuneRef, bool> on_stack;
        std::vector<RuneRef> stack;
        int counter = 0;
        std::vector<std::vector<RuneRef>> components;
        for (const RuneRef& start : nodes) {
            if (index.count(start)) continue;
            std::vector<std::pair<RuneRef, std::size_t>> work{{start, 0}};
            index[start] = low[start] = counter++;
            stack.push_back(start);
            on_stack[start] = true;
            while (!work.empty()) {
                RuneRef v = work.back().first;
                std::size_t& next = work.back().second;
                const auto& out_edges = adj[v];
                if (next < out_edges.size()) {
                    RuneRef w = out_edges[next++].first;
                    if (!index.count(w)) {
                        index[w] = low[w] = counter++;
                        stack.push_back(w);
                        on_stack[w] = true;
                        work.push_back({w, 0});
                    } else if (on_stack[w]) {
                        low[v] = std::min(low[v], index[w]);
                    }
                    continue;
                }
                if (low[v] == index[v]) {
                    std::vector<RuneRef> comp;
                    RuneRef w;
                    do {
                        w = stack.back();
                        stack.pop_back();
                        on_stack[w] = false;
                        comp.push_back(w);
                    } while (!(w == v));
                    components.push_back(std::move(comp));
                }
                work.pop_back();
                if (!work.empty()) {
                    RuneRef parent = work.back().first;
                    low[parent] = std::min(low[parent], low[v]);
                }
            }
        }
        for (auto& comp : components) {
            std::set<RuneRef> members(comp.begin(), comp.end());
            std::vector<std::string> inside;
            bool self_loop = false;
            for (const RuneRef& v : members)
                for (const auto& [w, l] : adj[v])
                    if (members.count(w)) {
                        inside.push_back(l->bytes);
                        if (w == v) self_loop = true;
                    }
            if (members.size() < 2 && !self_loop) continue;
            Violation viol;
            viol.kind = ViolationKind::cycle;
            viol.rule = pattern;
            viol.mantle = members.begin()->mantle;
            for (const RuneRef& r : members) viol.runes.push_back(r.id);
            viol.runes = sorted_unique(viol.runes);
            viol.links = sorted_unique(inside);
            out.push_back(std::move(viol));
        }
    }

    std::sort(out.begin(), out.end(), [](const Violation& a, const Violation& b) {
        return std::tie(a.kind, a.rule, a.mantle, a.slot, a.runes) <
               std::tie(b.kind, b.rule, b.mantle, b.slot, b.runes);
    });
    return out;
}

cJSON* violation_to_json(const Violation& v) {
    cJSON* o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "kind", kind_name(v.kind));
    cJSON_AddStringToObject(o, "rule", v.rule.c_str());
    cJSON_AddStringToObject(o, "mantle", v.mantle.c_str());
    if (!v.slot.empty()) cJSON_AddStringToObject(o, "slot", v.slot.c_str());
    cJSON* runes = cJSON_AddArrayToObject(o, "runes");
    for (const auto& r : v.runes) cJSON_AddItemToArray(runes, cJSON_CreateString(r.c_str()));
    cJSON* links = cJSON_AddArrayToObject(o, "links");
    for (const auto& b : v.links) {
        cJSON* e = decode(b);
        cJSON_AddItemToArray(links, e ? e : cJSON_CreateNull());
    }
    return o;
}

Digest Violation::hash() const {
    cJSON* o = violation_to_json(*this);
    std::string bytes = encode(o);
    cJSON_Delete(o);
    return domain_digest("violation", bytes, "");
}

}  // namespace voidpalabra
