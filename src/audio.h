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
#include <stdio.h>
#include <pulse/simple.h>
#include <pulse/error.h>

struct audio
{
  pa_simple *pa_s;
  GArray *sample;
  gint frames;
  GThread *play_thread;
  GMutex mutex;
  gint playing;
  gint loop;
};

void audio_play (struct audio *);

void audio_stop (struct audio *);

int audio_check (struct audio *);

int audio_init (struct audio *);

void audio_destroy (struct audio *);
