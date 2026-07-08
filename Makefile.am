ACLOCAL_AMFLAGS = -I m4

SUBDIRS = src res test man

if ELEKTROID_BUILD_GUI
SUBDIRS += po
endif

format:
	indent src/*.[ch] src/connectors/*.[ch] src/mactions/*.[ch] test/*.[ch]

.PHONY: format
