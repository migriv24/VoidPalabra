/* internal.hpp — shared inside the CRDT layer, and deliberately not public.
 *
 * The enriched document is stored as JSON so it can ride through the canonical
 * form and the container without a second object model. These helpers are the
 * translation between that JSON and the `OrSet` primitive.
 *
 * They are internal because hand-assembling CRDT metadata is how invariants get
 * broken quietly — that is not a hypothetical, it is what an early test fixture
 * did, and it produced denormalized documents that broke document-level
 * idempotence while the primitive itself was fine. The public surface is
 * `set_field` / `add_tag` / `remove_tag` / `seq_*`, and it should stay that way.
 */
#pragma once

#include <set>
#include <string>
#include <vector>

#include "voidpalabra/join.hpp"

struct cJSON;

namespace voidpalabra {
namespace crdt {

/* The enriched document shape, in one place:
 *
 *   { "palabra": 1,
 *     "mantles": { "<name>": {
 *         "present": <OrSet>,
 *         "fields":  { "id": <Reg>, "domain": <Reg> },
 *         "runes":   { "<spirit.id>": {
 *             "present": <OrSet>,
 *             "fields":  { "glyph": <Reg>, "facets.who": <Reg>, ...,
 *                          "content.<key>": <Reg>, "placement": <Reg> },
 *             "tags":    <OrSet> } },
 *         "edges":   <OrSet> } },
 *     "glyphs":  { "<glyph name>": {
 *         "present": <OrSet>,
 *         "fields":  { "descriptor": <Reg> } } } }
 *
 *   <OrSet> = { "a": { "<tag>": <hex value> }, "r": [ "<tag>", ... ] }
 *
 * Runes are keyed by `spirit.id` — immutable (VoidCore:SPEC.md §3.1) — so two
 * peers renaming one rune are editing a single object rather than creating two.
 * Mantles are keyed by name, which is what Core makes unique (§3.4); a mantle's
 * `id` rides as an ordinary field.
 *
 * A glyph declaration is ONE register holding the whole descriptor, which is the
 * opposite of how a rune's content is split. Per-key registers keep two peers
 * editing different fields of one rune from conflicting; per-key registers over a
 * SCHEMA would let a merge assemble a descriptor neither peer declared. The value
 * stored is the descriptor with `source` excluded (SPEC §4.5) — the same rule that
 * computes its name, shared rather than reimplemented.
 */

extern const char* const kFacets[6];

const cJSON* get(const cJSON* o, const char* k);

/* A field value is stored as its canonical encoding, so comparison and ordering
 * are the same operations the version name already uses. One encoder, one notion
 * of equality. */
std::string as_text(const cJSON* v);

std::string hexify(const std::string& raw);
std::string unhexify(const std::string& hex);

cJSON* orset_to_json(const OrSet& s);
OrSet orset_from_json(const cJSON* o);
bool is_orset(const cJSON* n);
/* Exactly {"a": {tag: lowercase-hex}, "r": [tag, ...]} and nothing else. */
bool orset_json_well_formed(const cJSON* o);

/* The recursive join over document trees. Returns a new tree the caller owns. */
cJSON* join_node(const cJSON* a, const cJSON* b);

/* Every LIVE tag (added and not retired) in every OrSet beneath `node`. With
 * `skip_own_present`, the node's own `present` is left out — which is what a
 * removal records as "seen", and what conflict detection compares against. */
void collect_live_tags(const cJSON* node, bool skip_own_present, std::set<std::string>& out);

/* Dead, and something beneath it holds a live tag the removal did not see. */
bool raced_by_an_edit(const cJSON* node);

/* One register's live value, or the marker that it is conflicted. A register is
 * conflicted exactly when two concurrent writes both survived. */
struct Live {
    bool conflicted = false;
    std::vector<std::string> values;  // canonical bytes, canonically ordered
};

Live live_of(const cJSON* reg);

/* Apply a field's declared join. READ-TIME ONLY: this never touches the stored
 * document, so two peers running different policies still hold identical bytes.
 * That property is asserted directly in tests/join_test.cpp. */
Live resolve(const cJSON* reg, FieldJoin how);

cJSON* first_or_null(const Live& l);

}  // namespace crdt
}  // namespace voidpalabra
