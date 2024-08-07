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
  guint32 start, end;
  size_t size;
  gint16 *dst, *src;
  guint bytes_per_frame;
  gboolean stopping = FALSE;
  struct sample_info *sample_info;
  guint8 *data;

  debug_print (2, "Writing %d frames...", frames);

  g_mutex_lock (&audio->control.mutex);

  sample_info = audio->sample.info;

  if (!sample_info)
    {
      goto end;
    }

  data = audio->sample.content->data;

  if (AUDIO_SEL_LEN (audio))
    {
      start = audio->sel_start;
      end = audio->sel_end;
    }
  else
    {
      start = sample_info->loop_start;
      end = sample_info->loop_end;
    }

  bytes_per_frame = SAMPLE_INFO_FRAME_SIZE (sample_info);
  size = frames * FRAME_SIZE (AUDIO_CHANNELS, SF_FORMAT_PCM_16);

  memset (buffer, 0, size);

  if ((audio->pos == sample_info->frames && !audio->loop) ||
      audio->status == AUDIO_STATUS_PREPARING_PLAYBACK ||
      audio->status == AUDIO_STATUS_STOPPING_PLAYBACK)
    {
      if (audio->status == AUDIO_STATUS_PREPARING_PLAYBACK)
	{
	  audio->status = AUDIO_STATUS_PLAYING;
	}
      else			//Stopping...
	{
	  stopping = TRUE;
	}
      goto end;
    }

  dst = buffer;

  src = (gint16 *) & data[audio->pos * bytes_per_frame];
  for (gint i = 0; i < frames; i++)
    {
      //Using "audio->pos >" instead of "audio->pos ==" improves the playback
      //of the selection while changing it because it's possible that an audio
      //iteration might be in the middle of 2 selection changes and in this
      //case the equality might not have a change.
      if (audio->pos == sample_info->frames || audio->pos > end + 1)
	{
	  if (!audio->loop)
	    {
	      break;
	    }
	  debug_print (2, "Sample reset");
	  audio->pos = start;
	  src = (gint16 *) & data[audio->pos * bytes_per_frame];
	}

      if (audio->mono_mix)
	{
	  gint16 mix = audio_mix_channels (&src, sample_info->channels);
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
  if (!sample_info || stopping)
    {
      audio->release_frames += frames;
    }

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
  struct sample_info *sample_info = audio->sample.info;

  debug_print (2, "Reading %d frames (recording = %d)...", frames, record);

  g_mutex_lock (&audio->control.mutex);
  recorded_frames = audio->sample.content->len / bytes_per_frame;
  remaining_frames = sample_info->frames - recorded_frames;
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
  guint size;
  GByteArray *content;
  struct sample_info *si = g_malloc (sizeof (struct sample_info));

  debug_print (1, "Resetting record buffer...");

  si->channels = (record_options & RECORD_STEREO) == 3 ? 2 : 1;
  si->frames = audio->rate * MAX_RECORDING_TIME_S;
  si->loop_start = si->frames - 1;
  si->loop_end = si->loop_start;
  si->format = SF_FORMAT_PCM_16;
  si->rate = audio->rate;
  si->midi_note = 0;
  si->loop_type = 0;

  size = si->frames * SAMPLE_INFO_FRAME_SIZE (si);
  content = g_byte_array_sized_new (size);

  g_mutex_lock (&audio->control.mutex);
  idata_free (&audio->sample);
  idata_init (&audio->sample, content, NULL, si);
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
  debug_print (1, "Initializing audio (%s %s)...", audio_name (),
	       audio_version ());
  idata_init (&audio->sample, NULL, NULL, NULL);
  audio->loop = FALSE;
  audio->path = NULL;
  audio->status = AUDIO_STATUS_STOPPED;
  audio->volume_change_callback = volume_change_callback;
  audio->ready_callback = audio_ready_callback;
  audio->callback_data = data;
  audio->control.callback = NULL;
  audio->sel_start = -1;
  audio->sel_end = -1;

  audio_init_int (audio);
}

void
audio_destroy (struct audio *audio)
{
  debug_print (1, "Destroying audio...");

  audio_stop_playback (audio);
  audio_stop_recording (audio);
  audio_reset_sample (audio);

  g_mutex_lock (&audio->control.mutex);
  audio_destroy_int (audio);
  g_mutex_unlock (&audio->control.mutex);
}

void
audio_reset_sample (struct audio *audio)
{
  debug_print (1, "Resetting sample...");

  g_mutex_lock (&audio->control.mutex);
  idata_free (&audio->sample);
  audio->pos = 0;
  g_free (audio->path);
  audio->path = NULL;
  audio->release_frames = 0;
  audio->status = AUDIO_STATUS_STOPPED;
  g_mutex_unlock (&audio->control.mutex);
}

void
audio_prepare (struct audio *audio, enum audio_status status)
{
  g_mutex_lock (&audio->control.mutex);
  audio->pos = audio->sel_end - audio->sel_start ? audio->sel_start : 0;
  audio->release_frames = 0;
  audio->status = status;
  g_mutex_unlock (&audio->control.mutex);
}

guint
audio_detect_start (struct audio *audio)
{
  guint start_frame = 0;
  struct sample_info *sample_info = audio->sample.info;
  gint16 *data = (gint16 *) audio->sample.content->data;

//Searching for audio data...
  for (gint i = 0; i < sample_info->frames; i++)
    {
      for (gint j = 0; j < sample_info->channels; j++, data++)
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
      for (gint j = 0; j < sample_info->channels; j++, data--)
	{
	  gint16 curr = *data;
	  gint16 prev = *(data - sample_info->channels);
	  if ((curr > 0 && prev < 0) || (curr < 0 && prev > 0))
	    {
	      start_frame = i - 1;
	      goto end;
	    }
	}
    }

end:
  data = (gint16 *) & audio->sample.content->data[start_frame];
  for (gint j = 0; j < sample_info->channels; j++, data++)
    {
      *data = 0;
    }
  debug_print (1, "Detected start at frame %d", start_frame);
  return start_frame;
}

void
audio_delete_range (struct audio *audio, guint32 start, guint32 length)
{
  guint bytes_per_frame, index, len;
  struct sample_info *sample_info = audio->sample.info;

  g_mutex_lock (&audio->control.mutex);

  bytes_per_frame = SAMPLE_INFO_FRAME_SIZE (sample_info);
  index = start * bytes_per_frame;
  len = length * bytes_per_frame;

  debug_print (2, "Deleting range from %d with len %d...", index, len);
  g_byte_array_remove_range (audio->sample.content, index, len);

  sample_info->frames -= length;

  if (sample_info->loop_start >= audio->sel_end)
    {
      sample_info->loop_start -= length;
    }
  else if (sample_info->loop_start >= audio->sel_start &&
	   sample_info->loop_start < audio->sel_end)
    {
      sample_info->loop_start = sample_info->frames - 1;
    }

  if (sample_info->loop_end >= audio->sel_end)
    {
      sample_info->loop_end -= length;
    }
  else if (sample_info->loop_end >= audio->sel_start &&
	   sample_info->loop_end < audio->sel_end)
    {
      sample_info->loop_end = sample_info->frames - 1;
    }

  audio->sel_start = -1;
  audio->sel_end = -1;

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

  debug_print (1, "Normalizing to %f...", ratio);

  data = (gint16 *) audio->sample.content->data;
  for (gint i = 0; i < samples; i++, data++)
    {
      *data = (gint16) (*data * ratio);
    }
}

void
audio_finish_recording (struct audio *audio)
{
  struct sample_info *sample_info = audio->sample.info;
  guint record = !(audio->record_options & RECORD_MONITOR_ONLY);

  g_mutex_lock (&audio->control.mutex);
  audio->status = AUDIO_STATUS_STOPPED;
  sample_info->frames =
    audio->sample.content->len / SAMPLE_INFO_FRAME_SIZE (sample_info);
  sample_info->loop_start = sample_info->frames - 1;
  sample_info->loop_end = sample_info->loop_start;
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
