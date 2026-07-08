#
# res/CMakeLists.txt - installed data files. Mirrors res/Makefile.am.
#

# Elektroid data dirs (datadir == datarootdir == share under GNUInstallDirs).
set(ELEKTROID_RESDIR         "${CMAKE_INSTALL_DATADIR}/elektroid")
set(ELEKTROID_DESKTOPDIR     "${CMAKE_INSTALL_DATADIR}/applications")
set(ELEKTROID_METAINFODIR    "${CMAKE_INSTALL_DATADIR}/metainfo")
set(ELEKTROID_SVGICONDIR     "${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/scalable/apps")

# res_DATA (always installed).
#
# THANKS and volca_sample_sdk-COPYING are symlinks in the source tree
# (-> ../THANKS and -> ../src/connectors/volca_sample_sdk/COPYING). CMake's
# install(FILES) copies a symlink verbatim, which would stage dangling links
# under DESTDIR. Resolve them to their real targets so the actual file content
# is installed, matching autotools where install(1) dereferences the symlinks.
get_filename_component(ELEKTROID_THANKS "THANKS" REALPATH
  BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
get_filename_component(ELEKTROID_VOLCA_SAMPLE_SDK_COPYING
  "volca_sample_sdk-COPYING" REALPATH
  BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")

install(FILES libraries.html
  DESTINATION "${ELEKTROID_RESDIR}")
install(FILES "${ELEKTROID_THANKS}"
  DESTINATION "${ELEKTROID_RESDIR}" RENAME THANKS)
install(FILES "${ELEKTROID_VOLCA_SAMPLE_SDK_COPYING}"
  DESTINATION "${ELEKTROID_RESDIR}" RENAME volca_sample_sdk-COPYING)

# elektron_DATA and volca_sample_2_DATA (always installed).
install(FILES elektron/devices.json
  DESTINATION "${ELEKTROID_RESDIR}/elektron")
install(FILES volca_sample_2/init_pattern.vlcsplpattb
  DESTINATION "${ELEKTROID_RESDIR}/volca_sample_2")

if(ELEKTROID_BUILD_GUI)
  # res_DATA += elektroid.ui elektroid.css
  install(FILES elektroid.ui elektroid.css
    DESTINATION "${ELEKTROID_RESDIR}")

  # svgicon_DATA
  install(FILES
    io.github.dagargo.Elektroid.svg
    io.github.dagargo.Elektroid-symbolic.svg
    elektroid-chip-symbolic.svg
    elektroid-file-symbolic.svg
    elektroid-folder-symbolic.svg
    elektroid-fx-delay-symbolic.svg
    elektroid-fx-modulation-symbolic.svg
    elektroid-fx-reverb-symbolic.svg
    elektroid-keys-symbolic.svg
    elektroid-oscillator-symbolic.svg
    elektroid-preset-symbolic.svg
    elektroid-project-symbolic.svg
    elektroid-sequence-symbolic.svg
    elektroid-settings-symbolic.svg
    elektroid-slice-symbolic.svg
    elektroid-track-symbolic.svg
    elektroid-track-loop-symbolic.svg
    elektroid-wave-symbolic.svg
    elektroid-wave-loop-symbolic.svg
    elektroid-wavetable-symbolic.svg
    DESTINATION "${ELEKTROID_SVGICONDIR}")

  # microbrute_DATA
  install(FILES microbrute/microbrute.ui
    DESTINATION "${ELEKTROID_RESDIR}/microbrute")

  # System dependent resources.
  if(ELEKTROID_SYSTEM STREQUAL "mingw")
    # ico_DATA
    install(FILES elektroid.ico DESTINATION "${ELEKTROID_RESDIR}")
  elseif(ELEKTROID_SYSTEM STREQUAL "opendesktop")
    # desktop_DATA / metainfo_DATA
    install(FILES io.github.dagargo.Elektroid.desktop
      DESTINATION "${ELEKTROID_DESKTOPDIR}")
    install(FILES io.github.dagargo.Elektroid.appdata.xml
      DESTINATION "${ELEKTROID_METAINFODIR}")
  endif()

  # install-data-hook: gtk-update-icon-cache (best effort, skipped when staging
  # into DESTDIR, mirroring the Makefile hook).
  install(CODE "
    if(NOT DEFINED ENV{DESTDIR} OR \"\$ENV{DESTDIR}\" STREQUAL \"\")
      find_program(GTK_UPDATE_ICON_CACHE gtk-update-icon-cache)
      if(GTK_UPDATE_ICON_CACHE)
        message(STATUS \"Updating Gtk icon cache.\")
        execute_process(COMMAND \${GTK_UPDATE_ICON_CACHE} -f -t
          \"${CMAKE_INSTALL_FULL_DATAROOTDIR}/icons/hicolor\"
          ERROR_QUIET RESULT_VARIABLE _ignored)
      endif()
    endif()
  ")
endif()
