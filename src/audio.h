/*
 *   audio.h
 *   Copyright (C) 2019 David García Goñi <dagargo@gmail.com>
 *
 *   This file is part of Elektroid.
 *
 *   Elektroid is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   Elektroid is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with Elektroid. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef AUDIO_H
#define AUDIO_H

#include <glib.h>
#include "sample.h"
#include "utils.h"
#include "preferences.h"
#if defined(ELEKTROID_RTAUDIO)
#include "rtaudio_c.h"
#else
#include <pulse/pulseaudio.h>
#endif

typedef void (*audio_monitor_notifier) (gpointer, gdouble);

#define MAX_RECORDING_TIME_S 30
#define AUDIO_CHANNELS 2	// Audio system is always stereo
#define AUDIO_BUF_FRAMES (preferences_get_int (PREF_KEY_AUDIO_BUFFER_LEN))
#define AUDIO_BUF_BYTES (AUDIO_BUF_FRAMES * FRAME_SIZE (AUDIO_CHANNELS,sample_get_internal_format ()))
#define AUDIO_SEL_LEN (audio.sel_start == -1 && audio.sel_end == -1 ? 0 : audio.sel_end - audio.sel_start + 1)

#define RECORD_LEFT 0x1
#define RECORD_RIGHT 0x2
#define RECORD_STEREO (RECORD_LEFT | RECORD_RIGHT)
#define RECORD_MONITOR_ONLY 0x4

enum audio_status
{
  AUDIO_STATUS_PREPARING_PLAYBACK,
  AUDIO_STATUS_PLAYING,
  AUDIO_STATUS_STOPPING_PLAYBACK,
  AUDIO_STATUS_PREPARING_RECORD,
  AUDIO_STATUS_RECORDING,
  AUDIO_STATUS_STOPPING_RECORD,
  AUDIO_STATUS_STOPPED
};

struct audio
{
// PulseAudio or RtAudio backend
#if defined(ELEKTROID_RTAUDIO)
  rtaudio_t playback_rtaudio;
  rtaudio_t record_rtaudio;
  gdouble volume;
#else
  pa_threaded_mainloop *mainloop;
  pa_context *context;
  pa_stream *playback_stream;
  pa_stream *record_stream;
  guint32 playback_index;
  guint32 record_index;
  pa_cvolume volume;
  pa_sample_spec sample_spec;
#endif
  gboolean float_mode;
  guint32 rate;
  struct idata sample;
  struct sample_info sample_info_src;
  gboolean loop;
  guint32 pos;
  void (*volume_change_callback) (gdouble);
  void (*ready_callback) ();
  guint32 release_frames;
  struct job_control control;	//Used to synchronize access to sample
  gchar *path;
  enum audio_status status;
  gint64 sel_start;		//Space for guint32 and -1
  gint64 sel_end;		//Space for guint32 and -1
  gboolean mono_mix;
  guint record_options;
  void (*monitor) (void *, gdouble);
  void *monitor_data;
};

extern struct audio audio;

void audio_start_playback ();

void audio_stop_playback ();

void audio_start_recording (guint, audio_monitor_notifier, void *);

void audio_stop_recording ();

gboolean audio_check ();

void audio_reset_record_buffer (guint, audio_monitor_notifier, void *);

void audio_init (void (*)(gdouble), void (*)(), gpointer);

gint audio_run ();

void audio_destroy ();

void audio_reset_sample ();

void audio_set_volume (gdouble);

void audio_write_to_output (void *, gint);

void audio_read_from_input (void *, gint);

void audio_prepare (enum audio_status);

void audio_delete_range (guint32, guint32);

const gchar *audio_name ();

const gchar *audio_version ();

#endif
