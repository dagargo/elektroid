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

AM_CPPFLAGS = -Wall -DTEST_DATA_DIR='"$(srcdir)/res"' -DDATADIR='"../res"'

check_PROGRAMS = tests_scala tests_common tests_microfreak tests_utils tests_sample tests_connector

tests_LIBS = glib-2.0 json-glib-1.0 cunit libzip zlib $(BE_LIBS)

tests_scala_CFLAGS = -I$(top_srcdir)/src `$(PKG_CONFIG) --cflags $(tests_LIBS)` -pthread
tests_scala_LDFLAGS = `$(PKG_CONFIG) --libs $(tests_LIBS)` $(MSYS2_LIBS)

tests_scala_SOURCES = \
        tests_scala.c \
        ../src/utils.c \
        ../src/utils.h \
        ../src/connectors/scala.c \
	../src/connectors/scala.h

tests_common_CFLAGS = -I$(top_srcdir)/src `$(PKG_CONFIG) --cflags $(tests_LIBS)` $(SNDFILE_CFLAGS) $(SAMPLERATE_CFLAGS) -pthread
tests_common_LDFLAGS = `$(PKG_CONFIG) --libs $(tests_LIBS)` $(SNDFILE_LIBS) $(SAMPLERATE_LIBS) $(MSYS2_LIBS)

tests_common_SOURCES = \
        tests_common.c \
	../src/utils.c \
        ../src/utils.h \
	../src/preferences.c \
	../src/preferences.h \
	../src/backend.c \
        ../src/backend.h \
	../src/connector.c \
        ../src/connector.h \
	$(BE_SOURCES) \
        ../src/connectors/common.c \
	../src/connectors/common.h \
	../src/sample.c \
        ../src/sample.h \
	../src/connectors/microfreak_sample.c \
	../src/connectors/microfreak_sample.h

tests_microfreak_CFLAGS = -I$(top_srcdir)/src `$(PKG_CONFIG) --cflags $(tests_LIBS)` $(SNDFILE_CFLAGS) $(SAMPLERATE_CFLAGS) -pthread
tests_microfreak_LDFLAGS = `$(PKG_CONFIG) --libs $(tests_LIBS)` $(SNDFILE_LIBS) $(SAMPLERATE_LIBS) $(MSYS2_LIBS)

tests_microfreak_SOURCES = \
        tests_microfreak.c \
	../src/utils.c \
        ../src/utils.h \
	../src/preferences.c \
	../src/preferences.h \
	../src/backend.c \
        ../src/backend.h \
	../src/connector.c \
        ../src/connector.h \
	$(BE_SOURCES) \
        ../src/connectors/common.c \
	../src/connectors/common.h \
	../src/connectors/microfreak_sample.c \
	../src/connectors/microfreak_sample.h \
	../src/sample.c \
        ../src/sample.h \
	../src/connectors/microfreak.c \
	../src/connectors/microfreak.h

tests_elektron_CFLAGS = -I$(top_srcdir)/src `$(PKG_CONFIG) --cflags $(tests_LIBS)` $(SNDFILE_CFLAGS) $(SAMPLERATE_CFLAGS) -pthread
tests_elektron_LDFLAGS = `$(PKG_CONFIG) --libs $(tests_LIBS)` $(SNDFILE_LIBS) $(SAMPLERATE_LIBS) $(MSYS2_LIBS)

tests_elektron_SOURCES = \
        tests_elektron.c \
	../src/utils.c \
        ../src/utils.h \
	../src/preferences.c \
	../src/preferences.h \
	../src/backend.c \
        ../src/backend.h \
	../src/connector.c \
        ../src/connector.h \
	$(BE_SOURCES) \
        ../src/connectors/common.c \
	../src/connectors/common.h \
	../src/connectors/microfreak_sample.c \
	../src/connectors/microfreak_sample.h \
	../src/sample.c \
        ../src/sample.h \
	../src/connectors/package.c \
	../src/connectors/package.h \
	../src/connectors/elektron.c \
	../src/connectors/elektron.h

tests_utils_CFLAGS = -I$(top_srcdir)/src `$(PKG_CONFIG) --cflags $(tests_LIBS)` -pthread
tests_utils_LDFLAGS = `$(PKG_CONFIG) --libs $(tests_LIBS)` $(MSYS2_LIBS)

tests_utils_SOURCES = \
        tests_utils.c \
	../src/utils.c \
        ../src/utils.h

tests_sample_CFLAGS = -I$(top_srcdir)/src `$(PKG_CONFIG) --cflags $(tests_LIBS)` $(SNDFILE_CFLAGS) $(SAMPLERATE_CFLAGS) -pthread
tests_sample_LDFLAGS = `$(PKG_CONFIG) --libs $(tests_LIBS)` $(SNDFILE_LIBS) $(SAMPLERATE_LIBS) $(MSYS2_LIBS)

tests_sample_SOURCES = \
        tests_sample.c \
	../src/utils.c \
        ../src/utils.h \
	../src/connectors/microfreak_sample.c \
	../src/connectors/microfreak_sample.h \
	../src/sample.c \
        ../src/sample.h

tests_connector_CFLAGS = -I$(top_srcdir)/src `$(PKG_CONFIG) --cflags $(tests_LIBS)` -pthread
tests_connector_LDFLAGS = `$(PKG_CONFIG) --libs $(tests_LIBS)` $(MSYS2_LIBS)

tests_connector_SOURCES = \
        tests_connector.c \
	../src/utils.c \
        ../src/utils.h \
	../src/preferences.c \
	../src/preferences.h \
	../src/backend.c \
        ../src/backend.h \
	../src/connector.c \
        ../src/connector.h \
	$(BE_SOURCES)

TESTS = integration/test.sh integration/system_all_fs_tests.sh $(check_PROGRAMS)

EXTRA_DIST = integration res

AM_TESTS_ENVIRONMENT = \
	ecli='$(abs_top_builddir)/src/elektroid-cli -vv'; \
	export ecli;
