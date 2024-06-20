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
audio_mix_channels (gint16 **src, guint channels)
{
  gdouble mix = 0;
  for (gint i = 0; i < channels; i++, (*src)++)
    {
      mix += **src;
    }
  return (gint16) (mix * MONO_MIX_GAIN (channels));
}

static inline void
audio_copy_sample (gint16 *dst, gint16 *src, struct audio *audio)
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
  guint32 len = audio->sel_len ? audio->sel_start + audio->sel_len :
    audio->sample_info.frames;
  guint bytes_per_frame = SAMPLE_INFO_FRAME_SIZE (&audio->sample_info);
  size_t size = frames * FRAME_SIZE (AUDIO_CHANNELS, SF_FORMAT_PCM_16);

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

  src =
    (gint16 *) & audio->sample.content->data[audio->pos * bytes_per_frame];
  for (gint i = 0; i < frames; i++)
    {
      if (audio->pos == audio->sample_info.loop_end + 1 && audio->loop)
	{
	  debug_print (2, "Sample reset\n");
	  audio->pos = audio->sample_info.loop_start;
	  src = (gint16 *) & audio->sample.content->data[audio->pos *
							 bytes_per_frame];
	}
      else if (audio->pos == len)
	{
	  if (!audio->loop)
	    {
	      break;
	    }
	  debug_print (2, "Sample reset\n");
	  audio->pos = audio->sel_len ? audio->sel_start : 0;
	  src = (gint16 *) audio->sample.content->data;
	}

      if (audio->mono_mix)
	{
	  gint16 mix = audio_mix_channels (&src, audio->sample_info.channels);
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
  guint bytes_per_frame = FRAME_SIZE (channels, SF_FORMAT_PCM_16);
  guint record = !(audio->record_options & RECORD_MONITOR_ONLY);

  debug_print (2, "Reading %d frames (recording = %d)...\n", frames, record);

  g_mutex_lock (&audio->control.mutex);
  recorded_frames = audio->sample.content->len / bytes_per_frame;
  remaining_frames = audio->sample_info.frames - recorded_frames;
  recording_frames = remaining_frames > frames ? frames : remaining_frames;

  if (channels == 2)
    {
      if (record)
	{
	  g_byte_array_append (audio->sample.content, buffer,
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
	      g_byte_array_append (audio->sample.content, (guint8 *) data,
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
  g_mutex_lock (&audio->control.mutex);
  audio->sample_info.channels = (record_options & RECORD_STEREO) == 3 ? 2 : 1;
  audio->sample_info.frames = audio->sample_info.rate * MAX_RECORDING_TIME_S;
  audio->sample_info.loop_start = audio->sample_info.frames - 1;
  audio->sample_info.loop_end = audio->sample_info.loop_start;
  audio->sample_info.format = SF_FORMAT_WAV | SF_FORMAT_PCM_16;
  guint size =
    audio->sample_info.frames * SAMPLE_INFO_FRAME_SIZE (&audio->sample_info);
  idata_free (&audio->sample);
  idata_init (&audio->sample, g_byte_array_sized_new (size), NULL, NULL);
  audio->pos = 0;
  audio->record_options = record_options;
  audio->monitor = monitor;
  audio->monitor_data = monitor_data;
  g_mutex_unlock (&audio->control.mutex);
}

void
audio_init (struct audio *audio,
	    void (*volume_change_callback) (gpointer, gdouble),
	    void (*audio_ready_callback) (gpointer), gpointer data)
{
  debug_print (1, "Initializing audio (%s %s)...\n", audio_name (),
	       audio_version ());
  idata_init (&audio->sample, NULL, NULL, NULL);
  audio->sample_info.frames = 0;
  audio->sample_info.rate = 0;
  audio->sample_info.channels = 0;
  audio->loop = FALSE;
  audio->path = NULL;
  audio->status = AUDIO_STATUS_STOPPED;
  audio->volume_change_callback = volume_change_callback;
  audio->ready_callback = audio_ready_callback;
  audio->callback_data = data;
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
  g_mutex_unlock (&audio->control.mutex);
}

void
audio_reset_sample (struct audio *audio)
{
  g_mutex_lock (&audio->control.mutex);
  debug_print (1, "Resetting sample...\n");
  idata_free (&audio->sample);
  audio->sample_info.frames = 0;
  audio->pos = 0;
  g_free (audio->path);
  audio->path = NULL;
  audio->release_frames = 0;
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
  gint16 *data = (gint16 *) audio->sample.content->data;

//Searching for audio data...
  for (gint i = 0; i < audio->sample_info.frames; i++)
    {
      for (gint j = 0; j < audio->sample_info.channels; j++, data++)
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
  for (gint i = start_frame - 1; i >= 1; i--)
    {
      for (gint j = 0; j < audio->sample_info.channels; j++, data--)
	{
	  gint16 curr = *data;
	  gint16 prev = *(data - audio->sample_info.channels);
	  if ((curr > 0 && prev < 0) || (curr < 0 && prev > 0))
	    {
	      start_frame = i - 1;
	      goto end;
	    }
	}
    }

end:
  data = (gint16 *) & audio->sample.content->data[start_frame];
  for (gint j = 0; j < audio->sample_info.channels; j++, data++)
    {
      *data = 0;
    }
  debug_print (1, "Detected start at frame %d\n", start_frame);
  return start_frame;
}

void
audio_delete_range (struct audio *audio, guint start_frame, guint frames)
{
  gdouble r;
  struct sample_info *sample_info_src;
  guint bytes_per_frame;
  guint index;
  guint len;

  g_mutex_lock (&audio->control.mutex);
  sample_info_src = audio->control.data;
  bytes_per_frame = SAMPLE_INFO_FRAME_SIZE (&audio->sample_info);
  index = start_frame * bytes_per_frame;
  len = frames * bytes_per_frame;

  debug_print (2, "Deleting range from %d with len %d...\n", index, len);

  g_byte_array_remove_range (audio->sample.content, index, len);

  audio->sample_info.frames -= (guint32) frames;

  if (audio->sample_info.loop_start >= audio->sel_start + audio->sel_len)
    {
      audio->sample_info.loop_start -= (guint32) audio->sel_len;
    }
  else if (audio->sample_info.loop_start >= audio->sel_start &&
	   audio->sample_info.loop_start < audio->sel_start + audio->sel_len)
    {
      audio->sample_info.loop_start = 0;
    }

  if (audio->sample_info.loop_end >= audio->sel_start + audio->sel_len)
    {
      audio->sample_info.loop_end -= (guint32) audio->sel_len;
    }
  else if (audio->sample_info.loop_end >= audio->sel_start &&
	   audio->sample_info.loop_end < audio->sel_start + audio->sel_len)
    {
      audio->sample_info.loop_end = audio->sample_info.frames - 1;
    }

  audio->sel_start = 0;
  audio->sel_len = 0;

  r = sample_info_src->rate / (double) audio->sample_info.rate;
  sample_info_src->frames = floor (audio->sample_info.frames * r);
  sample_info_src->loop_start = round (audio->sample_info.loop_start * r);
  sample_info_src->loop_end = round (audio->sample_info.loop_end * r);
  sample_check_and_fix_loop_points (sample_info_src);
  g_mutex_unlock (&audio->control.mutex);
}

static void
audio_normalize (struct audio *audio)
{
  gdouble ratio, ratiop, ration;
  gint16 *data, maxp = 1, minn = -1;
  guint samples = audio->sample.content->len / SAMPLE_SIZE (SF_FORMAT_PCM_16);

  data = (gint16 *) audio->sample.content->data;
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

  data = (gint16 *) audio->sample.content->data;
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
  audio->sample_info.frames =
    audio->sample.content->len / SAMPLE_INFO_FRAME_SIZE (&audio->sample_info);
  audio->sample_info.loop_start = audio->sample_info.frames - 1;
  audio->sample_info.loop_end = audio->sample_info.loop_start;
  memcpy (sample_info, &audio->sample_info, sizeof (struct sample_info));
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
