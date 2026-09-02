#include "internal.hpp"

#include "voidpalabra/canonical.hpp"
#include "cJSON.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <set>

namespace voidpalabra {

using namespace crdt;

/* sequence.cpp — ordered content: Fugue.
 *
 * See include/voidpalabra/join.hpp for the argument. Storage:
 *
 *   <field> = { "seq": { "nodes": <OrSet>, "dead": <OrSet> } }
 *
 * A node's OrSet tag IS its element id, so inserting is one add and the id needs
 * no separate home. A node's value is the encoding of [parent, side, payload].
 */

namespace {

constexpr int kSideLeft = 0;
constexpr int kSideRight = 1;

struct SeqNode {
    std::string id;
    std::string parent;   // "" == the virtual root
    int side = kSideRight;
    std::string payload;  // canonical bytes of the element's value
};

std::string encode_node(const std::string& parent, int side, const std::string& payload) {
    /* [parent, side, payload-as-hex]. Hex because the payload is canonical bytes,
     * which are not valid UTF-8 and so cannot ride inside a JSON string. */
    cJSON* a = cJSON_CreateArray();
    cJSON_AddItemToArray(a, cJSON_CreateString(parent.c_str()));
    cJSON_AddItemToArray(a, cJSON_CreateNumber(side));
    cJSON_AddItemToArray(a, cJSON_CreateString(hexify(payload).c_str()));
    std::string out = encode(a);
    cJSON_Delete(a);
    return out;
}

bool decode_node(const std::string& id, const std::string& bytes, SeqNode& out) {
    cJSON* a = decode(bytes);
    if (!a || !cJSON_IsArray(a) || cJSON_GetArraySize(a) != 3) {
        if (a) cJSON_Delete(a);
        return false;
    }
    cJSON* p = cJSON_GetArrayItem(a, 0);
    cJSON* s = cJSON_GetArrayItem(a, 1);
    cJSON* v = cJSON_GetArrayItem(a, 2);
    out.id = id;
    out.parent = (p && p->valuestring) ? p->valuestring : "";
    out.side = (s && cJSON_IsNumber(s)) ? static_cast<int>(s->valuedouble) : kSideRight;
    out.payload = (v && v->valuestring) ? unhexify(v->valuestring) : std::string();
    cJSON_Delete(a);
    return true;
}

cJSON* seq_holder(const cJSON* root, const std::string& mantle,
                  const std::string& rune_id, const std::string& field) {
    cJSON* ms = cJSON_GetObjectItemCaseSensitive(const_cast<cJSON*>(root), "mantles");
    cJSON* m = cJSON_GetObjectItemCaseSensitive(ms, mantle.c_str());
    if (!m) return nullptr;
    cJSON* runes = cJSON_GetObjectItemCaseSensitive(m, "runes");
    cJSON* r = cJSON_GetObjectItemCaseSensitive(runes, rune_id.c_str());
    if (!r) return nullptr;
    cJSON* fields = cJSON_GetObjectItemCaseSensitive(r, "fields");
    if (!fields) return nullptr;
    cJSON* f = cJSON_GetObjectItemCaseSensitive(fields, field.c_str());
    if (!f) return nullptr;
    return cJSON_GetObjectItemCaseSensitive(f, "seq");
}

/* The in-order traversal, tombstones included. This is the total order; the
 * visible list is this with the dead filtered out. */
std::vector<SeqNode> seq_order(const cJSON* seq) {
    std::vector<SeqNode> nodes;
    if (!seq) return nodes;
    OrSet ns = orset_from_json(
        cJSON_GetObjectItemCaseSensitive(const_cast<cJSON*>(seq), "nodes"));
    for (const auto& kv : ns.adds) {
        if (ns.removes.count(kv.first)) continue;
        SeqNode n;
        if (decode_node(kv.first, kv.second, n)) nodes.push_back(n);
    }

    /* children[parent][side] -> nodes, sorted ascending by id.
     *
     * Any deterministic sibling order converges; ascending by id is the choice.
     * What produces NON-INTERLEAVING is not this order but the chain shape a
     * typed run makes: siblings are whole subtrees, and a subtree is emitted
     * whole, so the traversal has no way to enter one run and leave it mid-way. */
    std::map<std::string, std::array<std::vector<const SeqNode*>, 2> > children;
    for (const SeqNode& n : nodes) {
        int side = (n.side == kSideLeft) ? 0 : 1;
        children[n.parent][side].push_back(&n);
    }
    for (auto& kv : children)
        for (auto& side : kv.second)
            std::sort(side.begin(), side.end(),
                      [](const SeqNode* a, const SeqNode* b) { return a->id < b->id; });

    /* An explicit stack rather than recursion: a long run is a long CHAIN, which
     * is exactly the shape that turns recursion into a stack overflow. */
    std::vector<SeqNode> out;
    struct Frame { const SeqNode* node; int stage; };
    std::vector<Frame> stack;
    auto push_side = [&](const std::string& parent, int side) {
        auto it = children.find(parent);
        if (it == children.end()) return;
        const std::vector<const SeqNode*>& v = it->second[side];
        for (std::size_t k = v.size(); k > 0; --k) stack.push_back({v[k - 1], 0});
    };

    push_side("", 1);   // the virtual root has only right children in practice
    push_side("", 0);   // ...but honour left children of root if any exist
    while (!stack.empty()) {
        Frame f = stack.back();
        stack.pop_back();
        if (f.stage == 0) {
            stack.push_back({f.node, 1});
            push_side(f.node->id, 0);   // left children come before the node
        } else if (f.stage == 1) {
            out.push_back(*f.node);
            stack.push_back({f.node, 2});
        } else {
            push_side(f.node->id, 1);   // right children come after it
        }
    }
    return out;
}

std::set<std::string> seq_dead(const cJSON* seq) {
    std::set<std::string> dead;
    if (!seq) return dead;
    OrSet d = orset_from_json(
        cJSON_GetObjectItemCaseSensitive(const_cast<cJSON*>(seq), "dead"));
    for (const auto& kv : d.adds)
        if (!d.removes.count(kv.first)) dead.insert(kv.first);
    return dead;
}

bool has_right_child(const std::vector<SeqNode>& all, const std::string& id) {
    for (const SeqNode& n : all)
        if (n.parent == id && n.side == kSideRight) return true;
    return false;
}

cJSON* make_empty_seq() {
    cJSON* seq = cJSON_CreateObject();
    cJSON_AddItemToObject(seq, "nodes", orset_to_json(OrSet()));
    cJSON_AddItemToObject(seq, "dead", orset_to_json(OrSet()));
    return seq;
}

}  // namespace

bool seq_is(const Doc& doc, const std::string& mantle, const std::string& rune_id,
            const std::string& field) {
    return seq_holder(doc.root, mantle, rune_id, field) != nullptr;
}

bool seq_init(Doc& doc, const std::string& mantle, const std::string& rune_id,
              const std::string& field, const cJSON* array, Mint& mint) {
    cJSON* ms = cJSON_GetObjectItemCaseSensitive(doc.root, "mantles");
    cJSON* m = cJSON_GetObjectItemCaseSensitive(ms, mantle.c_str());
    if (!m) return false;
    cJSON* runes = cJSON_GetObjectItemCaseSensitive(m, "runes");
    cJSON* r = cJSON_GetObjectItemCaseSensitive(runes, rune_id.c_str());
    if (!r) return false;
    cJSON* fields = cJSON_GetObjectItemCaseSensitive(r, "fields");
    if (!fields) return false;

    cJSON_DeleteItemFromObjectCaseSensitive(fields, field.c_str());
    cJSON* holder = cJSON_CreateObject();
    cJSON_AddItemToObject(holder, "seq", make_empty_seq());
    cJSON_AddItemToObject(fields, field.c_str(), holder);

    std::size_t i = 0;
    for (const cJSON* it = (array && cJSON_IsArray(array)) ? array->child : nullptr;
         it; it = it->next)
        seq_insert(doc, mantle, rune_id, field, i++, it, mint);
    return true;
}

bool seq_insert(Doc& doc, const std::string& mantle, const std::string& rune_id,
                const std::string& field, std::size_t index, const cJSON* value,
                Mint& mint) {
    cJSON* seq = seq_holder(doc.root, mantle, rune_id, field);
    if (!seq) return false;

    std::vector<SeqNode> all = seq_order(seq);
    std::set<std::string> dead = seq_dead(seq);

    /* Find the insertion point in the FULL order from a VISIBLE index. `left` is
     * the visible element the new one will follow; `right` is whatever comes next
     * in the full order, tombstone or not — attaching relative to a tombstone
     * still orders correctly, which is precisely why tombstones are kept. */
    const SeqNode* left = nullptr;
    std::size_t visible = 0;
    std::size_t left_pos = all.size();
    for (std::size_t i = 0; i < all.size(); ++i) {
        if (dead.count(all[i].id)) continue;
        if (visible == index) break;
        ++visible;
        left = &all[i];
        left_pos = i;
    }
    if (index > visible) return false;  // out of range

    const SeqNode* right = nullptr;
    std::size_t after = left ? left_pos + 1 : 0;
    if (after < all.size()) right = &all[after];

    std::string parent;
    int side;
    if (left && !has_right_child(all, left->id)) {
        parent = left->id;
        side = kSideRight;   // the common case: extending a run into a chain
    } else if (right) {
        parent = right->id;
        side = kSideLeft;    // squeeze in ahead of whatever follows
    } else if (left) {
        parent = left->id;
        side = kSideRight;
    } else {
        parent = "";         // the very first element
        side = kSideRight;
    }

    OrSet ns = orset_from_json(cJSON_GetObjectItemCaseSensitive(seq, "nodes"));
    ns.add(mint.next(), encode_node(parent, side, encode(value)));
    cJSON_DeleteItemFromObjectCaseSensitive(seq, "nodes");
    cJSON_AddItemToObject(seq, "nodes", orset_to_json(ns));
    return true;
}

bool seq_erase(Doc& doc, const std::string& mantle, const std::string& rune_id,
               const std::string& field, std::size_t index) {
    cJSON* seq = seq_holder(doc.root, mantle, rune_id, field);
    if (!seq) return false;
    std::vector<SeqNode> all = seq_order(seq);
    std::set<std::string> dead = seq_dead(seq);

    std::size_t visible = 0;
    for (const SeqNode& n : all) {
        if (dead.count(n.id)) continue;
        if (visible == index) {
            OrSet ds = orset_from_json(cJSON_GetObjectItemCaseSensitive(seq, "dead"));
            /* Tagged by the dead node's own id, so erasing twice is one fact and
             * two peers erasing the same element agree without coordinating. */
            ds.add(n.id, "1");
            cJSON_DeleteItemFromObjectCaseSensitive(seq, "dead");
            cJSON_AddItemToObject(seq, "dead", orset_to_json(ds));
            return true;
        }
        ++visible;
    }
    return false;
}

cJSON* seq_read(const Doc& doc, const std::string& mantle,
                const std::string& rune_id, const std::string& field) {
    const cJSON* seq = seq_holder(doc.root, mantle, rune_id, field);
    if (!seq) return nullptr;
    std::vector<SeqNode> all = seq_order(seq);
    std::set<std::string> dead = seq_dead(seq);
    cJSON* out = cJSON_CreateArray();
    for (const SeqNode& n : all) {
        if (dead.count(n.id)) continue;
        cJSON* v = decode(n.payload);
        cJSON_AddItemToArray(out, v ? v : cJSON_CreateNull());
    }
    return out;
}

}  // namespace voidpalabra
