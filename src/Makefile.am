PKG_CONFIG ?= pkg-config

CLI_LIBS_BASE = glib-2.0 zlib json-glib-1.0 libzip

if MINGW
  MSYS2_LIBS = -lws2_32
endif

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
elektroid_LDFLAGS = `$(PKG_CONFIG) --libs $(GUI_LIBS)` $(SNDFILE_LIBS) $(SAMPLERATE_LIBS) $(MSYS2_LIBS)
elektroid_cli_CFLAGS = -I$(top_srcdir)/src `$(PKG_CONFIG) --cflags $(CLI_LIBS)` $(SNDFILE_CFLAGS) $(SAMPLERATE_CFLAGS) -D_GNU_SOURCE
elektroid_cli_LDFLAGS = `$(PKG_CONFIG) --libs $(CLI_LIBS)` $(SNDFILE_LIBS) $(SAMPLERATE_LIBS) $(MSYS2_LIBS)

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

elektroid_common_sources = local.c local.h \
connector.c connector.h \
preferences.c preferences.h \
regconn.c regconn.h\
regpref.c regpref.h\
sample.c sample.h \
utils.c utils.h \
backend.c backend.h $(elektroid_backend_sources) \
connectors/common.c connectors/common.h \
connectors/system.c connectors/system.h \
connectors/microbrute.c connectors/microbrute.h \
connectors/microfreak_sample.c connectors/microfreak_sample.h \
connectors/microfreak.c connectors/microfreak.h \
connectors/cz.c connectors/cz.h \
connectors/sds.c connectors/sds.h \
connectors/efactor.c connectors/efactor.h \
connectors/phatty.c connectors/phatty.h \
connectors/summit.c connectors/summit.h \
connectors/scala.c connectors/scala.h \
connectors/default.c connectors/default.h

elektroid_cli_SOURCES = $(elektroid_common_sources) elektroid-cli.c

elektroid_SOURCES = $(elektroid_common_sources) \
audio.h audio.c $(elektroid_audio_sources) \
browser.c browser.h notifier.c notifier.h \
regma.c regma.h \
maction.c maction.h \
mactions/autosampler.c \
mactions/backend.c \
mactions/microbrute.c \
mactions/microfreak.c \
editor.c editor.h \
tasks.c tasks.h \
guirecorder.c guirecorder.h \
progress.c progress.h \
elektroid.c

SNDFILE_CFLAGS = @SNDFILE_CFLAGS@
SNDFILE_LIBS = @SNDFILE_LIBS@

SAMPLERATE_CFLAGS = @SAMPLERATE_CFLAGS@
SAMPLERATE_LIBS = @SAMPLERATE_LIBS@

AM_CPPFLAGS = -Wall -O3 -DDATADIR='"$(datadir)/$(PACKAGE)"' -DLOCALEDIR='"$(localedir)"'
