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
#include <pulse/pulseaudio.h>
#include "utils.h"

struct audio
{
  GByteArray *sample;
  guint frames;
  gboolean loop;
  pa_threaded_mainloop *mainloop;
  pa_context *context;
  pa_stream *stream;
  gint pos;
  pa_cvolume volume;
  uint32_t index;
  void (*volume_change_callback) (gdouble);
  gint release_frames;
  struct job_control control;
  gchar path[PATH_MAX];
};

void audio_play (struct audio *);

void audio_stop (struct audio *, gboolean);

gboolean audio_check (struct audio *);

gint audio_init (struct audio *, void (*)(gdouble), void (*)(gdouble));

void audio_destroy (struct audio *);

void audio_reset_sample (struct audio *);

void audio_set_volume (struct audio *, gdouble);
