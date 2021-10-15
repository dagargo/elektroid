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

#define PA_BUFFER_LEN 4800
#define CHANNELS 1

static const pa_buffer_attr buffer_attributes = {
  .maxlength = -1,
  .tlength = PA_BUFFER_LEN * 2,
  .prebuf = 0,
  .minreq = -1
};

static const pa_sample_spec sample_spec = {
  .format = PA_SAMPLE_S16LE,
  .channels = CHANNELS,
  .rate = 48000
};

static void
audio_write_callback (pa_stream * stream, size_t size, void *data)
{
  struct audio *audio = data;
  guint req_frames;
  void *buffer;
  gshort *v;
  gint i;

  if (audio->release_frames > PA_BUFFER_LEN)
    {
      pa_stream_cork (audio->stream, 1, NULL, NULL);
      return;
    }

  req_frames = size >> 1;
  debug_print (2, "Writing %2d frames...\n", req_frames);
  pa_stream_begin_write (stream, &buffer, &size);

  g_mutex_lock (&audio->control.mutex);

  if (!audio->sample->len)
    {
      g_mutex_unlock (&audio->control.mutex);
      pa_stream_cancel_write (stream);
      debug_print (2, "Canceled\n");
      return;
    }

  if (audio->pos == audio->sample->len >> 1 && !audio->loop)
    {
      g_mutex_unlock (&audio->control.mutex);
      memset (buffer, 0, size);
      pa_stream_write (stream, buffer, size, NULL, 0, PA_SEEK_RELATIVE);
      audio->release_frames += req_frames;
      return;
    }

  v = buffer;
  for (i = 0; i < req_frames; i++)
    {
      if (audio->pos < audio->sample->len >> 1)
	{
	  *v = ((short *) audio->sample->data)[audio->pos];
	  audio->pos++;
	}
      else
	{
	  if (audio->loop)
	    {
	      audio->pos = 0;
	      *v = ((short *) audio->sample->data)[0];
	    }
	  else
	    {
	      break;
	    }
	}
      v++;
    }

  g_mutex_unlock (&audio->control.mutex);

  pa_stream_write (stream, buffer, i * 2, NULL, 0, PA_SEEK_RELATIVE);
}

void
audio_stop (struct audio *audio, gboolean flush)
{
  pa_operation *operation;

  if (!audio->stream)
    {
      return;
    }

  debug_print (1, "Stopping audio...\n");

  pa_threaded_mainloop_lock (audio->mainloop);
  if (flush)
    {
      operation = pa_stream_flush (audio->stream, NULL, NULL);
      if (operation != NULL)
	{
	  pa_operation_unref (operation);
	}
    }

  operation = pa_stream_cork (audio->stream, 1, NULL, NULL);
  if (operation != NULL)
    {
      pa_operation_unref (operation);
    }
  pa_threaded_mainloop_unlock (audio->mainloop);
}

void
audio_play (struct audio *audio)
{
  pa_operation *operation;

  if (!audio->stream)
    {
      return;
    }

  audio_stop (audio, TRUE);

  debug_print (1, "Playing audio...\n");

  g_mutex_lock (&audio->control.mutex);
  audio->pos = 0;
  audio->release_frames = 0;
  g_mutex_unlock (&audio->control.mutex);

  pa_threaded_mainloop_lock (audio->mainloop);
  operation = pa_stream_cork (audio->stream, 0, NULL, NULL);
  if (operation != NULL)
    {
      pa_operation_unref (operation);
    }
  pa_threaded_mainloop_unlock (audio->mainloop);
}

static void
audio_set_sink_volume (pa_context * context, const pa_sink_input_info * info,
		       int eol, void *data)
{
  struct audio *audio = data;

  if (info && pa_cvolume_valid (&info->volume))
    {
      gdouble v = pa_sw_volume_to_linear (pa_cvolume_avg (&info->volume));
      audio->volume_change_callback (v);
    }
}

static void
audio_notify (pa_context * context, pa_subscription_event_type_t type,
	      uint32_t index, void *data)
{
  struct audio *audio = data;

  if (audio->context != context)
    {
      return;
    }

  if ((type & PA_SUBSCRIPTION_EVENT_FACILITY_MASK) ==
      PA_SUBSCRIPTION_EVENT_SINK_INPUT)
    {
      if ((type & PA_SUBSCRIPTION_EVENT_TYPE_MASK) ==
	  PA_SUBSCRIPTION_EVENT_CHANGE)
	{
	  pa_context_get_sink_input_info (audio->context, audio->index,
					  audio_set_sink_volume, audio);
	}
    }
}

static void
audio_connect_callback (pa_stream * stream, void *data)
{
  struct audio *audio = data;

  if (pa_stream_get_state (stream) == PA_STREAM_READY)
    {
      pa_stream_set_write_callback (stream, audio_write_callback, audio);
      audio->index = pa_stream_get_index (audio->stream);
      debug_print (2, "Sink index: %d\n", audio->index);
      pa_context_get_sink_input_info (audio->context, audio->index,
				      audio_set_sink_volume, audio);
    }
}

static void
audio_context_callback (pa_context * context, void *data)
{
  struct audio *audio = data;
  pa_operation *operation;
  pa_proplist *props = pa_proplist_new ();
  pa_stream_flags_t stream_flags =
    PA_STREAM_START_CORKED | PA_STREAM_INTERPOLATE_TIMING |
    PA_STREAM_NOT_MONOTONIC | PA_STREAM_AUTO_TIMING_UPDATE |
    PA_STREAM_ADJUST_LATENCY;


  if (pa_context_get_state (context) == PA_CONTEXT_READY)
    {
      pa_proplist_set (props,
		       PA_PROP_APPLICATION_ICON_NAME,
		       PACKAGE, sizeof (PACKAGE));
      audio->stream =
	pa_stream_new_with_proplist (context, PACKAGE, &sample_spec, NULL,
				     props);
      pa_proplist_free (props);
      pa_stream_set_state_callback (audio->stream, audio_connect_callback,
				    audio);
      pa_stream_connect_playback (audio->stream, NULL, &buffer_attributes,
				  stream_flags, NULL, NULL);
      pa_context_set_subscribe_callback (audio->context, audio_notify, audio);
      operation =
	pa_context_subscribe (audio->context,
			      PA_SUBSCRIPTION_MASK_SINK_INPUT, NULL, NULL);
      if (operation != NULL)
	{
	  pa_operation_unref (operation);
	}
    }
}

gint
audio_init (struct audio *audio, void (*volume_change_callback) (gdouble),
	    void (*load_progress_callback) (gdouble))
{
  pa_mainloop_api *api;
  gint err = 0;

  debug_print (1, "Initializing audio...\n");

  audio->sample = g_byte_array_new ();
  audio->frames = 0;
  audio->loop = FALSE;
  audio->mainloop = pa_threaded_mainloop_new ();
  api = pa_threaded_mainloop_get_api (audio->mainloop);
  audio->context = pa_context_new (api, PACKAGE);
  audio->stream = NULL;
  audio->index = PA_INVALID_INDEX;
  audio->volume_change_callback = volume_change_callback;
  audio->control.callback = load_progress_callback;
  audio->name = malloc (PATH_MAX);
  audio->name[0] = 0;

  if (pa_context_connect (audio->context, NULL, PA_CONTEXT_NOFLAGS, NULL) < 0)
    {
      pa_context_unref (audio->context);
      pa_threaded_mainloop_free (audio->mainloop);
      audio->mainloop = NULL;
      err = -1;
    }
  else
    {
      pa_context_set_state_callback (audio->context, audio_context_callback,
				     audio);
    }

  pa_threaded_mainloop_start (audio->mainloop);

  return err;
}

void
audio_destroy (struct audio *audio)
{
  debug_print (1, "Destroying audio...\n");

  audio_stop (audio, TRUE);

  pa_threaded_mainloop_stop (audio->mainloop);

  g_byte_array_free (audio->sample, TRUE);
  if (audio->stream)
    {
      pa_stream_unref (audio->stream);
      audio->stream = NULL;
    }

  if (audio->mainloop)
    {
      pa_context_unref (audio->context);
      pa_threaded_mainloop_free (audio->mainloop);
      audio->mainloop = NULL;
    }

  g_free (audio->name);
}

gboolean
audio_check (struct audio *audio)
{
  return audio->mainloop ? TRUE : FALSE;
}

void
audio_reset_sample (struct audio *audio)
{
  g_mutex_lock (&audio->control.mutex);
  g_byte_array_set_size (audio->sample, 0);
  audio->frames = 0;
  audio->pos = 0;
  g_mutex_unlock (&audio->control.mutex);
}

void
audio_set_volume (struct audio *audio, gdouble volume)
{
  pa_operation *operation;
  pa_volume_t v;

  if (audio->index != PA_INVALID_INDEX)
    {
      debug_print (1, "Setting volume to %f...\n", volume);
      v = pa_sw_volume_from_linear (volume);
      pa_cvolume_set (&audio->volume, CHANNELS, v);

      operation =
	pa_context_set_sink_input_volume (audio->context, audio->index,
					  &audio->volume, NULL, NULL);
      if (operation != NULL)
	{
	  pa_operation_unref (operation);
	}
    }
}
