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

#include <math.h>
#include "audio.h"

void audio_init_int (struct audio *);
void audio_destroy_int (struct audio *);
const gchar *audio_name ();
const gchar *audio_version ();

static inline gint16
audio_mix_channels (gint16 ** src, guint channels, gdouble gain)
{
  gdouble mix = 0;
  for (gint i = 0; i < channels; i++, (*src)++)
    {
      mix += **src;
    }
  return (gint16) (mix * gain);
}

static inline void
audio_copy_sample (gint16 * dst, gint16 * src, struct audio *audio)
{
#if defined(ELEKTROID_RTAUDIO)
  *dst = (gint16) (*src * audio->volume);
#else
  *dst = *src;
#endif
}

void
audio_write_to_output (struct audio *audio, void *buffer, gint frames,
		       size_t size)
{
  gint16 *dst, *src;
  guint32 len =
    audio->sel_len ? audio->sel_start + audio->sel_len : audio->frames;
  guint channels = (((struct sample_info *) audio->control.data)->channels);
  guint bytes_per_frame = channels * sizeof (gint16);
  gdouble gain = 1.0 / sqrt (channels);

  debug_print (2, "Writing %d frames...\n", frames);

  memset (buffer, 0, size);

  g_mutex_lock (&audio->control.mutex);

  if ((audio->pos == len && !audio->loop) ||
      audio->status == AUDIO_STATUS_PREPARING_PLAYBACK ||
      audio->status == AUDIO_STATUS_STOPPING_PLAYBACK)
    {
      memset (buffer, 0, size);
      if (audio->status == AUDIO_STATUS_PREPARING_PLAYBACK)
	{
	  audio->status = AUDIO_STATUS_PLAYING;
	}
      else			//Stopping...
	{
	  audio->release_frames += frames;
	}
      goto end;
    }

  dst = buffer;

  src = (gint16 *) & audio->sample->data[audio->pos * bytes_per_frame];
  for (gint i = 0; i < frames; i++)
    {
      if (audio->pos == len)
	{
	  if (!audio->loop)
	    {
	      break;
	    }
	  debug_print (2, "Sample reset\n");
	  audio->pos = audio->sel_len ? audio->sel_start : 0;
	  src = (gint16 *) audio->sample->data;
	}

      if (audio->mono_mix)
	{
	  gint16 mix = audio_mix_channels (&src, channels, gain);
	  audio_copy_sample (dst, &mix, audio);
	  dst++;
	  audio_copy_sample (dst, &mix, audio);
	  dst++;
	}
      else
	{
	  audio_copy_sample (dst, src, audio);
	  src++;
	  dst++;
	  audio_copy_sample (dst, src, audio);
	  src++;
	  dst++;
	}

      audio->pos++;
    }

end:
  g_mutex_unlock (&audio->control.mutex);

  if (audio->release_frames > AUDIO_BUF_FRAMES)
    {
      audio_stop_playback (audio);
    }
}

void
audio_read_from_input (struct audio *audio, void *buffer, gint frames,
		       size_t size)
{
  size_t total_bytes, wsize;

  debug_print (2, "Reading %d frames...\n", frames);

  g_mutex_lock (&audio->control.mutex);
  total_bytes = audio->frames * BYTES_PER_FRAME;
  wsize = total_bytes - (size_t) audio->sample->len;
  wsize = wsize < size ? wsize : size;
  g_byte_array_append (audio->sample, buffer, wsize);
  g_mutex_unlock (&audio->control.mutex);

  if (wsize != size)
    {
      audio_stop_recording (audio);
    }
}

void
audio_reset_record_buffer (struct audio *audio)
{
  struct sample_info *sample_info = audio->control.data;
  audio->frames = audio->samplerate * MAX_RECORDING_TIME_S;
  guint size = audio->frames * BYTES_PER_FRAME;
  g_byte_array_set_size (audio->sample, size);
  audio->sample->len = 0;
  audio->pos = 0;
  sample_info->loopstart = 0;
  sample_info->loopend = 0;
  sample_info->looptype = 0;
  sample_info->samplerate = audio->samplerate;
  sample_info->bitdepth = 16;
  sample_info->channels = AUDIO_CHANNELS;
  sample_info->frames = audio->frames;
}

void
audio_init (struct audio *audio,
	    void (*volume_change_callback) (gpointer, gdouble), gpointer data)
{
  debug_print (1, "Initializing audio (%s %s)...\n", audio_name (),
	       audio_version ());
  audio->sample = g_byte_array_new ();
  audio->frames = 0;
  audio->loop = FALSE;
  audio->path[0] = 0;
  audio->status = AUDIO_STATUS_STOPPED;
  audio->volume_change_callback = volume_change_callback;
  audio->volume_change_callback_data = data;
  audio->control.data = g_malloc (sizeof (struct sample_info));
  audio->control.callback = NULL;
  audio->sel_len = 0;

  audio_init_int (audio);
}

void
audio_destroy (struct audio *audio)
{
  debug_print (1, "Destroying audio...\n");

  audio_stop_playback (audio);
  audio_stop_recording (audio);
  audio_reset_sample (audio);

  g_mutex_lock (&audio->control.mutex);

  audio_destroy_int (audio);

  g_free (audio->control.data);
  g_byte_array_free (audio->sample, TRUE);
  audio->sample = NULL;

  g_mutex_unlock (&audio->control.mutex);
}

void
audio_reset_sample (struct audio *audio)
{
  g_mutex_lock (&audio->control.mutex);
  debug_print (1, "Resetting sample...\n");
  g_byte_array_set_size (audio->sample, 0);
  audio->frames = 0;
  audio->pos = 0;
  audio->path[0] = 0;
  audio->release_frames = 0;
  audio->src = AUDIO_SRC_NONE;
  audio->status = AUDIO_STATUS_STOPPED;
  memset (audio->control.data, 0, sizeof (struct sample_info));
  g_mutex_unlock (&audio->control.mutex);
}

void
audio_prepare (struct audio *audio, enum audio_status status)
{
  g_mutex_lock (&audio->control.mutex);
  audio->pos = audio->sel_len ? audio->sel_start : 0;
  audio->release_frames = 0;
  audio->status = status;
  g_mutex_unlock (&audio->control.mutex);
}
