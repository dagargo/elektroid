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
#include "utils.h"
#if defined(ELEKTROID_RTAUDIO)
#include "rtaudio_c.h"
#else
#include <pulse/pulseaudio.h>
#endif

typedef void (*audio_monitor_notifier) (void *, gdouble);

#define MAX_RECORDING_TIME_S 30
#define AUDIO_BUF_FRAMES 512
#define AUDIO_CHANNELS 2	// Audio system is always stereo
#define SAMPLE_SIZE (sizeof(gint16))
#define BYTES_PER_FRAME(x) (x * SAMPLE_SIZE)
#define AUDIO_SAMPLE_CHANNELS(audio) (((struct sample_info *)(audio)->control.data)->channels)
#define AUDIO_SAMPLE_BYTES_PER_FRAME(audio) (BYTES_PER_FRAME(AUDIO_SAMPLE_CHANNELS(audio)))

#define RECORD_LEFT 0x1
#define RECORD_RIGHT 0x2
#define RECORD_STEREO (RECORD_LEFT | RECORD_RIGHT)
#define RECORD_MONITOR_ONLY 0x4

enum audio_src
{
  AUDIO_SRC_NONE,
  AUDIO_SRC_LOCAL,
  AUDIO_SRC_REMOTE
};

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
  guint32 samplerate;
  GByteArray *sample;
  guint32 frames;
  gboolean loop;
  guint32 pos;
  void (*volume_change_callback) (gpointer, gdouble);
  gpointer volume_change_callback_data;
  void (*ready_callback) ();
  guint32 release_frames;
  struct job_control control;	//Used to synchronize access to sample, frames, loop and pos members.
  gchar path[PATH_MAX];
  enum audio_src src;
  enum audio_status status;
  guint32 sel_start;
  gint64 sel_len;
  gboolean mono_mix;
  guint record_options;
  void (*monitor) (void *, gdouble);
  void *monitor_data;
};

void audio_start_playback (struct audio *);

void audio_stop_playback (struct audio *);

void audio_start_recording (struct audio *, guint, audio_monitor_notifier,
			    void *);

void audio_stop_recording (struct audio *);

gboolean audio_check (struct audio *);

void audio_reset_record_buffer (struct audio *, guint, audio_monitor_notifier,
				void *);

void audio_init (struct audio *, void (*)(gpointer, gdouble),
		 void (*)(), gpointer);

gint audio_run (struct audio *);

void audio_destroy (struct audio *);

void audio_reset_sample (struct audio *);

void audio_set_volume (struct audio *, gdouble);

void audio_write_to_output (struct audio *, void *, gint);

void audio_read_from_input (struct audio *, void *, gint);

void audio_prepare (struct audio *, enum audio_status);

void audio_delete_range (struct audio *, guint, guint);

guint audio_detect_start (struct audio *);

const gchar *audio_name ();

const gchar *audio_version ();

#endif
