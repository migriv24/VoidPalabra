/* chunk.cpp - content-defined chunking (FastCDC-style gear hashing).
 *
 * The load-bearing detail, and the reason this is not "split every N bytes":
 * a fixed split means inserting one byte near the front shifts every later
 * boundary, so a diff degenerates to "everything changed". Content-defined
 * boundaries come from a rolling hash of the CONTENT, so an insertion perturbs
 * only the chunks around it. Measured in tests/store_test.cpp: 97.8% of chunks
 * survive a front insertion, against 0.0% for a fixed-size splitter.
 */
#include "voidpalabra/store.hpp"

#include <algorithm>
#include <cstring>

namespace voidpalabra {

namespace {

/* The gear table: one random 64-bit value per byte value. Generated here from a
 * fixed SplitMix64 sequence rather than pasted as a literal table, so it is
 * auditable and identical in every implementation of this format — a different
 * table means different chunk boundaries, which means no dedup between two peers
 * that should have agreed. */
const std::uint64_t* gear_table() {
    static std::uint64_t g[256];
    static bool built = false;
    if (!built) {
        std::uint64_t x = 0x9E3779B97F4A7C15ull;  // SplitMix64, seeded by golden ratio
        for (int i = 0; i < 256; ++i) {
            x += 0x9E3779B97F4A7C15ull;
            std::uint64_t z = x;
            z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
            z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
            g[i] = z ^ (z >> 31);
        }
        built = true;
    }
    return g;
}

/* How many of the HIGH bits must be zero for a cut. More bits = cuts less often.
 *
 * The high bits, emphatically not the low ones. In `h = (h << 1) + g[b]` the low
 * bits barely mix — bit 0 of h is just bit 0 of the current byte's gear value —
 * so a low-bit mask is a function of one or two bytes and, over a small alphabet,
 * can be structurally unable to hit zero at all. That is not hypothetical: the
 * first version of this file masked the low bits and produced *zero* cuts on
 * repetitive text, so every chunk came out at max_size and dedup never fired.
 *
 * The top bits accumulate contributions from the last ~64 bytes, which is what
 * makes the boundary a function of content rather than of position. */
int cut_bits(std::size_t avg, int extra) {
    int bits = 0;
    while ((std::size_t{1} << bits) < avg) ++bits;
    bits += extra;
    if (bits < 1) bits = 1;
    if (bits > 40) bits = 40;
    return bits;
}

inline bool is_cut(std::uint64_t h, int bits) { return (h >> (64 - bits)) == 0; }

/* The gear hash self-windows: after 64 shifts a byte's contribution has left the
 * register, so `h` depends only on the preceding ~64 bytes. Priming over those
 * bytes before the first legal cut point removes the warm-up artifact — without
 * it the hash starts at 0 at `min_size`, which makes the first 64 bytes of the
 * scan depend on where the previous chunk ended rather than on content, and that
 * is exactly the position-dependence content-defined chunking exists to avoid. */
constexpr std::size_t kWindow = 64;

}  // namespace

std::vector<std::size_t> chunk(const void* data, std::size_t len,
                               const ChunkParams& p) {
    std::vector<std::size_t> out;
    const unsigned char* b = static_cast<const unsigned char*>(data);
    const std::uint64_t* g = gear_table();

    /* Normalized chunking: below the average use a STRICTER mask so a cut is less
     * likely, above it a looser one so a cut is more likely. This pulls the size
     * distribution toward `avg` instead of the long exponential tail a single mask
     * produces — which matters because very small chunks cost more in hash
     * bookkeeping than they recover in dedup. */
    const int strict = cut_bits(p.avg_size, +2);
    const int loose = cut_bits(p.avg_size, -2);

    std::size_t start = 0;
    while (start < len) {
        std::size_t remaining = len - start;
        if (remaining <= p.min_size) { out.push_back(remaining); break; }

        std::size_t limit = remaining < p.max_size ? remaining : p.max_size;
        std::size_t normal = remaining < p.avg_size ? remaining : p.avg_size;

        /* Prime over the window ending at the first legal cut point, so the hash
         * at any absolute position is the same whatever chunk it falls in. */
        std::uint64_t h = 0;
        std::size_t prime_from = p.min_size > kWindow ? p.min_size - kWindow : 0;
        for (std::size_t j = prime_from; j < p.min_size; ++j)
            h = (h << 1) + g[b[start + j]];

        std::size_t i = p.min_size;  // never cut before min_size
        for (; i < normal; ++i) {
            h = (h << 1) + g[b[start + i]];
            if (is_cut(h, strict)) break;
        }
        if (i >= normal) {
            for (; i < limit; ++i) {
                h = (h << 1) + g[b[start + i]];
                if (is_cut(h, loose)) break;
            }
        }
        std::size_t size = i < limit ? i + 1 : limit;
        if (start + size > len) size = len - start;
        out.push_back(size);
        start += size;
    }
    return out;
}


}  // namespace voidpalabra
