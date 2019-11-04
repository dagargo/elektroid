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
#include <pulse/glib-mainloop.h>

struct audio
{
  GArray *sample;
  gint frames;
  gboolean loop;
  pa_glib_mainloop *mainloop;
  pa_context *context;
  pa_stream *stream;
  gint pos;
  pa_cvolume volume;
  uint32_t index;
  void (*set_volume_gui_callback) (gdouble);
};

void audio_play (struct audio *);

void audio_stop (struct audio *);

int audio_check (struct audio *);

int audio_init (struct audio *, void (*)(gdouble));

void audio_destroy (struct audio *);

void audio_reset_sample (struct audio *);

void audio_set_volume (struct audio *, gdouble);
