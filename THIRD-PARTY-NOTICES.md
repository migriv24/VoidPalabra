# Third-party notices

Void Palabra is **zero-dependency** by design, and that is a constraint rather
than a boast: [`okf/concepts/peer-and-tier.md`](okf/concepts/peer-and-tier.md)
makes an ESP32 a full peer, so every line has to be reachable on a device with no
package manager. There is exactly one vendored component.

This file exists separately from [`LICENSE`](LICENSE) on purpose. GitHub detects a
repository's license by matching that file against known texts, and appending a
dependency index to it makes the repository classify as "Other" instead of MIT.
The license text stays pure; the index lives here.

---

## cJSON 1.7.18

- **Where:** `vendor/cJSON.c`, `vendor/cJSON.h`
- **Upstream:** https://github.com/DaveGamble/cJSON
- **Copyright:** © 2009–2017 Dave Gamble and cJSON contributors
- **License:** MIT — the same terms as this project, reproduced in full at the
  top of both vendored files.

**Why it is vendored whole** rather than fetched: Void Maiz and Void Core vendor
it the same way, and the input to Palabra's canonical form is the cJSON tree
those projects already hold. Defining a value type of our own would mean a
conversion at every boundary, and a conversion is a place for two peers to
disagree about what a value is.

**A consumer that already vendors cJSON should link its own copy and drop ours.**
Nothing in the public headers mentions cJSON beyond a forward declaration
(`struct cJSON;`), so the two never conflict at the interface. Void Hormiga links
against both `libvoidmaiz.a` and `libvoidpalabra.a` at the same cJSON version and
it works — that is measured, not reasoned.

`CJSON_HIDE_SYMBOLS` is defined for our translation units so cJSON stays internal
to the static library, mirroring what Void Core and Void Maiz do.

---

## Nothing else

No build-time downloads, no package manager, no submodules. `tools/check_okf.py`
uses only the Python standard library and is skipped silently when no interpreter
is present — it is developer tooling, and the library itself stays dependency-free.

## Conditions that travel downstream

None beyond MIT attribution. cJSON is MIT, this project is MIT, and MIT's only
requirement is that the copyright notice and permission notice travel with copies
or substantial portions of the software. Redistributing a binary that links Void
Palabra therefore means carrying both notices — this file and [`LICENSE`](LICENSE)
together satisfy that.

There are no copyleft, non-commercial, or font-style restrictions anywhere in this
tree. Nothing here may not be sold.
