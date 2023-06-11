/*
 *   audio_pa.c
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

#include "audio.h"

#define AUDIO_BUF_FRAMES 2048

static const pa_buffer_attr BUFFER_ATTR = {
  .maxlength = -1,
  .tlength = AUDIO_BUF_FRAMES << AUDIO_CHANNELS,	//bytes
  .prebuf = 0,
  .minreq = -1,
  .fragsize = AUDIO_BUF_FRAMES << AUDIO_CHANNELS
};

static void
audio_success_cb (pa_stream * stream, int success, void *data)
{
  struct audio *audio = data;
  pa_threaded_mainloop_signal (audio->mainloop, 0);
}

static void
audio_wait_success (struct audio *audio, pa_operation * operation)
{
  if (!operation)
    {
      debug_print (2, "No operation. Skipping wait...\n");
      return;
    }
  while (pa_operation_get_state (operation) != PA_OPERATION_DONE)
    {
      pa_threaded_mainloop_wait (audio->mainloop);
    }
}

static void
audio_read_callback (pa_stream * stream, size_t size, void *data)
{
  const void *buffer;
  size_t total_bytes, wsize;
  struct audio *audio = data;

  if (audio->release_frames > AUDIO_BUF_FRAMES)
    {
      return;
    }

  if (pa_stream_peek (stream, &buffer, &size) < 0)
    {
      return;
    }

  g_mutex_lock (&audio->control.mutex);
  total_bytes = audio->frames << AUDIO_CHANNELS;
  wsize = total_bytes - (size_t) audio->sample->len;
  wsize = wsize < size ? wsize : size;

  debug_print (2, "Audio read (%ld bytes)\n", wsize);

  g_byte_array_append (audio->sample, buffer, wsize);

  pa_stream_drop (stream);

  g_mutex_unlock (&audio->control.mutex);
}

static void
audio_write_callback (pa_stream * stream, size_t size, void *data)
{
  struct audio *audio = data;
  guint32 frames;
  void *buffer;

  if (audio->release_frames > AUDIO_BUF_FRAMES)
    {
      pa_stream_cork (audio->playback_stream, 1, NULL, NULL);
      return;
    }

  frames = size >> AUDIO_CHANNELS;

  pa_stream_begin_write (stream, &buffer, &size);

  g_mutex_lock (&audio->control.mutex);

  if (!audio->sample || !audio->sample->len)
    {
      g_mutex_unlock (&audio->control.mutex);
      pa_stream_cancel_write (stream);
      debug_print (2, "Cancelled\n");
      return;
    }

  audio_write_to_output_buffer (audio, buffer, frames);

  g_mutex_unlock (&audio->control.mutex);

  pa_stream_write (stream, buffer, size, NULL, 0, PA_SEEK_RELATIVE);
}

void
audio_stop_playback (struct audio *audio)
{
  pa_operation *operation;

  if (!audio->playback_stream)
    {
      return;
    }

  g_mutex_lock (&audio->control.mutex);
  if (audio->status == AUDIO_STATUS_PREPARING_RECORD ||
      audio->status == AUDIO_STATUS_RECORDING ||
      audio->status == AUDIO_STATUS_STOPPING_RECORD)
    {
      g_mutex_unlock (&audio->control.mutex);
    }
  else if (audio->status == AUDIO_STATUS_PREPARING_PLAYBACK ||
	   audio->status == AUDIO_STATUS_PLAYING)
    {
      audio->status = AUDIO_STATUS_STOPPING_PLAYBACK;
      g_mutex_unlock (&audio->control.mutex);

      debug_print (1, "Stopping playback audio stream...\n");

      pa_threaded_mainloop_lock (audio->mainloop);
      operation =
	pa_stream_flush (audio->playback_stream, audio_success_cb, audio);
      audio_wait_success (audio, operation);

      operation =
	pa_stream_cork (audio->playback_stream, 1, audio_success_cb, audio);
      audio_wait_success (audio, operation);
      pa_threaded_mainloop_unlock (audio->mainloop);

      g_mutex_lock (&audio->control.mutex);
      audio->status = AUDIO_STATUS_STOPPED;
      g_mutex_unlock (&audio->control.mutex);
    }
  else
    {
      while (audio->status != AUDIO_STATUS_STOPPED)
	{
	  g_mutex_unlock (&audio->control.mutex);
	  usleep (100000);
	  g_mutex_lock (&audio->control.mutex);
	}
      g_mutex_unlock (&audio->control.mutex);
    }
}

void
audio_start_playback (struct audio *audio)
{
  pa_operation *operation;

  if (!audio->playback_stream)
    {
      return;
    }

  audio_stop_playback (audio);

  debug_print (1, "Playing audio...\n");

  audio_prepare (audio, AUDIO_STATUS_PREPARING_PLAYBACK);

  pa_threaded_mainloop_lock (audio->mainloop);
  operation =
    pa_stream_cork (audio->playback_stream, 0, audio_success_cb, audio);
  audio_wait_success (audio, operation);
  pa_threaded_mainloop_unlock (audio->mainloop);
}

void
audio_stop_recording (struct audio *audio)
{
  pa_operation *operation;
  struct sample_info *sample_info = audio->control.data;

  if (!audio->record_stream)
    {
      return;
    }

  g_mutex_lock (&audio->control.mutex);

  if (audio->status == AUDIO_STATUS_PREPARING_PLAYBACK ||
      audio->status == AUDIO_STATUS_PLAYING ||
      audio->status == AUDIO_STATUS_STOPPING_PLAYBACK)
    {
      g_mutex_unlock (&audio->control.mutex);
    }
  else if (audio->status == AUDIO_STATUS_PREPARING_RECORD ||
	   audio->status == AUDIO_STATUS_RECORDING)
    {
      audio->status = AUDIO_STATUS_STOPPING_RECORD;
      g_mutex_unlock (&audio->control.mutex);

      debug_print (1, "Stopping record audio stream (%d frames read)...\n",
		   audio->frames);

      pa_threaded_mainloop_lock (audio->mainloop);
      operation =
	pa_stream_flush (audio->record_stream, audio_success_cb, audio);
      audio_wait_success (audio, operation);

      operation =
	pa_stream_cork (audio->record_stream, 1, audio_success_cb, audio);
      audio_wait_success (audio, operation);
      pa_threaded_mainloop_unlock (audio->mainloop);

      g_mutex_lock (&audio->control.mutex);
      audio->status = AUDIO_STATUS_STOPPED;
      audio->frames = audio->sample->len >> sample_info->channels;
      sample_info->frames = audio->frames;
      g_mutex_unlock (&audio->control.mutex);
    }
  else
    {
      while (audio->status != AUDIO_STATUS_STOPPED)
	{
	  g_mutex_unlock (&audio->control.mutex);
	  usleep (100000);
	  g_mutex_lock (&audio->control.mutex);
	}
      g_mutex_unlock (&audio->control.mutex);
    }
}

void
audio_start_recording (struct audio *audio, guint channels)
{
  pa_operation *operation;
  struct sample_info *sample_info = audio->control.data;

  if (!audio->record_stream)
    {
      return;
    }

  audio_stop_recording (audio);

  audio_prepare (audio, AUDIO_STATUS_PREPARING_RECORD);
  audio->frames = audio->samplerate * MAX_RECORDING_TIME_S;
  g_byte_array_set_size (audio->sample, audio->frames << channels);
  audio->sample->len = 0;
  audio->pos = 0;
  sample_info->loopstart = 0;
  sample_info->loopend = 0;
  sample_info->looptype = 0;
  sample_info->samplerate = audio->samplerate;
  sample_info->bitdepth = 16;
  sample_info->channels = channels;
  sample_info->frames = audio->frames;

  debug_print (1, "Recording audio (max %d frames)...\n", audio->frames);

  pa_threaded_mainloop_lock (audio->mainloop);
  operation = pa_stream_cork (audio->record_stream, 0, audio_success_cb,
			      audio);
  audio_wait_success (audio, operation);
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
      debug_print (1, "Setting volume to %f...\n", v);
      audio->volume_change_callback (audio->volume_change_callback_data, v);
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
	  pa_context_get_sink_input_info (audio->context,
					  audio->playback_index,
					  audio_set_sink_volume, audio);
	}
    }
}

static void
audio_connect_playback_stream_callback (pa_stream * stream, void *data)
{
  struct audio *audio = data;

  if (pa_stream_get_state (stream) == PA_STREAM_READY)
    {
      pa_stream_set_write_callback (stream, audio_write_callback, audio);
      audio->playback_index = pa_stream_get_index (audio->playback_stream);
      debug_print (2, "Sink index: %d\n", audio->playback_index);
      pa_context_get_sink_input_info (audio->context, audio->playback_index,
				      audio_set_sink_volume, audio);
    }
}

static void
audio_connect_record_stream_callback (pa_stream * stream, void *data)
{
  struct audio *audio = data;

  if (pa_stream_get_state (stream) == PA_STREAM_READY)
    {
      pa_stream_set_read_callback (stream, audio_read_callback, audio);
      audio->record_index = pa_stream_get_index (audio->record_stream);
      debug_print (2, "Sink index: %d\n", audio->record_index);
    }
}

void
audio_server_info_callback (pa_context * context, const pa_server_info * info,
			    void *data)
{
  struct audio *audio = data;
  pa_operation *operation;
  pa_stream_flags_t stream_flags =
    PA_STREAM_START_CORKED | PA_STREAM_INTERPOLATE_TIMING |
    PA_STREAM_NOT_MONOTONIC | PA_STREAM_AUTO_TIMING_UPDATE |
    PA_STREAM_ADJUST_LATENCY;
  pa_proplist *props = pa_proplist_new ();

  audio->samplerate = info->sample_spec.rate;
  audio->sample_spec.format = PA_SAMPLE_S16LE;
  audio->sample_spec.channels = AUDIO_CHANNELS;
  audio->sample_spec.rate = audio->samplerate;

  debug_print (1, "Using %d Hz sample rate...\n", audio->samplerate);

  pa_proplist_set (props, PA_PROP_APPLICATION_ICON_NAME, PACKAGE,
		   sizeof (PACKAGE));
  audio->playback_stream = pa_stream_new_with_proplist (context, PACKAGE,
							&audio->sample_spec,
							NULL, props);
  audio->record_stream = pa_stream_new_with_proplist (context, PACKAGE,
						      &audio->sample_spec,
						      NULL, props);
  pa_proplist_free (props);

  pa_stream_set_state_callback (audio->playback_stream,
				audio_connect_playback_stream_callback,
				audio);
  pa_stream_connect_playback (audio->playback_stream, NULL, &BUFFER_ATTR,
			      stream_flags, NULL, NULL);

  pa_stream_set_state_callback (audio->record_stream,
				audio_connect_record_stream_callback, audio);
  pa_stream_connect_record (audio->record_stream, NULL, &BUFFER_ATTR,
			    stream_flags);

  pa_context_set_subscribe_callback (audio->context, audio_notify, audio);
  operation =
    pa_context_subscribe (audio->context, PA_SUBSCRIPTION_MASK_SINK_INPUT,
			  NULL, NULL);
  if (operation != NULL)
    {
      pa_operation_unref (operation);
    }
}

static void
audio_context_callback (pa_context * context, void *data)
{
  struct audio *audio = data;
  if (pa_context_get_state (context) == PA_CONTEXT_READY)
    {
      pa_context_get_server_info (context, audio_server_info_callback, audio);
    }
}

void
audio_init_int (struct audio *audio)
{
  audio->playback_stream = NULL;
  audio->playback_index = PA_INVALID_INDEX;

  audio->record_stream = NULL;
  audio->record_index = PA_INVALID_INDEX;
}

gint
audio_run (struct audio *audio)
{
  pa_mainloop_api *api;
  audio->mainloop = pa_threaded_mainloop_new ();
  if (!audio->mainloop)
    {
      return -1;
    }

  api = pa_threaded_mainloop_get_api (audio->mainloop);
  audio->context = pa_context_new (api, PACKAGE);

  if (pa_context_connect (audio->context, NULL, PA_CONTEXT_NOFLAGS, NULL) < 0)
    {
      pa_context_unref (audio->context);
      pa_threaded_mainloop_free (audio->mainloop);
      audio->mainloop = NULL;
      return -1;
    }
  else
    {
      pa_context_set_state_callback (audio->context, audio_context_callback,
				     audio);
      pa_threaded_mainloop_start (audio->mainloop);
      pa_threaded_mainloop_wait (audio->mainloop);
    }

  return 0;
}

void
audio_destroy_int (struct audio *audio)
{
  if (audio->mainloop)
    {
      pa_threaded_mainloop_stop (audio->mainloop);
      pa_context_disconnect (audio->context);
      pa_context_unref (audio->context);
      if (audio->playback_stream)
	{
	  pa_stream_unref (audio->playback_stream);
	  audio->playback_stream = NULL;
	}
      if (audio->record_stream)
	{
	  pa_stream_unref (audio->record_stream);
	  audio->record_stream = NULL;
	}
      pa_threaded_mainloop_free (audio->mainloop);
      audio->mainloop = NULL;
    }
}

gboolean
audio_check (struct audio *audio)
{
  return audio->mainloop ? TRUE : FALSE;
}

void
audio_set_volume (struct audio *audio, gdouble volume)
{
  pa_operation *operation;
  pa_volume_t v;

  if (audio->playback_index != PA_INVALID_INDEX)
    {
      debug_print (1, "Setting volume to %f...\n", volume);
      v = pa_sw_volume_from_linear (volume);
      pa_cvolume_set (&audio->volume, AUDIO_CHANNELS, v);

      operation = pa_context_set_sink_input_volume (audio->context,
						    audio->playback_index,
						    &audio->volume, NULL,
						    NULL);
      if (operation != NULL)
	{
	  pa_operation_unref (operation);
	}
    }
}

const gchar *
audio_name ()
{
  return "PulseAudio";
}

const gchar *
audio_version ()
{
  return pa_get_library_version ();
}
