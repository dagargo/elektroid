#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.69])
AC_INIT([elektroid],[2.1],[dagargo@gmail.com])
AC_CONFIG_SRCDIR([src])
AC_CONFIG_HEADERS([config.h])
AM_PROG_LIBTOOL
AC_SEARCH_LIBS([sqrt], [m])
AC_CONFIG_MACRO_DIRS([m4])
AM_INIT_AUTOMAKE([subdir-objects])

# Checks for programs.
AC_PROG_CC

# Define conditional prior to package checks
AM_CONDITIONAL([CLI_ONLY], [test "${CLI_ONLY}" == yes])
AM_CONDITIONAL([GUI], [test "${CLI_ONLY}" != yes])

# Checks for libraries.
PKG_CHECK_MODULES(ALSA, alsa >= 1.1.3)
PKG_CHECK_MODULES(zlib, zlib >= 1.1.8)
PKG_CHECK_MODULES(libzip, libzip >= 1.1.2)
AM_COND_IF(GUI, [PKG_CHECK_MODULES([GTK], [gtk+-3.0])])

PKG_CHECK_MODULES(SNDFILE, sndfile >= 1.0.2, ac_cv_sndfile=1, ac_cv_sndfile=0)
AC_DEFINE_UNQUOTED([HAVE_SNDFILE],${ac_cv_sndfile}, [Set to 1 if you have libsndfile.])
AC_SUBST(SNDFILE_CFLAGS)
AC_SUBST(SNDFILE_LIBS)

PKG_CHECK_MODULES(SAMPLERATE, samplerate >= 0.1.8, ac_cv_samplerate=1, ac_cv_samplerate=0)
AC_DEFINE_UNQUOTED([HAVE_SAMPLERATE],${ac_cv_samplerate}, [Set to 1 if you have libsamplerate.])
AC_SUBST(SAMPLERATE_CFLAGS)
AC_SUBST(SAMPLERATE_LIBS)

AM_COND_IF(GUI, [PKG_CHECK_MODULES([PULSEAUDIO], [libpulse >= 5.0])])
AC_SUBST(PULSEAUDIO_CFLAGS)
AC_SUBST(PULSEAUDIO_LIBS)

AM_COND_IF(GUI, [
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
