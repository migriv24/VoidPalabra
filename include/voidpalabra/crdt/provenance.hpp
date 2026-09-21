/* provenance.hpp — who wrote what the document shows.
 *
 * ── The general statement ──
 *
 * Every add in the document carries a tag `<replica id>_<n>` (SPEC §5.7), so the
 * question "who wrote this?" was answered at the moment of writing and has been
 * sitting in the document since. This file reads it; it records nothing new and
 * changes no format.
 *
 * One read answers several questions an application asks:
 *   - a canvas flashes a remotely changed node in the colour of whoever changed it;
 *   - a conflict row says WHOSE the two sides are, not just what they are
 *     (`Conflict` gives the place; `writers` at that place gives each side's
 *     writers, matched by value);
 *   - an activity feed or an audit view says who created, restored or last edited a
 *     thing, per field;
 *   - a moderator finds everything one device wrote, by filtering on its id;
 *   - `FieldJoin::Latest` shows its reasoning: the `stamp` is what it ranked by.
 *
 * ── What a writer is, and is not ──
 *
 * A writer is a REPLICA id — one device's copy, not a person. Mapping replicas to
 * people is the application's (a session already learns each peer's id). And it is
 * a CLAIM, not proof: until the trust model exists (okf/design/open-questions.md
 * §6.1), any peer can mint a tag under any id, and `identity_collision` catches
 * that only for this replica's own id. Show it; do not authorize with it.
 */
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "voidpalabra/crdt/document.hpp"

namespace voidpalabra {

/* Where to look. The same coordinates a Conflict carries:
 *   - a rune field:     mantle, rune (spirit.id), field ("content.body", "placement")
 *   - a mantle field:   mantle, field ("domain", "tags.<key>", "rules")
 *   - a declaration:    glyph, field "descriptor"
 * and three sets, by field name:
 *   - "present"  who created (or restored) the thing named by mantle/rune/glyph
 *   - "tags"     a rune's tags; one entry per tag
 *   - "edges"    a mantle's links (rune empty); one entry per link */
struct Place {
    std::string mantle;
    std::string rune;
    std::string glyph;
    std::string field;
};

/* One live value, and every write that currently holds it up. */
struct Written {
    std::string value;                 // canonical bytes, as in Conflict::sides
    std::vector<std::string> writers;  // replica ids, sorted, distinct
    /* The highest Lamport counter among those writes — what `FieldJoin::Latest`
     * ranks by. Comparable across writers; see policy.hpp for what it promises. */
    std::uint64_t stamp = 0;
};

/* Every LIVE value at `place`, in canonical value order, with its writers. Retired
 * values are not reported: this answers "who wrote what is here", not the history
 * (that is the utterance log's). Empty when the place does not exist or holds
 * nothing. A value written by two replicas independently lists both. */
std::vector<Written> writers(const Doc& doc, const Place& place);

}  // namespace voidpalabra
