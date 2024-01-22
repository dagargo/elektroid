PKG_CONFIG ?= pkg-config

if MINGW
  MSYS2_LIBS = -lws2_32
endif

if ELEKTROID_RTMIDI
BE_LIBS = rtmidi
BE_SOURCES = ../src/backend_rtmidi.c
else
BE_LIBS = alsa
BE_SOURCES = ../src/backend_alsa.c
endif

AM_CPPFLAGS = -Wall -DSCALA_TEST_DIR='"$(srcdir)/res/scala"'

check_PROGRAMS = tests_scala tests_common tests_microfreak

tests_LIBS = glib-2.0 json-glib-1.0 cunit $(BE_LIBS)

tests_scala_CFLAGS = -I$(top_srcdir)/src `$(PKG_CONFIG) --cflags $(tests_LIBS)` -pthread
tests_scala_LDFLAGS = `$(PKG_CONFIG) --libs $(tests_LIBS)` $(MSYS2_LIBS)

tests_scala_SOURCES = \
        tests_scala.c \
        ../src/utils.c \
        ../src/utils.h \
        ../src/connectors/scala.c \
	../src/connectors/scala.h

tests_common_CFLAGS = -I$(top_srcdir)/src `$(PKG_CONFIG) --cflags $(tests_LIBS)` -pthread
tests_common_LDFLAGS = `$(PKG_CONFIG) --libs $(tests_LIBS)` $(MSYS2_LIBS)

tests_common_SOURCES = \
        tests_common.c \
	../src/utils.c \
        ../src/utils.h \
	../src/backend.c \
        ../src/backend.h \
	$(BE_SOURCES) \
        ../src/connectors/common.c \
	../src/connectors/common.h

tests_microfreak_CFLAGS = -I$(top_srcdir)/src `$(PKG_CONFIG) --cflags $(tests_LIBS)` -pthread
tests_microfreak_LDFLAGS = `$(PKG_CONFIG) --libs $(tests_LIBS)` $(MSYS2_LIBS)

tests_microfreak_SOURCES = \
        tests_microfreak.c \
	../src/utils.c \
        ../src/utils.h \
	../src/backend.c \
        ../src/backend.h \
	$(BE_SOURCES) \
        ../src/connectors/common.c \
	../src/connectors/common.h \
	../src/connectors/microfreak.c \
	../src/connectors/microfreak.h

TESTS = integration/test.sh integration/system_all_fs_tests.sh $(check_PROGRAMS)

EXTRA_DIST = integration res

AM_TESTS_ENVIRONMENT = \
	ecli='$(abs_top_builddir)/src/elektroid-cli -vv'; \
	export ecli;
