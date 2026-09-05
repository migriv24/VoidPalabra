/* internal.hpp — the SPEC §2 encoding primitives, shared inside the library.
 *
 * NOT a public header. The public surface is `encode` / `decode` in
 * voidpalabra/canonical.hpp; these are the pieces the Void Core bindings
 * (src/canonical/) need in order to build a rune or a mantle field by field
 * without re-deriving the encoding rules.
 *
 * The utterance layer (src/utterance/) uses the same primitives for the same
 * reason: an utterance's content address is §2 bytes under a §3 header, and
 * re-deriving either would be a second implementation of a contract that only
 * works if there is one.
 *
 * Keeping them here rather than in the public header is the point of the split:
 * an application must not be able to hand-assemble canonical bytes, because the
 * rules about what is a SET and what is a SEQUENCE are semantic decisions this
 * library owns (SPEC §1.2).
 */
#pragma once

#include <string>
#include <utility>
#include <vector>

#include "voidpalabra/canonical.hpp"

struct cJSON;

namespace voidpalabra {
namespace enc {

/* SPEC §2 tag bytes. */
extern const char kNull;
extern const char kFalse;
extern const char kTrue;
extern const char kInt;
extern const char kFloat;
extern const char kStr;
extern const char kSeq;   // order is meaningful
extern const char kMap;   // keys sorted by encoded bytes
extern const char kSet;   // order is NOT meaningful; sorted and deduplicated

void put_uvarint(std::string& out, std::uint64_t n);
std::uint64_t read_uvarint(const std::string& b, std::size_t& i);

std::string encode_str(const char* s, std::size_t len);
std::string encode_str(const std::string& s);
std::string encode_int(std::int64_t v);
std::string encode_number(double v);

std::string encode_seq(const std::vector<std::string>& items);
std::string encode_set(std::vector<std::string> items);
std::string encode_map(std::vector<std::pair<std::string, std::string> > pairs);

/* A map whose values are ALREADY encoded — how a rune or mantle is assembled. */
std::string encode_map_of_encoded(
    const std::vector<std::pair<std::string, std::string> >& fields);

/* SPEC §3: the domain-separated digest. `kind` keeps two structurally identical
 * payloads of different types from colliding; the version and the policy string
 * keep two differently-configured peers from quietly agreeing on a name they
 * computed under different rules.
 *
 * Defined once and shared rather than repeated per layer, because a second copy
 * of a header format is a second chance to get it wrong — and a divergent domain
 * header is a divergence that only shows up when two peers meet. */
Digest domain_digest(const char* kind, const std::string& payload,
                     const std::string& policy_tag);

/* The canonical bytes of ONE glyph declaration (VoidCore:SPEC.md §2, 0.2.14),
 * with the peer-local `source` key excluded — SPEC.md §4.4.
 *
 * Shared with the CRDT layer rather than duplicated there, because the register
 * that CARRIES a declaration across a merge and the name computed OVER it have to
 * agree about what a declaration is. When they disagreed briefly during
 * development, two peers differing only in `source` computed one version name and
 * still reported a redeclaration conflict — the state said "identical" and the
 * merge said "you disagree", which is the worst of both answers. */
std::string canon_glyph_descriptor(const cJSON* descriptor);

/* --- small cJSON helpers, shared by the bindings ------------------------- */
const cJSON* get(const cJSON* obj, const char* key);
std::string str_or(const cJSON* item, const char* fallback);
bool is_absent(const cJSON* item);

}  // namespace enc
}  // namespace voidpalabra
