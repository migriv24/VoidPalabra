# run.cmake — embed Void Palabra in a fresh project and check what the host gets.
#
#   cmake -DPALABRA_SOURCE=<repo> -DWORK=<dir> -DVENDOR=ON|OFF -P run.cmake
#
# Configures tests/embed as a separate top-level project, builds it, and runs ITS
# tests. Passing means: the library builds when embedded, no test of ours leaks into
# the host, the host's ctest holds only the host's test, and — with VENDOR=OFF — the
# library links against a cJSON the host brought rather than its own.

set(src "${PALABRA_SOURCE}/tests/embed")
set(bin "${WORK}/embed-${VENDOR}")
file(REMOVE_RECURSE "${bin}")

execute_process(
  COMMAND ${CMAKE_COMMAND} -S "${src}" -B "${bin}" -G "${GENERATOR}"
          "-DPALABRA_SOURCE=${PALABRA_SOURCE}" "-DVOIDPALABRA_VENDOR_CJSON=${VENDOR}"
  RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "embedding project did not configure:\n${out}\n${err}")
endif()

execute_process(COMMAND ${CMAKE_COMMAND} --build "${bin}"
                RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "embedding project did not build:\n${out}\n${err}")
endif()

execute_process(COMMAND ${CMAKE_CTEST_COMMAND} --test-dir "${bin}" -N
                RESULT_VARIABLE rc OUTPUT_VARIABLE listing)
string(REGEX MATCH "Total Tests: ([0-9]+)" _ "${listing}")
if(NOT CMAKE_MATCH_1 STREQUAL "1")
  message(FATAL_ERROR "the host's ctest holds ${CMAKE_MATCH_1} tests, not its own 1:\n${listing}")
endif()

execute_process(COMMAND ${CMAKE_CTEST_COMMAND} --test-dir "${bin}" --output-on-failure
                RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "the embedding project's own test failed:\n${out}\n${err}")
endif()
message(STATUS "embedded with VOIDPALABRA_VENDOR_CJSON=${VENDOR}: ok")
