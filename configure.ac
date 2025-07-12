#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.71])
AC_INIT([elektroid],[3.3],[dagargo@gmail.com])
AC_CONFIG_SRCDIR([src])
AC_CONFIG_HEADERS([config.h])
LT_INIT
AC_SEARCH_LIBS([sqrt], [m])
AC_CONFIG_MACRO_DIRS([m4])
AM_INIT_AUTOMAKE([subdir-objects])

# Checks for programs.
AC_PROG_CC

# Define conditional prior to package checks
AM_CONDITIONAL([ELEKTROID_CLI_ONLY], [test "${CLI_ONLY}" == yes])

# Needed to access the 'host_os' variable
AC_CANONICAL_HOST

case "${host_os}" in
  linux*)
    SYSTEM=opendesktop
    ;;
  *mingw*)
    SYSTEM=mingw
    RTMIDI=yes
    RTAUDIO=yes
    ;;
  darwin*)
    SYSTEM=darwin
    RTMIDI=yes
    RTAUDIO=yes
    ;;
  *)
    SYSTEM=opendesktop
    RTMIDI=yes
    RTAUDIO=yes
    ;;
esac

AM_CONDITIONAL([MINGW], [test "${SYSTEM}" == mingw])
AM_CONDITIONAL([DARWIN], [test "${SYSTEM}" == darwin])
AM_CONDITIONAL([OPENDESKTOP], [test "${SYSTEM}" == opendesktop])

AM_CONDITIONAL([ELEKTROID_RTMIDI], [test "${RTMIDI}" == yes])
AS_IF([test "${RTMIDI}" == yes], [AC_DEFINE([ELEKTROID_RTMIDI], [1], ["Use RtMidi"])])

AM_CONDITIONAL([ELEKTROID_RTAUDIO], [test "${RTAUDIO}" == yes])
AS_IF([test "${RTAUDIO}" == yes], [AC_DEFINE([ELEKTROID_RTAUDIO], [1], ["Use RtAudio"])])

# Checks for libraries.
PKG_CHECK_MODULES(zlib, zlib >= 1.1.8)
PKG_CHECK_MODULES(libzip, libzip >= 1.1.2)
AM_COND_IF(ELEKTROID_CLI_ONLY, [], [PKG_CHECK_MODULES([GTK], [gtk+-3.0])])

PKG_CHECK_MODULES(SNDFILE, sndfile >= 1.0.2)
PKG_CHECK_MODULES(SNDFILE_MP3, sndfile >= 1.1.0, ac_cv_sndfile_mp3=1, ac_cv_sndfile_mp3=0)
AC_DEFINE_UNQUOTED([HAVE_SNDFILE_MP3],${ac_cv_sndfile_mp3}, [Set to 1 if you have libsndfile > 1.1.0.])
AC_SUBST(SNDFILE_CFLAGS)
AC_SUBST(SNDFILE_LIBS)

PKG_CHECK_MODULES(SAMPLERATE, samplerate >= 0.1.8)
AC_SUBST(SAMPLERATE_CFLAGS)
AC_SUBST(SAMPLERATE_LIBS)

AM_COND_IF(ELEKTROID_RTMIDI, [PKG_CHECK_MODULES([RTMIDI], [rtmidi >= 5.0.0])], [PKG_CHECK_MODULES([ALSA], [alsa >= 1.1.3])])

AM_COND_IF(ELEKTROID_CLI_ONLY, [], [AM_COND_IF(ELEKTROID_RTAUDIO, [PKG_CHECK_MODULES([RTAUDIO], [rtaudio >= 5.2.0])], [PKG_CHECK_MODULES([ALSA], [alsa >= 1.1.3])])])

AC_SUBST(PULSEAUDIO_CFLAGS)
AC_SUBST(PULSEAUDIO_LIBS)

AM_COND_IF(ELEKTROID_CLI_ONLY, [], [
AM_GNU_GETTEXT([external])
AM_GNU_GETTEXT_VERSION([0.19])
])

# Checks for header files.
AC_CHECK_HEADERS([unistd.h limits.h])

# Checks for typedefs, structures, and compiler characteristics.

# Checks for library functions.
AC_FUNC_MALLOC

AC_CONFIG_FILES([Makefile src/Makefile res/Makefile test/Makefile po/Makefile.in po/Makefile man/Makefile])
AC_OUTPUT
