# vendor/reticulum — what is here, from where, and what we changed

Everything under this folder is built only into the OPTIONAL companion target
`voidpalabra_reticulum` (`-DVOIDPALABRA_RETICULUM=ON`, the default when Void
Palabra is the top-level project). The `voidpalabra` core links none of it and
stays zero-dependency. Why Reticulum, and why this implementation:
[`okf/concepts/reticulum.md`](../../okf/concepts/reticulum.md).

Each component is a verbatim copy of upstream at the pinned commit (only the
files the build needs, plus each one's LICENSE and README), with the patches
below and nothing else.

| folder | upstream | commit | license |
|---|---|---|---|
| `microReticulum/` | https://github.com/attermann/microReticulum | `40fa628` | Apache-2.0 |
| `microStore/` | https://github.com/attermann/microStore | `0f28567` | Apache-2.0 |
| `Crypto/` | https://github.com/attermann/Crypto (fork of rweather/arduinolibs Crypto) | `984dc89` | MIT |
| `ArduinoJson/` | https://github.com/bblanchon/ArduinoJson | `733bc4e` | MIT |
| `MsgPack/` | https://github.com/hideakitai/MsgPack | `1f552c3` | MIT |
| `ArxContainer/` | https://github.com/hideakitai/ArxContainer | `d6affcd` | MIT |
| `ArxTypeTraits/` | https://github.com/hideakitai/ArxTypeTraits | `702de9c` | MIT |
| `DebugLog/` | https://github.com/hideakitai/DebugLog | `b581f7d` | MIT |

Pinned 2026-09-23. The copies keep upstream's own line endings (CRLF on this
checkout).

## Patches

**We do not ask microReticulum's author for changes** (the author's ruling,
2026-09-23): its bugs are worked around here. Every patch is in one script,
[`tools/patch_reticulum.py`](../../tools/patch_reticulum.py). It is idempotent,
it refuses loudly when the text it expects has moved (which is how a new pin
announces that a patch needs rereading), and every patched line carries the
marker `VOIDPALABRA PATCH`:

    grep -rn "VOIDPALABRA PATCH" vendor/reticulum

| file | patch | why |
|---|---|---|
| `microReticulum/.../Utilities/Memory.h` | `#include <cstdint>` | used `uint32_t` without it; compiled on Linux only by accident |
| `microReticulum/.../Log.cpp` | copy `tv_sec` into a `time_t` | `long` is 32 bits on Windows; `localtime` wants `time_t*` |
| `microStore/.../PosixFileSystem.h` | Windows: no `sys/ioctl.h`; `_mkdir`; `fsync` → `_commit` | POSIX-only calls |
| `microStore/.../PosixFileSystem.h` | `O_BINARY` on `open()` | text mode rewrote `0x0A` and stopped at `0x1A`: key files came back corrupted depending on their random bytes |
| `microReticulum/.../Resource.h/.cpp` | `const Link& Resource::link() const` | the resource-concluded callback gets only the Resource, which exposed no link |
| `microReticulum/.../Transport.cpp` | release `_jobs_running` around the resource-watchdog pump | a watchdog that re-requests a lost part sends, and `Transport::outbound` spin-waits for `_jobs_running`, forever on one thread: a hang under packet loss |

## Worked around outside the vendored code

These live in our own sources, not in patches, because the public API was
enough. The details are in `okf/concepts/reticulum.md` under "measured".

- **The persistence stores are file-backed only in transport mode.** Built with
  `RNS_PERSIST_*` (which upstream's `Type.h` turns ON when left undefined), an
  ordinary node's stores are never initialized, so it cannot remember an
  identity it hears and can never open a link. The CMake target sets all three to
  `0`, so the stores live in memory.
- **The storage path is reset by the Reticulum constructor**, so it is set after.
- **Relative paths** (`./cache`, `./path_store/`…) are rooted in the node's own
  folder by `src/reticulum/rooted_fs.hpp` instead of the process's working
  directory.
- **No link watchdog.** microReticulum never implemented it, so there are no
  establishment timeouts, keepalives, or stale detection. `watchdog()` in
  `src/reticulum/node.cpp` re-implements the reference's rules.
- **A second outgoing Resource on a busy link is silently dropped.** Large
  messages queue per link in `Node` and start when the link is ready.
- **A lost Resource proof is never re-requested**, so the sender holds the
  link for about 50 s. When newer messages wait, `Node` abandons a transfer
  that lacks only its proof after max(2 s, 4 x RTT).
- **No bz2.** A compressed Resource from the Python reference is rejected (and
  the link closed). We never send compressed ones; a peer talking to a Void
  device must not either.
- **Only single-segment Resources** (up to 16 MiB), which bounds one message.
- **Not with MSVC.** `Crypto/` writes its rotations as GCC statement
  expressions. The CMake option switches itself off under MSVC, and consumers
  check `TARGET voidpalabra_reticulum`. GCC and Clang build it everywhere:
  MinGW, Linux, macOS and the Android NDK.
