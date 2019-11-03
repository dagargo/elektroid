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
#include "audio.h"
#include "utils.h"

#define PA_BUFFER_LEN 4800

static const pa_buffer_attr buffer_attributes = {
  .maxlength = -1,
  .tlength = PA_BUFFER_LEN * 2,
  .prebuf = 0,
  .minreq = -1
};

static const pa_sample_spec sample_spec = {
  .format = PA_SAMPLE_S16LE,
  .channels = 1,
  .rate = 48000
};

static void
write_callback (pa_stream * stream, size_t size, void *data)
{
  guint req_frames;
  guint frames_to_copy;
  guint max_frames;
  struct audio *audio = data;

  if (!audio->sample->len)
    {
      return;
    }

  if (audio->pos == audio->sample->len)
    {
      if (audio->loop)
	{
	  audio->pos = 0;
	}
      else
	{
	  audio_stop (audio);
	  return;
	}
    }

  req_frames = size / 2;
  max_frames = req_frames > PA_BUFFER_LEN ? PA_BUFFER_LEN : req_frames;

  if (audio->pos + max_frames <= audio->sample->len)
    {
      frames_to_copy = max_frames;
    }
  else
    {
      frames_to_copy = audio->sample->len - audio->pos;
    }

  debug_print (2, "Writing %2d frames...\n", frames_to_copy);
  pa_stream_write (stream, &audio->sample->data[audio->pos * 2],
		   frames_to_copy * 2, NULL, 0, PA_SEEK_RELATIVE);

  audio->pos += frames_to_copy;
}

void
audio_stop (struct audio *audio)
{
  pa_operation *operation;

  if (!audio->stream)
    {
      return;
    }

  debug_print (1, "Stopping audio...\n");

  operation = pa_stream_flush (audio->stream, NULL, NULL);
  if (operation != NULL)
    {
      pa_operation_unref (operation);
    }

  operation = pa_stream_drain (audio->stream, NULL, NULL);
  if (operation != NULL)
    {
      pa_operation_unref (operation);
    }

  operation = pa_stream_cork (audio->stream, 1, NULL, NULL);
  if (operation != NULL)
    {
      pa_operation_unref (operation);
    }
}

void
audio_play (struct audio *audio)
{
  pa_operation *operation;

  if (!audio->stream)
    {
      return;
    }

  audio_stop (audio);
  debug_print (1, "Playing audio...\n");
  audio->pos = 0;

  operation = pa_stream_cork (audio->stream, 0, NULL, NULL);
  if (operation != NULL)
    {
      pa_operation_unref (operation);
    }
}

static void
connect_callback (pa_stream * stream, void *data)
{
  struct audio *audio = data;

  if (pa_stream_get_state (stream) == PA_STREAM_READY)
    {
      pa_stream_set_write_callback (stream, write_callback, audio);
    }
}

static void
context_callback (pa_context * context, void *data)
{
  struct audio *audio = data;
  pa_stream_flags_t stream_flags =
    PA_STREAM_START_CORKED | PA_STREAM_INTERPOLATE_TIMING |
    PA_STREAM_NOT_MONOTONIC | PA_STREAM_AUTO_TIMING_UPDATE |
    PA_STREAM_ADJUST_LATENCY;

  if (pa_context_get_state (context) == PA_CONTEXT_READY)
    {
      audio->stream = pa_stream_new (context, PACKAGE, &sample_spec, NULL);
      pa_stream_set_state_callback (audio->stream, connect_callback, audio);
      pa_stream_connect_playback (audio->stream, NULL, &buffer_attributes,
				  stream_flags, NULL, NULL);
    }
}

int
audio_init (struct audio *audio)
{
  pa_mainloop_api *api;
  int err = 0;

  debug_print (1, "Initializing audio...\n");

  audio->sample = g_array_new (FALSE, FALSE, sizeof (gshort));
  audio->loop = FALSE;
  audio->mainloop = pa_glib_mainloop_new (NULL);
  api = pa_glib_mainloop_get_api (audio->mainloop);
  audio->context = pa_context_new (api, PACKAGE);
  audio->stream = NULL;

  if (pa_context_connect (audio->context, NULL, PA_CONTEXT_NOFLAGS, NULL) < 0)
    {
      pa_context_unref (audio->context);
      pa_glib_mainloop_free (audio->mainloop);
      audio->mainloop = NULL;
      err = -1;
    }

  pa_context_set_state_callback (audio->context, context_callback, audio);

  return err;
}

void
audio_destroy (struct audio *audio)
{
  debug_print (1, "Destroying audio...\n");

  audio_stop (audio);
  g_array_free (audio->sample, TRUE);
  if (audio->stream)
    {
      pa_stream_unref (audio->stream);
      audio->stream = NULL;
    }

  if (audio->mainloop)
    {
      pa_context_unref (audio->context);
      pa_glib_mainloop_free (audio->mainloop);
      audio->mainloop = NULL;
    }
}

int
audio_check (struct audio *audio)
{
  return audio->mainloop ? 1 : 0;
}

void
audio_reset_sample (struct audio *audio)
{
  g_array_set_size (audio->sample, 0);
  audio->pos = 0;
}
