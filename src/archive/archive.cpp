/* archive.cpp — see include/voidpalabra/archive.hpp for the contract.
 *
 * The index, which is what the container's `doc` slot holds:
 *
 *   { "palabra_archive": 1,
 *     "saves":  [ { "v": "v:…", "label": "…", "when": 0, "chunks": ["<hex>", …] } ],
 *     "assets": { "<name>": ["<hex>", …] } }
 *
 * Saves and assets are both just recipes into the block store, so the index stays
 * tiny however large the archive grows. A save's STATE is stored as its JSON text
 * rather than its canonical encoding, deliberately: the canonical form names a
 * slice, the container stores it, and persistence.md keeps those two jobs apart —
 * "the format may compress, chunk or reorder on disk so long as it reproduces the
 * canonical bytes on read." Storing the text also keeps the reader free of a
 * decoder for an encoding that is meant to be one-way.
 */
#include "voidpalabra/archive.hpp"

#include "voidpalabra/canonical.hpp"
#include "cJSON.h"

#include <cerrno>
#include <cstdio>
#include <cstring>

namespace voidpalabra {
namespace {

std::string hex_of(const Digest& d) { return to_hex(d); }

bool digest_from_hex(const std::string& h, Digest& out) {
    if (h.size() != 64) return false;
    auto nib = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        return -1;
    };
    for (int i = 0; i < 32; ++i) {
        int hi = nib(h[i * 2]), lo = nib(h[i * 2 + 1]);
        if (hi < 0 || lo < 0) return false;
        out[i] = static_cast<std::uint8_t>(hi * 16 + lo);
    }
    return true;
}

cJSON* recipe_to_json(const std::vector<Digest>& r) {
    cJSON* a = cJSON_CreateArray();
    for (const Digest& d : r) cJSON_AddItemToArray(a, cJSON_CreateString(hex_of(d).c_str()));
    return a;
}

bool recipe_from_json(const cJSON* a, std::vector<Digest>& out) {
    if (!a || !cJSON_IsArray(a)) return false;
    for (const cJSON* it = a->child; it; it = it->next) {
        Digest d{};
        if (!it->valuestring || !digest_from_hex(it->valuestring, d)) return false;
        out.push_back(d);
    }
    return true;
}

std::string print_compact(const cJSON* v) {
    char* txt = cJSON_PrintUnformatted(const_cast<cJSON*>(v));
    std::string s = txt ? txt : "";
    if (txt) cJSON_free(txt);
    return s;
}

/* ── Structural chunking ──────────────────────────────────────────────────
 *
 * A state document is NOT chunked by bytes. It is split along its own structure:
 * **one block per rune**, plus a manifest naming them.
 *
 * Why, measured rather than assumed. Byte-chunking a 20 KB document bills a whole
 * chunk for any edit inside it, so a two-character change costs ~2 KB. Tuning the
 * chunk size only moves the problem: coarser chunks bill more per edit, finer ones
 * spend it on keys instead. The tradeoff is real and neither end of it is good.
 *
 * Structure escapes the tradeoff because it aligns block boundaries with edit
 * boundaries: an edit to one rune **cannot** dirty another, whatever the byte
 * offsets do. That is the property a prolly tree provides, at the granularity that
 * actually matters here.
 *
 * The manifest still changes on every save (one rune key moves), so it is
 * byte-chunked with small parameters — it is a compact array of 32-byte keys, and
 * one changed key dirties one small chunk. */
constexpr ChunkParams kManifestChunks{128, 512, 8192};

/* The manifest: a compact binary index naming a save's rune blocks.
 *
 * Binary rather than JSON because it is a dense array of 32-byte keys, and hex in
 * JSON would double the one part of a save whose size grows with rune count. */
constexpr char kManifestMagic[4] = {'V', 'P', 'S', 'T'};

/* v2 (2026-08-21) added the REMAINDER block: the whole state document minus
 * `mantles`, stored beside the manifest. v1 stored `mantles` alone and silently
 * lost everything else — see Archive::save. Nothing shipped on v1, so v1 is
 * refused rather than migrated. */
constexpr std::uint32_t kManifestVersion = 2;

/* The document minus `mantles`. Small for every client we know of — Hormiga
 * measured ~4 KB against 456 KB of mantles — so it rides as one inline block,
 * content-addressed like everything else. */
cJSON* remainder_of(const cJSON* state) {
    cJSON* rest = cJSON_Duplicate(const_cast<cJSON*>(state), 1);
    if (rest) cJSON_DeleteItemFromObjectCaseSensitive(rest, "mantles");
    return rest ? rest : cJSON_CreateObject();
}

/* The address of the whole document: the canonical cut, plus the canonical
 * remainder. Canonical on both halves, so a host that reorders its JSON keys
 * does not produce a spurious save — while the STORED remainder stays exact
 * text, because a config value is not Palabra's to canonicalize. */
Digest content_digest(const cJSON* state, const cJSON* rest) {
    std::string buf = "voidpalabra/content";
    buf.push_back('\0');
    buf += canon_slice(state);
    buf += encode(rest);
    return sha256(buf);
}

void put_u32le(std::string& s, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) s.push_back(static_cast<char>((v >> (i * 8)) & 0xFF));
}
bool get_u32le(const std::string& s, std::size_t& i, std::uint32_t& v) {
    if (i + 4 > s.size()) return false;
    v = 0;
    for (int k = 0; k < 4; ++k)
        v |= static_cast<std::uint32_t>(static_cast<unsigned char>(s[i + k])) << (k * 8);
    i += 4;
    return true;
}
void put_bytes(std::string& s, const std::string& b) {
    put_u32le(s, static_cast<std::uint32_t>(b.size()));
    s += b;
}
bool get_bytes(const std::string& s, std::size_t& i, std::string& out) {
    std::uint32_t n = 0;
    if (!get_u32le(s, i, n)) return false;
    if (i + n > s.size()) return false;
    out = s.substr(i, n);
    i += n;
    return true;
}

}  // namespace

std::string Archive::save(const cJSON* state, const std::string& label) {
    std::string version = version_name(state);

    cJSON* rest = remainder_of(state);
    Digest content = content_digest(state, rest);

    /* Saving the same DOCUMENT twice is free — no blocks, no entry. Compared on
     * `content` rather than `version`, because `version` names `mantles` alone:
     * comparing it meant a config-only edit produced no save and no error. */
    if (!saves_.empty() && saves_.back().content == content) {
        cJSON_Delete(rest);
        return version;
    }

    /* Build the manifest, storing each rune as its own block. Everything that is
     * not a rune — the mantle's id, name, domain, tags, rules, layout — is small
     * and travels inline. */
    std::string manifest(kManifestMagic, 4);
    put_u32le(manifest, kManifestVersion);

    /* The remainder rides as exact JSON text: lossless, because `config` and
     * `scripts` are the application's content and round-tripping them through a
     * canonicalizer would fold numbers and reorder keys. */
    Digest rest_key = blocks_.put(print_compact(rest));
    cJSON_Delete(rest);
    manifest.append(reinterpret_cast<const char*>(rest_key.data()), rest_key.size());

    const cJSON* mantles = cJSON_GetObjectItemCaseSensitive(
        const_cast<cJSON*>(state), "mantles");
    std::uint32_t mantle_count = 0;
    for (const cJSON* m = mantles ? mantles->child : nullptr; m; m = m->next) ++mantle_count;
    put_u32le(manifest, mantle_count);

    for (const cJSON* m = mantles ? mantles->child : nullptr; m; m = m->next) {
        /* The mantle minus its runes: everything the shape needs, cheaply. */
        cJSON* shell = cJSON_Duplicate(const_cast<cJSON*>(m), 1);
        cJSON_DeleteItemFromObjectCaseSensitive(shell, "runes");
        put_bytes(manifest, print_compact(shell));
        cJSON_Delete(shell);

        const cJSON* runes = cJSON_GetObjectItemCaseSensitive(const_cast<cJSON*>(m), "runes");
        std::uint32_t rune_count = 0;
        for (const cJSON* r = runes ? runes->child : nullptr; r; r = r->next) ++rune_count;
        put_u32le(manifest, rune_count);
        for (const cJSON* r = runes ? runes->child : nullptr; r; r = r->next) {
            Digest k = blocks_.put(print_compact(r));  // one block per rune
            manifest.append(reinterpret_cast<const char*>(k.data()), k.size());
        }
    }

    Save s;
    s.version = version;
    s.label = label;
    s.when = 0;  // the caller stamps this if it wants one; nothing here reads it
    s.content = content;
    s.recipe = blocks_.put_blob(manifest, kManifestChunks);
    saves_.push_back(std::move(s));
    return version;
}

cJSON* Archive::load(const std::string& version) const {
    /* Reverse order: since 2026-08-21 two saves may share a `version` — same cut
     * of the versioned slice, different document — so "load this version" means
     * the MOST RECENT one carrying that name. Scanning forwards returned the
     * oldest, which made `load_latest` hand back a stale `config`. */
    for (auto it = saves_.rbegin(); it != saves_.rend(); ++it) {
        if (it->version != version) continue;
        return load_manifest(*it);
    }
    return nullptr;
}

cJSON* Archive::load_manifest(const Save& s) const {
    {
        std::string manifest;
        if (!blocks_.get_blob(s.recipe, manifest)) return nullptr;  // partial store

        std::size_t i = 0;
        std::string magic;
        if (manifest.size() < 4) return nullptr;
        magic = manifest.substr(0, 4);
        i = 4;
        if (magic != std::string(kManifestMagic, 4)) return nullptr;
        std::uint32_t mver = 0, mantle_count = 0;
        if (!get_u32le(manifest, i, mver) || mver != kManifestVersion) return nullptr;

        /* The remainder first: it becomes the document, and the reconstructed
         * `mantles` is added back into it. Rebuilding this way rather than
         * starting from an empty object is what keeps `config`, `scripts`,
         * `domains`, `bindings` and `active` alive across a round trip. */
        if (i + 32 > manifest.size()) return nullptr;
        Digest rest_key{};
        std::memcpy(rest_key.data(), manifest.data() + i, 32);
        i += 32;
        std::string rest_text;
        if (!blocks_.get(rest_key, rest_text)) return nullptr;
        cJSON* parsed = cJSON_Parse(rest_text.c_str());
        if (!parsed || !cJSON_IsObject(parsed)) { if (parsed) cJSON_Delete(parsed); return nullptr; }

        if (!get_u32le(manifest, i, mantle_count)) { cJSON_Delete(parsed); return nullptr; }

        cJSON* arr = cJSON_CreateArray();
        cJSON_DeleteItemFromObjectCaseSensitive(parsed, "mantles");
        cJSON_AddItemToObject(parsed, "mantles", arr);
        bool ok = true;
        for (std::uint32_t k = 0; k < mantle_count && ok; ++k) {
            std::string shell_text;
            if (!get_bytes(manifest, i, shell_text)) { ok = false; break; }
            cJSON* mantle = cJSON_Parse(shell_text.c_str());
            if (!mantle) { ok = false; break; }
            cJSON* runes = cJSON_CreateArray();
            cJSON_AddItemToObject(mantle, "runes", runes);

            std::uint32_t rune_count = 0;
            if (!get_u32le(manifest, i, rune_count)) { cJSON_Delete(mantle); ok = false; break; }
            for (std::uint32_t j = 0; j < rune_count; ++j) {
                if (i + 32 > manifest.size()) { ok = false; break; }
                Digest key{};
                std::memcpy(key.data(), manifest.data() + i, 32);
                i += 32;
                std::string rune_text;
                if (!blocks_.get(key, rune_text)) { ok = false; break; }
                cJSON* rune = cJSON_Parse(rune_text.c_str());
                if (!rune) { ok = false; break; }
                cJSON_AddItemToArray(runes, rune);
            }
            if (!ok) { cJSON_Delete(mantle); break; }
            cJSON_AddItemToArray(arr, mantle);
        }
        if (!ok || i != manifest.size()) { cJSON_Delete(parsed); return nullptr; }
        /* Verify rather than trust: the bytes must still name the version they are
         * filed under. A store that returned the wrong state under the right name
         * would defeat the point of naming it by content. */
        if (version_name(parsed) != s.version) {
            cJSON_Delete(parsed);
            return nullptr;
        }
        return parsed;
    }
    return nullptr;
}

cJSON* Archive::load_latest() const {
    if (saves_.empty()) return nullptr;
    /* By position, not by name: the last entry is the last entry, whatever it
     * shares a version with. */
    return load_at(saves_.size() - 1);
}

cJSON* Archive::load_at(std::size_t index) const {
    if (index >= saves_.size()) return nullptr;
    return load_manifest(saves_[index]);
}

bool Archive::has(const std::string& version) const {
    for (const Save& s : saves_)
        if (s.version == version) return true;
    return false;
}

void Archive::put_asset(const std::string& name, const std::string& bytes) {
    assets_[name] = blocks_.put_blob(bytes);
}

bool Archive::get_asset(const std::string& name, std::string& out) const {
    auto it = assets_.find(name);
    if (it == assets_.end()) return false;
    return blocks_.get_blob(it->second, out);
}

std::vector<std::string> Archive::asset_names() const {
    std::vector<std::string> out;
    for (const auto& kv : assets_) out.push_back(kv.first);  // std::map: sorted
    return out;
}

std::string Archive::to_bytes() const {
    cJSON* index = cJSON_CreateObject();
    cJSON_AddNumberToObject(index, "palabra_archive", 1);

    cJSON* saves = cJSON_CreateArray();
    for (const Save& s : saves_) {
        cJSON* o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "v", s.version.c_str());
        cJSON_AddStringToObject(o, "label", s.label.c_str());
        cJSON_AddNumberToObject(o, "when", static_cast<double>(s.when));
        cJSON_AddItemToObject(o, "chunks", recipe_to_json(s.recipe));
        cJSON_AddItemToArray(saves, o);
    }
    cJSON_AddItemToObject(index, "saves", saves);

    cJSON* assets = cJSON_CreateObject();
    for (const auto& kv : assets_)
        cJSON_AddItemToObject(assets, kv.first.c_str(), recipe_to_json(kv.second));
    cJSON_AddItemToObject(index, "assets", assets);

    Container c;
    /* The index rides as its own canonical encoding, so the file is byte-identical
     * for equal archives whatever order the host built the tree in. */
    c.doc = encode(index);
    cJSON_Delete(index);
    c.blocks = blocks_;
    return container_write(c);
}

bool Archive::from_bytes(const std::string& bytes, Archive& out) {
    Container c;
    if (!container_read(bytes, c)) return false;  // verifies every block
    out.blocks_ = c.blocks;

    /* The index was stored §2-encoded; decode it back to a tree. */
    cJSON* index = decode(c.doc);
    if (!index) return false;
    bool ok = true;
    const cJSON* ver = cJSON_GetObjectItemCaseSensitive(index, "palabra_archive");
    if (!ver || !cJSON_IsNumber(ver) || ver->valuedouble != 1) ok = false;

    if (ok) {
        const cJSON* saves = cJSON_GetObjectItemCaseSensitive(index, "saves");
        for (const cJSON* it = saves ? saves->child : nullptr; it && ok; it = it->next) {
            Save s;
            const cJSON* v = cJSON_GetObjectItemCaseSensitive(it, "v");
            const cJSON* l = cJSON_GetObjectItemCaseSensitive(it, "label");
            const cJSON* w = cJSON_GetObjectItemCaseSensitive(it, "when");
            if (!v || !v->valuestring) { ok = false; break; }
            s.version = v->valuestring;
            s.label = (l && l->valuestring) ? l->valuestring : "";
            s.when = w && cJSON_IsNumber(w) ? static_cast<std::int64_t>(w->valuedouble) : 0;
            if (!recipe_from_json(cJSON_GetObjectItemCaseSensitive(it, "chunks"),
                                  s.recipe)) { ok = false; break; }
            out.saves_.push_back(std::move(s));
        }
    }
    if (ok) {
        const cJSON* assets = cJSON_GetObjectItemCaseSensitive(index, "assets");
        for (const cJSON* it = assets ? assets->child : nullptr; it && ok; it = it->next) {
            std::vector<Digest> r;
            if (!recipe_from_json(it, r)) { ok = false; break; }
            out.assets_[it->string ? it->string : ""] = std::move(r);
        }
    }
    cJSON_Delete(index);
    return ok;
}

}  // namespace voidpalabra
