/* references.hpp — what a document names but does not contain.
 *
 * ── The general statement ──
 *
 * The versioned slice holds runes; it does not hold the bytes runes point at. A
 * picture, a font, a sprite a glyph's `presentations` names, a model's weights, an
 * attachment — each is referred to BY ADDRESS and lives outside the document. Syncing
 * the document therefore syncs the reference and never the thing, and a device that
 * joins later holds a rune pointing at a file it has never seen.
 *
 * Three jobs follow, and they belong to three different owners — which is the whole
 * answer:
 *
 *   1. WHAT IS REFERRED TO — a fact about the shared document, identical on every
 *      peer. Palabra's: `references`.
 *   2. WHAT THIS DEVICE LACKS — a fact about one device, never merged, never a
 *      conflict, and never a reason to delete anything. Answered by asking the host
 *      what it holds: `missing`.
 *   3. FETCHING — I/O. Not Palabra's, by the sans-IO ruling
 *      (okf/design/transport-shape.md). Whoever owns the network — a host, or a
 *      networking module in a view library — asks peers for the missing addresses.
 *      And whoever RECEIVES bytes checks them before storing: `content_matches`. An
 *      address that is a hash means a peer cannot hand over the wrong file under the
 *      right name, whoever that peer is.
 *
 * What a rune's content MEANS is the application's (Core does not interpret
 * `content`, and neither does Palabra), so which fields hold addresses is DECLARED,
 * per field, in a ReferencePolicy — the same shape as a JoinPolicy, for the same
 * reason. Bytes MUST NOT ride inside the document to avoid this: that is the base64
 * bundle this library's archive exists to replace, and it makes every version carry
 * every file.
 */
#pragma once

#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

struct cJSON;

namespace voidpalabra {

class BlockStore;

/* Finds content addresses inside ONE field value. Adds to `out`; never removes. */
using AddressFinder = std::function<void(const cJSON* value, std::set<std::string>& out)>;

/* The common case, so most applications declare a field and nothing else: every run
 * of exactly 64 lowercase hex characters inside any string in the value (at any
 * depth) is a SHA-256 address. `assets/3f9a…c2.png` and a bare digest both work. A
 * run of 65 or more is not an address — it is something else that happens to be hex. */
AddressFinder sha256_hex_anywhere();

struct ReferencePolicy {
    /* Keyed the way JoinPolicy is: "content.photo", or "content.*" for every content
     * key, the longest match winning. Two further keys reach outside a rune's fields:
     *
     *   "descriptor"   every glyph declaration, whole — a schema's `presentations`
     *                  may name a sprite or a font (VoidCore SPEC §3.3);
     *   "mantle.tags" / "mantle.rules"   a mantle's own members. */
    std::map<std::string, AddressFinder> fields;

    const AddressFinder* lookup(const std::string& field) const;
};

/* One place the document names an address. `rune` is the spirit.id, empty for a
 * mantle member; `glyph` is set, and `mantle` empty, for a declaration. The location
 * is what lets an interface say WHICH rune is waiting on which file. */
struct Reference {
    std::string address;
    std::string mantle;
    std::string rune;
    std::string glyph;
    std::string field;
};

/* Every reference in a Void Core state document's versioned slice, sorted by
 * (address, mantle, rune, glyph, field) so two peers list them identically.
 *
 * Run it over a FLATTENED document, or over any past version in an archive — the
 * union of references across the versions a device keeps is the set of files it must
 * not delete, because time travel to an old version needs that version's files. */
std::vector<Reference> references(const cJSON* state, const ReferencePolicy& policy);

/* The distinct addresses among `refs` for which `have` answers false, sorted. A
 * device's want-list. Recomputed, never stored: it changes when a file arrives, and
 * nothing about it belongs in the shared document. */
std::vector<std::string> missing(const std::vector<Reference>& refs,
                                 const std::function<bool(const std::string&)>& have);

/* `have` for a BlockStore: the address as lowercase SHA-256 hex. */
std::function<bool(const std::string&)> held_by(const BlockStore& store);

/* Do these bytes deserve this address? SHA-256, lowercase hex. Check this BEFORE
 * storing anything a peer sent; a store that files unverified bytes under a hash
 * hands out wrong data under a right-looking name. An address in any other scheme is
 * the host's to verify, and this returns false for it rather than guessing. */
bool content_matches(const std::string& address, const std::string& bytes);

}  // namespace voidpalabra
