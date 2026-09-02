/* sequence.hpp — ordered content: Fugue.
 */
#pragma once

#include <map>
#include <set>
#include <string>
#include <vector>

#include "voidpalabra/canonical.hpp"

#include "voidpalabra/crdt/document.hpp"

struct cJSON;


namespace voidpalabra {

/* --- ordered content: the sequence CRDT ---------------------------------- */
/*
 * Void Core ruled rune order non-semantic (SPEC §4), so an application that needs
 * an ordering puts it in a CONTENT FIELD. This is the machinery for such a field,
 * and okf/concepts/join.md names the forcing case: Void Hormiga's newsletter block
 * order, with two editors inserting concurrently.
 *
 * -- The property that matters, and it is not convergence --
 *
 * Convergence is easy; a list CRDT that merges deterministically is not hard to
 * write. The hard part is INTERLEAVING. If one editor types "abc" at a spot while
 * another types "xyz" at the same spot, a naive list CRDT (RGA, Logoot) can
 * converge on "axbycz" — every peer agrees, and the result is nonsense nobody
 * wrote.
 *
 * FUGUE (Weidner & Kleppmann, 2023) exists for exactly this, and the mechanism is
 * a tree rather than a total order over identifiers:
 *
 *   - Each element is a node with a parent and a side (left or right).
 *   - Inserting immediately after `left` makes the new node `left`'s RIGHT child,
 *     when it has none yet; otherwise it becomes the LEFT child of the node that
 *     currently follows.
 *   - The list is the in-order traversal.
 *
 * A run typed by one person therefore forms a CHAIN — each character the right
 * child of the previous — so it is one contiguous subtree. Two concurrent runs are
 * two sibling subtrees, and subtrees are emitted whole. The runs cannot interleave
 * because the traversal has no way to enter one and leave it mid-way.
 *
 * -- Why this needs no new merge code --
 *
 * A sequence field is stored as two OrSets: `nodes` (tag = the element's own id)
 * and `dead` (tag = the id of a removed element). Both merge by union, so the
 * three laws are inherited from the primitive and `join` did not have to learn
 * anything. Deletion is an ADD to `dead` rather than a removal from `nodes`,
 * because a removed element may still be some other element's parent — a
 * tombstone is load-bearing structure here, not litter.
 *
 * -- Sequences are a representation, not a FieldJoin --
 *
 * `FieldJoin` decides how to RESOLVE two concurrent writes to one scalar. A
 * sequence never has that problem: concurrent edits merge structurally rather
 * than competing, so there is nothing to resolve. The two are orthogonal axes and
 * a sequence is deliberately not a `FieldJoin` value. The storage is
 * self-describing, so no declaration is needed at read time either.
 */

/* Turn a field into a sequence, seeded from a JSON array (or empty). Existing
 * scalar content at that field is replaced. */
bool seq_init(Doc& doc, const std::string& mantle, const std::string& rune_id,
              const std::string& field, const cJSON* array, Mint& mint);

/* Is this field stored as a sequence? */
bool seq_is(const Doc& doc, const std::string& mantle, const std::string& rune_id,
            const std::string& field);

/* Insert `value` so it becomes the element at `index` in the visible order.
 * `index` may equal the current length, meaning append. */
bool seq_insert(Doc& doc, const std::string& mantle, const std::string& rune_id,
                const std::string& field, std::size_t index, const cJSON* value,
                Mint& mint);

/* Remove the element at `index`. The node survives as a tombstone. */
bool seq_erase(Doc& doc, const std::string& mantle, const std::string& rune_id,
               const std::string& field, std::size_t index);

/* The visible elements, in order, as a JSON array. Caller owns it; nullptr if the
 * field is absent or is not a sequence. */
cJSON* seq_read(const Doc& doc, const std::string& mantle,
                const std::string& rune_id, const std::string& field);


}  // namespace voidpalabra
