# A stand-in for a project that embeds Void Palabra — Void Maiz's networking
# module, or any application. Built by tests/embed/run.cmake, never by hand.
cmake_minimum_required(VERSION 3.20)
project(palabra_embed_check CXX C)
enable_testing()

add_subdirectory(${PALABRA_SOURCE} palabra)

# An embedding project gets the library and nothing else. A test target leaking
# out of the subdirectory is the defect this check exists for.
if(TARGET voidpalabra_test OR TARGET voidpalabra_conformance)
  message(FATAL_ERROR "Void Palabra built its own tests inside an embedding project")
endif()
if(NOT TARGET voidpalabra::voidpalabra)
  message(FATAL_ERROR "the voidpalabra::voidpalabra target is missing")
endif()

add_executable(embed_app main.cpp)
target_link_libraries(embed_app PRIVATE voidpalabra::voidpalabra)

# With VOIDPALABRA_VENDOR_CJSON=OFF, this project plays the part of Void Maiz: it
# brings its own cJSON, and Palabra must link against it rather than a second copy.
if(NOT VOIDPALABRA_VENDOR_CJSON)
  add_library(host_cjson STATIC ${PALABRA_SOURCE}/vendor/cJSON.c)
  target_compile_definitions(host_cjson PRIVATE CJSON_HIDE_SYMBOLS)
  target_link_libraries(embed_app PRIVATE host_cjson)
endif()

add_test(NAME embed_app COMMAND embed_app)
