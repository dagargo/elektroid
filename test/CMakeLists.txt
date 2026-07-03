#
# test/CMakeLists.txt - CUnit test programs + integration scripts.
# Mirrors test/Makefile.am (check_PROGRAMS and TESTS).
#
pkg_check_modules(CUNIT REQUIRED IMPORTED_TARGET cunit)

set(SRC "${PROJECT_SOURCE_DIR}/src")

# BE_SOURCES / AUDIO_SOURCES, selected like the test/Makefile.am conditionals.
if(ELEKTROID_RTMIDI)
  set(BE_SRC "${SRC}/backend_rtmidi.c")
else()
  set(BE_SRC "${SRC}/backend_alsa.c")
endif()
if(ELEKTROID_RTAUDIO)
  set(AUDIO_SRC "${SRC}/audio_rtaudio.c")
else()
  set(AUDIO_SRC "${SRC}/audio_pa.c")
endif()

# tests_LIBS = glib-2.0 json-glib-1.0 cunit libzip zlib $(BE_LIBS) rubberband
#
# SNDFILE + SAMPLERATE are added to every test target even though the autotools
# Makefile only lists them for some. utils.c/sample.c (compiled into nearly all
# of the test programs) transitively include <sndfile.h>; the autotools build
# gets away without the flags on the lean targets only because on Linux the
# header sits in a default include path. On macOS (Homebrew) and Windows
# (vcpkg) the dependency lives in a non-default prefix, so the include dirs
# must be provided explicitly for the build to be portable.
set(TEST_BASE_LIBS
  PkgConfig::GLIB
  PkgConfig::JSONGLIB
  PkgConfig::CUNIT
  PkgConfig::LIBZIP
  PkgConfig::ZLIBPC
  ${ELEKTROID_MIDI_TARGET}
  PkgConfig::RUBBERBAND
  PkgConfig::SNDFILE
  PkgConfig::SAMPLERATE)
if(NOT WIN32)
  list(APPEND TEST_BASE_LIBS m)
endif()

function(elektroid_test name)
  cmake_parse_arguments(T "" "" "SOURCES;LIBS" ${ARGN})
  add_executable(${name} ${T_SOURCES})
  target_include_directories(${name} PRIVATE ${SRC} ${CMAKE_CURRENT_BINARY_DIR})
  target_compile_definitions(${name} PRIVATE
    TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/res"
    DATADIR="../res")
  target_link_libraries(${name} PRIVATE ${TEST_BASE_LIBS} ${T_LIBS})
  if(NOT MSVC)
    target_compile_options(${name} PRIVATE -Wall)
  endif()
  if(WIN32)
    target_link_libraries(${name} PRIVATE ws2_32)
  endif()
  add_test(NAME ${name} COMMAND ${name}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
endfunction()

elektroid_test(tests_scala
  SOURCES tests_scala.c ${SRC}/utils.c ${SRC}/connectors/scala.c)

elektroid_test(tests_common
  SOURCES tests_common.c ${SRC}/utils.c ${SRC}/preferences.c ${SRC}/backend.c
          ${SRC}/connector.c ${SRC}/sample.c ${BE_SRC}
          ${SRC}/connectors/common.c ${SRC}/connectors/scala.c
          ${SRC}/connectors/microfreak_sample.c)

elektroid_test(tests_microfreak
  SOURCES tests_microfreak.c ${SRC}/utils.c ${SRC}/preferences.c ${SRC}/backend.c
          ${SRC}/connector.c ${SRC}/sample.c ${BE_SRC}
          ${SRC}/connectors/common.c ${SRC}/connectors/microfreak_sample.c
          ${SRC}/connectors/scala.c ${SRC}/connectors/microfreak.c)

elektroid_test(tests_elektron
  SOURCES tests_elektron.c ${SRC}/utils.c ${SRC}/preferences.c ${SRC}/backend.c
          ${SRC}/connector.c ${BE_SRC} ${SRC}/sample.c ${SRC}/sample_ops.c
          ${SRC}/connectors/common.c ${SRC}/connectors/elektron.c
          ${SRC}/connectors/elektron_pkg.c
          ${SRC}/connectors/microfreak_sample.c ${SRC}/connectors/scala.c)

elektroid_test(tests_utils
  SOURCES tests_utils.c ${SRC}/utils.c)

elektroid_test(tests_sample
  SOURCES tests_sample.c ${SRC}/utils.c ${SRC}/preferences.c
          ${SRC}/connectors/microfreak_sample.c ${SRC}/sample.c)

elektroid_test(tests_connector
  SOURCES tests_connector.c ${SRC}/utils.c ${SRC}/preferences.c ${SRC}/backend.c
          ${SRC}/connector.c ${BE_SRC})

elektroid_test(tests_volca_sample
  SOURCES tests_volca_sample.c ${SRC}/utils.c ${SRC}/preferences.c
          ${SRC}/backend.c ${SRC}/connector.c ${BE_SRC} ${SRC}/audio.c
          ${AUDIO_SRC} ${SRC}/sample.c ${SRC}/connectors/common.c
          ${SRC}/connectors/scala.c ${SRC}/connectors/volca_sample.c
          ${SRC}/connectors/microfreak_sample.c
          ${SRC}/connectors/volca_sample_sdk/korg_syro_comp.c
          ${SRC}/connectors/volca_sample_sdk/korg_syro_func.c
          ${SRC}/connectors/volca_sample_sdk/korg_syro_volcasample.c
  LIBS ${ELEKTROID_AUDIO_TARGET})

elektroid_test(tests_sample_ops
  SOURCES tests_sample_ops.c ${SRC}/utils.c ${SRC}/preferences.c
          ${SRC}/connectors/microfreak_sample.c ${SRC}/sample.c
          ${SRC}/sample_ops.c
  LIBS ${ELEKTROID_AUDIO_TARGET})

elektroid_test(tests_logue
  SOURCES tests_logue.c ${SRC}/utils.c ${SRC}/preferences.c ${SRC}/backend.c
          ${SRC}/connector.c ${BE_SRC} ${SRC}/sample.c
          ${SRC}/connectors/common.c ${SRC}/connectors/logue.c
          ${SRC}/connectors/microfreak_sample.c ${SRC}/connectors/scala.c)

# Integration shell tests (TESTS in test/Makefile.am). They no-op cleanly when
# no hardware device is configured (TEST_DEVICE unset), matching make check.
# They drive the freshly built elektroid-cli via the $ecli environment variable.
if(TARGET elektroid-cli)
  foreach(_script test.sh system_all_fs_tests.sh)
    add_test(NAME integration_${_script}
      COMMAND ${CMAKE_COMMAND} -E env
        "ecli=$<TARGET_FILE:elektroid-cli> -vv"
        "srcdir=${CMAKE_CURRENT_SOURCE_DIR}"
        bash ${CMAKE_CURRENT_SOURCE_DIR}/integration/${_script}
      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  endforeach()
endif()
