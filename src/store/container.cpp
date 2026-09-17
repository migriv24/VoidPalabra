/* container.cpp - the `VPAL` envelope.
 *
 * Self-verifying rather than merely parseable: every block is re-hashed against
 * its key on read, because handing out wrong data under a right-looking name is
 * the one thing content addressing exists to prevent. Blocks are written in key
 * order, so equal content produces byte-identical files - the container inherits
 * the canonical form's determinism instead of having a weaker notion of its own.
 */
#include "internal.hpp"

#include <algorithm>
#include <cstring>

namespace voidpalabra {

using blk::key_bytes;
using blk::key_of;


namespace {

void put_u32(std::string& s, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) s.push_back(static_cast<char>((v >> (i * 8)) & 0xFF));
}
void put_u64(std::string& s, std::uint64_t v) {
    for (int i = 0; i < 8; ++i) s.push_back(static_cast<char>((v >> (i * 8)) & 0xFF));
}
bool take(const std::string& s, std::size_t& i, std::size_t n, std::string& out) {
    /* Written as a subtraction, not `i + n > size`. A length read from a hostile
     * file can be close to SIZE_MAX, and the addition then wraps to a small number
     * that passes the check — after which `i` jumps backwards and the reader walks
     * bytes it has already read. */
    if (i > s.size() || n > s.size() - i) return false;
    out = s.substr(i, n);
    i += n;
    return true;
}
bool take_u32(const std::string& s, std::size_t& i, std::uint32_t& v) {
    if (i > s.size() || s.size() - i < 4) return false;
    v = 0;
    for (int k = 0; k < 4; ++k)
        v |= static_cast<std::uint32_t>(static_cast<unsigned char>(s[i + k])) << (k * 8);
    i += 4;
    return true;
}
bool take_u64(const std::string& s, std::size_t& i, std::uint64_t& v) {
    if (i > s.size() || s.size() - i < 8) return false;
    v = 0;
    for (int k = 0; k < 8; ++k)
        v |= static_cast<std::uint64_t>(static_cast<unsigned char>(s[i + k])) << (k * 8);
    i += 8;
    return true;
}

}  // namespace

std::string container_write(const Container& c) {
    std::string out = "VPAL";
    put_u32(out, kContainerVersion);
    put_u64(out, c.doc.size());
    out += c.doc;
    put_u64(out, c.blocks.blocks().size());
    /* std::map iterates in key order, so equal content produces byte-identical
     * files. The container inherits the canonical form's determinism rather than
     * having a second, weaker notion of its own. */
    for (const auto& kv : c.blocks.blocks()) {
        out += kv.first;  // 32-byte key
        put_u64(out, kv.second.size());
        out += kv.second;
    }
    return out;
}

bool container_read(const std::string& bytes, Container& out) {
    std::size_t i = 0;
    std::string magic;
    if (!take(bytes, i, 4, magic) || magic != "VPAL") return false;
    std::uint32_t version = 0;
    if (!take_u32(bytes, i, version)) return false;
    if (version != kContainerVersion) return false;  // refuse, do not guess

    std::uint64_t doc_len = 0;
    if (!take_u64(bytes, i, doc_len)) return false;
    if (!take(bytes, i, static_cast<std::size_t>(doc_len), out.doc)) return false;

    std::uint64_t count = 0;
    if (!take_u64(bytes, i, count)) return false;
    for (std::uint64_t k = 0; k < count; ++k) {
        std::string key;
        if (!take(bytes, i, 32, key)) return false;
        std::uint64_t len = 0;
        if (!take_u64(bytes, i, len)) return false;
        std::string block;
        if (!take(bytes, i, static_cast<std::size_t>(len), block)) return false;
        /* Verify rather than trust. A block whose bytes do not hash to its key
         * means the file is damaged or forged, and a store that accepted it would
         * hand out wrong data under a right-looking name — which is the one thing
         * content addressing exists to make impossible. */
        if (key_bytes(sha256(block)) != key) return false;
        out.blocks.insert_raw(key_of(key), block);
    }
    return i == bytes.size();  // trailing garbage is a damaged file, not a valid one
}

}  // namespace voidpalabra
