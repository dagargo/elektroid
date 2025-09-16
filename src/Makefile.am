PKG_CONFIG ?= pkg-config

AM_CFLAGS = -Wall -O3
AM_CPPFLAGS = -DDATADIR='"$(datadir)/$(PACKAGE)"' -DLOCALEDIR='"$(localedir)"'

CLI_LIBS_BASE = glib-2.0 zlib json-glib-1.0 libzip

if MINGW
  MSYS2_LIBS = -lws2_32
endif

if ELEKTROID_RTMIDI
CLI_LIBS = rtmidi $(CLI_LIBS_BASE)
else
CLI_LIBS = alsa $(CLI_LIBS_BASE)
endif

if ELEKTROID_RTAUDIO
CLI_LIBS += rtaudio
else
CLI_LIBS += libpulse libpulse-mainloop-glib
endif

GUI_LIBS = $(CLI_LIBS) gtk+-3.0

elektroid_CFLAGS = -I$(top_srcdir)/src `$(PKG_CONFIG) --cflags $(GUI_LIBS)` $(SNDFILE_CFLAGS) $(SAMPLERATE_CFLAGS) -D_GNU_SOURCE $(AM_CFLAGS)
elektroid_LDFLAGS = `$(PKG_CONFIG) --libs $(GUI_LIBS)` $(SNDFILE_LIBS) $(SAMPLERATE_LIBS) $(MSYS2_LIBS)
elektroid_cli_CFLAGS = -I$(top_srcdir)/src `$(PKG_CONFIG) --cflags $(CLI_LIBS)` $(SNDFILE_CFLAGS) $(SAMPLERATE_CFLAGS) -D_GNU_SOURCE $(AM_CFLAGS)
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

elektroid_common_sources = audio.c audio.h \
connector.c connector.h \
local.c local.h \
preferences.c preferences.h \
regconn.c regconn.h\
regpref.c regpref.h\
sample.c sample.h \
utils.c utils.h \
backend.c backend.h $(elektroid_backend_sources) \
connectors/common.c connectors/common.h \
connectors/cz.c connectors/cz.h \
connectors/default.c connectors/default.h \
connectors/efactor.c connectors/efactor.h \
connectors/elektron.c connectors/elektron.h \
connectors/microbrute.c connectors/microbrute.h \
connectors/microfreak.c connectors/microfreak.h \
connectors/microfreak_sample.c connectors/microfreak_sample.h \
connectors/package.c connectors/package.h \
connectors/padkontrol.c connectors/padkontrol.h \
connectors/phatty.c connectors/phatty.h \
connectors/scala.c connectors/scala.h \
connectors/sds.c connectors/sds.h \
connectors/summit.c connectors/summit.h \
connectors/system.c connectors/system.h \
connectors/volca_sample.c connectors/volca_sample.h \
connectors/volca_sample_sdk/korg_syro_comp.c connectors/volca_sample_sdk/korg_syro_comp.h \
connectors/volca_sample_sdk/korg_syro_func.c connectors/volca_sample_sdk/korg_syro_func.h \
connectors/volca_sample_sdk/korg_syro_volcasample.c connectors/volca_sample_sdk/korg_syro_volcasample.h \
connectors/volca_sample_sdk/korg_syro_type.h \
connectors/volca_sample_2.c connectors/volca_sample_2.h

elektroid_cli_SOURCES = $(elektroid_common_sources) \
$(elektroid_audio_sources) \
elektroid-cli.c

elektroid_SOURCES = $(elektroid_common_sources) \
$(elektroid_audio_sources) \
browser.c browser.h \
editor.c editor.h \
guirecorder.c guirecorder.h \
maction.c maction.h \
mactions/autosampler.c \
mactions/backend.c \
mactions/microbrute.c \
mactions/microfreak.c \
name_window.c name_window.h \
notifier.c notifier.h \
preferences_window.c preferences_window.h \
progress_window.c progress_window.h \
record_window.c record_window.h \
regma.c regma.h \
tasks.c tasks.h \
elektroid.c elektroid.h

SNDFILE_CFLAGS = @SNDFILE_CFLAGS@
SNDFILE_LIBS = @SNDFILE_LIBS@

SAMPLERATE_CFLAGS = @SAMPLERATE_CFLAGS@
SAMPLERATE_LIBS = @SAMPLERATE_LIBS@
