/* wire.cpp — one frame, and everything about it checked before it is believed.
 *
 *   "VPS1" | header length (u32, little-endian) | header (JSON) | payload (raw)
 *
 * The header is small and structured; the payload is whatever the kind carries —
 * a document's JSON, a file's bytes, a presence blob — and is never parsed here.
 * Encoding is deterministic (fixed member order, no optional member emitted empty),
 * which is what lets a signature be checked over a re-encoding of what arrived.
 */
#include "voidpalabra/sync.hpp"

#include "cJSON.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>

namespace voidpalabra {
namespace sync {

namespace {

const char kMagic[4] = {'V', 'P', 'S', '1'};

const char* kind_name(Kind k) {
    switch (k) {
        case Kind::hello:    return "hello";
        case Kind::doc:      return "doc";
        case Kind::ack:      return "ack";
        case Kind::refuse:   return "refuse";
        case Kind::want:     return "want";
        case Kind::content:  return "content";
        case Kind::absent:   return "absent";
        case Kind::presence: return "presence";
        case Kind::bye:      return "bye";
    }
    return "?";
}

bool kind_from(const char* s, Kind& k) {
    static const Kind all[] = {Kind::hello, Kind::doc, Kind::ack, Kind::refuse, Kind::want,
                               Kind::content, Kind::absent, Kind::presence, Kind::bye};
    for (Kind c : all)
        if (std::strcmp(s, kind_name(c)) == 0) { k = c; return true; }
    return false;
}

bool carries_payload(Kind k) {
    return k == Kind::doc || k == Kind::content || k == Kind::presence;
}

std::string hex(const std::string& b) {
    static const char* d = "0123456789abcdef";
    std::string out;
    for (unsigned char c : b) { out += d[c >> 4]; out += d[c & 0xF]; }
    return out;
}

bool unhex(const char* s, std::string& out) {
    std::size_t n = std::strlen(s);
    if (n % 2) return false;
    out.clear();
    auto nib = [](char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1; };
    for (std::size_t i = 0; i < n; i += 2) {
        int a = nib(s[i]), b = nib(s[i + 1]);
        if (a < 0 || b < 0) return false;
        out.push_back(static_cast<char>(a * 16 + b));
    }
    return true;
}

bool fail(std::string* why, const std::string& w) {
    if (why) *why = w;
    return false;
}

const cJSON* member(const cJSON* o, const char* k) {
    return cJSON_GetObjectItemCaseSensitive(const_cast<cJSON*>(o), k);
}

bool text(const cJSON* o, const char* k, std::string& out, bool required, std::size_t max_len) {
    const cJSON* v = member(o, k);
    if (!v) return !required;
    if (!cJSON_IsString(v) || !v->valuestring) return false;
    if (std::strlen(v->valuestring) > max_len) return false;
    out = v->valuestring;
    return true;
}

}  // namespace

std::string encode_frame(const Message& m) {
    cJSON* h = cJSON_CreateObject();
    cJSON_AddStringToObject(h, "kind", kind_name(m.kind));
    cJSON_AddStringToObject(h, "from", m.from.c_str());
    cJSON_AddStringToObject(h, "session", m.session.c_str());
    cJSON_AddNumberToObject(h, "seq", static_cast<double>(m.seq));
    if (!m.digest.empty()) cJSON_AddStringToObject(h, "digest", m.digest.c_str());
    if (!m.reason.empty()) cJSON_AddStringToObject(h, "reason", m.reason.c_str());
    if (!m.addresses.empty()) {
        cJSON* a = cJSON_AddArrayToObject(h, "addresses");
        for (const auto& s : m.addresses) cJSON_AddItemToArray(a, cJSON_CreateString(s.c_str()));
    }
    if (!m.address.empty()) cJSON_AddStringToObject(h, "address", m.address.c_str());
    if (!m.auth.empty()) cJSON_AddStringToObject(h, "auth", hex(m.auth).c_str());
    char* s = cJSON_PrintUnformatted(h);
    std::string header = s ? s : "";
    if (s) cJSON_free(s);
    cJSON_Delete(h);

    std::string out(kMagic, 4);
    std::uint32_t n = static_cast<std::uint32_t>(header.size());
    for (int k = 0; k < 4; ++k) out.push_back(static_cast<char>((n >> (8 * k)) & 0xFF));
    out += header;
    out += m.payload;
    return out;
}

bool decode_frame(const std::string& frame, Message& out, const Limits& limits, std::string* why,
            std::string* with_auth_blank) {
    if (frame.size() > limits.max_frame) return fail(why, "frame larger than max_frame");
    if (frame.size() < 8 || std::memcmp(frame.data(), kMagic, 4) != 0)
        return fail(why, "not a Void Palabra sync frame");
    std::uint32_t n = 0;
    for (int k = 0; k < 4; ++k)
        n |= static_cast<std::uint32_t>(static_cast<unsigned char>(frame[4 + k])) << (8 * k);
    if (n > frame.size() - 8) return fail(why, "header length beyond the frame");

    cJSON* h = cJSON_ParseWithLength(frame.data() + 8, n);
    if (!h || !cJSON_IsObject(h)) {
        cJSON_Delete(h);
        return fail(why, "header is not a JSON object");
    }

    Message m;
    bool ok = true;
    std::string reason;
    do {
        std::string kind;
        if (!text(h, "kind", kind, true, 16) || !kind_from(kind.c_str(), m.kind)) { reason = "unknown kind"; break; }
        if (!text(h, "from", m.from, true, 256)) { reason = "bad 'from'"; break; }
        if (!text(h, "session", m.session, true, 256)) { reason = "bad 'session'"; break; }
        const cJSON* seq = member(h, "seq");
        if (!seq || !cJSON_IsNumber(seq) || seq->valuedouble < 0 || seq->valuedouble > 9007199254740992.0) {
            reason = "bad 'seq'"; break;
        }
        m.seq = static_cast<std::uint64_t>(seq->valuedouble);
        if (!text(h, "digest", m.digest, false, 128)) { reason = "bad 'digest'"; break; }
        if (!text(h, "reason", m.reason, false, 1024)) { reason = "bad 'reason'"; break; }
        if (!text(h, "address", m.address, false, 256)) { reason = "bad 'address'"; break; }
        if (const cJSON* a = member(h, "addresses")) {
            if (!cJSON_IsArray(a)) { reason = "bad 'addresses'"; break; }
            if (static_cast<std::size_t>(cJSON_GetArraySize(a)) > limits.max_addresses) {
                reason = "more addresses than max_addresses"; break;
            }
            for (const cJSON* it = a->child; it; it = it->next) {
                if (!cJSON_IsString(it) || !it->valuestring || std::strlen(it->valuestring) > 256) {
                    reason = "bad address in 'addresses'"; ok = false; break;
                }
                m.addresses.push_back(it->valuestring);
            }
            if (!ok) break;
        }
        std::string auth_hex;
        if (!text(h, "auth", auth_hex, false, 4096) || !unhex(auth_hex.c_str(), m.auth)) {
            reason = "bad 'auth'"; break;
        }
        m.payload = frame.substr(8 + n);
        if (!carries_payload(m.kind) && !m.payload.empty()) { reason = "a payload on a kind that carries none"; break; }
        if (m.kind == Kind::presence && m.payload.size() > limits.max_presence) {
            reason = "presence larger than max_presence"; break;
        }
    } while (false);
    if (!reason.empty()) ok = false;
    cJSON_Delete(h);
    if (!ok) return fail(why, reason);

    if (with_auth_blank) {
        Message blank = m;
        blank.auth.clear();
        *with_auth_blank = encode_frame(blank);
    }
    out = std::move(m);
    return true;
}

/* ── the stream envelope, SPEC §11.9 ──────────────────────────────────────── */

namespace {

constexpr std::size_t kPrefix = 8;  // "VPS1" + the header length

std::uint32_t read_u32(const std::string& b, std::size_t at) {
    return static_cast<std::uint32_t>(static_cast<unsigned char>(b[at])) |
           static_cast<std::uint32_t>(static_cast<unsigned char>(b[at + 1])) << 8 |
           static_cast<std::uint32_t>(static_cast<unsigned char>(b[at + 2])) << 16 |
           static_cast<std::uint32_t>(static_cast<unsigned char>(b[at + 3])) << 24;
}

}  // namespace

std::string stream_frame(const std::string& frame) {
    std::string out;
    out.reserve(frame.size() + 4);
    std::uint32_t n = static_cast<std::uint32_t>(frame.size());
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<char>((n >> (8 * i)) & 0xFF));
    out += frame;
    return out;
}

void StreamReader::feed(const char* data, std::size_t n) {
    if (broken() || n == 0) return;
    /* Compact before growing, so a long-lived connection does not keep every byte
     * it ever received. */
    if (at_ > 0 && at_ >= buf_.size() / 2) {
        buf_.erase(0, at_);
        at_ = 0;
    }
    buf_.append(data, n);
}

bool StreamReader::next(std::string& frame) {
    if (broken()) return false;
    std::size_t have = buf_.size() - at_;
    if (have < 4) return false;
    std::uint32_t len = read_u32(buf_, at_);
    if (len > limits_.max_frame) {
        why_ = "a frame longer than the limit";
        return false;
    }
    if (len < kPrefix) {
        why_ = "a frame shorter than its own prefix";
        return false;
    }
    /* The magic as soon as it is here — not after the whole claimed length. */
    std::size_t magic = std::min<std::size_t>(have - 4, 4);
    if (buf_.compare(at_ + 4, magic, "VPS1", magic) != 0) {
        why_ = "not a Void Palabra frame";
        return false;
    }
    if (have - 4 < len) return false;
    frame.assign(buf_, at_ + 4, len);
    at_ += 4 + len;
    if (at_ == buf_.size()) {
        buf_.clear();
        at_ = 0;
    }
    return true;
}

}  // namespace sync
}  // namespace voidpalabra
