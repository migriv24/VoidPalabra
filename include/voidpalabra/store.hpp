/* store.hpp — content-addressed block storage. Phase 2.
 *
 * The cheapest genuinely useful rung for a user: it pays for itself with ONE
 * device and no sync at all (okf/concepts/persistence.md).
 *
 * ── The problem it fixes, concretely ──
 *
 * Void Hormiga's `.miga` v3 inlines assets as base64 because it has no dedup, so
 * an image that never changed is re-stored in full in every version. That is the
 * failure this file exists to remove.
 *
 * ── Why content-DEFINED chunking, not fixed-size blocks ──
 *
 * The load-bearing detail, and the one worth stating because it looks like an
 * optimization and is not:
 *
 *     Splitting a blob every N bytes means inserting one byte near the front
 *     shifts every subsequent boundary, so every later block gets a new hash and
 *     a diff degenerates to "everything changed".
 *
 * Content-defined chunking picks boundaries from a rolling hash of the CONTENT, so
 * an insertion perturbs only the chunks around it and every other boundary lands
 * exactly where it did before. That is what makes a diff O(changes) rather than
 * O(size) — and okf/concepts/reconciliation.md needs the same property over the
 * wire, because "storage and sync are the same problem viewed at different
 * distances".
 *
 * The algorithm is FastCDC-style gear hashing: a 64-entry table, one shift and one
 * add per byte, cut when the low bits of the rolling value hit a mask. Normalized
 * chunking (a stricter mask before the target size, a looser one after) keeps the
 * size distribution tight, which matters because tiny chunks cost more in hashes
 * than they save in dedup.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "voidpalabra/canonical.hpp"

struct cJSON;

namespace voidpalabra {

/* Chunk sizes. Defaults chosen for the workload persistence.md describes —
 * mantles of text runes plus image assets — not for backup-grade dedup. */
struct ChunkParams {
    std::size_t min_size = 2 * 1024;
    std::size_t avg_size = 8 * 1024;
    std::size_t max_size = 64 * 1024;
};

/* Split a byte range at content-defined boundaries. Returns the chunk lengths, in
 * order; they sum to `len`. */
std::vector<std::size_t> chunk(const void* data, std::size_t len,
                               const ChunkParams& p = {});

/* A content-addressed block store. Keys are the SHA-256 of the block, so putting
 * the same bytes twice is free and storing an unchanged asset across a hundred
 * versions costs one copy. */
class BlockStore {
public:
    /* Store bytes, returning their address. Idempotent by construction. */
    Digest put(const std::string& bytes);

    /* Retrieve by address. Returns false if absent — a store is allowed to be
     * partial, which is what a `Recent`-tier peer is. */
    bool get(const Digest& key, std::string& out) const;
    bool has(const Digest& key) const;

    std::size_t block_count() const { return blocks_.size(); }
    std::size_t byte_count() const;

    /* Split a blob into content-defined chunks, store each, and return the recipe
     * — the ordered list of block addresses that reconstitutes it. Unchanged
     * regions of a re-stored blob cost nothing. */
    std::vector<Digest> put_blob(const std::string& bytes, const ChunkParams& p = {});
    bool get_blob(const std::vector<Digest>& recipe, std::string& out) const;

    const std::map<std::string, std::string>& blocks() const { return blocks_; }
    void insert_raw(const Digest& key, const std::string& bytes);

private:
    std::map<std::string, std::string> blocks_;  // key bytes -> block bytes
};

/* --- the container ------------------------------------------------------- */
/* One documented, versioned envelope for "a Void Core state, or part of one, at
 * rest" — replacing the per-app bundle every application currently invents.
 *
 * Layout, all integers little-endian (this is a local file format, not the wire;
 * §2's big-endian rule governs the canonical form, which is a different thing):
 *
 *   magic   "VPAL"                     4 bytes
 *   version u32                        the container version
 *   doc_len u64, doc bytes             the enriched document, §2-encoded
 *   count   u64                        number of blocks
 *   repeat: key 32 bytes, len u64, bytes
 *
 * Blocks are written in key order, so the file is byte-identical for equal
 * content — the container inherits the canonical form's determinism instead of
 * having its own. */
inline constexpr std::uint32_t kContainerVersion = 1;

struct Container {
    std::string doc;      // §2-encoded enriched document
    BlockStore blocks;
};

std::string container_write(const Container& c);
/* Returns false on a bad magic, an unsupported version, truncation, or a block
 * whose bytes do not hash to its key — the last being the property that makes a
 * container self-verifying rather than merely parseable. */
bool container_read(const std::string& bytes, Container& out);

}  // namespace voidpalabra
