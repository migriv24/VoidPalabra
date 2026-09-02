/* encode.cpp - values to canonical bytes.
 *
 * Part of the SPEC §2 encoding. See src/encoding/internal.hpp for why these
 * primitives are library-internal rather than public.
 */
#include "internal.hpp"

#include "cJSON.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace voidpalabra {
namespace enc {

/* --- tags ---------------------------------------------------------------- */
constexpr char kNull = '\x00';
const char kFalse = '\x01';
const char kTrue = '\x02';
const char kInt = '\x03';
const char kFloat = '\x04';
const char kStr = '\x05';
const char kSeq = '\x07';  // order is meaningful
const char kMap = '\x08';  // keys sorted by encoded bytes
const char kSet = '\x09';  // order is NOT meaningful; sorted and deduped

void put_uvarint(std::string& out, std::uint64_t n) {
    do {
        std::uint8_t b = static_cast<std::uint8_t>(n & 0x7F);
        n >>= 7;
        out.push_back(static_cast<char>(b | (n ? 0x80 : 0)));
    } while (n);
}

std::uint64_t zigzag(std::int64_t n) {
    return (static_cast<std::uint64_t>(n) << 1) ^
           static_cast<std::uint64_t>(n >> 63);
}

/* UTF-8 validation. Palabra refuses to hash bytes it cannot interpret as text:
 * a name that is not valid UTF-8 has no stable meaning across peers, and
 * hashing it anyway would produce a name that looks authoritative.
 *
 * NOTE — Unicode normalization is NOT applied. Two peers whose editors emit NFC
 * and NFD for the same visible name will compute different hashes. Doing this
 * properly needs the Unicode decomposition tables, which is disproportionate at
 * Rung 0 and heavy for an ESP32. Recorded as an open question; until it is
 * answered, callers should hand Palabra NFC. */
bool valid_utf8(const char* s, std::size_t len) {
    std::size_t i = 0;
    while (i < len) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        std::size_t n;
        unsigned int cp;
        if (c < 0x80) { n = 0; cp = c; }
        else if ((c & 0xE0) == 0xC0) { n = 1; cp = c & 0x1Fu; }
        else if ((c & 0xF0) == 0xE0) { n = 2; cp = c & 0x0Fu; }
        else if ((c & 0xF8) == 0xF0) { n = 3; cp = c & 0x07u; }
        else return false;
        if (n > 0 && i + n >= len) return false;  // truncated sequence
        for (std::size_t k = 1; k <= n; ++k) {
            unsigned char cc = static_cast<unsigned char>(s[i + k]);
            if ((cc & 0xC0) != 0x80) return false;
            cp = (cp << 6) | (cc & 0x3Fu);
        }
        // reject overlong forms, surrogates, and out-of-range
        if (n == 1 && cp < 0x80) return false;
        if (n == 2 && cp < 0x800) return false;
        if (n == 3 && cp < 0x10000) return false;
        if (cp > 0x10FFFF) return false;
        if (cp >= 0xD800 && cp <= 0xDFFF) return false;
        i += n + 1;
    }
    return true;
}

std::string encode_str(const char* s, std::size_t len) {
    if (!valid_utf8(s, len)) throw CanonicalError("string is not valid UTF-8");
    std::string out(1, kStr);
    put_uvarint(out, len);
    out.append(s, len);
    return out;
}

std::string encode_str(const std::string& s) {
    return encode_str(s.data(), s.size());
}

std::string encode_int(std::int64_t v) {
    std::string out(1, kInt);
    put_uvarint(out, zigzag(v));
    return out;
}

std::string encode_number(double v) {
    if (std::isnan(v) || std::isinf(v))
        throw CanonicalError("NaN and infinity have no canonical form");
    /* 1 and 1.0 survive a JSON round-trip as the same value, so they must be the
     * same bytes. This also folds -0.0 into 0. cJSON stores every number as a
     * double, so without this rule the encoding would depend on how a host
     * happened to write the literal. */
    if (v == std::floor(v) && std::fabs(v) < 9.007199254740992e15)
        return encode_int(static_cast<std::int64_t>(v));

    std::string out(1, kFloat);
    std::uint64_t bits;
    static_assert(sizeof(double) == sizeof(std::uint64_t), "IEEE754 double");
    std::memcpy(&bits, &v, sizeof(bits));
    for (int i = 7; i >= 0; --i)  // big-endian, so the bytes are platform-stable
        out.push_back(static_cast<char>((bits >> (i * 8)) & 0xFF));
    return out;
}

std::string encode_seq(const std::vector<std::string>& items) {
    std::string out(1, kSeq);
    put_uvarint(out, items.size());
    for (const auto& it : items) out += it;
    return out;
}

/* Encode pre-encoded items whose order carries no information.
 *
 * Duplicates collapse, because a set is a set: holding the same tag twice is
 * holding it once. That is not tidiness — it is idempotence (a ⊔ a = a,
 * okf/concepts/join.md), the law that makes receiving the same thing twice free.
 * A "set" encoding that preserved multiplicity would break it, and a peer that
 * received a duplicate over a lossy transport would diverge from one that did
 * not. */
std::string encode_set(std::vector<std::string> items) {
    std::sort(items.begin(), items.end());
    items.erase(std::unique(items.begin(), items.end()), items.end());
    std::string out(1, kSet);
    put_uvarint(out, items.size());
    for (const auto& it : items) out += it;
    return out;
}

std::string encode_map(std::vector<std::pair<std::string, std::string>> pairs) {
    std::sort(pairs.begin(), pairs.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    for (std::size_t i = 1; i < pairs.size(); ++i)
        if (pairs[i].first == pairs[i - 1].first)
            throw CanonicalError("duplicate object key");
    std::string out(1, kMap);
    put_uvarint(out, pairs.size());
    for (const auto& kv : pairs) { out += kv.first; out += kv.second; }
    return out;
}


/* SPEC §3. The NUL terminator cannot occur in the ASCII header fields, so the
 * header can never be confused with the start of the payload. */
Digest domain_digest(const char* kind, const std::string& payload,
                     const std::string& policy_tag) {
    std::string buf = "voidpalabra/v";
    buf += std::to_string(kCanonVersion);
    buf += '/';
    buf += kind;
    buf += '/';
    buf += policy_tag;
    buf.push_back('\0');
    buf += payload;
    return sha256(buf);
}


const cJSON* get(const cJSON* obj, const char* key) {
    if (!obj) return nullptr;
    return cJSON_GetObjectItemCaseSensitive(const_cast<cJSON*>(obj), key);
}

std::string str_or(const cJSON* item, const char* fallback) {
    if (item && cJSON_IsString(item) && item->valuestring) return item->valuestring;
    return fallback;
}

bool is_absent(const cJSON* item) { return item == nullptr; }


std::string encode_map_of_encoded(
    const std::vector<std::pair<std::string, std::string> >& fields) {
    std::vector<std::pair<std::string, std::string> > pairs;
    pairs.reserve(fields.size());
    for (const auto& kv : fields) pairs.emplace_back(encode_str(kv.first), kv.second);
    std::sort(pairs.begin(), pairs.end(),
              [](const std::pair<std::string, std::string>& a,
                 const std::pair<std::string, std::string>& b) { return a.first < b.first; });
    std::string out(1, kMap);
    put_uvarint(out, pairs.size());
    for (const auto& kv : pairs) { out += kv.first; out += kv.second; }
    return out;
}

}  // namespace enc


std::string encode(const cJSON* v) {
    using namespace enc;
    if (!v || cJSON_IsNull(v)) return std::string(1, kNull);
    if (cJSON_IsFalse(v)) return std::string(1, kFalse);
    if (cJSON_IsTrue(v)) return std::string(1, kTrue);
    if (cJSON_IsNumber(v)) return encode_number(v->valuedouble);
    if (cJSON_IsString(v)) return encode_str(v->valuestring ? v->valuestring : "");
    if (cJSON_IsArray(v)) {
        std::vector<std::string> items;
        for (const cJSON* it = v->child; it; it = it->next) items.push_back(encode(it));
        return encode_seq(items);
    }
    if (cJSON_IsObject(v)) {
        std::vector<std::pair<std::string, std::string>> pairs;
        for (const cJSON* it = v->child; it; it = it->next) {
            if (!it->string) throw CanonicalError("object member without a key");
            pairs.emplace_back(encode_str(it->string, std::strlen(it->string)),
                               encode(it));
        }
        return encode_map(std::move(pairs));
    }
    /* cJSON_Raw is unparsed text — it has no value, only bytes someone meant to
     * splice in later. Refusing is the honest answer. */
    throw CanonicalError("value has no canonical form (raw or invalid)");
}


}  // namespace voidpalabra
