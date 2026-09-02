/* blocks.cpp - the content-addressed block store.
 *
 * Keys are the SHA-256 of the block, so putting the same bytes twice is free and
 * an unchanged asset costs one copy across a hundred versions. A store is allowed
 * to be PARTIAL - that is what a Recent-tier peer is - so `get` reports a miss
 * rather than inventing anything.
 */
#include "internal.hpp"

#include <algorithm>
#include <cstring>

namespace voidpalabra {

using blk::key_bytes;
using blk::key_of;



Digest BlockStore::put(const std::string& bytes) {
    Digest k = sha256(bytes);
    blocks_.emplace(key_bytes(k), bytes);  // emplace: storing twice is free
    return k;
}

void BlockStore::insert_raw(const Digest& key, const std::string& bytes) {
    blocks_.emplace(key_bytes(key), bytes);
}

bool BlockStore::get(const Digest& key, std::string& out) const {
    auto it = blocks_.find(key_bytes(key));
    if (it == blocks_.end()) return false;
    out = it->second;
    return true;
}

bool BlockStore::has(const Digest& key) const {
    return blocks_.count(key_bytes(key)) != 0;
}

std::size_t BlockStore::byte_count() const {
    std::size_t n = 0;
    for (const auto& kv : blocks_) n += kv.second.size();
    return n;
}

std::vector<Digest> BlockStore::put_blob(const std::string& bytes,
                                         const ChunkParams& p) {
    std::vector<Digest> recipe;
    std::size_t off = 0;
    for (std::size_t n : chunk(bytes.data(), bytes.size(), p)) {
        recipe.push_back(put(bytes.substr(off, n)));
        off += n;
    }
    return recipe;
}

bool BlockStore::get_blob(const std::vector<Digest>& recipe, std::string& out) const {
    out.clear();
    for (const Digest& k : recipe) {
        std::string part;
        if (!get(k, part)) return false;  // a partial store is legal; lying is not
        out += part;
    }
    return true;
}


}  // namespace voidpalabra
