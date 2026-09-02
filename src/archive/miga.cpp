/* miga.cpp - importing Void Hormiga's `.miga` v3 bundle.
 *
 * One-way BY DESIGN. There is no exporter back: a migration that can round-trip
 * is a migration nobody finishes. A bundle whose base64 asset fails to decode is
 * refused ENTIRELY, because a partial import that looks complete is worse than a
 * refusal.
 */
#include "voidpalabra/archive.hpp"

#include "cJSON.h"

#include <cstring>
#include <string>

namespace voidpalabra {


namespace {

/* Standard base64 decode. `.miga` v3 inlines assets this way, which is the thing
 * being migrated away from; a decoder is needed exactly once, on import. */
bool base64_decode(const std::string& in, std::string& out) {
    static const std::string alphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    int val = 0, bits = -8;
    out.clear();
    for (unsigned char c : in) {
        if (c == '=' || c == '\n' || c == '\r') continue;
        std::size_t pos = alphabet.find(static_cast<char>(c));
        if (pos == std::string::npos) return false;
        val = (val << 6) + static_cast<int>(pos);
        bits += 6;
        if (bits >= 0) {
            out.push_back(static_cast<char>((val >> bits) & 0xFF));
            bits -= 8;
        }
    }
    return true;
}

}  // namespace

bool import_miga(const std::string& miga_json, Archive& out, const std::string& label) {
    cJSON* root = cJSON_Parse(miga_json.c_str());
    if (!root) return false;

    const cJSON* magic = cJSON_GetObjectItemCaseSensitive(root, "magic");
    const cJSON* version = cJSON_GetObjectItemCaseSensitive(root, "version");
    const cJSON* state = cJSON_GetObjectItemCaseSensitive(root, "state");
    bool ok = magic && magic->valuestring && std::strcmp(magic->valuestring, "MIGA") == 0 &&
              version && cJSON_IsNumber(version) && version->valuedouble == 3 && state;

    if (ok) {
        out.save(state, label);
        const cJSON* assets = cJSON_GetObjectItemCaseSensitive(root, "assets");
        for (const cJSON* it = assets ? assets->child : nullptr; it; it = it->next) {
            if (!it->valuestring || !it->string) continue;
            std::string raw;
            /* A base64 blob that does not decode is a damaged bundle. Skipping it
             * silently would produce an archive that looks complete and is not, so
             * the import fails instead. */
            if (!base64_decode(it->valuestring, raw)) { ok = false; break; }
            out.put_asset(it->string, raw);
        }
    }
    cJSON_Delete(root);
    return ok;
}


/* --- files --------------------------------------------------------------- */

namespace {

/* Distinguish the failures rather than collapsing them into `false`. A full
 * disk, a read-only volume and a missing directory need different responses from
 * a user, and reporting one message for all three is the silent-wrong-answer
 * failure wearing a different hat. */
std::string describe_errno(const char* what, const std::string& path) {
    return std::string(what) + " '" + path + "': " + std::strerror(errno);
}

}  // namespace

bool Archive::write_file(const std::string& path, std::string* error) const {
    const std::string tmp = path + ".tmp";
    std::string bytes = to_bytes();

    /* Write to a temporary beside the target — beside, so the rename stays on one
     * filesystem and is therefore atomic. A temp in /tmp would make the final step
     * a copy, which is exactly the non-atomic operation this avoids. */
    FILE* f = std::fopen(tmp.c_str(), "wb");
    if (!f) { if (error) *error = describe_errno("cannot create", tmp); return false; }

    bool ok = bytes.empty() ||
              std::fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size();
    if (!ok && error) *error = describe_errno("cannot write", tmp);

    /* Flush before rename. Without this the rename can land while the data is
     * still in the OS cache, and a power loss leaves a correctly-named empty
     * file — the failure mode atomic-rename is supposed to prevent. */
    if (ok && std::fflush(f) != 0) {
        ok = false;
        if (error) *error = describe_errno("cannot flush", tmp);
    }
    std::fclose(f);

    if (ok) {
        std::remove(path.c_str());  // Windows rename fails if the target exists
        if (std::rename(tmp.c_str(), path.c_str()) != 0) {
            ok = false;
            if (error) *error = describe_errno("cannot rename onto", path);
        }
    }
    if (!ok) std::remove(tmp.c_str());  // never leave a half-written temp behind
    return ok;
}

bool Archive::read_file(const std::string& path, Archive& out, std::string* error) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) { if (error) *error = describe_errno("cannot open", path); return false; }

    std::string bytes;
    char buf[65536];
    std::size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) bytes.append(buf, n);
    bool read_ok = std::ferror(f) == 0;
    std::fclose(f);
    if (!read_ok) { if (error) *error = describe_errno("cannot read", path); return false; }

    if (!from_bytes(bytes, out)) {
        /* Distinct from an I/O failure: the bytes arrived and are not a valid
         * archive. `container_read` has already verified every block against its
         * key, so this means damage or a foreign file, not a transient problem. */
        if (error) *error = "'" + path + "' is not a valid Void Palabra archive "
                            "(bad magic, unsupported version, truncation, or a "
                            "block that does not match its hash)";
        return false;
    }
    return true;
}

}  // namespace voidpalabra