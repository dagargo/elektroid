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

#define AUDIO_BUF_FRAMES 512
#define AUDIO_CHANNELS 2	// Audio system is always stereo
#define BYTES_PER_FRAME (AUDIO_CHANNELS * sizeof(gint16))
#define MAX_RECORDING_TIME_S 30

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
  guint32 release_frames;
  struct job_control control;	//Used to synchronize access to sample, frames, loop and pos members.
  gchar path[PATH_MAX];
  enum audio_src src;
  enum audio_status status;
  guint32 sel_start;
  gint64 sel_len;
  gboolean mono_mix;
};

void audio_start_playback (struct audio *);

void audio_stop_playback (struct audio *);

void audio_start_recording (struct audio *);

void audio_stop_recording (struct audio *);

gboolean audio_check (struct audio *);

void audio_reset_record_buffer (struct audio *);

void audio_init (struct audio *, void (*)(gpointer, gdouble), gpointer);

gint audio_run (struct audio *);

void audio_destroy (struct audio *);

void audio_reset_sample (struct audio *);

void audio_set_volume (struct audio *, gdouble);

void audio_write_to_output_buffer (struct audio *, void *, gint);

void audio_prepare (struct audio *, enum audio_status);

#endif
