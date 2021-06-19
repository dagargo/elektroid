PKG_CONFIG ?= pkg-config

AM_CFLAGS = -I$(top_srcdir)/src `$(PKG_CONFIG) --cflags alsa gtk+-3.0 libpulse libpulse-mainloop-glib zlib` $(SNDFILE_CFLAGS) $(SAMPLERATE_CFLAGS)
elektroid_LDFLAGS = `$(PKG_CONFIG) --libs alsa gtk+-3.0 libpulse libpulse-mainloop-glib zlib` $(SNDFILE_LIBS) $(SAMPLERATE_LIBS)
elektroid_cli_LDFLAGS = `$(PKG_CONFIG) --libs alsa glib-2.0 zlib` $(SNDFILE_LIBS) $(SAMPLERATE_LIBS)

bin_PROGRAMS = elektroid elektroid-cli
elektroid_SOURCES = audio.c audio.h browser.c browser.h connector.c connector.h elektroid.c sample.c sample.h utils.c utils.h notifier.c notifier.h
elektroid_cli_SOURCES = connector.c connector.h elektroid-cli.c sample.c sample.h utils.c utils.h

SNDFILE_CFLAGS = @SNDFILE_CFLAGS@
SNDFILE_LIBS = @SNDFILE_LIBS@

SAMPLERATE_CFLAGS = @SAMPLERATE_CFLAGS@
SAMPLERATE_LIBS = @SAMPLERATE_LIBS@

AM_CPPFLAGS = -Wall -O3 -DDATADIR='"$(datadir)"' -DLOCALEDIR=\""$(localedir)"\"
