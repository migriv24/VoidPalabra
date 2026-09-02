/* archive.hpp — save, load, and local version tracking. The surface an application
 * actually calls.
 *
 * Built for the first forcing client: Void Hormiga's save system
 * (okf/design/forcing-clients.md). Its `.miga` v3 bundle is
 * `{magic, version, meta, state, assets:{path: base64}}` — the whole database in
 * one file, assets inlined as base64 because there was no dedup. An Archive is the
 * replacement, and the assets map is the part that disappears.
 *
 * ── Why this needs no history graph ──
 *
 * An Archive is a LINEAR list of named saves, and that is not a compromise:
 *
 *   - A save is a cut, named by its canonical hash — already exact, already
 *     verifiable, already order-independent.
 *   - Keeping every save costs almost nothing, because saves are chunked into the
 *     content-addressed store and consecutive saves share nearly all their chunks.
 *   - On one device, with one user at one keyboard, the history genuinely IS a
 *     sequence. okf/design/why-not-linear.md §4 is explicit that linear thinking is
 *     correct there, and forcing a partial order onto it would be the mirror-image
 *     mistake.
 *
 * The history graph (Phase 3) buys the PARTIAL order — concurrency, blame across
 * peers, merge. None of that exists until device two. So an application gets
 * save/load/time-travel today, on Rung 0 plus Phase 2, with nothing blocked.
 *
 * ── The vocabulary stays the application's ──
 *
 * Hormiga will keep calling these "save" and "load", and it should. That is the
 * same client-word/system-word split okf/concepts/version-as-cut.md draws for
 * `undo`: the user says "save", the system says "a cut named v:8c41f2…", and
 * neither has to learn the other's word.
 */
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "voidpalabra/store.hpp"

struct cJSON;

namespace voidpalabra {

struct Save {
    std::string version;        // "v:…" — the canonical name of the CUT
    std::string label;          // whatever the user called it; may be empty
    std::int64_t when = 0;      // wall clock, for DISPLAY ONLY (see below)
    std::vector<Digest> recipe; // chunks of the manifest

    /* The address of the WHOLE document, not just the versioned slice.
     *
     * Naming and storing are two different jobs and this is where they separate.
     * `version` names the cut — `mantles`, and only `mantles`, for every reason
     * canonical.hpp gives about a domain carrying deploy commands. But an archive
     * is not a sync payload; it is the file the user's entire database lives in,
     * so it stores the whole document and needs a second address to answer "did
     * anything change at all".
     *
     * Two saves may therefore share a `version` and differ in `content`. That is
     * honest rather than awkward: they ARE the same cut of the versioned slice,
     * and they are different documents. */
    Digest content{};
};

/* Wall clock appears here and nowhere else. okf/concepts/history-graph.md is
 * strict: time may be RECORDED for humans and must never be CONSULTED to decide
 * causality or resolve a conflict. `when` is shown in a list of saves; nothing in
 * this library reads it. */

class Archive {
public:
    /* Append a save. Returns its version name.
     *
     * Stores the WHOLE state document — every top-level key, not just `mantles`.
     * Reported by Void Hormiga 2026-08-21 after measuring against a real state
     * document: an earlier version stored `mantles` alone, so `config`,
     * `scripts`, `domains`, `bindings` and `active` were silently dropped on a
     * round trip. `site.base_url` lives in `config`, and losing it breaks every
     * canonical URL a site emits.
     *
     * Idempotent in the way that matters: saving a document identical to the
     * latest save adds no blocks and no entry — Ctrl+S twice is free, which
     * matters to a client that autosaves aggressively.
     *
     * The dedup guard compares `content`, not `version`. It used to compare the
     * version name, which is derived from `mantles` alone — so a change to
     * `config` and nothing else produced NO SAVE AT ALL, with no error. A user
     * changing their site's address and pressing Save lost the change silently.
     * That is the worst shape a bug can have and the reason this comment is
     * long. */
    std::string save(const cJSON* state, const std::string& label = "");

    /* Any past save, by version name. Caller owns the returned tree; nullptr if
     * absent or if the stored bytes do not hash to the name they are filed under.
     * Verification is not optional — see `container_read`. */
    cJSON* load(const std::string& version) const;

    /* The MOST RECENT save is returned when several share a version — which they
     * may, since a version names the cut and two documents can share a cut. */
    cJSON* load_latest() const;

    /* By position in `saves()`, when the caller means one specific entry rather
     * than "whatever is newest under this name". */
    cJSON* load_at(std::size_t index) const;

    const std::vector<Save>& saves() const { return saves_; }
    bool has(const std::string& version) const;

    /* Assets, by application-chosen name. Storing the same bytes under two names
     * costs one copy; storing an edited asset costs the edit. */
    void put_asset(const std::string& name, const std::string& bytes);
    bool get_asset(const std::string& name, std::string& out) const;
    std::vector<std::string> asset_names() const;

    /* Bytes on disk: the §2-encoded index plus every block, in the `VPAL`
     * container. Writing is deterministic — equal archives produce equal files. */
    std::string to_bytes() const;
    static bool from_bytes(const std::string& bytes, Archive& out);

    /* --- files ---------------------------------------------------------- */
    /* Palabra does file I/O, and this is deliberate rather than a leak of scope.
     * Void Core does none BY DEFINITION (SPEC §9), so if this library does not
     * own "where a mantle lands on disk" then every application re-solves it —
     * which is the exact failure that founded Palabra. See
     * okf/concepts/persistence.md. */

    /* Write atomically: to a temporary beside the target, flushed, then renamed
     * over it. A crash therefore costs the SAVE, never the archive — the
     * difference matters because this file is the user's whole database, and
     * Hormiga already learned it the same way for `.miga`. */
    bool write_file(const std::string& path, std::string* error = nullptr) const;
    static bool read_file(const std::string& path, Archive& out,
                          std::string* error = nullptr);

    const BlockStore& blocks() const { return blocks_; }
    std::size_t stored_bytes() const { return blocks_.byte_count(); }

private:
    cJSON* load_manifest(const Save& s) const;

    BlockStore blocks_;
    std::vector<Save> saves_;
    std::map<std::string, std::vector<Digest>> assets_;
};

/* Import a Void Hormiga `.miga` v3 bundle as a one-save Archive.
 *
 * One-way and deliberately so: it reads `{version, meta, state, assets}`, decodes
 * the base64 assets into the block store, and records `state` as the first save.
 * There is no exporter back to `.miga` — a migration that can round-trip is a
 * migration nobody finishes.
 *
 * Returns false if the bundle is not v3 or does not parse. */
bool import_miga(const std::string& miga_json, Archive& out,
                 const std::string& label = "imported");

}  // namespace voidpalabra
