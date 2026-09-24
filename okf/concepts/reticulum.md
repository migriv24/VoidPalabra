---
type: Concept
title: Reticulum — the family's network, with Palabra as its Void translation
description: "Decided by the author 2026-09-23: all device-to-device networking in the Void family runs over Reticulum, and Void Palabra is its Void-based translation. The core stays zero-dependency and pure; the optional companion target voidpalabra_reticulum (built 2026-09-23/24) carries a vendored, pinned microReticulum, and Palabra's sessions ride Reticulum links. Interop with the Python reference is proven by test (destination hashes, links, packets and Resources both ways), and a replica converges over a link, including through a relay that drops 10% of datagrams. Ten measured defects in microReticulum and how each is worked around (we do not ask its author for changes), what Android and iOS allow, what stays outside, and the order of work."
tags: [status:current, audience:dev, audience:library, confidence:measured]
resource: src/reticulum/node.cpp
timestamp: 2026-09-24T00:00:00Z
---

# The decision

The author, 2026-09-23, in a Void Hormiga session, after weighing a separate
cryptography sibling (Void Snape, founded and archived the same day):

> let's just use reticulum for everything! all our netoworking needs, with
> palabra as its void based translation! ... this will involve working over
> multiple different software, including void palabra, and void maiz (hormiga
> shouldn't need to actually ddeal with too much of ths, just re-implementing
> things)

Earlier the same day the author gave the reason: *"there's a rise in reticulum
usage in the open source community"*. Reticulum is a cryptography-based
networking stack that works over LAN, TCP, LoRa, packet radio and serial, with
no source addresses on packets. The author also named the forcing cases: an
ESP32 talking to a Windows desktop, iOS, and *"messages [that] can travel no
matter what"*.

# How it fits Palabra's own rulings (nothing here reverses them)

| Palabra's ruling | how Reticulum fits it |
|---|---|
| **zero dependencies, a pure state machine** ([transport shape](/design/transport-shape.md)) | the core does not change. Reticulum lives in an **optional companion target, `voidpalabra_reticulum`**, which links the Reticulum implementation. An application that syncs in one process, or on a trusted wire, links the core alone |
| **transports are dumb byte pipes** | a **Reticulum link is the pipe**. Session frames ride inside links, opaque to Reticulum, which seals them. Every medium Reticulum reaches becomes a sync transport |
| **hooks, not primitives** (2026-09-19, [open questions](/design/open-questions.md) §6) | a link's remote identity is proven by Reticulum's handshake (an Ed25519 signature), so **`Host::verify` is answered by who the link says the peer is**, and `Host::sign` by this device's Reticulum identity. The core still never links cryptography |
| **a device has two keys, never one reused** (2026-09-16, §6.1 finding 2) | a Reticulum identity *is* exactly that: an Ed25519 key to sign and an X25519 key to agree. Palabra reached the same shape independently |
| **Palabra declined the socket layer** (2026-09-20) | still declined. The companion takes interfaces from the host (on a desktop or phone, Void Maiz's sockets; on a microcontroller, a radio driver), because the chosen implementation is host-driven (below) |

**Capabilities and "who may" stay Palabra's** (§6). Reticulum answers *who is
this* and *can anyone else read it*. It never answers *may they*.

# What Reticulum is, in the parts that bind us

From Reticulum's manual, read 2026-09-23. The manual is the specification.
- **Authoritative primitives:** Ed25519, X25519, HKDF, AES-256-CBC with PKCS7,
  HMAC-SHA256, SHA-256, SHA-512. The manual says: *"Anything claiming to be
  Reticulum, but not using these exact primitives is not Reticulum."*
- **Identities:** Ed25519 and X25519 public keys, 32 bytes each.
  **Destinations:** 16-byte truncated SHA-256 hashes. **Packets:** a 2-byte
  header, 16 or 32 bytes of addresses, a context byte, and up to 465 bytes of
  data, over a physical MTU of at least 500.
- **Links:** forward secrecy, set up in 3 packets totalling 297 bytes.
  **Resources:** large transfers over a link, which a whole-document exchange
  needs.
- **LAN discovery (AutoInterface):** IPv6 link-local multicast plus UDP (ports
  29716 and 42671), with no router or DHCP needed.

# Which implementation: microReticulum (measured 2026-09-23)

Three implementations exist: the Python reference (the authority; its license
is modified MIT with use restrictions), **microReticulum** (C++, Apache 2.0),
and Reticulum-Go (Apache 2.0). A C++ family links microReticulum. Read and built
from a scratch clone at `40fa628` (2026-07-20):

- **About 17.7k lines of C++17**, CMake and PlatformIO, running on ESP32,
  nRF52, LoRa boards, Linux, macOS and Raspberry Pi.
- **The host drives the I/O.** An interface implements `send_outgoing(bytes)`
  and pushes received bytes with `handle_incoming(bytes)`. The stack advances
  when the host calls `Reticulum::loop()`. It opens no socket of its own: its
  UDP interface is an *example*. This is Palabra's own shape, and it is why
  Void Maiz's sockets can feed it directly.
- **Built:** announces, links, Resources, packet proofs, transport and path
  finding, persistence. **Not yet built:** Ratchets, and Channel/Buffer.
- **No AutoInterface.** LAN discovery with zero configuration is ours to write
  as an interface, or a LAN uses Reticulum's simpler UDP broadcast interface.
- **One Reticulum per process:** `Transport` is static and its callbacks are
  plain function pointers. So `Node` is a process singleton, and every test of
  two peers is two processes.
- **Vendored, pinned, patched:** microReticulum and its seven dependencies live
  in `vendor/reticulum/` at pinned commits (upstream fetches two of them from an
  unpinned `master`). The pins, licenses and every patch are listed in
  [`vendor/reticulum/VENDORED.md`](../../vendor/reticulum/VENDORED.md).

# What is built (`voidpalabra_reticulum`)

[`include/voidpalabra/reticulum.hpp`](../../include/voidpalabra/reticulum.hpp).
No microReticulum type appears in it.

- **`Node`**, one per process: `start(Options)` loads or creates the device's
  identity (kept in `storage_dir`) and opens the UDP interfaces it is given.
  `loop()` pumps the network. `open(destination)` asks for a path if none is
  known. `send(link, bytes)` sends a packet when the message fits and a Resource
  when it does not, and `poll()` returns events (`announce`,
  `link_established`, `peer_identified`, `link_closed`, `data`, `error`). The
  UDP interface is ours (`src/reticulum/udp.cpp`, Winsock and POSIX), shaped
  like Reticulum's own `UDPInterface` (listen and forward), so the Python
  reference talks to it with mirrored ports. With `learn_peers` it also sends
  every datagram, by unicast, to each address heard from in the last minute.
  That is Void Maiz's "beacon out, unicast back" (Android drops incoming
  broadcast but never unicast), and it is how one node answers many on a single
  machine. `set_announce_data` changes what announces carry while the node runs.
- **`SyncLinks`**: one `sync::Session` per link. Each Palabra frame goes as one
  whole message, so no stream framing is needed. A session that ends closes its
  link.

# Measured: what microReticulum gets wrong, and what we do about it

**We do not ask microReticulum's author for changes** (the author's ruling,
2026-09-24: *"we probably can't talk to reticulum's author, so count that out
entirely, and work around the bugs instead"*). A fix lives in our code when the
public API allows it, and otherwise as a marked patch applied by
`tools/patch_reticulum.py`. Every one of these was found by a test failing,
not by reading.

| # | defect | effect | workaround |
|---|---|---|---|
| 1 | Windows portability: `<cstdint>`, 32-bit `tv_sec`, POSIX-only `ioctl`/`mkdir`/`fsync` | does not compile | patches |
| 2 | files opened in **text mode** on Windows | key files corrupted depending on their random bytes: an identity whose Ed25519 half changed between runs | patch: `O_BINARY` |
| 3 | the Reticulum constructor **resets the storage path** to `.` | our node's files landed in the caller's working directory | set the path after construction |
| 4 | stores open **relative paths** (`./cache`, `./path_store/`...) | keys and routing tables written into whatever folder the program started in | `RootedFileSystem` roots every relative path in the node's folder |
| 5 | with `RNS_PERSIST_*` on (upstream's default), the file-backed stores are **initialized only in transport mode** | an ordinary node cannot remember an identity it hears, so it can never open a link | build with all three set to `0`: the stores live in memory. Peers are relearned from announces; our identity file persists regardless |
| 6 | `Resource(bytes, link)` **sends nothing** until `.start()`, and the concluded callback exposes no link | a large message never leaves; a received one has no link | call `start()`; patch in `Resource::link()` |
| 7 | **no link watchdog** (`Link::start_watchdog` is empty) | a lost link request stays PENDING forever; an idle link sends no keepalives (so the Python reference drops it as stale); a vanished peer's link stays "up" forever | `watchdog()` in `node.cpp` re-implements the reference's rules (Link.py, 1.5.4) from the public API, with the reference's constants |
| 8 | `Transport::jobs()` pumps resource watchdogs **while flagged as running**, and a watchdog that sends spins in `Transport::outbound` waiting for that flag | the process **hangs** the first time a lost resource part is re-requested | patch: release the flag around the pump |
| 9 | a second outgoing Resource on a busy link is marked QUEUED and **silently dropped** | every state Palabra re-sent during a slow transfer vanished, for up to a minute | `Node` keeps a per-link outbox (in order, 32 deep, identical bytes not queued twice) and starts each when the link is ready |
| 10 | a **lost Resource proof** is not re-requested (the reference asks the network cache again; microReticulum left that out) | the sender waits three proof timeouts (~50 s), and with one Resource per link, everything queued waits too: a Palabra state stuck 44 s behind a transfer that had in fact arrived | when something is waiting, `Node` abandons a Resource that has sent every part and lacks only its proof after max(2 s, 4 x RTT). With nothing waiting, it keeps the reference's full patience |

Two limits are **accepted, not worked around**:
- **No bz2.** The Python reference compresses a Resource when that makes it
  smaller, and microReticulum rejects a compressed one and closes the link. Our
  side never compresses. A peer that talks to a Void device must send
  uncompressed (`auto_compress=False`), as the interop test's Python peer does.
- **Single-segment Resources only**, which caps one message at 16 MiB.

And one gap in **Reticulum's own protocol**, which the reference shares: a
link's handshake ends with one RTT packet from the initiator. If that packet is
lost, the initiator's link is up while the other side's never activates, and by
the reference's rule it waits 6 minutes (`KEEPALIVE` + a per-hop allowance)
before giving up. It still answers keepalives, so no link watchdog can see the
problem. **The sync session can**: no hello within `Timing::hello_timeout`
(20 s). So `SyncLinks` closes a link whose session ended, and the host opens a
new one.

# Tests (one device, three kinds of network)

`tests/reticulum/interop.py` drives `voidpalabra_rns_peer` (the C++ side) and,
where it needs one, the **official Python Reticulum** (`pip install rns`; the
tests that need it SKIP when it is absent). Every peer is its own process on
`127.0.0.1`. In ctest:

| test | proves |
|---|---|
| `reticulum_vectors` | our destination hash for a public key equals the reference's, and an identity survives a restart |
| `reticulum_interop_py_server` / `_cpp_server` | C++ and Python both ways: a link, a packet and a 9 KB Resource each way, and the C++ side proves its identity on the link |
| `reticulum_big_py` / `_big_cpp` | a 400 KB Resource, past one advertisement's hashmap |
| `reticulum_sync` | a founder and a joiner converge on one document over a link, with a change from each side |
| `reticulum_lossy_sync` / `_lossy_big` (label `lossy`) | the same through **a relay that drops and reorders datagrams** (10% for sync, 5% for a 100 KB Resource) |

**One device cannot test a real network, but it can test a bad one.** Every
defect from 7 onward was invisible on a clean loopback and found by the relay.
Measured 2026-09-24 at 10% loss, ten relay seeds, with every workaround in:
**all ten converged**, in 2.2, 2.9, 4.3, 6.1, 16.6, 27.4, 29.2, 35.6, 39.1 and
63.7 s. Before defects 9 and 10 were handled, the same test took 58–85 s
every time. When the handshake survives, sync converges in 2–8 s. **A lost
handshake costs about 13 s** (the establishment timeout) and a half-open link
about 20 s, and the slowest seed paid two of the first and one of the second.
A 100 KB Resource takes about 25 s at 5% loss and about 80 s at 10%.
Reticulum's retry timers set that pace, and we keep them.

# Android and iOS

- **Android: proven for Reticulum, compiled for us.** Reticulum's own Sideband
  app runs there. Our vendored microReticulum, with its patches, cross-compiles
  for arm64 with the NDK unchanged (measured 2026-09-24, standalone and inside
  Interaction Combinators' APK build). It has never started on a device. LAN
  discovery needs the multicast lock that `voidmaiz_lan` already takes, and
  `learn_peers` covers the broadcast that Android drops.
- **iOS: limited by Apple, not Reticulum.** No long-running background
  networking means an iPhone cannot be a relay node, and LAN multicast needs
  Apple's restricted multicast entitlement, granted on request. **The
  practical shape:** an iPhone links over **TCP to the organization's always-on
  node** (a desktop or Raspberry Pi running a transport node). That needs no
  entitlement and no background relay, and it helps Android's background limits
  too.

# What stays outside Reticulum

- **HTTPS to web services**: publishing, object stores, image hosts, update
  feeds. Those are ordinary internet calls, and Reticulum is not the internet.
- **At-rest encryption**: Void Hormiga's credential vault and encrypted
  backups (Argon2id + XChaCha20-Poly1305). Reticulum has no file format for
  this. Where it lives is an open question for the author, recorded in Void
  Hormiga ([Q88 there](../../../VoidHormiga/okf/developer_questions.md)).

# The order of work

1. ~~**Vendor microReticulum and its dependencies as pinned source**~~ **Done
   2026-09-23.** The Python reference and our node exchange announces, open
   links and round-trip packets and Resources, both ways.
2. ~~**Session frames over a link.**~~ **Done 2026-09-24.** Two processes
   converge, over a clean loopback and through a lossy relay.
3. **Void Maiz's sockets become Reticulum interfaces**, and `voidmaiz_net`
   runs over links. `LanSession`'s unencrypted peer traffic retires. **Exit:**
   Interaction Combinators on a PC and a phone share a net over Reticulum.
   **Begun 2026-09-24:** Void Maiz's `RnsSession` is `LanSession`'s job over
   `Node`: join, a person's Allow, "allowed" kept by proven identity. Its
   `rnslink_smoke` runs host and joiners as separate processes. Interaction
   Combinators has not moved yet (Void Maiz's Q37).
4. **Void Hormiga re-implements its member sharing on it**: the sealed beacon,
   join code, pairing code and `secretstream` retire. Who may join and what
   stays private remain Hormiga's. **Exit:** two Hormiga desktops share a
   database over Reticulum, and an older member is told to update.
5. **The always-on node and phones.** **Exit:** a phone joins over TCP to a
   desktop's transport node.

**No upstream contributions** (the author's ruling, 2026-09-24): microReticulum's
defects are worked around here and recorded above. A new pin is taken by
re-running `tools/patch_reticulum.py`, which refuses loudly when a patch no
longer applies, and then the full ctest, lossy tests included.
