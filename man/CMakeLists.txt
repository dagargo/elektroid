#
# man/CMakeLists.txt - man pages. Mirrors man/Makefile.am (dist_man1_MANS).
#
if(ELEKTROID_CLI_ONLY)
  install(FILES elektroid-cli.1 DESTINATION "${CMAKE_INSTALL_MANDIR}/man1")
else()
  install(FILES elektroid.1 elektroid-cli.1
    DESTINATION "${CMAKE_INSTALL_MANDIR}/man1")
endif()
