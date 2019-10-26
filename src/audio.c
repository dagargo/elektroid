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

gpointer
audio_play_task (gpointer data)
{
  int err;
  int remaining;
  short *buffer;
  int buffer_len;
  int len;
  struct audio *audio = data;

  if (!audio->pa_s)
    {
      return NULL;
    }

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

      if (remaining <= 0 && audio->loop)
	{
	  buffer = (short *) audio->sample->data;
	  remaining = audio->sample->len;
	}
    }

  return NULL;
}

void
audio_stop (struct audio *audio)
{
  int err;

  if (!audio->pa_s)
    {
      return;
    }

  g_mutex_lock (&audio->mutex);

  if (audio->playing == 0)
    {
      goto end;
    }

  debug_print (1, "Stopping audio...\n");

  audio->playing = 0;
  if (audio->play_thread)
    {
      g_thread_join (audio->play_thread);
      g_thread_unref (audio->play_thread);
    }
  audio->play_thread = NULL;

  if (pa_simple_flush (audio->pa_s, &err) < 0)
    fprintf (stderr, __FILE__ ": pa_simple_flush() failed: %s\n",
	     pa_strerror (err));

  if (pa_simple_drain (audio->pa_s, &err) < 0)
    fprintf (stderr, __FILE__ ": pa_simple_drain() failed: %s\n",
	     pa_strerror (err));

end:
  g_mutex_unlock (&audio->mutex);
}

void
audio_play (struct audio *audio)
{
  if (audio_check (audio))
    {
      audio_stop (audio);

      g_mutex_lock (&audio->mutex);

      debug_print (1, "Playing audio...\n");

      audio->playing = 1;
      audio->play_thread =
	g_thread_new ("audio_play_task", audio_play_task, audio);

      g_mutex_unlock (&audio->mutex);
    }
}

int
audio_init (struct audio *audio)
{
  int err = 0;

  audio->sample = g_array_new (FALSE, FALSE, sizeof (short));
  audio->loop = FALSE;

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

  debug_print (1, "Destroying audio...\n");

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
