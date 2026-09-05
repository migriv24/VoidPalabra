cmake_minimum_required(VERSION 3.20)
# VERSION is the number void.json publishes; keeping it here means the manifest
# reports something the build actually knows rather than a claim only a JSON file
# makes. 0.1.0 is the first public release: four rungs built, no C ABI yet, and
# no stable API promise.
project(voidpalabra VERSION 0.1.0 LANGUAGES CXX C) # C for the vendored cJSON

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Void Palabra — the system layer for Void Core. A library, not an application.
#
# BUILT SO FAR: Rung 0 only, the canonical form. Everything else in okf/ is
# status:planned; see okf/roadmap.md.
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
  src/crdt/orset.cpp
  src/crdt/policy.cpp
  src/crdt/document.cpp
  src/crdt/conflict.cpp
  src/crdt/sequence.cpp
  src/store/chunk.cpp
  src/store/blocks.cpp
  src/store/container.cpp
  src/utterance/journal.cpp
  src/utterance/utterance.cpp
  src/utterance/history.cpp
  src/archive/archive.cpp
  src/archive/miga.cpp
  vendor/cJSON.c
)
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
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

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

# The OKF honesty convention, made enforceable. A 2026-08-21 refactor moved every
# source file and left all four `resource:` links dangling while the bundle still
# claimed `status:current` - the convention is only a guarantee if something checks
# it. Skipped silently when no Python is present; this is dev tooling, and the
# library itself stays zero-dependency.
find_program(PALABRA_PYTHON NAMES python python3)
if(PALABRA_PYTHON)
  add_test(NAME okf COMMAND ${PALABRA_PYTHON} ${CMAKE_SOURCE_DIR}/tools/check_okf.py
           WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
endif()
