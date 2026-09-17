/* conflict.hpp — a conflict as a VALUE with a content address.
 */
#pragma once

#include <map>
#include <set>
#include <string>
#include <vector>

#include "voidpalabra/canonical.hpp"

#include "voidpalabra/crdt/policy.hpp"
#include "voidpalabra/crdt/document.hpp"

struct cJSON;


namespace voidpalabra {

/* A conflict: two concurrent writes to one register that both survived.
 *
 * okf/concepts/conflict.md requires this to be a VALUE, not an error — "it has a
 * hash. It can be stored, synced, queried, tagged, and rendered." So it is a
 * struct with an address, not a diagnostic string. */
enum class ConflictKind {
    /* Two concurrent writes to one register both survived. */
    values,

    /* Something was deleted on one device while it was edited on another.
     *
     * Without this, the merge is silent: the thing is not present, so `flatten`
     * does not show it, and the other device's edit simply vanishes from view on
     * both devices. In an automatic sync that is a colleague's work disappearing
     * with no message anywhere. Detectable because a removal records every live
     * tag it saw (SPEC §5.7), so an edit it did NOT see is still distinguishable
     * after the merge.
     *
     * `field` is "present" and `sides` are the two canonical strings "deleted" and
     * "kept", in canonical order. Located at the outermost thing that was deleted:
     * a rune edited inside a deleted mantle is reported at the mantle. */
    deleted_while_edited,
};

struct Conflict {
    ConflictKind kind = ConflictKind::values;
    std::string mantle;              // mantle name; empty for a glyph conflict
    std::string rune;                // spirit.id; empty for a mantle-level field
    /* Set only for a concurrent REDECLARATION — two peers gave one glyph name two
     * schemas (VoidCore:SPEC.md §2, 0.2.14). When this is non-empty, `mantle` and
     * `rune` are empty and `field` is "descriptor": a declaration is a property of
     * the document, not of any one mantle.
     *
     * A separate member rather than a sentinel in `mantle`, because "" as a
     * stand-in for "not in a mantle" is exactly how a renderer ends up printing a
     * conflict with a blank where a name should be. */
    std::string glyph;
    std::string field;               // e.g. "content.body", "domain", "descriptor"
    std::vector<std::string> sides;  // canonical value bytes, canonically ordered

    /* The conflict's own content address. Two peers who see the same divergence
     * compute the same hash — which is what "peers converge ON THE CONFLICT
     * ITSELF" means and what makes it storable and syncable. */
    Digest hash() const;
};

/* Every conflict in a document, ordered deterministically so two peers enumerate
 * them identically. Covers mantle-level registers, rune fields, glyph
 * declarations, and deletes that raced an edit. Value conflicts inside something
 * that has been deleted are NOT reported: nothing shows them, and the question a
 * user can actually answer is whether the deletion should stand. Empty when there
 * are none. */
std::vector<Conflict> conflicts(const Doc& doc, const JoinPolicy& policy = {});

/* Render one conflict in the conflict.md shape:
 *   {kind, at:{mantle,rune,field}, sides:[{value}], hash}
 * Built through cJSON so names containing quotes or backslashes are escaped
 * properly — `spirit.name` and mantle names are user-editable text. Caller owns
 * the returned tree. */
cJSON* conflict_to_json(const Conflict& c);


}  // namespace voidpalabra
