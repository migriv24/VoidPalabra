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
struct Conflict {
    std::string mantle;              // mantle name
    std::string rune;                // spirit.id; empty for a mantle-level field
    std::string field;               // e.g. "content.body", "domain"
    std::vector<std::string> sides;  // canonical value bytes, canonically ordered

    /* The conflict's own content address. Two peers who see the same divergence
     * compute the same hash — which is what "peers converge ON THE CONFLICT
     * ITSELF" means and what makes it storable and syncable. */
    Digest hash() const;
};

/* Every conflict in a document, ordered deterministically by (mantle, rune,
 * field) so two peers enumerate them identically. Covers mantle-level registers
 * as well as rune fields. Empty when there are none. */
std::vector<Conflict> conflicts(const Doc& doc, const JoinPolicy& policy = {});

/* Render one conflict in the conflict.md shape:
 *   {kind, at:{mantle,rune,field}, sides:[{value}], hash}
 * Built through cJSON so names containing quotes or backslashes are escaped
 * properly — `spirit.name` and mantle names are user-editable text. Caller owns
 * the returned tree. */
cJSON* conflict_to_json(const Conflict& c);


}  // namespace voidpalabra
