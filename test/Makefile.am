PKG_CONFIG ?= pkg-config

AM_CPPFLAGS = -Wall -DSCALA_TEST_DIR='"$(srcdir)/res/scala"'

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

TESTS = integration/test.sh $(check_PROGRAMS)

EXTRA_DIST = connectors res

AM_TESTS_ENVIRONMENT = \
	ecli='$(abs_top_builddir)/src/elektroid-cli -vv'; \
	export ecli;
