"""interop.py — voidpalabra_reticulum against the official Python Reticulum, and against itself.

okf/concepts/reticulum.md: compatibility is proven by running the reference, not
by our reading of its manual. Reticulum's transport is process-wide in both
implementations, so every peer is its own process; this script spawns them,
reads what they print, and gives a verdict.

    python interop.py <path to the `voidpalabra_rns_peer` binary> [test ...]

Tests (all by default):
    vectors       our destination hash for a public key == the reference's,
                  and an identity survives a restart
    py-echo       Python echo server  <- C++ client (a packet and a Resource)
    cpp-echo      C++ echo server     <- Python client (a packet and a Resource)
    big-py        Python echo server  <- C++ client, a 400 KB message
    big-cpp       C++ echo server     <- C++ client, a 400 KB message
    sync          C++ founder <-> C++ joiner: a Palabra replica converges over a
                  Reticulum link, both ways
    lossy-sync    sync, through a relay that drops 10% of datagrams and reorders
    lossy-big     a 100 KB Resource through the relay at 5% loss

VP_RNS_LOG=0..5 sets the C++ peers' log level (default 1, errors).

Exit 0 = all passed, 1 = a failure, 77 = skipped (no Python Reticulum installed;
ctest reads 77 as SKIP). Every peer talks over UDP on 127.0.0.1 only.
"""
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import threading
import time

try:
    import RNS  # noqa: F401
    HAVE_RNS = True
except Exception:
    HAVE_RNS = False

APP, ASPECTS = "voidpalabra", "sync"

# NO_BZ2: the Python reference compresses a Resource with bz2 whenever that makes
# it smaller; microReticulum has no bz2 and REJECTS a compressed Resource (it
# closes the link). Our C++ side never compresses. So the Python peers here send
# uncompressed, which is what any peer talking to a Void device must do; the
# measured limit is recorded in okf/concepts/reticulum.md.


def large_message(n):
    return bytes(ord('a') + (i * 7 + i // 251) % 26 for i in range(n))


SMALL = b"small message: one packet"
LARGE = large_message(9000)


def free_udp_port():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.bind(("127.0.0.1", 0))
    p = s.getsockname()[1]
    s.close()
    return p


# ── the Python peer (this same file, run as a child) ──────────────────────────

def write_config(configdir, listen, forward):
    os.makedirs(configdir, exist_ok=True)
    with open(os.path.join(configdir, "config"), "w") as f:
        f.write(f"""[reticulum]
  enable_transport = No
  share_instance = No
  panic_on_interface_error = Yes

[logging]
  loglevel = 1

[interfaces]
  [[Test UDP]]
    type = UDPInterface
    enabled = yes
    listen_ip = 127.0.0.1
    listen_port = {listen}
    forward_ip = 127.0.0.1
    forward_port = {forward}
""")


def python_peer(role, configdir, listen, forward, seconds):
    import RNS
    write_config(configdir, listen, forward)
    RNS.Reticulum(configdir=configdir, loglevel=RNS.LOG_ERROR)
    identity = RNS.Identity()
    say = lambda s: print(s, flush=True)
    deadline = time.time() + seconds

    if role == "echo":
        dest = RNS.Destination(identity, RNS.Destination.IN, RNS.Destination.SINGLE, APP, ASPECTS)

        def established(link):
            say("LINK " + link.link_id.hex())
            link.set_resource_strategy(RNS.Link.ACCEPT_ALL)
            link.set_packet_callback(lambda data, packet: (RNS.Packet(link, data).send(), say(f"ECHOED {len(data)}")))

            def concluded(resource):
                if resource.status == RNS.Resource.COMPLETE:
                    data = resource.data.read()
                    RNS.Resource(data, link, auto_compress=False)  # see NO_BZ2
                    say(f"ECHOED {len(data)}")
                else:
                    say("ERROR a resource did not complete")
            link.set_resource_concluded_callback(concluded)
            link.set_remote_identified_callback(lambda l, ident: say("IDENTIFIED " + ident.hexhash))

        dest.set_link_established_callback(established)
        say("DEST " + dest.hexhash)
        while time.time() < deadline:
            dest.announce(app_data=b"python-echo")
            time.sleep(1)
        say("RESULT ok")
        return 0

    if role == "client":
        heard = {}

        class Handler:
            aspect_filter = f"{APP}.{ASPECTS}"

            def received_announce(self, destination_hash, announced_identity, app_data):
                heard.setdefault("dest", (destination_hash, announced_identity))

        RNS.Transport.register_announce_handler(Handler())
        while "dest" not in heard and time.time() < deadline:
            time.sleep(0.05)
        if "dest" not in heard:
            say("RESULT fail no announce heard")
            return 1
        dh, ident = heard["dest"]
        say("ANNOUNCE " + dh.hex())
        out = RNS.Destination(ident, RNS.Destination.OUT, RNS.Destination.SINGLE, APP, ASPECTS)
        got = {}
        done = threading.Event()

        def check():
            if got.get("small") and got.get("large"):
                done.set()

        def on_packet(data, packet):
            if data == SMALL:
                got["small"] = True
            else:
                got["bad"] = f"packet of {len(data)} bytes"
            check()

        def on_resource(resource):
            data = resource.data.read() if resource.status == RNS.Resource.COMPLETE else None
            if data == LARGE:
                got["large"] = True
            else:
                got["bad"] = "resource differed or failed"
            check()

        def established(link):
            say("LINK " + link.link_id.hex())
            link.identify(identity)
            link.set_packet_callback(on_packet)
            link.set_resource_strategy(RNS.Link.ACCEPT_ALL)
            link.set_resource_concluded_callback(on_resource)
            RNS.Packet(link, SMALL).send()
            RNS.Resource(LARGE, link, auto_compress=False)  # see NO_BZ2

        RNS.Link(out, established_callback=established)
        while not done.is_set() and "bad" not in got and time.time() < deadline:
            time.sleep(0.05)
        if done.is_set():
            say("RESULT ok")
            return 0
        say("RESULT fail " + got.get("bad", "timed out waiting for echoes " + repr(got)))
        return 1
    return 2


# ── the harness ───────────────────────────────────────────────────────────────

class Proc:
    def __init__(self, name, argv):
        self.name = name
        self.lines = []
        self.stamped = []  # (seconds since start, line), for timelines
        self.t0 = time.time()
        self.p = subprocess.Popen(argv, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, bufsize=1)
        self.t = threading.Thread(target=self._read, daemon=True)
        self.t.start()

    def _read(self):
        for line in self.p.stdout:
            self.lines.append(line.rstrip("\n"))
            self.stamped.append((time.time() - self.t0, self.lines[-1]))

    def value(self, key):
        for l in self.lines:
            if l.startswith(key + " "):
                return l[len(key) + 1:]
        return None

    def wait(self, timeout):
        try:
            self.p.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            self.p.kill()
            self.p.wait()
        self.t.join(timeout=2)
        return self.p.returncode

    def kill(self):
        if self.p.poll() is None:
            self.p.kill()
            self.p.wait()
        self.t.join(timeout=2)

    def result(self):
        r = self.value("RESULT")
        return r if r is not None else f"(no verdict, exit {self.p.returncode})"


def cpp(peer, role, d, listen, forward, seconds=25, extra=()):
    return Proc("c++ " + role, [peer, role, "--dir", d, "--listen", str(listen), "--forward", str(forward),
                                "--seconds", str(seconds), "--log", os.environ.get("VP_RNS_LOG", "1"), *extra])


def py(role, d, listen, forward, seconds=25):
    return Proc("python " + role, [sys.executable, os.path.abspath(__file__), "--python-peer", role, d,
                                   str(listen), str(forward), str(seconds)])


def dump(*procs):
    for p in procs:
        print(f"  --- {p.name} (exit {p.p.returncode})")
        for l in p.lines[-int(os.environ.get("VP_DUMP", "25")):]:
            print("    " + l)


def test_vectors(peer, tmp):
    import RNS
    d = os.path.join(tmp, "vec")
    a = cpp(peer, "vector", d, 0, 0)
    a.wait(30)
    pub, dest = a.value("PUB"), a.value("DEST")
    if a.result() != "ok" or not pub or not dest:
        dump(a)
        return "the C++ node did not report its keys"
    ident = RNS.Identity(create_keys=False)
    ident.load_public_key(bytes.fromhex(pub))
    theirs = RNS.Destination.hash(ident, APP, ASPECTS).hex()
    if theirs != dest:
        return f"destination hash differs: ours {dest}, reference {theirs}"
    # the identity is the device's: a second start reads it back
    b = cpp(peer, "vector", d, 0, 0)
    b.wait(30)
    if b.value("PUB") != pub:
        dump(a, b)
        return f"the identity changed between two starts in one storage dir ({pub[:16]} then {str(b.value('PUB'))[:16]})"
    return None


def test_pair(server, client, stop_server=True, wait=60):
    client.wait(wait)
    if stop_server:
        server.kill()
    else:
        server.wait(10)
    if client.result() != "ok":
        dump(server, client)
        return f"{client.name}: {client.result()}"
    return None


def test_py_echo(peer, tmp):
    a, b = free_udp_port(), free_udp_port()
    server = py("echo", os.path.join(tmp, "py-echo-server"), a, b, 60)
    time.sleep(1.0)
    client = cpp(peer, "client", os.path.join(tmp, "cpp-client"), b, a, 45)
    return test_pair(server, client)


def test_cpp_echo(peer, tmp):
    a, b = free_udp_port(), free_udp_port()
    server = cpp(peer, "echo", os.path.join(tmp, "cpp-echo-server"), a, b, 60)
    time.sleep(0.5)
    client = py("client", os.path.join(tmp, "py-client"), b, a, 45)
    return test_pair(server, client)


BIG = int(os.environ.get("VP_BIG", "400000"))  # past one advertisement's hashmap: exercises hashmap-update packets


def test_big_py(peer, tmp):
    a, b = free_udp_port(), free_udp_port()
    server = py("echo", os.path.join(tmp, "big-py-server"), a, b, 90)
    time.sleep(1.0)
    client = cpp(peer, "client", os.path.join(tmp, "big-py-client"), b, a, 80, ("--large", str(BIG)))
    return test_pair(server, client, wait=90)


def test_big_cpp(peer, tmp):
    a, b = free_udp_port(), free_udp_port()
    server = cpp(peer, "echo", os.path.join(tmp, "big-cpp-server"), a, b, 90)
    time.sleep(0.5)
    client = cpp(peer, "client", os.path.join(tmp, "big-cpp-client"), b, a, 80, ("--large", str(BIG)))
    return test_pair(server, client, wait=90)


class LossyRelay:
    """A UDP relay between two peers on one machine that drops and reorders.

    Each peer is told to forward to the relay instead of to the other peer; the
    relay passes datagrams on, dropping `loss` of them and holding back a few
    to deliver out of order. One device cannot test a real network, but it can
    test a bad one. Seeded, so a failure replays."""

    def __init__(self, port_a, port_b, loss, seed=7):
        import random
        self.rng = random.Random(seed)
        self.loss = loss
        self.a_port, self.b_port = port_a, port_b  # where each peer listens
        self.from_a = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)  # A forwards here
        self.from_b = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)  # B forwards here
        self.from_a.bind(("127.0.0.1", 0))
        self.from_b.bind(("127.0.0.1", 0))
        self.from_a.settimeout(0.05)
        self.from_b.settimeout(0.05)
        self.relay_for_a = self.from_a.getsockname()[1]
        self.relay_for_b = self.from_b.getsockname()[1]
        self.stats = {"passed": 0, "dropped": 0, "reordered": 0}
        self.stop = threading.Event()
        self.threads = [threading.Thread(target=self._pump, args=(self.from_a, self.b_port), daemon=True),
                        threading.Thread(target=self._pump, args=(self.from_b, self.a_port), daemon=True)]
        for t in self.threads:
            t.start()

    def _pump(self, sock, to_port):
        held = None
        out = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        while not self.stop.is_set():
            try:
                data, _ = sock.recvfrom(65536)
            except socket.timeout:
                if held is not None:
                    out.sendto(held, ("127.0.0.1", to_port))
                    held = None
                continue
            except OSError:
                return
            r = self.rng.random()
            if r < self.loss:
                self.stats["dropped"] += 1
                continue
            if held is None and r < self.loss * 2:
                held = data  # goes out after the next one
                self.stats["reordered"] += 1
                continue
            out.sendto(data, ("127.0.0.1", to_port))
            self.stats["passed"] += 1
            if held is not None:
                out.sendto(held, ("127.0.0.1", to_port))
                held = None

    def close(self):
        self.stop.set()
        for t in self.threads:
            t.join(timeout=1)
        self.from_a.close()
        self.from_b.close()


LOSSY_BIG = int(os.environ.get("VP_LOSSY_BIG", "100000"))
LOSS = float(os.environ.get("VP_LOSS", "0.10"))
# a Resource at 10% loss is correct but slow (100 KB took ~80 s, measured
# 2026-09-24); 5% keeps the test inside a minute and still exercises every retry path
LOSSY_BIG_LOSS = float(os.environ.get("VP_LOSSY_BIG_LOSS", "0.05"))


def test_lossy_sync(peer, tmp):
    a, b = free_udp_port(), free_udp_port()
    relay = LossyRelay(a, b, LOSS)
    try:
        founder = cpp(peer, "sync-founder", os.path.join(tmp, "lossy-founder"), a, relay.relay_for_a, 90)
        time.sleep(0.3)
        joiner = cpp(peer, "sync-joiner", os.path.join(tmp, "lossy-joiner"), b, relay.relay_for_b, 90)
        joiner.wait(100)
        founder.wait(20)
    finally:
        relay.close()
    print(f"      relay: {relay.stats}")
    if joiner.result() != "ok" or founder.result() != "ok":
        dump(founder, joiner)
        return f"founder: {founder.result()}; joiner: {joiner.result()}"
    if founder.value("VERSION") != joiner.value("VERSION"):
        return "the two devices hold different documents"
    return None


def test_lossy_big(peer, tmp):
    a, b = free_udp_port(), free_udp_port()
    relay = LossyRelay(a, b, LOSSY_BIG_LOSS)
    try:
        server = cpp(peer, "echo", os.path.join(tmp, "lossy-big-server"), a, relay.relay_for_a, 120)
        time.sleep(0.5)
        client = cpp(peer, "client", os.path.join(tmp, "lossy-big-client"), b, relay.relay_for_b, 110,
                     ("--large", str(LOSSY_BIG), "--lossy", "1"))
        why = test_pair(server, client, wait=120)
    finally:
        relay.close()
    print(f"      relay: {relay.stats}")
    return why


def test_sync(peer, tmp):
    a, b = free_udp_port(), free_udp_port()
    founder = cpp(peer, "sync-founder", os.path.join(tmp, "founder"), a, b, 60)
    time.sleep(0.3)
    joiner = cpp(peer, "sync-joiner", os.path.join(tmp, "joiner"), b, a, 60)
    rj = joiner.wait(70)
    rf = founder.wait(20)
    if joiner.result() != "ok" or founder.result() != "ok":
        dump(founder, joiner)
        return f"founder: {founder.result()}; joiner: {joiner.result()}"
    vf, vj = founder.value("VERSION"), joiner.value("VERSION")
    if vf != vj:
        dump(founder, joiner)
        return f"the two devices hold different documents: {vf} vs {vj}"
    if founder.value("RUNES") != "3":
        return f"the founder holds {founder.value('RUNES')} runes, not 3"
    return None


TESTS = {"vectors": test_vectors, "py-echo": test_py_echo, "cpp-echo": test_cpp_echo, "sync": test_sync,
         "big-py": test_big_py, "big-cpp": test_big_cpp, "lossy-sync": test_lossy_sync,
         "lossy-big": test_lossy_big}
NEEDS_RNS = {"vectors", "py-echo", "cpp-echo", "big-py"}


def main(argv):
    if len(argv) >= 2 and argv[1] == "--python-peer":
        role, d, listen, forward, seconds = argv[2], argv[3], int(argv[4]), int(argv[5]), int(argv[6])
        return python_peer(role, d, listen, forward, seconds)
    if len(argv) < 2:
        print(__doc__)
        return 2
    peer = os.path.abspath(argv[1])
    wanted = argv[2:] or list(TESTS)
    if not HAVE_RNS and all(t in NEEDS_RNS for t in wanted):
        print("SKIP: the Python Reticulum reference is not installed (pip install rns)")
        return 77
    tmp = tempfile.mkdtemp(prefix="vp-rns-")
    failures = 0
    try:
        for name in wanted:
            if name in NEEDS_RNS and not HAVE_RNS:
                print(f"skip  {name} (no Python Reticulum)")
                continue
            t0 = time.time()
            why = TESTS[name](peer, tmp)
            dt = time.time() - t0
            if why:
                failures += 1
                print(f"FAIL  {name} ({dt:.1f}s): {why}")
            else:
                print(f"ok    {name} ({dt:.1f}s)")
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
