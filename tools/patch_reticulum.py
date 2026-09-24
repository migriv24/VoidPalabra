"""patch_reticulum.py — apply Void Palabra's patches to the vendored Reticulum.

The vendored copies under vendor/reticulum/ are pinned upstream source
(vendor/reticulum/VENDORED.md) plus these patches, and nothing else. The author
ruled on 2026-09-23 that we do not ask microReticulum's author for changes: we
work around its bugs here. So every patch lives in this one script. It is
idempotent (a patch already applied is skipped), and it refuses loudly when the
text it expects has moved, which is how an updated pin announces that a patch
needs re-reading.

Every patched line carries the marker `VOIDPALABRA PATCH`, so
`grep -rn "VOIDPALABRA PATCH" vendor/reticulum` lists them all.

    python tools/patch_reticulum.py
"""
import os
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'vendor', 'reticulum')

# (file, the text upstream has, the text we want, why)
PATCHES = [
    ('microReticulum/src/microReticulum/Utilities/Memory.h',
     '#pragma once\n',
     '#pragma once\n\n#include <cstdint> // VOIDPALABRA PATCH (windows): uint*_t were used without it; glibc headers leaked it in\n',
     'Memory.h uses uint32_t and friends without including <cstdint>; it compiled '
     'on Linux only because another header included it transitively.'),
    ('microReticulum/src/microReticulum/Log.cpp',
     '\tstruct tm* tm = localtime(&tv.tv_sec);\n',
     '\ttime_t secs = (time_t)tv.tv_sec; // VOIDPALABRA PATCH (windows): tv_sec is a 32-bit long there, time_t is 64-bit\n'
     '\tstruct tm* tm = localtime(&secs);\n',
     'timeval::tv_sec is `long` (32 bits on Windows) and was passed where a time_t* is expected.'),
    ('microStore/include/microStore/Adapters/PosixFileSystem.h',
     '#include <sys/ioctl.h>\n',
     '#ifndef _WIN32 // VOIDPALABRA PATCH (windows): no sys/ioctl.h, mkdir takes no mode, fsync is _commit\n'
     '#include <sys/ioctl.h>\n'
     '#define VP_MKDIR(p, m) ::mkdir(p, m)\n'
     '#else\n'
     '#include <direct.h>\n'
     '#include <io.h>\n'
     '#define VP_MKDIR(p, m) ::_mkdir(p)\n'
     '#define fsync _commit\n'
     '#endif\n',
     'POSIX-only header and calls in the filesystem adapter.'),
    ('microReticulum/src/microReticulum/Resource.h',
     '\t\tType::Resource::status status() const;\n',
     '\t\tType::Resource::status status() const;\n'
     '\t\t// VOIDPALABRA PATCH (api): which link a resource belongs to (defined in Resource.cpp,\n'
     '\t\t// where the data object is complete).\n'
     '\t\tconst Link& link() const;\n',
     'The resource-concluded callback receives only the Resource, and the Resource '
     'exposed no link, so a host with several links could not tell where a large '
     'message came from.'),
    ('microReticulum/src/microReticulum/Resource.cpp',
     'Type::Resource::status Resource::status() const {\n',
     '// VOIDPALABRA PATCH (api): see Resource.h\n'
     'const Link& Resource::link() const {\n'
     '\tassert(_object);\n'
     '\treturn _object->_link;\n'
     '}\n'
     '\n'
     'Type::Resource::status Resource::status() const {\n',
     'The accessor declared in Resource.h, defined where ResourceData is complete.'),
    ('microStore/include/microStore/Adapters/PosixFileSystem.h',
     '\t\t\tint fd = ::open(path, flags, 0644);\n',
     '#ifdef _WIN32\n'
     '\t\t\t// VOIDPALABRA PATCH (windows): BINARY. Without O_BINARY the C runtime opens\n'
     '\t\t\t// in text mode: a 0x0A written becomes CR LF and a 0x1A read ends the file, so\n'
     '\t\t\t// key files came back corrupted depending on the random bytes in them\n'
     '\t\t\t// (found 2026-09-23: an identity whose Ed25519 half changed between runs).\n'
     '\t\t\tflags |= O_BINARY;\n'
     '#endif\n'
     '\t\t\tint fd = ::open(path, flags, 0644);\n',
     'Windows opens files in text mode by default, which corrupts binary key files.'),
    ('microStore/include/microStore/Adapters/PosixFileSystem.h',
     'return (::mkdir(path, 0700) == 0);',
     'return (VP_MKDIR(path, 0700) == 0); // VOIDPALABRA PATCH (windows)',
     'mkdir with a mode argument is POSIX.'),
    ('microReticulum/src/microReticulum/Transport.cpp',
     '\t\t\t\t\tif (link.status() != Type::Link::CLOSED) {\n'
     '\t\t\t\t\t\tlink.tick_resources();\n'
     '\t\t\t\t\t}\n',
     '\t\t\t\t\tif (link.status() != Type::Link::CLOSED) {\n'
     '\t\t\t\t\t\t// VOIDPALABRA PATCH (deadlock): a resource watchdog that re-requests a\n'
     '\t\t\t\t\t\t// lost part SENDS, and Transport::outbound spin-waits until\n'
     '\t\t\t\t\t\t// _jobs_running clears - which, on one thread, is never. Release it\n'
     '\t\t\t\t\t\t// around the pump (outbound takes and frees its own lock).\n'
     '\t\t\t\t\t\t_jobs_running = false;\n'
     '\t\t\t\t\t\tlink.tick_resources();\n'
     '\t\t\t\t\t\t_jobs_running = true;\n'
     '\t\t\t\t\t}\n',
     'Transport::jobs() pumps resource watchdogs while flagged as running; a watchdog '
     'that sends (a re-request after a lost part) deadlocks in Transport::outbound. '
     'Only under packet loss, so only a lossy test finds it (2026-09-23).'),
]


def main() -> int:
    bad = 0
    for rel, old, new, why in PATCHES:
        path = os.path.normpath(os.path.join(ROOT, rel))
        with open(path, encoding='utf-8', newline='') as f:
            text = f.read()
        # a checkout may have CRLF line endings (git's autocrlf); speak the file's own
        if '\r\n' in text:
            old, new = old.replace('\n', '\r\n'), new.replace('\n', '\r\n')
        if new in text:
            print(f'already applied  {rel}')
            continue
        if old not in text:
            print(f'CANNOT APPLY     {rel}\n  expected text not found; upstream moved. Why it exists: {why}')
            bad += 1
            continue
        text = text.replace(old, new, 1)
        with open(path, 'w', encoding='utf-8', newline='') as f:
            f.write(text)
        print(f'applied          {rel}')
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
