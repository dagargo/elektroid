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

#include <glib/gi18n.h>
#include "audio.h"

void audio_finish_recording ();

#define WAIT_TIME_TO_STOP_US 10000

static void
audio_success_cb (pa_stream *stream, int success, void *data)
{
  pa_threaded_mainloop_signal (audio.mainloop, 0);
}

static void
audio_wait_success (pa_operation *operation)
{
  if (!operation)
    {
      debug_print (2, "No operation. Skipping wait...");
      return;
    }
  while (pa_operation_get_state (operation) != PA_OPERATION_DONE)
    {
      pa_threaded_mainloop_wait (audio.mainloop);
    }
}

static void
audio_read_callback (pa_stream *stream, size_t size, void *data)
{
  const void *buffer;
  size_t frame_size;

  if (pa_stream_peek (stream, &buffer, &size) < 0)
    {
      audio_stop_recording ();
      return;
    }

  frame_size = FRAME_SIZE (AUDIO_CHANNELS, sample_get_internal_format ());

  audio_read_from_input ((void *) buffer, size / frame_size);
  pa_stream_drop (stream);
}

static void
audio_write_callback (pa_stream *stream, size_t size, void *data)
{
  void *buffer;
  size_t frame_size;

  frame_size = FRAME_SIZE (AUDIO_CHANNELS, sample_get_internal_format ());

  pa_stream_begin_write (stream, &buffer, &size);
  audio_write_to_output (buffer, size / frame_size);
  pa_stream_write (stream, buffer, size, NULL, 0, PA_SEEK_RELATIVE);
}

void
audio_stop_and_flush_stream (pa_stream *stream)
{
  pa_operation *operation;

  if (pa_threaded_mainloop_in_thread (audio.mainloop))
    {
      pa_stream_flush (stream, NULL, NULL);
      pa_stream_cork (stream, 1, NULL, NULL);
    }
  else
    {
      pa_threaded_mainloop_lock (audio.mainloop);

      operation = pa_stream_flush (stream, audio_success_cb, NULL);
      audio_wait_success (operation);

      operation = pa_stream_cork (stream, 1, audio_success_cb, NULL);
      audio_wait_success (operation);

      pa_threaded_mainloop_unlock (audio.mainloop);
    }
}

void
audio_stop_playback ()
{
  g_mutex_lock (&audio.control.controllable.mutex);
  if (audio.status == AUDIO_STATUS_PREPARING_RECORD ||
      audio.status == AUDIO_STATUS_RECORDING ||
      audio.status == AUDIO_STATUS_STOPPING_RECORD)
    {
      g_mutex_unlock (&audio.control.controllable.mutex);
    }
  else if (audio.status == AUDIO_STATUS_PREPARING_PLAYBACK ||
	   audio.status == AUDIO_STATUS_PLAYING)
    {
      audio.status = AUDIO_STATUS_STOPPING_PLAYBACK;
      g_mutex_unlock (&audio.control.controllable.mutex);

      debug_print (1, "Stopping playback...");

      audio_stop_and_flush_stream (audio.playback_stream);

      g_mutex_lock (&audio.control.controllable.mutex);
      audio.status = AUDIO_STATUS_STOPPED;
      g_mutex_unlock (&audio.control.controllable.mutex);
    }
  else
    {
      while (audio.status != AUDIO_STATUS_STOPPED &&
	     !pa_threaded_mainloop_in_thread (audio.mainloop))
	{
	  g_mutex_unlock (&audio.control.controllable.mutex);
	  usleep (WAIT_TIME_TO_STOP_US);
	  g_mutex_lock (&audio.control.controllable.mutex);
	}
      g_mutex_unlock (&audio.control.controllable.mutex);
    }
}

void
audio_start_playback (audio_playback_cursor_notifier cursor_notifier)
{
  pa_operation *operation;

  audio_stop_playback ();

  g_mutex_lock (&audio.control.controllable.mutex);
  audio.cursor_notifier = cursor_notifier;
  g_mutex_unlock (&audio.control.controllable.mutex);

  debug_print (1, "Starting playback...");

  audio_prepare (AUDIO_STATUS_PREPARING_PLAYBACK);

  pa_threaded_mainloop_lock (audio.mainloop);
  operation = pa_stream_cork (audio.playback_stream, 0, audio_success_cb,
			      NULL);
  audio_wait_success (operation);
  pa_threaded_mainloop_unlock (audio.mainloop);
}

void
audio_stop_recording ()
{
  if (!audio.record_stream)
    {
      return;
    }

  g_mutex_lock (&audio.control.controllable.mutex);

  if (audio.status == AUDIO_STATUS_PREPARING_PLAYBACK ||
      audio.status == AUDIO_STATUS_PLAYING ||
      audio.status == AUDIO_STATUS_STOPPING_PLAYBACK)
    {
      g_mutex_unlock (&audio.control.controllable.mutex);
    }
  else if (audio.status == AUDIO_STATUS_PREPARING_RECORD ||
	   audio.status == AUDIO_STATUS_RECORDING)
    {
      audio.status = AUDIO_STATUS_STOPPING_RECORD;
      g_mutex_unlock (&audio.control.controllable.mutex);

      audio_finish_recording ();
      audio_stop_and_flush_stream (audio.record_stream);
    }
  else
    {
      while (audio.status != AUDIO_STATUS_STOPPED &&
	     !pa_threaded_mainloop_in_thread (audio.mainloop))
	{
	  g_mutex_unlock (&audio.control.controllable.mutex);
	  usleep (WAIT_TIME_TO_STOP_US);
	  g_mutex_lock (&audio.control.controllable.mutex);
	}
      g_mutex_unlock (&audio.control.controllable.mutex);
    }
}

void
audio_start_recording (guint32 options,
		       audio_monitor_notifier monitor_notifier,
		       void *monitor_data)
{
  pa_operation *operation;

  if (!audio.record_stream)
    {
      return;
    }

  audio_stop_recording ();
  audio_reset_record_buffer (options, monitor_notifier, monitor_data);
  audio_prepare (AUDIO_STATUS_PREPARING_RECORD);

  debug_print (1, "Starting recording...");

  pa_threaded_mainloop_lock (audio.mainloop);
  operation = pa_stream_cork (audio.record_stream, 0, audio_success_cb, NULL);
  audio_wait_success (operation);
  pa_threaded_mainloop_unlock (audio.mainloop);
}

static void
audio_set_sink_volume (pa_context *context, const pa_sink_input_info *info,
		       int eol, void *data)
{
  if (info && pa_cvolume_valid (&info->volume)
      && audio.volume_change_callback)
    {
      gdouble v = pa_sw_volume_to_linear (pa_cvolume_avg (&info->volume));
      debug_print (1, "Setting volume to %f...", v);
      audio.volume_change_callback (v);
    }
}

static void
audio_notify (pa_context *context, pa_subscription_event_type_t type,
	      uint32_t index, void *data)
{
  if (audio.context != context)
    {
      return;
    }

  if ((type & PA_SUBSCRIPTION_EVENT_FACILITY_MASK) ==
      PA_SUBSCRIPTION_EVENT_SINK_INPUT)
    {
      if ((type & PA_SUBSCRIPTION_EVENT_TYPE_MASK) ==
	  PA_SUBSCRIPTION_EVENT_CHANGE)
	{
	  pa_context_get_sink_input_info (audio.context,
					  audio.playback_index,
					  audio_set_sink_volume, NULL);
	}
    }
}

static void
audio_connect_playback_stream_callback (pa_stream *stream, void *data)
{
  if (pa_stream_get_state (stream) == PA_STREAM_READY)
    {
      pa_stream_set_write_callback (stream, audio_write_callback, NULL);
      audio.playback_index = pa_stream_get_index (audio.playback_stream);
      debug_print (2, "Sink index: %d", audio.playback_index);
      pa_context_get_sink_input_info (audio.context, audio.playback_index,
				      audio_set_sink_volume, NULL);
    }
}

static void
audio_connect_record_stream_callback (pa_stream *stream, void *data)
{
  if (pa_stream_get_state (stream) == PA_STREAM_READY)
    {
      pa_stream_set_read_callback (stream, audio_read_callback, NULL);
      audio.record_index = pa_stream_get_index (audio.record_stream);
      debug_print (2, "Sink index: %d", audio.record_index);
    }
}

void
audio_server_info_callback (pa_context *context, const pa_server_info *info,
			    void *data)
{
  pa_operation *operation;
  pa_stream_flags_t stream_flags =
    PA_STREAM_START_CORKED | PA_STREAM_INTERPOLATE_TIMING |
    PA_STREAM_NOT_MONOTONIC | PA_STREAM_AUTO_TIMING_UPDATE;
  pa_proplist *props = pa_proplist_new ();
  pa_buffer_attr buffer_attr = {
    .maxlength = AUDIO_BUF_BYTES,
    .tlength = -1,
    .prebuf = 0,
    .minreq = AUDIO_BUF_BYTES,
    .fragsize = AUDIO_BUF_BYTES
  };

  audio.rate = info->sample_spec.rate;
  audio.sample_spec.format =
    audio.float_mode ? PA_SAMPLE_FLOAT32NE : PA_SAMPLE_S16LE;
  audio.sample_spec.channels = AUDIO_CHANNELS;
  audio.sample_spec.rate = audio.rate;

  debug_print (1, "Using %d Hz sample rate...", audio.rate);

  pa_proplist_set (props, PA_PROP_APPLICATION_ICON_NAME, PACKAGE,
		   sizeof (PACKAGE));
  audio.playback_stream = pa_stream_new_with_proplist (context, _("Output"),
						       &audio.sample_spec,
						       NULL, props);
  audio.record_stream = pa_stream_new_with_proplist (context, _("Input"),
						     &audio.sample_spec,
						     NULL, props);
  pa_proplist_free (props);

  pa_stream_set_state_callback (audio.playback_stream,
				audio_connect_playback_stream_callback, NULL);
  pa_stream_connect_playback (audio.playback_stream, NULL, &buffer_attr,
			      stream_flags, NULL, NULL);

  pa_stream_set_state_callback (audio.record_stream,
				audio_connect_record_stream_callback, NULL);
  pa_stream_connect_record (audio.record_stream, NULL, &buffer_attr,
			    stream_flags);

  pa_context_set_subscribe_callback (audio.context, audio_notify, NULL);
  operation = pa_context_subscribe (audio.context,
				    PA_SUBSCRIPTION_MASK_SINK_INPUT, NULL,
				    NULL);
  if (operation != NULL)
    {
      pa_operation_unref (operation);
    }

  audio.ready_callback ();
}

static void
audio_context_callback (pa_context *context, void *data)
{
  if (pa_context_get_state (context) == PA_CONTEXT_READY)
    {
      pa_context_get_server_info (context, audio_server_info_callback, NULL);
    }
}

void
audio_init_int ()
{
  pa_mainloop_api *api;

  audio.playback_stream = NULL;
  audio.playback_index = PA_INVALID_INDEX;

  audio.record_stream = NULL;
  audio.record_index = PA_INVALID_INDEX;

  audio.mainloop = pa_threaded_mainloop_new ();
  if (!audio.mainloop)
    {
      audio.ready_callback ();
      return;
    }

  api = pa_threaded_mainloop_get_api (audio.mainloop);
  audio.context = pa_context_new (api, APP_NAME);

  if (pa_context_connect (audio.context, NULL, PA_CONTEXT_NOFLAGS, NULL) < 0)
    {
      pa_context_unref (audio.context);
      pa_threaded_mainloop_free (audio.mainloop);
      audio.mainloop = NULL;
      audio.ready_callback ();
      return;
    }
  else
    {
      pa_context_set_state_callback (audio.context, audio_context_callback,
				     NULL);
      pa_threaded_mainloop_start (audio.mainloop);
      pa_threaded_mainloop_wait (audio.mainloop);
    }
}

void
audio_destroy_int ()
{
  if (audio.mainloop)
    {
      pa_threaded_mainloop_stop (audio.mainloop);
      pa_context_disconnect (audio.context);
      pa_context_unref (audio.context);
      if (audio.playback_stream)
	{
	  pa_stream_unref (audio.playback_stream);
	  audio.playback_stream = NULL;
	}
      if (audio.record_stream)
	{
	  pa_stream_unref (audio.record_stream);
	  audio.record_stream = NULL;
	}
      pa_threaded_mainloop_free (audio.mainloop);
      audio.mainloop = NULL;
    }
}

gboolean
audio_check ()
{
  return audio.mainloop ? TRUE : FALSE;
}

void
audio_set_volume (gdouble volume)
{
  pa_operation *operation;
  pa_volume_t v;

  if (audio.playback_index != PA_INVALID_INDEX)
    {
      debug_print (1, "Setting volume to %f...", volume);
      v = pa_sw_volume_from_linear (volume);
      pa_cvolume_set (&audio.volume, AUDIO_CHANNELS, v);

      operation = pa_context_set_sink_input_volume (audio.context,
						    audio.playback_index,
						    &audio.volume, NULL,
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
