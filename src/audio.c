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

#define FRAMES_TO_MONITOR 10000

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
audio_write_to_output (struct audio *audio, void *buffer, gint frames)
{
  gint16 *dst, *src;
  guint32 len =
    audio->sel_len ? audio->sel_start + audio->sel_len : audio->frames;
  guint channels = AUDIO_SAMPLE_CHANNELS (audio);
  guint bytes_per_frame = BYTES_PER_FRAME (channels);
  gdouble gain = 1.0 / sqrt (channels);
  size_t size = frames * BYTES_PER_FRAME (AUDIO_CHANNELS);

  debug_print (2, "Writing %d frames...\n", frames);

  memset (buffer, 0, size);

  g_mutex_lock (&audio->control.mutex);

  if ((audio->pos == len && !audio->loop) ||
      audio->status == AUDIO_STATUS_PREPARING_PLAYBACK ||
      audio->status == AUDIO_STATUS_STOPPING_PLAYBACK)
    {
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
audio_read_from_input (struct audio *audio, void *buffer, gint frames)
{
  static gint monitor_frames = 0;
  static gint16 level = 0;
  gint16 *data;
  guint recorded_frames, remaining_frames, recording_frames;
  guint channels =
    (audio->record_options & RECORD_STEREO) == RECORD_STEREO ? 2 : 1;
  guint bytes_per_frame = channels * sizeof (gint16);
  guint record = !(audio->record_options & RECORD_MONITOR_ONLY);
  struct sample_info *sample_info = audio->control.data;

  debug_print (2, "Reading %d frames (recording = %d)...\n", frames, record);

  g_mutex_lock (&audio->control.mutex);
  recorded_frames = audio->sample->len / bytes_per_frame;
  remaining_frames = sample_info->frames - recorded_frames;
  recording_frames = remaining_frames > frames ? frames : remaining_frames;

  if (channels == 2)
    {
      if (record)
	{
	  g_byte_array_append (audio->sample, buffer,
			       recording_frames * bytes_per_frame);
	}
      data = buffer;
      for (gint i = 0; i < frames * 2; i++, data++)
	{
	  if (*data > level)
	    {
	      level = *data;
	    }
	}
    }
  else if (channels == 1)
    {
      data = buffer;
      if (audio->record_options & RECORD_RIGHT)
	{
	  data++;
	}
      for (gint i = 0; i < recording_frames; i++, data += 2)
	{
	  if (record)
	    {
	      g_byte_array_append (audio->sample, (guint8 *) data,
				   sizeof (gint16));
	    }
	  if (*data > level)
	    {
	      level = *data;
	    }
	}
    }

  monitor_frames += frames;
  if (audio->monitor && monitor_frames >= FRAMES_TO_MONITOR)
    {
      audio->monitor (audio->monitor_data, level / (gdouble) SHRT_MAX);
      level = 0;
      monitor_frames -= FRAMES_TO_MONITOR;
    }
  g_mutex_unlock (&audio->control.mutex);

  if (recording_frames < frames)
    {
      audio_stop_recording (audio);
    }
}

void
audio_reset_record_buffer (struct audio *audio, guint record_options,
			   void (*monitor) (void *, gdouble),
			   void *monitor_data)
{
  struct sample_info *sample_info = audio->control.data;
  guint channels = (record_options & RECORD_STEREO) == 3 ? 2 : 1;
  audio->frames = audio->samplerate * MAX_RECORDING_TIME_S;
  guint size = audio->frames * channels * sizeof (gint16);
  g_byte_array_set_size (audio->sample, size);
  audio->sample->len = 0;
  audio->pos = 0;
  audio->record_options = record_options;
  audio->monitor = monitor;
  audio->monitor_data = monitor_data;
  sample_info->loopstart = 0;
  sample_info->loopend = 0;
  sample_info->looptype = 0;
  sample_info->samplerate = audio->samplerate;
  sample_info->bitdepth = 16;
  sample_info->channels = channels;
  sample_info->frames = audio->frames;
}

void
audio_init (struct audio *audio,
	    void (*volume_change_callback) (gpointer, gdouble),
	    void (*audio_ready_callback) (), gpointer data)
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
  audio->ready_callback = audio_ready_callback;
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

guint
audio_detect_start (struct audio *audio)
{
  guint start_frame = 0;
  gint16 *data = (gint16 *) audio->sample->data;

//Searching for audio data...
  for (gint i = 0; i < audio->frames; i++)
    {
      for (gint j = 0; j < AUDIO_SAMPLE_CHANNELS (audio); j++, data++)
	{
	  if (!start_frame && abs (*data) >= SHRT_MAX * 0.01)
	    {
	      start_frame = i;
	      data -= j + 1;
	      goto search_last_zero;
	    }
	}
    }

search_last_zero:
  for (gint i = start_frame - 1; i >= 0; i--)
    {
      for (gint j = 0; j < AUDIO_SAMPLE_CHANNELS (audio); j++, data--)
	{
	  if (abs (*data) == 0)	//SHRT_MAX * 0.001)
	    {
	      start_frame = i;
	      goto end;
	    }
	}
    }

end:
  debug_print (1, "Detected start at frame %d\n", start_frame);
  return start_frame;
}

void
audio_delete_range (struct audio *audio, guint start_frame, guint frames)
{
  struct sample_info *sample_info = audio->control.data;
  guint index = start_frame * AUDIO_SAMPLE_BYTES_PER_FRAME (audio);
  guint len = frames * AUDIO_SAMPLE_BYTES_PER_FRAME (audio);

  debug_print (2, "Deleting range from %d with len %d...\n", index, len);
  g_byte_array_remove_range (audio->sample, index, len);

  g_mutex_lock (&audio->control.mutex);
  audio->frames -= (guint32) frames;
  audio->sel_start = 0;
  audio->sel_len = 0;
  sample_info->frames = audio->frames;
  g_mutex_unlock (&audio->control.mutex);
}

static void
audio_normalize (struct audio *audio)
{
  gdouble ratio, ratiop, ration;
  gint16 *data, maxp = 1, minn = -1;
  guint samples = audio->sample->len / SAMPLE_SIZE;

  data = (gint16 *) audio->sample->data;
  for (gint i = 0; i < samples; i++, data++)
    {
      gint16 v = *data;
      if (v >= 0)
	{
	  if (v > maxp)
	    {
	      maxp = v;
	    }
	}
      else
	{
	  if (v < minn)
	    {
	      minn = v;
	    }
	}
    }
  ratiop = SHRT_MAX / (gdouble) maxp;
  ration = SHRT_MIN / (gdouble) minn;
  ratio = ratiop < ration ? ratiop : ration;

  debug_print (1, "Normalizing to %f...\n", ratio);

  data = (gint16 *) audio->sample->data;
  for (gint i = 0; i < samples; i++, data++)
    {
      *data = (gint16) (*data * ratio);
    }
}

void
audio_finish_recording (struct audio *audio)
{
  struct sample_info *sample_info = audio->control.data;
  guint record = !(audio->record_options & RECORD_MONITOR_ONLY);

  g_mutex_lock (&audio->control.mutex);
  audio->status = AUDIO_STATUS_STOPPED;
  audio->frames = audio->sample->len / AUDIO_SAMPLE_BYTES_PER_FRAME (audio);
  sample_info->frames = audio->frames;
  if (record)
    {
      audio_normalize (audio);
    }
  if (audio->monitor)
    {
      audio->monitor (audio->monitor_data, 0.0);
    }
  g_mutex_unlock (&audio->control.mutex);
}
