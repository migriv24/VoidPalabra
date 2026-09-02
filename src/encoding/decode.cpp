/* decode.cpp - canonical bytes back to a value, for the storage path only.
 *
 * The canonical form is one-way BY DESIGN (see voidpalabra/canonical.hpp): §2.1's
 * integral fold and §2.4's set deduplication are deliberately lossy about how a
 * value was written down. This recovers a value that is EQUAL under the encoding,
 * which is what a store needs and is not a round-trip law.
 */
#include "internal.hpp"

#include "cJSON.h"

#include <cstring>

namespace voidpalabra {

/* --- decoding (the storage path only; see the header) -------------------- */

namespace {
using namespace enc;
/* A minimal decoder for the SPEC.md §2 encoding — only what `enrich` writes.
 * Full decoding is not required anywhere else: the canonical form is a one-way
 * function by design, and this exists solely so `flatten` can rebuild a Core
 * document from stored field values. */
cJSON* decode_at(const std::string& b, std::size_t& i);

std::uint64_t read_uvarint(const std::string& b, std::size_t& i) {
    std::uint64_t v = 0;
    int shift = 0;
    while (i < b.size()) {
        unsigned char c = static_cast<unsigned char>(b[i++]);
        v |= static_cast<std::uint64_t>(c & 0x7F) << shift;
        if (!(c & 0x80)) break;
        shift += 7;
    }
    return v;
}

cJSON* decode_at(const std::string& b, std::size_t& i) {
    if (i >= b.size()) return cJSON_CreateNull();
    unsigned char tag = static_cast<unsigned char>(b[i++]);
    switch (tag) {
        case 0x00: return cJSON_CreateNull();
        case 0x01: return cJSON_CreateFalse();
        case 0x02: return cJSON_CreateTrue();
        case 0x03: {
            std::uint64_t z = read_uvarint(b, i);
            std::int64_t n = static_cast<std::int64_t>((z >> 1) ^ (~(z & 1) + 1));
            return cJSON_CreateNumber(static_cast<double>(n));
        }
        case 0x04: {
            std::uint64_t bits = 0;
            for (int k = 0; k < 8 && i < b.size(); ++k)
                bits = (bits << 8) | static_cast<unsigned char>(b[i++]);
            double d;
            std::memcpy(&d, &bits, sizeof d);
            return cJSON_CreateNumber(d);
        }
        case 0x05: {
            std::uint64_t n = read_uvarint(b, i);
            std::string s = b.substr(i, static_cast<std::size_t>(n));
            i += static_cast<std::size_t>(n);
            return cJSON_CreateString(s.c_str());
        }
        case 0x07: case 0x09: {
            std::uint64_t n = read_uvarint(b, i);
            cJSON* a = cJSON_CreateArray();
            for (std::uint64_t k = 0; k < n; ++k)
                cJSON_AddItemToArray(a, decode_at(b, i));
            return a;
        }
        case 0x08: {
            std::uint64_t n = read_uvarint(b, i);
            cJSON* o = cJSON_CreateObject();
            for (std::uint64_t k = 0; k < n; ++k) {
                cJSON* key = decode_at(b, i);
                cJSON* val = decode_at(b, i);
                cJSON_AddItemToObject(o, key->valuestring ? key->valuestring : "", val);
                cJSON_Delete(key);
            }
            return o;
        }
        default: return cJSON_CreateNull();
    }
}


}  // namespace

cJSON* decode(const std::string& bytes) {
    std::size_t i = 0;
    cJSON* v = decode_at(bytes, i);
    if (i != bytes.size()) { cJSON_Delete(v); return nullptr; }  // trailing garbage
    return v;
}


}  // namespace voidpalabra
