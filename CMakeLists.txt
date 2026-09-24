cmake_minimum_required(VERSION 3.20)
# VERSION is the number void.json publishes; keeping it here means the manifest
# reports something the build actually knows rather than a claim only a JSON file
# makes. 0.1.0 is the first public release: four rungs built, no C ABI yet, and
# no stable API promise.
project(voidpalabra VERSION 0.1.0 LANGUAGES CXX C) # C for the vendored cJSON

# ── Consumed two ways, and both must work ───────────────────────────────────
#
# Top level: this repo's own build — the library, every test suite, conformance.
#
# Embedded: another project writes `add_subdirectory(${VOIDPALABRA_ROOT} ...)` and
# links the `voidpalabra` target. It gets the library and NOTHING else. Before
# 2026-09-18 an embedding project built all thirteen of our test programs and
# inherited all twelve of our tests into its own ctest — two of which failed there,
# because they located files relative to the CONSUMER's source directory. A
# library whose adoption turns the adopter's CI red has not been adopted.
#
# The other way siblings have consumed each other — copying a list of source files
# into their own CMakeLists — is the one this section exists to retire. It drifts
# silently: Void Maiz's list of Void Core sources went months without the journal,
# and a consumer of this library had to be told by message to add two new files.
# A target moves with the library; a file list does not.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  set(VOIDPALABRA_TOP_LEVEL ON)
else()
  set(VOIDPALABRA_TOP_LEVEL OFF)
endif()

option(VOIDPALABRA_BUILD_TESTS
       "Build Void Palabra's tests and conformance runner" ${VOIDPALABRA_TOP_LEVEL})

# One cJSON per process. Void Core, Void Maiz and this library each vendor the same
# cJSON, and an application that links all three statically holds three copies of
# every cJSON symbol. OFF compiles against our cJSON.h (1.7.x — the ABI all three
# share) but leaves the implementation to whatever the consumer already links.
option(VOIDPALABRA_VENDOR_CJSON "Compile the vendored cJSON implementation" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/bin)

# Void Palabra — the system layer for Void Core. A library, not an application.
#
# Zero dependencies, and that is a design constraint rather than an accident:
# okf/concepts/peer-and-tier.md makes an ESP32 a full peer, so every line here
# has to be reachable on a device with no package manager. cJSON is vendored
# whole (as Void Maiz and Void Core do) because the input to the canonical form
# is the tree those projects already hold.

# Modular by concern, hexagonal in the same spirit Void Core is: the encoding
# layer knows nothing about runes, the bindings know nothing about CRDTs, and the
# CRDT layer knows nothing about files.
add_library(voidpalabra STATIC
  src/encoding/encode.cpp
  src/encoding/decode.cpp
  src/encoding/sha256.cpp
  src/canonical/bindings.cpp
  src/canonical/references.cpp
  src/canonical/links.cpp
  src/crdt/orset.cpp
  src/crdt/policy.cpp
  src/crdt/document.cpp
  src/crdt/conflict.cpp
  src/crdt/sequence.cpp
  src/crdt/validate.cpp
  src/crdt/replica.cpp
  src/crdt/anomaly.cpp
  src/crdt/provenance.cpp
  src/store/chunk.cpp
  src/store/blocks.cpp
  src/store/container.cpp
  src/utterance/journal.cpp
  src/utterance/utterance.cpp
  src/utterance/history.cpp
  src/sync/export.cpp
  src/sync/wire.cpp
  src/sync/session.cpp
  src/archive/archive.cpp
  src/archive/miga.cpp
)
if(VOIDPALABRA_VENDOR_CJSON)
  target_sources(voidpalabra PRIVATE vendor/cJSON.c)
endif()
add_library(voidpalabra::voidpalabra ALIAS voidpalabra)
# The standard travels with the target, not with a directory variable an embedding
# project never sees.
target_compile_features(voidpalabra PUBLIC cxx_std_20)
target_include_directories(voidpalabra PRIVATE src src/encoding src/crdt src/store src/utterance)
target_include_directories(voidpalabra
  PUBLIC  include
  PRIVATE vendor
)
# CJSON_HIDE_SYMBOLS: mirror Void Core and Void Maiz — cJSON stays internal.
target_compile_definitions(voidpalabra PRIVATE CJSON_HIDE_SYMBOLS)
if(NOT MSVC)
  target_compile_options(voidpalabra PRIVATE -Wall -Wextra)
endif()

# A consumer that already vendors cJSON (Maiz does) should link its own copy and
# drop ours; see README.md. Nothing in the public header mentions cJSON beyond a
# forward declaration, so the two never conflict at the interface.

# ── voidpalabra_reticulum: the family's network (okf/concepts/reticulum.md) ──
#
# Decided by the author 2026-09-23: all device-to-device networking runs over
# Reticulum, with Palabra as its Void translation. It is an OPTIONAL companion:
# the `voidpalabra` target above does not link it and stays zero-dependency.
# microReticulum and its dependencies are vendored as pinned source with our
# patches (vendor/reticulum/VENDORED.md, tools/patch_reticulum.py). Nothing is
# fetched at build time.
option(VOIDPALABRA_RETICULUM
       "Build voidpalabra_reticulum (Reticulum networking, vendored microReticulum)" ${VOIDPALABRA_TOP_LEVEL})
if(VOIDPALABRA_RETICULUM)
  set(VPR ${CMAKE_CURRENT_SOURCE_DIR}/vendor/reticulum)
  file(GLOB VPR_CRYPTO ${VPR}/Crypto/*.cpp)
  file(GLOB_RECURSE VPR_RNS ${VPR}/microReticulum/src/*.cpp ${VPR}/microReticulum/src/*.c)
  add_library(voidpalabra_rns_vendor STATIC ${VPR_CRYPTO} ${VPR_RNS})
  # microReticulum is C++17 upstream; it is compiled as such, and only our own
  # code around it is C++20.
  set_target_properties(voidpalabra_rns_vendor PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
  target_include_directories(voidpalabra_rns_vendor SYSTEM PUBLIC
    ${VPR}/microReticulum/src ${VPR}/Crypto ${VPR}/ArduinoJson/src ${VPR}/MsgPack
    ${VPR}/ArxContainer ${VPR}/ArxTypeTraits ${VPR}/DebugLog ${VPR}/microStore/include)
  # PUBLIC, as upstream's CMake does, so every translation unit that includes
  # its headers agrees on its types.
  #
  # RNS_PERSIST_PATHS / _KNOWN_DESTINATIONS / _HASHLIST are deliberately 0
  # (upstream's Type.h turns each ON when it is left undefined, so it must be
  # written out as 0).
  # With them on, microReticulum initializes its file-backed stores only when
  # TRANSPORT mode is enabled (Transport.cpp), so an ordinary node - an app, not
  # a router - has uninitialized stores, cannot remember any identity it hears
  # ("remember: failed to store identity") and can never open a link. Found
  # 2026-09-23 against the Python reference. Off, the stores are in memory:
  # paths and known peers are re-learned from announces after a restart, and our
  # own identity file (Node::start) persists regardless.
  # The most detail microReticulum's log CAN print (0 none .. 6 verbose, 7 debug,
  # 8 trace), fixed at compile time; Options::log_level picks within it at run
  # time. Set explicitly, since upstream #warns in every file that includes its
  # log header otherwise. 7 is how the lost-proof stall was found (2026-09-24).
  set(VOIDPALABRA_RNS_LOG_LEVEL 6 CACHE STRING "microReticulum's compiled-in log ceiling (0-9)")
  target_compile_definitions(voidpalabra_rns_vendor PUBLIC NATIVE USTORE_USE_UNIVERSALFS RNS_USE_FS
    RNS_PERSIST_PATHS=0 RNS_PERSIST_KNOWN_DESTINATIONS=0 RNS_PERSIST_HASHLIST=0
    RNS_LOG_LEVEL=${VOIDPALABRA_RNS_LOG_LEVEL})
  if(NOT MSVC)
    target_compile_options(voidpalabra_rns_vendor PRIVATE -w) # third-party: its warnings are not ours to fix
  endif()
  if(WIN32)
    target_link_libraries(voidpalabra_rns_vendor PUBLIC ws2_32)
  endif()

  add_library(voidpalabra_reticulum STATIC src/reticulum/node.cpp src/reticulum/udp.cpp
    src/reticulum/sync_links.cpp)
  add_library(voidpalabra::reticulum ALIAS voidpalabra_reticulum)
  target_include_directories(voidpalabra_reticulum PUBLIC include PRIVATE src/reticulum)
  target_link_libraries(voidpalabra_reticulum PUBLIC voidpalabra PRIVATE voidpalabra_rns_vendor)
  target_compile_features(voidpalabra_reticulum PUBLIC cxx_std_20)
endif()

if(NOT VOIDPALABRA_BUILD_TESTS)
  return()
endif()

enable_testing()
add_executable(voidpalabra_test tests/canonical_test.cpp)
target_include_directories(voidpalabra_test PRIVATE vendor)
target_link_libraries(voidpalabra_test PRIVATE voidpalabra)
if(NOT MSVC)
  target_compile_options(voidpalabra_test PRIVATE -Wall -Wextra)
endif()
add_test(NAME canonical COMMAND voidpalabra_test)

add_executable(voidpalabra_join_test tests/join_test.cpp)
target_include_directories(voidpalabra_join_test PRIVATE vendor)
target_link_libraries(voidpalabra_join_test PRIVATE voidpalabra)
if(NOT MSVC)
  target_compile_options(voidpalabra_join_test PRIVATE -Wall -Wextra)
endif()
add_test(NAME join COMMAND voidpalabra_join_test)

add_executable(voidpalabra_sequence_test tests/sequence_test.cpp)
target_include_directories(voidpalabra_sequence_test PRIVATE vendor)
target_link_libraries(voidpalabra_sequence_test PRIVATE voidpalabra)
if(NOT MSVC)
  target_compile_options(voidpalabra_sequence_test PRIVATE -Wall -Wextra)
endif()
add_test(NAME sequence COMMAND voidpalabra_sequence_test)

add_executable(voidpalabra_store_test tests/store_test.cpp)
target_include_directories(voidpalabra_store_test PRIVATE vendor)
target_link_libraries(voidpalabra_store_test PRIVATE voidpalabra)
if(NOT MSVC)
  target_compile_options(voidpalabra_store_test PRIVATE -Wall -Wextra)
endif()
add_test(NAME store COMMAND voidpalabra_store_test)

add_executable(voidpalabra_archive_test tests/archive_test.cpp)
target_include_directories(voidpalabra_archive_test PRIVATE vendor)
target_link_libraries(voidpalabra_archive_test PRIVATE voidpalabra)
if(NOT MSVC)
  target_compile_options(voidpalabra_archive_test PRIVATE -Wall -Wextra)
endif()
add_test(NAME archive COMMAND voidpalabra_archive_test)

add_executable(voidpalabra_glyphs_test tests/glyphs_test.cpp)
target_include_directories(voidpalabra_glyphs_test PRIVATE vendor)
target_link_libraries(voidpalabra_glyphs_test PRIVATE voidpalabra)
if(NOT MSVC)
  target_compile_options(voidpalabra_glyphs_test PRIVATE -Wall -Wextra)
endif()
add_test(NAME glyphs COMMAND voidpalabra_glyphs_test)

add_executable(voidpalabra_replica_test tests/replica_test.cpp)
target_include_directories(voidpalabra_replica_test PRIVATE vendor)
target_link_libraries(voidpalabra_replica_test PRIVATE voidpalabra)
if(NOT MSVC)
  target_compile_options(voidpalabra_replica_test PRIVATE -Wall -Wextra)
endif()
add_test(NAME replica COMMAND voidpalabra_replica_test)

add_executable(voidpalabra_links_test tests/links_test.cpp)
target_include_directories(voidpalabra_links_test PRIVATE vendor)
target_link_libraries(voidpalabra_links_test PRIVATE voidpalabra)
if(NOT MSVC)
  target_compile_options(voidpalabra_links_test PRIVATE -Wall -Wextra)
endif()
add_test(NAME links COMMAND voidpalabra_links_test)

add_executable(voidpalabra_hostile_test tests/hostile_test.cpp)
target_include_directories(voidpalabra_hostile_test PRIVATE vendor)
target_link_libraries(voidpalabra_hostile_test PRIVATE voidpalabra)
if(NOT MSVC)
  target_compile_options(voidpalabra_hostile_test PRIVATE -Wall -Wextra)
endif()
add_test(NAME hostile COMMAND voidpalabra_hostile_test)

add_executable(voidpalabra_merge_rules_test tests/merge_rules_test.cpp)
target_include_directories(voidpalabra_merge_rules_test PRIVATE vendor)
target_link_libraries(voidpalabra_merge_rules_test PRIVATE voidpalabra)
if(NOT MSVC)
  target_compile_options(voidpalabra_merge_rules_test PRIVATE -Wall -Wextra)
endif()
add_test(NAME merge_rules COMMAND voidpalabra_merge_rules_test)

add_executable(voidpalabra_sync_test tests/sync_test.cpp)
target_include_directories(voidpalabra_sync_test PRIVATE vendor)
target_link_libraries(voidpalabra_sync_test PRIVATE voidpalabra)
if(NOT MSVC)
  target_compile_options(voidpalabra_sync_test PRIVATE -Wall -Wextra)
endif()
add_test(NAME sync COMMAND voidpalabra_sync_test)
set_tests_properties(sync PROPERTIES TIMEOUT 600)

add_executable(voidpalabra_utterance_test tests/utterance_test.cpp)
target_include_directories(voidpalabra_utterance_test PRIVATE vendor)
target_link_libraries(voidpalabra_utterance_test PRIVATE voidpalabra)
if(NOT MSVC)
  target_compile_options(voidpalabra_utterance_test PRIVATE -Wall -Wextra)
endif()
add_test(NAME utterance COMMAND voidpalabra_utterance_test)

# Allocation-balance check. Not redundant with a sanitizer: the MinGW/ucrt64
# toolchain has no working ASan, so -fsanitize=address silently builds nothing.
add_executable(voidpalabra_leak_test tests/leak_test.cpp)
target_include_directories(voidpalabra_leak_test PRIVATE vendor)
target_link_libraries(voidpalabra_leak_test PRIVATE voidpalabra)
if(NOT MSVC)
  target_compile_options(voidpalabra_leak_test PRIVATE -Wall -Wextra)
endif()
add_test(NAME leaks COMMAND voidpalabra_leak_test)

# The conformance suite. SPEC.md is a contract only because this exists: pure-JSON
# vectors any implementation can be checked against, in the shape Void Core's
# conformance/reduce/ proved. Run from the repo root so the case paths resolve.
add_executable(voidpalabra_conformance conformance/run.cpp)
target_include_directories(voidpalabra_conformance PRIVATE vendor)
target_link_libraries(voidpalabra_conformance PRIVATE voidpalabra)
if(NOT MSVC)
  target_compile_options(voidpalabra_conformance PRIVATE -Wall -Wextra)
endif()
add_test(NAME conformance COMMAND voidpalabra_conformance
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

# The regenerator for conformance/cases/14-linear-extension.json, whose utterances
# name each other by hash and therefore cannot be hand-written or `--regen`ed.
#
# EXCLUDE_FROM_ALL and no add_test, deliberately: it WRITES a contract rather than
# checking one. A regenerator that runs as part of the ordinary build is how a
# conformance suite quietly starts agreeing with whatever the implementation
# happens to do today. Build it on purpose when CANON_VERSION moves:
#   cmake --build build --target gen_linear_vectors
add_executable(gen_linear_vectors EXCLUDE_FROM_ALL tools/gen_linear_vectors.cpp)
target_include_directories(gen_linear_vectors PRIVATE vendor)
target_link_libraries(gen_linear_vectors PRIVATE voidpalabra)

# Embedding, checked rather than assumed: a fresh project pulls this repo in with
# add_subdirectory, once with our cJSON and once bringing its own, and must build,
# link, hold only its own test, and pass it. It is slow (it builds the library
# twice more), and it is here because the property it checks broke without anyone
# noticing — see the top of this file.
add_test(NAME embed_vendored
         COMMAND ${CMAKE_COMMAND} -DPALABRA_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}
                 -DWORK=${CMAKE_CURRENT_BINARY_DIR} -DVENDOR=ON
                 -DGENERATOR=${CMAKE_GENERATOR}
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/embed/run.cmake)
add_test(NAME embed_host_cjson
         COMMAND ${CMAKE_COMMAND} -DPALABRA_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}
                 -DWORK=${CMAKE_CURRENT_BINARY_DIR} -DVENDOR=OFF
                 -DGENERATOR=${CMAKE_GENERATOR}
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/embed/run.cmake)
set_tests_properties(embed_vendored embed_host_cjson PROPERTIES TIMEOUT 600)

# The OKF honesty convention, made enforceable. A 2026-08-21 refactor moved every
# source file and left all four `resource:` links dangling while the bundle still
# claimed `status:current` - the convention is only a guarantee if something checks
# it. Skipped silently when no Python is present; this is dev tooling, and the
# library itself stays zero-dependency.
find_program(PALABRA_PYTHON NAMES python python3)
if(PALABRA_PYTHON)
  add_test(NAME okf COMMAND ${PALABRA_PYTHON} ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_okf.py
           WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
endif()

# ── Reticulum: the peer program and the interop harness ─────────────────────
# One Reticulum per process (okf/concepts/reticulum.md), so the two-peer tests
# are processes driven by a Python harness, which can also run the official
# Python Reticulum as the other peer. Without `pip install rns`, the tests that
# need it report SKIP (exit 77) rather than fail; the C++-to-C++ sync test
# needs only Python itself.
if(VOIDPALABRA_RETICULUM AND VOIDPALABRA_BUILD_TESTS)
  add_executable(voidpalabra_rns_peer tests/reticulum/peer.cpp)
  target_include_directories(voidpalabra_rns_peer PRIVATE vendor)
  target_link_libraries(voidpalabra_rns_peer PRIVATE voidpalabra_reticulum)
  # A harness starts this program with ITS PATH, and on Windows that PATH can
  # hold another MinGW's libstdc++ (Git ships one): the child then dies at load
  # with STATUS_ENTRYPOINT_NOT_FOUND (0xC0000139), which is what the first run
  # did. A static runtime has nothing to mismatch.
  if(MINGW)
    target_link_options(voidpalabra_rns_peer PRIVATE -static)
  endif()
  find_package(Python3 COMPONENTS Interpreter)
  if(Python3_Interpreter_FOUND)
    add_test(NAME reticulum_vectors COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/reticulum/interop.py $<TARGET_FILE:voidpalabra_rns_peer> vectors)
    add_test(NAME reticulum_interop_py_server COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/reticulum/interop.py $<TARGET_FILE:voidpalabra_rns_peer> py-echo)
    add_test(NAME reticulum_interop_cpp_server COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/reticulum/interop.py $<TARGET_FILE:voidpalabra_rns_peer> cpp-echo)
    add_test(NAME reticulum_sync COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/reticulum/interop.py $<TARGET_FILE:voidpalabra_rns_peer> sync)
    add_test(NAME reticulum_big_py COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/reticulum/interop.py $<TARGET_FILE:voidpalabra_rns_peer> big-py)
    add_test(NAME reticulum_big_cpp COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/reticulum/interop.py $<TARGET_FILE:voidpalabra_rns_peer> big-cpp)
    add_test(NAME reticulum_lossy_sync COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/reticulum/interop.py $<TARGET_FILE:voidpalabra_rns_peer> lossy-sync)
    add_test(NAME reticulum_lossy_big COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/reticulum/interop.py $<TARGET_FILE:voidpalabra_rns_peer> lossy-big)
    set_tests_properties(reticulum_vectors reticulum_interop_py_server reticulum_interop_cpp_server
      reticulum_big_py PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 150)
    set_tests_properties(reticulum_sync reticulum_big_cpp PROPERTIES TIMEOUT 150)
    # through a relay that drops and reorders datagrams: one device cannot test a
    # real network, but it can test a bad one (tests/reticulum/interop.py)
    set_tests_properties(reticulum_lossy_sync reticulum_lossy_big PROPERTIES TIMEOUT 200 LABELS lossy)
  endif()
endif()

