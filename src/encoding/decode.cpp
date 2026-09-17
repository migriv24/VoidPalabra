/* decode.cpp - canonical bytes back to a value, for the storage path only.
 *
 * The canonical form is one-way BY DESIGN (see voidpalabra/canonical.hpp): §2.1's
 * integral fold and §2.4's set deduplication are deliberately lossy about how a
 * value was written down. This recovers a value that is EQUAL under the encoding,
 * which is what a store needs and is not a round-trip law.
 *
 * ── THE INPUT IS HOSTILE ──
 *
 * This was written as "the storage path only", on the assumption that it read
 * bytes this library had written. That stopped being true when enriched documents
 * started travelling between peers: `flatten` decodes every register value a peer
 * sent. So this decoder now treats its input as coming from someone who wants it to
 * fail, and the four ways the first version DID fail are the four rules below.
 * Each was demonstrated before it was fixed (2026-09-16):
 *
 *   1. A COUNT IS A CLAIM, NOT A FACT. A nine-byte value declaring 2^63 sequence
 *      elements made the old decoder allocate nulls until the process died. Every
 *      element costs at least one byte, so a count larger than the bytes left is
 *      refused before anything is allocated. This is the amplification a
 *      per-message size ceiling cannot catch: the message was tiny.
 *
 *   2. RUNNING OUT OF INPUT IS A FAILURE. The old decoder returned `null` at the
 *      end of the input, so a truncated sequence decoded to `[true, null, null]` —
 *      a value nobody wrote, presented as if someone had.
 *
 *   3. NESTING IS BOUNDED. Two million nested sequences overflowed the stack. The
 *      bound is cJSON's own CJSON_NESTING_LIMIT, because no deeper value can have
 *      come from a JSON document in the first place — so the limit refuses nothing
 *      legitimate, and there is one knob for both parsers rather than two that
 *      disagree. Constrained devices lower it at build time.
 *
 *   4. A VARINT HAS AT MOST TEN BYTES. Shifting a 64-bit value by 70 is undefined
 *      behaviour, not an error, so the length is checked rather than trusted.
 *
 * Refusal is always `nullptr`. Nothing here guesses.
 */
#include "internal.hpp"

#include "cJSON.h"

#include <cstring>

namespace voidpalabra {

namespace {
using namespace enc;

const int kMaxDepth = CJSON_NESTING_LIMIT;

bool read_varint(const std::string& b, std::size_t& i, std::uint64_t& out) {
    std::uint64_t v = 0;
    for (int k = 0; k < 10; ++k) {
        if (i >= b.size()) return false;  // truncated
        unsigned char c = static_cast<unsigned char>(b[i++]);
        if (k == 9 && c > 1) return false;  // would not fit in 64 bits
        v |= static_cast<std::uint64_t>(c & 0x7F) << (7 * k);
        if (!(c & 0x80)) { out = v; return true; }
    }
    return false;  // eleven bytes or more
}

std::size_t remaining(const std::string& b, std::size_t i) {
    return i < b.size() ? b.size() - i : 0;
}

cJSON* decode_at(const std::string& b, std::size_t& i, int depth) {
    if (depth > kMaxDepth) return nullptr;
    if (i >= b.size()) return nullptr;
    unsigned char tag = static_cast<unsigned char>(b[i++]);
    switch (tag) {
        case 0x00: return cJSON_CreateNull();
        case 0x01: return cJSON_CreateFalse();
        case 0x02: return cJSON_CreateTrue();
        case 0x03: {
            std::uint64_t z;
            if (!read_varint(b, i, z)) return nullptr;
            std::int64_t n = static_cast<std::int64_t>((z >> 1) ^ (~(z & 1) + 1));
            return cJSON_CreateNumber(static_cast<double>(n));
        }
        case 0x04: {
            if (remaining(b, i) < 8) return nullptr;
            std::uint64_t bits = 0;
            for (int k = 0; k < 8; ++k)
                bits = (bits << 8) | static_cast<unsigned char>(b[i++]);
            double d;
            std::memcpy(&d, &bits, sizeof d);
            return cJSON_CreateNumber(d);
        }
        case 0x05: {
            std::uint64_t n;
            if (!read_varint(b, i, n)) return nullptr;
            if (n > remaining(b, i)) return nullptr;
            std::string s = b.substr(i, static_cast<std::size_t>(n));
            i += static_cast<std::size_t>(n);
            return cJSON_CreateString(s.c_str());
        }
        case 0x07: case 0x09: {
            std::uint64_t n;
            if (!read_varint(b, i, n)) return nullptr;
            if (n > remaining(b, i)) return nullptr;  // each element is >= 1 byte
            cJSON* a = cJSON_CreateArray();
            for (std::uint64_t k = 0; k < n; ++k) {
                cJSON* item = decode_at(b, i, depth + 1);
                if (!item) { cJSON_Delete(a); return nullptr; }
                cJSON_AddItemToArray(a, item);
            }
            return a;
        }
        case 0x08: {
            std::uint64_t n;
            if (!read_varint(b, i, n)) return nullptr;
            if (n > remaining(b, i) / 2) return nullptr;  // key + value >= 2 bytes
            cJSON* o = cJSON_CreateObject();
            for (std::uint64_t k = 0; k < n; ++k) {
                cJSON* key = decode_at(b, i, depth + 1);
                /* A key that is not a string has no JSON spelling. The old decoder
                 * filed the value under "" instead, which invented a member. */
                if (!key || !cJSON_IsString(key) || !key->valuestring) {
                    if (key) cJSON_Delete(key);
                    cJSON_Delete(o);
                    return nullptr;
                }
                cJSON* val = decode_at(b, i, depth + 1);
                if (!val) { cJSON_Delete(key); cJSON_Delete(o); return nullptr; }
                cJSON_AddItemToObject(o, key->valuestring, val);
                cJSON_Delete(key);
            }
            return o;
        }
        /* An unknown tag used to decode to null. It is a byte this implementation
         * does not understand, which is a reason to stop rather than to guess. */
        default: return nullptr;
    }
}

}  // namespace

cJSON* decode(const std::string& bytes) {
    std::size_t i = 0;
    cJSON* v = decode_at(bytes, i, 0);
    if (!v) return nullptr;
    if (i != bytes.size()) { cJSON_Delete(v); return nullptr; }  // trailing garbage
    return v;
}

bool is_canonical(const std::string& bytes) {
    cJSON* v = decode(bytes);
    if (!v) return false;
    /* Re-encoding is the whole check. Two different byte strings that decode to
     * one value are the same value spelled twice, and a register comparing bytes
     * would call them a conflict. A peer — hostile, or just a second implementation
     * with a bug — could otherwise manufacture a disagreement that flattens to the
     * same thing on both sides. Encoding can also refuse (invalid UTF-8, a NaN),
     * which is a refusal here too. */
    bool ok;
    try {
        ok = encode(v) == bytes;
    } catch (const CanonicalError&) {
        ok = false;
    }
    cJSON_Delete(v);
    return ok;
}


}  // namespace voidpalabra
