#
# src/CMakeLists.txt - elektroid (GUI) and elektroid-cli targets.
# Mirrors src/Makefile.am.
#

# Backend source, selected exactly like the ELEKTROID_RTMIDI conditional.
if(ELEKTROID_RTMIDI)
  set(ELEKTROID_BACKEND_SOURCES backend_rtmidi.c)
else()
  set(ELEKTROID_BACKEND_SOURCES backend_alsa.c)
endif()

# Audio source, selected exactly like the ELEKTROID_RTAUDIO conditional.
if(ELEKTROID_RTAUDIO)
  set(ELEKTROID_AUDIO_SOURCES audio_rtaudio.c)
else()
  set(ELEKTROID_AUDIO_SOURCES audio_pa.c)
endif()

# elektroid_common_sources
set(ELEKTROID_COMMON_SOURCES
  audio.c
  connector.c
  local.c
  preferences.c
  regconn.c
  regpref.c
  sample.c
  sample_ops.c
  utils.c
  backend.c
  ${ELEKTROID_BACKEND_SOURCES}
  connectors/common.c
  connectors/cz.c
  connectors/default.c
  connectors/efactor.c
  connectors/elektron.c
  connectors/elektron_pkg.c
  connectors/logue.c
  connectors/microbrute.c
  connectors/microfreak.c
  connectors/microfreak_sample.c
  connectors/padkontrol.c
  connectors/phatty.c
  connectors/scala.c
  connectors/sds.c
  connectors/summit.c
  connectors/system.c
  connectors/volca_sample.c
  connectors/volca_sample_sdk/korg_syro_comp.c
  connectors/volca_sample_sdk/korg_syro_func.c
  connectors/volca_sample_sdk/korg_syro_volcasample.c
  connectors/volca_sample_2.c)

# GUI-only sources (elektroid_SOURCES beyond the common set).
set(ELEKTROID_GUI_SOURCES
  browser.c
  editor.c
  guirecorder.c
  maction.c
  mactions/autosampler.c
  mactions/backend.c
  mactions/elektron.c
  mactions/microbrute.c
  mactions/microfreak.c
  name_window.c
  notifier.c
  preferences_window.c
  progress_window.c
  record_window.c
  regma.c
  tags_window.c
  tasks.c
  elektroid.c)

# Common link set: CLI_LIBS_BASE + backend + audio + sndfile + samplerate.
set(ELEKTROID_COMMON_DEPS
  PkgConfig::GLIB
  PkgConfig::JSONGLIB
  PkgConfig::ZLIBPC
  PkgConfig::LIBZIP
  PkgConfig::RUBBERBAND
  PkgConfig::SNDFILE
  PkgConfig::SAMPLERATE
  ${ELEKTROID_MIDI_TARGET}
  ${ELEKTROID_AUDIO_TARGET})

if(NOT WIN32)
  # AC_SEARCH_LIBS([sqrt], [m]) - libm is folded into libc on macOS/Windows.
  list(APPEND ELEKTROID_COMMON_DEPS m)
endif()

# elektroid_cli_SOURCES = common + audio + elektroid-cli.c
if(ELEKTROID_BUILD_CLI)
add_executable(elektroid-cli
  ${ELEKTROID_COMMON_SOURCES}
  ${ELEKTROID_AUDIO_SOURCES}
  elektroid-cli.c)
endif()

# elektroid_SOURCES = common + audio + GUI sources
if(ELEKTROID_BUILD_GUI)
  add_executable(elektroid
    ${ELEKTROID_COMMON_SOURCES}
    ${ELEKTROID_AUDIO_SOURCES}
    ${ELEKTROID_GUI_SOURCES})
endif()

# Shared per-target configuration.
foreach(_bin elektroid-cli elektroid)
  if(NOT TARGET ${_bin})
    continue()
  endif()

  # -I$(top_srcdir)/src for "utils.h", "connectors/*.h", etc.
  # CMAKE_CURRENT_BINARY_DIR (= <build>/src) makes the sources' relative
  # #include "../config.h" resolve to <build>/config.h, exactly as the
  # autotools per-directory "-I." does for the generated header.
  target_include_directories(${_bin} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR})

  target_compile_definitions(${_bin} PRIVATE
    _GNU_SOURCE
    DATADIR="${ELEKTROID_DATADIR}"
    LOCALEDIR="${ELEKTROID_LOCALEDIR}")

  target_link_libraries(${_bin} PRIVATE ${ELEKTROID_COMMON_DEPS})

  if(NOT MSVC)
    target_compile_options(${_bin} PRIVATE -Wall)
  endif()

  if(WIN32)
    # MSYS2_LIBS = -lws2_32 (winsock is used by the socket/system code).
    target_link_libraries(${_bin} PRIVATE ws2_32)
  endif()
endforeach()

# The GUI additionally needs GTK.
if(TARGET elektroid)
  target_link_libraries(elektroid PRIVATE PkgConfig::GTK)
endif()

if(ELEKTROID_BUILD_CLI)
  install(TARGETS elektroid-cli RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
if(ELEKTROID_BUILD_GUI)
  install(TARGETS elektroid RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
