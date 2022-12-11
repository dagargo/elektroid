PKG_CONFIG ?= pkg-config

CLI_LIBS_BASE = glib-2.0 zlib json-glib-1.0 libzip

if ELEKTROID_RTMIDI
CLI_LIBS = rtmidi $(CLI_LIBS_BASE)
else
CLI_LIBS = alsa $(CLI_LIBS_BASE)
endif

GUI_LIBS_BASE = $(CLI_LIBS) gtk+-3.0

if ELEKTROID_RTAUDIO
GUI_LIBS = rtaudio $(GUI_LIBS_BASE)
else
GUI_LIBS = libpulse libpulse-mainloop-glib $(GUI_LIBS_BASE)
endif

elektroid_CFLAGS = -I$(top_srcdir)/src `$(PKG_CONFIG) --cflags $(GUI_LIBS)` $(SNDFILE_CFLAGS) $(SAMPLERATE_CFLAGS) -D_GNU_SOURCE
elektroid_LDFLAGS = `$(PKG_CONFIG) --libs $(GUI_LIBS)` $(SNDFILE_LIBS) $(SAMPLERATE_LIBS)
elektroid_cli_CFLAGS = -I$(top_srcdir)/src `$(PKG_CONFIG) --cflags $(CLI_LIBS)` $(SNDFILE_CFLAGS) $(SAMPLERATE_CFLAGS) -D_GNU_SOURCE
elektroid_cli_LDFLAGS = `$(PKG_CONFIG) --libs $(CLI_LIBS)` $(SNDFILE_LIBS) $(SAMPLERATE_LIBS)

if ELEKTROID_CLI_ONLY
bin_PROGRAMS = elektroid-cli
else
bin_PROGRAMS = elektroid elektroid-cli
endif

if ELEKTROID_RTMIDI
elektroid_backend_sources = backend_rtmidi.c
else
elektroid_backend_sources = backend_alsa.c
endif

if ELEKTROID_RTAUDIO
elektroid_audio_sources = audio_rtaudio.c
else
elektroid_audio_sources = audio_pa.c
endif

elektroid_common_sources = local.c local.h connector.c connector.h \
sample.c sample.h utils.c utils.h \
backend.c backend.h $(elektroid_backend_sources) \
connectors/common.c connectors/common.h \
connectors/elektron.c connectors/elektron.h \
connectors/package.c connectors/package.h \
connectors/microbrute.c connectors/microbrute.h \
connectors/cz.c connectors/cz.h \
connectors/sds.c connectors/sds.h \
connectors/efactor.c connectors/efactor.h \
connectors/phatty.c connectors/phatty.h

elektroid_cli_SOURCES = $(elektroid_common_sources) elektroid-cli.c

elektroid_SOURCES = $(elektroid_common_sources) \
audio.h audio.c $(elektroid_audio_sources) \
browser.c browser.h notifier.c notifier.h \
preferences.c preferences.h elektroid.c

SNDFILE_CFLAGS = @SNDFILE_CFLAGS@
SNDFILE_LIBS = @SNDFILE_LIBS@

SAMPLERATE_CFLAGS = @SAMPLERATE_CFLAGS@
SAMPLERATE_LIBS = @SAMPLERATE_LIBS@

AM_CPPFLAGS = -Wall -O3 -DDATADIR='"$(datadir)/$(PACKAGE)"' -DLOCALEDIR='"$(localedir)"'
