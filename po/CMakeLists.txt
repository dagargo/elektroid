#
# po/CMakeLists.txt - gettext message catalogs (GUI only, like the autotools
# AM_GNU_GETTEXT handling gated on ELEKTROID_BUILD_GUI).
#
# For each language in LINGUAS, compile <lang>.po -> <lang>.gmo and install it
# as $(localedir)/<lang>/LC_MESSAGES/$(PACKAGE).mo, matching what the gettext
# autotools rules produce.
#
find_program(MSGFMT_EXECUTABLE msgfmt)
if(NOT MSGFMT_EXECUTABLE)
  message(WARNING "msgfmt not found; translations will not be built. "
    "Set -DELEKTROID_NLS=OFF to silence this.")
  return()
endif()

# Read LINGUAS, skipping comment/blank lines.
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/LINGUAS" _linguas_lines)
set(_languages "")
foreach(_line ${_linguas_lines})
  string(STRIP "${_line}" _line)
  if(_line STREQUAL "" OR _line MATCHES "^#")
    continue()
  endif()
  # A LINGUAS line may hold several space-separated codes.
  separate_arguments(_line_langs UNIX_COMMAND "${_line}")
  list(APPEND _languages ${_line_langs})
endforeach()

set(_gmo_files "")
foreach(_lang ${_languages})
  set(_po "${CMAKE_CURRENT_SOURCE_DIR}/${_lang}.po")
  set(_gmo "${CMAKE_CURRENT_BINARY_DIR}/${_lang}.gmo")
  add_custom_command(
    OUTPUT "${_gmo}"
    COMMAND "${MSGFMT_EXECUTABLE}" -o "${_gmo}" "${_po}"
    DEPENDS "${_po}"
    COMMENT "Compiling ${_lang}.po"
    VERBATIM)
  list(APPEND _gmo_files "${_gmo}")
  install(FILES "${_gmo}"
    DESTINATION "${CMAKE_INSTALL_LOCALEDIR}/${_lang}/LC_MESSAGES"
    RENAME "${PACKAGE}.mo")
endforeach()

add_custom_target(translations ALL DEPENDS ${_gmo_files})
