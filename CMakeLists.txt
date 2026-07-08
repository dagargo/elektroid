#
# CMake build for Elektroid.
#
# This is an additive, cross-platform build that mirrors the autotools build
# (configure.ac + the Makefile.am files). The autotools build remains the
# reference on Linux/macOS; this CMake build exists mainly to compile the GUI
# on Windows without MSYS2 (issue #188), using RtMidi + RtAudio, while staying
# usable everywhere else too.
#
# Quick start:
#   cmake -S . -B build
#   cmake --build build -j
#   ctest --test-dir build --output-on-failure
#   cmake --install build --prefix /usr/local
#
# Options (map to the ./configure switches):
#   -DELEKTROID_BUILD_CLI=ON   build CLI (configure BUILD_CLI=yes)
#   -DELEKTROID_BUILD_GUI=ON   build GUI (configure BUILD_GUI=yes)
#   -DELEKTROID_RTMIDI=ON/OFF  RtMidi vs ALSA MIDI backend (configure RTMIDI=yes)
#   -DELEKTROID_RTAUDIO=ON/OFF RtAudio vs PulseAudio audio (configure RTAUDIO=yes)
#   -DELEKTROID_NLS=ON/OFF     compile/install gettext catalogs (GUI only)
#   -DELEKTROID_BUILD_TESTS=ON build the CUnit test programs
#
cmake_minimum_required(VERSION 3.16)

# --- Version: parsed from configure.ac so there is a single source of truth ---
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/configure.ac" _configure_ac)
string(REGEX MATCH
  "AC_INIT\\(\\[([^]]+)\\],[ \t]*\\[([^]]+)\\],[ \t]*\\[([^]]+)\\]"
  _ac_init_match "${_configure_ac}")
if(NOT _ac_init_match)
  message(FATAL_ERROR "Could not parse AC_INIT from configure.ac")
endif()
set(PACKAGE          "${CMAKE_MATCH_1}")
set(PACKAGE_VERSION  "${CMAKE_MATCH_2}")
set(PACKAGE_BUGREPORT "${CMAKE_MATCH_3}")

project(elektroid
  VERSION "${PACKAGE_VERSION}"
  HOMEPAGE_URL "https://github.com/dagargo/elektroid"
  LANGUAGES C)

# autoconf-equivalent identification macros for config.h
set(PACKAGE_NAME    "${PACKAGE}")
set(PACKAGE_TARNAME "${PACKAGE}")
set(PACKAGE_STRING  "${PACKAGE} ${PACKAGE_VERSION}")
set(PACKAGE_URL     "")
set(VERSION         "${PACKAGE_VERSION}")

include(GNUInstallDirs)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  # autotools uses "-Wall -O3"; Release gives an equivalent optimisation level.
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# --- Platform detection, mirroring the case "${host_os}" block in configure.ac.
#   linux   -> opendesktop, ALSA + PulseAudio by default
#   mingw   -> RtMidi + RtAudio (forced)
#   darwin  -> RtMidi + RtAudio by default
#   other   -> RtMidi + RtAudio by default
if(WIN32)
  set(ELEKTROID_SYSTEM "mingw")
  set(_rt_default ON)
  set(_rt_forced TRUE)
elseif(APPLE)
  set(ELEKTROID_SYSTEM "darwin")
  set(_rt_default ON)
  set(_rt_forced FALSE)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set(ELEKTROID_SYSTEM "opendesktop")
  set(_rt_default OFF)
  set(_rt_forced FALSE)
else()
  set(ELEKTROID_SYSTEM "opendesktop")
  set(_rt_default ON)
  set(_rt_forced FALSE)
endif()

option(ELEKTROID_BUILD_CLI  "Build CLI"                                           ON)
option(ELEKTROID_BUILD_GUI  "Build GUI"                                           ON)
option(ELEKTROID_RTMIDI     "Use the RtMidi MIDI backend (else ALSA)"             ${_rt_default})
option(ELEKTROID_RTAUDIO    "Use the RtAudio audio backend (else PulseAudio)"     ${_rt_default})
option(ELEKTROID_NLS        "Compile and install gettext translations (GUI only)" ON)
option(ELEKTROID_BUILD_TESTS "Build the CUnit test programs"                      ON)

if(_rt_forced)
  # On Windows the ALSA/PulseAudio backends are not available.
  set(ELEKTROID_RTMIDI ON CACHE BOOL "" FORCE)
  set(ELEKTROID_RTAUDIO ON CACHE BOOL "" FORCE)
endif()

# --- Dependencies (pkg-config, modern IMPORTED_TARGET form) -------------------
find_package(PkgConfig REQUIRED)

# Always required (CLI_LIBS_BASE + sndfile + samplerate).
pkg_check_modules(GLIB       REQUIRED IMPORTED_TARGET glib-2.0)
pkg_check_modules(JSONGLIB   REQUIRED IMPORTED_TARGET json-glib-1.0)
pkg_check_modules(ZLIBPC     REQUIRED IMPORTED_TARGET zlib>=1.1.8)
pkg_check_modules(LIBZIP     REQUIRED IMPORTED_TARGET libzip>=1.1.2)
pkg_check_modules(RUBBERBAND REQUIRED IMPORTED_TARGET rubberband>=3.3)
pkg_check_modules(SNDFILE    REQUIRED IMPORTED_TARGET sndfile>=1.0.2)
pkg_check_modules(SAMPLERATE REQUIRED IMPORTED_TARGET samplerate>=0.1.8)

# HAVE_SNDFILE_MP3: 1 when libsndfile >= 1.1.0 (matches the second PKG_CHECK).
if(SNDFILE_VERSION VERSION_GREATER_EQUAL "1.1.0")
  set(HAVE_SNDFILE_MP3 1)
else()
  set(HAVE_SNDFILE_MP3 0)
endif()

# GUI toolkit.
if(ELEKTROID_BUILD_GUI)
  pkg_check_modules(GTK REQUIRED IMPORTED_TARGET gtk+-3.0)
endif()

# MIDI backend.
if(ELEKTROID_RTMIDI)
  pkg_check_modules(RTMIDI REQUIRED IMPORTED_TARGET rtmidi>=5.0.0)
  set(ELEKTROID_MIDI_TARGET PkgConfig::RTMIDI)
else()
  pkg_check_modules(ALSA REQUIRED IMPORTED_TARGET alsa>=1.1.3)
  set(ELEKTROID_MIDI_TARGET PkgConfig::ALSA)
endif()

# Audio backend. The audio sources are compiled for both the GUI and the CLI,
# so the backend is required in every configuration (the CLI plays/records).
if(ELEKTROID_RTAUDIO)
  pkg_check_modules(RTAUDIO REQUIRED IMPORTED_TARGET rtaudio>=5.2.0)
  set(ELEKTROID_AUDIO_TARGET PkgConfig::RTAUDIO)
else()
  pkg_check_modules(PULSE REQUIRED IMPORTED_TARGET libpulse libpulse-mainloop-glib)
  set(ELEKTROID_AUDIO_TARGET PkgConfig::PULSE)
endif()

# --- config.h -----------------------------------------------------------------
include(CheckIncludeFile)
check_include_file(limits.h HAVE_LIMITS_H)

configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/config.h.cmake"
  "${CMAKE_CURRENT_BINARY_DIR}/config.h"
  @ONLY)

# --- Shared compile settings --------------------------------------------------
# DATADIR/LOCALEDIR are baked in like the autotools -D flags in src/Makefile.am.
set(ELEKTROID_DATADIR   "${CMAKE_INSTALL_FULL_DATADIR}/${PACKAGE}")
set(ELEKTROID_LOCALEDIR "${CMAKE_INSTALL_FULL_LOCALEDIR}")

add_subdirectory(src)
add_subdirectory(res)
if(ELEKTROID_BUILD_GUI AND ELEKTROID_NLS)
  add_subdirectory(po)
endif()
add_subdirectory(man)

if(ELEKTROID_BUILD_TESTS)
  enable_testing()
  add_subdirectory(test)
endif()

message(STATUS "Elektroid ${PACKAGE_VERSION} configured:")
message(STATUS "  system          : ${ELEKTROID_SYSTEM}")
message(STATUS "  build CLI       : ${ELEKTROID_BUILD_CLI}")
message(STATUS "  build GUI       : ${ELEKTROID_BUILD_GUI}")
message(STATUS "  MIDI backend    : ${ELEKTROID_MIDI_TARGET}")
message(STATUS "  audio backend   : ${ELEKTROID_AUDIO_TARGET}")
message(STATUS "  sndfile MP3     : ${HAVE_SNDFILE_MP3}")
message(STATUS "  translations    : ${ELEKTROID_NLS}")
message(STATUS "  tests           : ${ELEKTROID_BUILD_TESTS}")
