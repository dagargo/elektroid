ACLOCAL_AMFLAGS=-I m4

if ELEKTROID_CLI_ONLY
SUBDIRS=src res test man
else
SUBDIRS=src res test po man
endif

format:
	indent src/*.[ch] src/connectors/*.[ch] src/mactions/*.[ch] test/*.[ch]

.PHONY: format
