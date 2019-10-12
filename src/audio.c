/*
 *   audio.c
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

#include "../config.h"
#include <sndfile.h>
#include <samplerate.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <glib.h>
#include <netinet/in.h>
#include "audio.h"
#include "utils.h"

#define PA_BUFFER_LEN 4800

void
audio_play (struct audio *audio)
{
  int err;
  int remaining;
  short *buffer;
  int buffer_len;
  int len;

  if (!audio->pa_s)
    {
      return;
    }

  audio->playing = 1;
  buffer = (short *) audio->sample->data;
  remaining = audio->sample->len;
  buffer_len =
    (audio->sample->len < PA_BUFFER_LEN ? audio->sample->len : PA_BUFFER_LEN);
  while (remaining > 0 && audio->playing)
    {
      len = remaining > buffer_len ? buffer_len : remaining;
      //PulseAudio API uses bytes instead of samples
      if (pa_simple_write (audio->pa_s, buffer, len * sizeof (short), &err) <
	  0)
	fprintf (stderr, __FILE__ ": pa_simple_write() failed: %s\n",
		 pa_strerror (err));
      remaining -= buffer_len;
      buffer += buffer_len;
    }
}

void
audio_stop (struct audio *audio)
{
  int err;

  if (!audio->pa_s)
    {
      return;
    }

  audio->playing = 0;

  if (pa_simple_flush (audio->pa_s, &err) < 0)
    fprintf (stderr, __FILE__ ": pa_simple_flush() failed: %s\n",
	     pa_strerror (err));

  if (pa_simple_drain (audio->pa_s, &err) < 0)
    fprintf (stderr, __FILE__ ": pa_simple_drain() failed: %s\n",
	     pa_strerror (err));
}

int
audio_init (struct audio *audio)
{
  int err = 0;

  audio->sample = g_array_new (FALSE, FALSE, sizeof (short));

  pa_sample_spec pa_ss;
  pa_ss.format = PA_SAMPLE_S16LE;
  pa_ss.channels = 1;
  pa_ss.rate = 48000;
  audio->pa_s = pa_simple_new (NULL,	// Use the default server.
			       PACKAGE,	// Our application's name.
			       PA_STREAM_PLAYBACK, NULL,	// Use the default device.
			       PACKAGE,	// Description of our stream.
			       &pa_ss,	// Our sample format.
			       NULL,	// Use default channel map
			       NULL,	// Use default buffering attributes.
			       NULL	// Ignore error code.
    );

  if (!audio->pa_s)
    {
      fprintf (stderr, __FILE__ ": No pulseaudio.\n");
    }

  return err;
}

void
audio_destroy (struct audio *audio)
{
  audio_stop (audio);

  g_array_free (audio->sample, TRUE);

  if (audio->pa_s)
    {
      pa_simple_free (audio->pa_s);
    }
}

int
audio_check (struct audio *audio)
{
  return audio->pa_s ? 1 : 0;
}
