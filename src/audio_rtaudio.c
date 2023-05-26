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

#define AUDIO_BUF_FRAMES 512

static struct rtaudio_stream_options STREAM_OPTIONS = {
  .flags = RTAUDIO_FLAGS_SCHEDULE_REALTIME,
  .num_buffers = 2,
  .priority = 99,
  .name = PACKAGE
};

void
audio_stop (struct audio *audio)
{
  enum audio_status status;

  g_mutex_lock (&audio->control.mutex);
  status = audio->status;
  audio->status = AUDIO_STATUS_STOPPED;
  g_mutex_unlock (&audio->control.mutex);

  if (status != AUDIO_STATUS_STOPPED)
    {
      rtaudio_abort_stream (audio->rtaudio);	//Stop and flush buffer
    }
}

void
audio_play (struct audio *audio)
{
  audio_prepare (audio);

  if (!rtaudio_is_stream_running (audio->rtaudio))
    {
      rtaudio_start_stream (audio->rtaudio);
    }
}

int
audio_cb (void *out, void *in, unsigned int frames, double stream_time,
	  rtaudio_stream_status_t status, void *userdata)
{
  struct audio *audio = userdata;

  if (audio->release_frames > AUDIO_BUF_FRAMES)
    {
      audio_stop (audio);
      return 0;
    }

  g_mutex_lock (&audio->control.mutex);

  if (audio->pos == audio->frames && !audio->loop)
    {
      g_mutex_unlock (&audio->control.mutex);
      audio_stop (audio);
      return 0;
    }

  audio_write_to_output_buffer (audio, out, frames);
  g_mutex_unlock (&audio->control.mutex);
  return 0;
}

void
audio_error_cb (rtaudio_error_t err, const char *msg)
{
  error_print ("Audio error: %s\n", msg);
}

void
audio_init_int (struct audio *audio)
{
  gint i, err, dev_count;
  guint dev_id, buffer_frames;
  rtaudio_device_info_t dev_info;
  struct rtaudio_stream_parameters out_stream_params;
  const rtaudio_api_t *apis = rtaudio_compiled_api ();
  gint api_count = rtaudio_get_num_compiled_apis ();

  audio->rtaudio = NULL;

  for (i = 0; i < api_count; i++)
    {
      debug_print (2, "Testing API %s...\n", rtaudio_api_name (apis[i]));
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
      if (apis[i] == RTAUDIO_API_WINDOWS_WASAPI)
	{
	  break;
	}
      else if (apis[i] == RTAUDIO_API_WINDOWS_DS)
	{
	  break;
	}
#endif
    }

  if (i == api_count)
    {
      return;
    }

  audio->rtaudio = rtaudio_create (apis[i]);
  if (rtaudio_error (audio->rtaudio))
    {
      error_print ("Error while initilizing RtAudio: %s\n",
		   rtaudio_error (audio->rtaudio));
      return;
    }

  dev_count = rtaudio_device_count (audio->rtaudio);
  if (dev_count == 0)
    {
      error_print ("No devices found\n");
      goto error;
    }

  rtaudio_get_default_output_device (audio->rtaudio);
  dev_id = (guint) rtaudio_get_default_output_device (audio->rtaudio);
  dev_info = rtaudio_get_device_info (audio->rtaudio, dev_id);

  buffer_frames = AUDIO_BUF_FRAMES;
  audio->samplerate = dev_info.preferred_sample_rate;
  debug_print (1, "Using %s with %d Hz sample rate and %d frames...\n",
	       dev_info.name, audio->samplerate, buffer_frames);


  out_stream_params = (struct rtaudio_stream_parameters)
  {
    .device_id = dev_id,
    .num_channels = AUDIO_CHANNELS,
    .first_channel = 0
  };

  err = rtaudio_open_stream (audio->rtaudio, &out_stream_params, NULL,
			     RTAUDIO_FORMAT_SINT16, audio->samplerate,
			     &buffer_frames, audio_cb, audio,
			     &STREAM_OPTIONS, audio_error_cb);
  if (err || !rtaudio_is_stream_open (audio->rtaudio))
    {
      error_print ("Error occurred while opening the RtAudio stream: %s\n",
		   rtaudio_error (audio->rtaudio));
      goto error;
    }

  audio->volume = 1.0;
  audio->volume_change_callback (audio->volume_change_callback_data,
				 audio->volume);
  return;

error:
  rtaudio_destroy (audio->rtaudio);
  audio->rtaudio = NULL;
}

gint
audio_run (struct audio *audio)
{
  //Nothing to do here.
  return 0;
}

void
audio_destroy_int (struct audio *audio)
{
  if (audio_check (audio))
    {
      rtaudio_destroy (audio->rtaudio);
    }
}

gboolean
audio_check (struct audio *audio)
{
  return audio->rtaudio != NULL;
}

void
audio_set_volume (struct audio *audio, gdouble volume)
{
  audio->volume = volume;
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
