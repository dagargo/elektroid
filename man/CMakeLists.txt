#
# man/CMakeLists.txt - man pages. Mirrors man/Makefile.am (dist_man1_MANS).
#
if(ELEKTROID_BUILD_CLI)
  install(FILES elektroid-cli.1 DESTINATION "${CMAKE_INSTALL_MANDIR}/man1")
endif()
if(ELEKTROID_BUILD_GUI)
  install(FILES elektroid.1 DESTINATION "${CMAKE_INSTALL_MANDIR}/man1")
endif()
