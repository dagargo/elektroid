AM_CFLAGS = -I$(top_srcdir)/src `pkg-config --cflags alsa gtk+-3.0 libpulse libpulse-simple` $(SNDFILE_CFLAGS) $(SAMPLERATE_CFLAGS)
elektroid_LDFLAGS = `pkg-config --libs alsa gtk+-3.0 libpulse libpulse-simple` $(SNDFILE_LIBS) $(SAMPLERATE_LIBS)
elektroid_cli_LDFLAGS = `pkg-config --libs alsa glib-2.0` $(SNDFILE_LIBS) $(SAMPLERATE_LIBS)

bin_PROGRAMS = elektroid elektroid-cli
elektroid_SOURCES = elektroid.c connector.c audio.c utils.c sample.c
elektroid_cli_SOURCES = elektroid-cli.c connector.c utils.c sample.c

SNDFILE_CFLAGS = @SNDFILE_CFLAGS@
SNDFILE_LIBS = @SNDFILE_LIBS@

SAMPLERATE_CFLAGS = @SAMPLERATE_CFLAGS@
SAMPLERATE_LIBS = @SAMPLERATE_LIBS@

AM_CPPFLAGS = -Wall -O3 -DDATADIR='"$(datadir)"'
