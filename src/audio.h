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

#include <glib.h>
#include "utils.h"
#if defined(__linux__) && !defined(ELEKTROID_RTAUDIO)
#include <pulse/pulseaudio.h>
#else
#include "rtaudio_c.h"
#endif

#define AUDIO_BUF_FRAMES 2048 // At 48 kHz, it's around 50 ms
#define AUDIO_CHANNELS 2       // Audio system is always stereo

enum audio_src
{
  AUDIO_SRC_NONE,
  AUDIO_SRC_LOCAL,
  AUDIO_SRC_REMOTE
};

enum audio_status
{
  AUDIO_STATUS_PREPARING,
  AUDIO_STATUS_PLAYING,
  AUDIO_STATUS_STOPPING,
  AUDIO_STATUS_STOPPED
};

struct audio
{
// PulseAudio or RtAudio backend
#if defined(__linux__) && !defined(ELEKTROID_RTAUDIO)
  pa_threaded_mainloop *mainloop;
  pa_context *context;
  pa_stream *stream;
  pa_cvolume volume;
  guint32 index;
#else
  rtaudio_t rtaudio;
  gdouble volume;
#endif
  GByteArray *sample;
  guint32 frames;
  gboolean loop;
  guint32 pos;
  void (*volume_change_callback) (gdouble);
  guint32 release_frames;
  struct job_control control;	//Used to synchronize access to sample, frames, loop and pos members.
  gchar path[PATH_MAX];
  guint32 channels;
  enum audio_src src;
  enum audio_status status;
};

void audio_play (struct audio *);

void audio_stop (struct audio *);

gboolean audio_check (struct audio *);

void audio_init (struct audio *, void (*)(gdouble), job_control_callback);

gint audio_run (struct audio *);

void audio_destroy (struct audio *);

void audio_reset_sample (struct audio *);

void audio_set_volume (struct audio *, gdouble);

void audio_write_to_output_buffer (struct audio *, void *, gint);
