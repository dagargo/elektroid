PKG_CONFIG ?= pkg-config

GUI_LIBS = alsa gtk+-3.0 libpulse zlib json-glib-1.0
CLI_LIBS = alsa glib-2.0 zlib

elektroid_CFLAGS = -I$(top_srcdir)/src `$(PKG_CONFIG) --cflags $(GUI_LIBS)` $(SNDFILE_CFLAGS) $(SAMPLERATE_CFLAGS)
elektroid_LDFLAGS = `$(PKG_CONFIG) --libs $(GUI_LIBS)` $(SNDFILE_LIBS) $(SAMPLERATE_LIBS)
elektroid_cli_CFLAGS = -I$(top_srcdir)/src `$(PKG_CONFIG) --cflags $(CLI_LIBS)` $(SNDFILE_CFLAGS) $(SAMPLERATE_CFLAGS)
elektroid_cli_LDFLAGS = `$(PKG_CONFIG) --libs $(CLI_LIBS)` $(SNDFILE_LIBS) $(SAMPLERATE_LIBS)

if CLI_ONLY
bin_PROGRAMS = elektroid-cli
else
bin_PROGRAMS = elektroid elektroid-cli
endif

elektroid_SOURCES = audio.c audio.h browser.c browser.h connector.c connector.h elektroid.c sample.c sample.h utils.c utils.h notifier.c notifier.h local.c local.h config.c config.h
elektroid_cli_SOURCES = connector.c connector.h elektroid-cli.c sample.c sample.h utils.c utils.h

SNDFILE_CFLAGS = @SNDFILE_CFLAGS@
SNDFILE_LIBS = @SNDFILE_LIBS@

SAMPLERATE_CFLAGS = @SAMPLERATE_CFLAGS@
SAMPLERATE_LIBS = @SAMPLERATE_LIBS@

AM_CPPFLAGS = -Wall -O3 -DDATADIR='"$(datadir)"' -DLOCALEDIR=\""$(localedir)"\"
