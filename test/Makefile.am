PKG_CONFIG ?= pkg-config

AM_CFLAGS = -Wall

check_PROGRAMS = tests_scala

tests_scala_LIBS = glib-2.0 json-glib-1.0 cunit

tests_scala_CFLAGS = -I$(top_srcdir)/src `$(PKG_CONFIG) --cflags $(tests_scala_LIBS)` -pthread
tests_scala_LDFLAGS = `$(PKG_CONFIG) --libs $(tests_scala_LIBS)`

tests_scala_SOURCES = \
        tests_scala.c \
        ../src/utils.c \
        ../src/utils.h \
        ../src/connectors/scala.c \
	../src/connectors/scala.h

TESTS = connectors/test.sh $(check_PROGRAMS)

EXTRA_DIST = \
	$(TESTS) \
        res/connectors/CZ-101 internal 01 src.syx \
	res/connectors/CZ-101 panel 01 src.syx \
	res/connectors/devices.json \
        res/connectors/Eventide Factor New Name.syx \
	res/connectors/Eventide Factor Preset.syx \
        res/connectors/Moog Little Phatty New Name.syx \
	res/connectors/Moog Little Phatty Preset.syx \
	res/connectors/sequence_back.mbseq \
	res/connectors/sequence.mbseq \
	res/connectors/silence.wav \
	res/connectors/SOUND.dtdata \
	res/connectors/square.wav \
	res/connectors/square_loop.wav

AM_TESTS_ENVIRONMENT = \
	ecli='$(abs_top_builddir)/src/elektroid-cli -vv'; \
	export ecli;
