/* anomaly.hpp — rules every device kept, that a merge broke anyway.
 *
 * ── The general statement ──
 *
 * A join preserves exactly one thing: the join. It is a union of independent
 * objects, so any rule that relates TWO objects — a name must be unique among its
 * siblings, a link must point at something, a rune's type must be declared — can
 * hold on every device and fail on the merge of them. No CRDT can prevent that for
 * a rule spanning objects, because each device's change was valid against the state
 * it could see.
 *
 * So the rule is not enforced; it is CHECKED after the merge, and a violation is
 * reported the way a conflict is: as a value with a content address, identical on
 * every peer that holds the same document, so two members looking at one problem
 * see one problem with one name.
 *
 * ── Why these kinds and not more ──
 *
 * Each kind is a rule Void Core's SPEC states (Core owns the rule; Palabra does not
 * restate it differently), and each is one that Core's own `validate` cannot tell
 * apart from an ordinary mistake — because only the merge knows two devices were
 * involved. That second condition is what keeps this file from becoming a copy of
 * `validate`:
 *
 *   duplicate_name  two live runes in one mantle share a name (Core SPEC §2: a name
 *                   is unique within its mantle). Each device minted one; neither
 *                   minted a duplicate. Every command and every edge that addresses
 *                   the name is ambiguous from this moment.
 *
 *   link_broken     a live edge names a rune that no live rune holds, AND the merge
 *                   explains why: a rune with that name was REMOVED here, or a live
 *                   rune was RENAMED away from it. Core allows a dangling link on
 *                   purpose (a link to something not written yet), so a dangling
 *                   link alone is not reported — only one a concurrent change broke.
 *
 *   type_removed    a live rune's glyph names a declaration this document holds as
 *                   REMOVED. Core refuses `glyph undeclare` while runes carry the
 *                   glyph, but that guard is per device: one member undeclares while
 *                   another creates a rune of it.
 *
 * Nothing here resolves an anomaly. The fix is always an ordinary edit — rename one
 * rune, redraw or delete the link, redeclare or retype — and the anomaly disappears
 * when the state no longer breaks the rule. In particular Palabra NEVER renames:
 * renaming changes what every command and every edge naming that rune means, and
 * which of two members' `note-1` keeps the name is a human decision.
 */
#pragma once

#include <string>
#include <vector>

#include "voidpalabra/canonical.hpp"
#include "voidpalabra/crdt/document.hpp"
#include "voidpalabra/crdt/policy.hpp"

struct cJSON;

namespace voidpalabra {

enum class AnomalyKind {
    duplicate_name,
    link_broken,
    type_removed,
};

struct Anomaly {
    AnomalyKind kind = AnomalyKind::duplicate_name;
    std::string mantle;              // the mantle the rule is about
    std::string subject;             // the name, the link endpoint, or the glyph
    std::string cause;               // link_broken: "removed" | "renamed"; else empty
    std::vector<std::string> runes;  // spirit.ids involved, sorted

    /* Named like a conflict: two peers that see the same problem compute the same
     * hash, so it can be stored, synced, dismissed and referred to. */
    Digest hash() const;
};

/* Every anomaly in a document, in a deterministic order. Names are read the way
 * `flatten` shows them, under `policy`. Things that are not present are not
 * checked — a removed rune has no name to collide with. */
std::vector<Anomaly> anomalies(const Doc& doc, const JoinPolicy& policy = {});

/* {kind, mantle, subject, cause?, runes}. Caller owns the tree. */
cJSON* anomaly_to_json(const Anomaly& a);

}  // namespace voidpalabra
