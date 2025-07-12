/*
 *   audio_rtaudio.c
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

void audio_finish_recording ();

static struct rtaudio_stream_options STREAM_OPTIONS = {
  .flags = 0,
  .priority = 99,
  .name = APP_NAME
};

void
audio_stop_playback ()
{
  enum audio_status status;

  g_mutex_lock (&audio.control.controllable.mutex);
  status = audio.status;
  g_mutex_unlock (&audio.control.controllable.mutex);

  if (status != AUDIO_STATUS_PLAYING)
    {
      return;
    }

  g_mutex_lock (&audio.control.controllable.mutex);
  audio.status = AUDIO_STATUS_STOPPED;
  g_mutex_unlock (&audio.control.controllable.mutex);

  debug_print (1, "Stopping playback...");
  rtaudio_abort_stream (audio.playback_rtaudio);	//Stop and flush buffer
}

void
audio_start_playback ()
{
  audio_stop_playback ();
  audio_prepare (AUDIO_STATUS_PLAYING);
  debug_print (1, "Starting playback...");
  rtaudio_start_stream (audio.playback_rtaudio);
}

void
audio_stop_recording ()
{
  struct sample_info *sample_info = audio.sample.info;
  enum audio_status status;

  g_mutex_lock (&audio.control.controllable.mutex);
  status = audio.status;
  g_mutex_unlock (&audio.control.controllable.mutex);

  if (status != AUDIO_STATUS_RECORDING)
    {
      return;
    }

  g_mutex_lock (&audio.control.controllable.mutex);
  audio.status = AUDIO_STATUS_STOPPING_RECORD;
  g_mutex_unlock (&audio.control.controllable.mutex);

  audio_finish_recording ();

  debug_print (1, "Stopping recording (%d frames read)...",
	       sample_info->frames);
  rtaudio_abort_stream (audio.record_rtaudio);	//Stop and flush buffer
}

void
audio_start_recording (guint options,
		       audio_monitor_notifier monitor_notifier,
		       void *monitor_data)
{
  struct sample_info *sample_info;

  audio_stop_recording ();
  audio_reset_record_buffer (options, monitor_notifier, monitor_data);
  audio_prepare (AUDIO_STATUS_RECORDING);

  sample_info = audio.sample.info;
  debug_print (1, "Starting recording (max %d frames)...",
	       sample_info->frames);

  rtaudio_start_stream (audio.record_rtaudio);
}

int
audio_record_cb (void *out, void *in, unsigned int frames, double stream_time,
		 rtaudio_stream_status_t rtaudio_status, void *audio)
{
  audio_read_from_input (in, frames);
  return 0;
}

int
audio_playback_cb (void *out, void *in, unsigned int frames,
		   double stream_time, rtaudio_stream_status_t rtaudio_status,
		   void *audio)
{
  audio_write_to_output (out, frames);
  return 0;
}

void
audio_error_cb (rtaudio_error_t err, const char *msg)
{
  error_print ("Audio error: %s", msg);
}

void
audio_init_int ()
{
  gint i, err, dev_id;
  guint buffer_frames;
  rtaudio_device_info_t dev_info;
  struct rtaudio_stream_parameters playback_stream_params,
    record_stream_params;
  const rtaudio_api_t *apis = rtaudio_compiled_api ();
  gint api_count = rtaudio_get_num_compiled_apis ();
  rtaudio_format_t format =
    audio.float_mode ? RTAUDIO_FORMAT_FLOAT32 : RTAUDIO_FORMAT_SINT16;

  audio.playback_rtaudio = NULL;
  audio.record_rtaudio = NULL;

  for (i = 0; i < api_count; i++)
    {
      debug_print (2, "Testing %s API...", rtaudio_api_name (apis[i]));
#if defined(__linux__)
      if (apis[i] == RTAUDIO_API_LINUX_PULSE)
	{
	  break;
	}
#elif defined(__APPLE__)
      if (apis[i] == RTAUDIO_API_MACOSX_CORE)
	{
	  break;
	}
#elif defined(_WIN32)
      if (apis[i] == RTAUDIO_API_WINDOWS_DS)
	{
	  break;
	}
#endif
    }

  if (i == api_count)
    {
      return;
    }

  audio.playback_rtaudio = rtaudio_create (apis[i]);
  if (rtaudio_error (audio.playback_rtaudio))
    {
      error_print ("Error while initilizing playback RtAudio: %s",
		   rtaudio_error (audio.playback_rtaudio));
      return;
    }

  if (!rtaudio_device_count (audio.playback_rtaudio))
    {
      error_print ("No devices found");
      goto error_playback;
    }

  dev_id = rtaudio_get_default_output_device (audio.playback_rtaudio);
  playback_stream_params = (struct rtaudio_stream_parameters)
  {
    .device_id = dev_id,
    .num_channels = AUDIO_CHANNELS,
    .first_channel = 0
  };

  dev_info = rtaudio_get_device_info (audio.playback_rtaudio, dev_id);
  audio.rate = dev_info.preferred_sample_rate;
  buffer_frames = AUDIO_BUF_FRAMES;
  err = rtaudio_open_stream (audio.playback_rtaudio, &playback_stream_params,
			     NULL, format, audio.rate, &buffer_frames,
			     audio_playback_cb, NULL, &STREAM_OPTIONS,
			     audio_error_cb);
  if (err || !rtaudio_is_stream_open (audio.playback_rtaudio))
    {
      error_print
	("Error occurred while opening the playback RtAudio stream: %s",
	 rtaudio_error (audio.playback_rtaudio));
      goto error_playback;
    }

  debug_print (1,
	       "Using %s for playback with %d Hz sample rate and %d frames...",
	       dev_info.name, audio.rate, buffer_frames);

  audio.volume = 1.0;
  audio.volume_change_callback (audio.volume);

  audio.record_rtaudio = rtaudio_create (apis[i]);
  if (rtaudio_error (audio.record_rtaudio))
    {
      error_print ("Error while initilizing recording RtAudio: %s",
		   rtaudio_error (audio.record_rtaudio));
      goto error_playback;
    }

  if (!rtaudio_device_count (audio.record_rtaudio))
    {
      error_print ("No devices found");
      goto error_record;
    }

  dev_id = rtaudio_get_default_input_device (audio.record_rtaudio);
  record_stream_params = (struct rtaudio_stream_parameters)
  {
    .device_id = dev_id,
    .num_channels = AUDIO_CHANNELS,
    .first_channel = 0
  };

  dev_info = rtaudio_get_device_info (audio.record_rtaudio, dev_id);
  buffer_frames = AUDIO_BUF_FRAMES;
  err = rtaudio_open_stream (audio.record_rtaudio, NULL,
			     &record_stream_params, format, audio.rate,
			     &buffer_frames, audio_record_cb, NULL,
			     &STREAM_OPTIONS, audio_error_cb);
  if (err || !rtaudio_is_stream_open (audio.record_rtaudio))
    {
      error_print
	("Error occurred while opening the recording RtAudio stream: %s",
	 rtaudio_error (audio.record_rtaudio));
      goto error_record;
    }

  debug_print (1,
	       "Using %s for recording with %d Hz sample rate and %d frames...",
	       dev_info.name, audio.rate, buffer_frames);

  goto end;

error_record:
  rtaudio_destroy (audio.record_rtaudio);
  audio.record_rtaudio = NULL;
error_playback:
  rtaudio_destroy (audio.playback_rtaudio);
  audio.playback_rtaudio = NULL;
end:
  audio.ready_callback ();
}

void
audio_destroy_int ()
{
  if (audio_check ())
    {
      rtaudio_destroy (audio.playback_rtaudio);
      rtaudio_destroy (audio.record_rtaudio);
    }
}

gboolean
audio_check ()
{
  return audio.playback_rtaudio != NULL;
}

void
audio_set_volume (gdouble volume)
{
  audio.volume = volume;
}

const gchar *
audio_name ()
{
  return "RtAudio";
}

const gchar *
audio_version ()
{
  return rtaudio_version ();
}
