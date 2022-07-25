PKG_CONFIG ?= pkg-config

CLI_LIBS = alsa glib-2.0 zlib json-glib-1.0 libzip
GUI_LIBS = $(CLI_LIBS) gtk+-3.0 libpulse libpulse-mainloop-glib

elektroid_CFLAGS = -I$(top_srcdir)/src `$(PKG_CONFIG) --cflags $(GUI_LIBS)` $(SNDFILE_CFLAGS) $(SAMPLERATE_CFLAGS) -D_GNU_SOURCE
elektroid_LDFLAGS = `$(PKG_CONFIG) --libs $(GUI_LIBS)` $(SNDFILE_LIBS) $(SAMPLERATE_LIBS)
elektroid_cli_CFLAGS = -I$(top_srcdir)/src `$(PKG_CONFIG) --cflags $(CLI_LIBS)` $(SNDFILE_CFLAGS) $(SAMPLERATE_CFLAGS) -D_GNU_SOURCE
elektroid_cli_LDFLAGS = `$(PKG_CONFIG) --libs $(CLI_LIBS)` $(SNDFILE_LIBS) $(SAMPLERATE_LIBS)

if CLI_ONLY
bin_PROGRAMS = elektroid-cli
else
bin_PROGRAMS = elektroid elektroid-cli
endif

elektroid_common_sources = connector.c connector.h \
sample.c sample.h utils.c utils.h backend.c backend.h \
connectors/sds.c connectors/sds.h connectors/elektron.c connectors/elektron.h \
connectors/package.c connectors/package.h

elektroid_cli_SOURCES = $(elektroid_common_sources) elektroid-cli.c

elektroid_SOURCES = $(elektroid_common_sources) audio.c audio.h \
browser.c browser.h notifier.c notifier.h local.c local.h \
preferences.c preferences.h elektroid.c

SNDFILE_CFLAGS = @SNDFILE_CFLAGS@
SNDFILE_LIBS = @SNDFILE_LIBS@

SAMPLERATE_CFLAGS = @SAMPLERATE_CFLAGS@
SAMPLERATE_LIBS = @SAMPLERATE_LIBS@

AM_CPPFLAGS = -Wall -O3 -DDATADIR='"$(datadir)/$(PACKAGE)"' -DLOCALEDIR='"$(localedir)"'
