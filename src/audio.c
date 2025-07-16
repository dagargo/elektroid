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
#include "preferences.h"

struct audio audio;

#define AUDIO_FRAMES_TO_MONITOR 10000
#define AUDIO_SILENCE_THRESHOLD 0.01
#define AUDIO_SAMPLE_SIZE (audio.float_mode ? sizeof(gfloat) : sizeof(gint16))

void audio_init_int ();
void audio_destroy_int ();
const gchar *audio_name ();
const gchar *audio_version ();

static inline gfloat
audio_mix_channels_f32 (gfloat **src, guint channels)
{
  gdouble mix = 0;
  for (gint i = 0; i < channels; i++, (*src)++)
    {
      mix += **src;
    }
  return mix * MONO_MIX_GAIN (channels);
}

static inline gint16
audio_mix_channels_s16 (gint16 **src, guint channels)
{
  gdouble mix = 0;
  for (gint i = 0; i < channels; i++, (*src)++)
    {
      mix += **src;
    }
  return (gint16) (mix * MONO_MIX_GAIN (channels));
}

static inline void
audio_copy_sample_f32 (gfloat *dst, gfloat *src)
{
#if defined(ELEKTROID_RTAUDIO)
  *dst = (gfloat) (*src * audio.volume);
#else
  *dst = *src;
#endif
}

static inline void
audio_copy_sample_s16 (gint16 *dst, gint16 *src)
{
#if defined(ELEKTROID_RTAUDIO)
  *dst = (gint16) (*src * audio.volume);
#else
  *dst = *src;
#endif
}

void
audio_write_to_output (void *buffer, gint frames)
{
  size_t size;
  guint8 *dst, *src;
  guint bytes_per_frame;
  gboolean end, stopping = FALSE;
  struct sample_info *sample_info;
  guint8 *data;
  gboolean selection_mode;

  debug_print (2, "Writing %d frames...", frames);

  g_mutex_lock (&audio.control.controllable.mutex);

  sample_info = audio.sample.info;

  if (!sample_info)
    {
      goto end;
    }

  data = audio.sample.content->data;

  if (AUDIO_SEL_LEN)
    {
      selection_mode = TRUE;
    }
  else
    {
      selection_mode = FALSE;
    }

  bytes_per_frame = SAMPLE_INFO_FRAME_SIZE (sample_info);
  size = frames * FRAME_SIZE (AUDIO_CHANNELS, sample_get_internal_format ());

  memset (buffer, 0, size);

  if (selection_mode)
    {
      end = audio.pos > audio.sel_end;
    }
  else
    {
      end = audio.pos == sample_info->frames;
    }

  if (audio.status == AUDIO_STATUS_PREPARING_PLAYBACK ||
      audio.status == AUDIO_STATUS_STOPPING_PLAYBACK || (end && !audio.loop))
    {
      if (audio.status == AUDIO_STATUS_PREPARING_PLAYBACK)
	{
	  audio.status = AUDIO_STATUS_PLAYING;
	}
      else			//Stopping...
	{
	  stopping = TRUE;
	}
      goto end;
    }

  dst = buffer;
  src = &data[audio.pos * bytes_per_frame];

  for (gint i = 0; i < frames; i++)
    {
      if (audio.loop)
	{
	  //Using "audio.pos >" instead of "audio.pos ==" improves the playback
	  //of the selection while changing it because it's possible that an audio
	  //iteration might be in the middle of 2 selection changes and in this
	  //case the equality might not have a change.
	  if (selection_mode)
	    {
	      if (audio.pos > audio.sel_end)
		{
		  debug_print (2, "Selection loop");
		  audio.pos = audio.sel_start;
		}
	    }
	  else
	    {
	      if (audio.pos > sample_info->loop_end)
		{
		  debug_print (2, "Sample loop");
		  audio.pos = sample_info->loop_start;
		}
	    }
	  src = &data[audio.pos * bytes_per_frame];
	}
      else
	{
	  if (selection_mode)
	    {
	      if (audio.pos > audio.sel_end)
		{
		  break;
		}
	    }
	  else
	    {
	      if (audio.pos == sample_info->frames)
		{
		  break;
		}
	    }
	}

      if (audio.mono_mix)
	{
	  if (audio.float_mode)
	    {
	      gfloat mix = audio_mix_channels_f32 ((gfloat **) & src,
						   sample_info->channels);
	      audio_copy_sample_f32 ((gfloat *) dst, &mix);
	      dst += sizeof (gfloat);
	      audio_copy_sample_f32 ((gfloat *) dst, &mix);
	      dst += sizeof (gfloat);
	    }
	  else
	    {
	      gint16 mix = audio_mix_channels_s16 ((gint16 **) & src,
						   sample_info->channels);
	      audio_copy_sample_s16 ((gint16 *) dst, &mix);
	      dst += sizeof (gint16);
	      audio_copy_sample_s16 ((gint16 *) dst, &mix);
	      dst += sizeof (gint16);
	    }
	}
      else
	{
	  if (audio.float_mode)
	    {
	      audio_copy_sample_f32 ((gfloat *) dst, (gfloat *) src);
	      src += sizeof (gfloat);
	      dst += sizeof (gfloat);
	      audio_copy_sample_f32 ((gfloat *) dst, (gfloat *) src);
	      src += sizeof (gfloat);
	      dst += sizeof (gfloat);
	    }
	  else
	    {
	      audio_copy_sample_s16 ((gint16 *) dst, (gint16 *) src);
	      src += sizeof (gint16);
	      dst += sizeof (gint16);
	      audio_copy_sample_s16 ((gint16 *) dst, (gint16 *) src);
	      src += sizeof (gint16);
	      dst += sizeof (gint16);
	    }

	}

      audio.pos++;
    }

end:
  if (!sample_info || stopping)
    {
      audio.release_frames += frames;
    }

  g_mutex_unlock (&audio.control.controllable.mutex);

  if (audio.release_frames > AUDIO_BUF_FRAMES)
    {
      audio_stop_playback (audio);
    }
}

void
audio_read_from_input (void *buffer, gint frames)
{
  gdouble v;
  static gint monitor_frames = 0;
  static gdouble monitor_level = 0;
  guint8 *data;
  guint recorded_frames, remaining_frames, recording_frames;
  guint channels =
    (audio.record_options & RECORD_STEREO) == RECORD_STEREO ? 2 : 1;
  guint bytes_per_frame =
    FRAME_SIZE (channels, sample_get_internal_format ());
  guint record = !(audio.record_options & RECORD_MONITOR_ONLY);
  struct sample_info *sample_info = audio.sample.info;

  debug_print (2, "Reading %d frames (recording = %d)...", frames, record);

  g_mutex_lock (&audio.control.controllable.mutex);
  recorded_frames = audio.sample.content->len / bytes_per_frame;
  remaining_frames = sample_info->frames - recorded_frames;
  recording_frames = remaining_frames > frames ? frames : remaining_frames;

  if (channels == 2)
    {
      if (record)
	{
	  g_byte_array_append (audio.sample.content, buffer,
			       recording_frames * bytes_per_frame);
	}
      data = buffer;
      for (gint i = 0; i < recording_frames * 2; i++)
	{
	  if (audio.float_mode)
	    {
	      v = *((gfloat *) data);
	      data += sizeof (gfloat);
	    }
	  else
	    {
	      v = *((gint16 *) data);
	      data += sizeof (gint16);
	    }
	  if (v > monitor_level)
	    {
	      monitor_level = v;
	    }
	}
    }
  else if (channels == 1)
    {
      data = buffer;
      if (audio.record_options & RECORD_RIGHT)
	{
	  if (audio.float_mode)
	    {
	      data += sizeof (gfloat);
	    }
	  else
	    {
	      data += sizeof (gint16);
	    }
	}
      for (gint i = 0; i < recording_frames; i++)
	{
	  if (audio.float_mode)
	    {
	      gfloat s = *((gfloat *) data);
	      data += sizeof (gfloat) * 2;
	      v = s;
	      if (record)
		{
		  g_byte_array_append (audio.sample.content, (guint8 *) & s,
				       sizeof (gfloat));
		}
	    }
	  else
	    {
	      gint16 s = *((gint16 *) data);
	      data += sizeof (gint16) * 2;
	      v = s;
	      if (record)
		{
		  g_byte_array_append (audio.sample.content, (guint8 *) & s,
				       sizeof (gint16));
		}
	    }

	  if (v > monitor_level)
	    {
	      monitor_level = v;
	    }
	}
    }

  monitor_frames += frames;
  if (audio.monitor && monitor_frames >= AUDIO_FRAMES_TO_MONITOR)
    {
      if (audio.float_mode)
	{
	  audio.monitor (audio.monitor_data, monitor_level);
	}
      else
	{
	  audio.monitor (audio.monitor_data,
			 monitor_level / (gdouble) SHRT_MAX);
	}
      monitor_frames -= AUDIO_FRAMES_TO_MONITOR;
      monitor_level = 0;
    }
  g_mutex_unlock (&audio.control.controllable.mutex);

  if (recording_frames < frames)
    {
      audio_stop_recording (audio);
    }
}

void
audio_reset_record_buffer (guint record_options,
			   void (*monitor) (void *, gdouble),
			   void *monitor_data)
{
  guint size;
  GByteArray *content;
  struct sample_info *si = g_malloc (sizeof (struct sample_info));

  debug_print (1, "Resetting record buffer...");

  si->channels = (record_options & RECORD_STEREO) == 3 ? 2 : 1;
  si->frames = audio.rate * MAX_RECORDING_TIME_S;
  si->loop_start = si->frames - 1;
  si->loop_end = si->loop_start;
  si->format = audio.float_mode ? SF_FORMAT_FLOAT : SF_FORMAT_PCM_16;
  si->rate = audio.rate;
  si->midi_note = 0;
  si->midi_fraction = 0;
  si->loop_type = 0;

  size = si->frames * SAMPLE_INFO_FRAME_SIZE (si);
  content = g_byte_array_sized_new (size);

  g_mutex_lock (&audio.control.controllable.mutex);
  idata_free (&audio.sample);
  idata_init (&audio.sample, content, NULL, si);
  audio.pos = 0;
  audio.record_options = record_options;
  audio.monitor = monitor;
  audio.monitor_data = monitor_data;
  g_mutex_unlock (&audio.control.controllable.mutex);
}

void
audio_init (void (*volume_change_callback) (gdouble),
	    void (*audio_ready_callback) (), gpointer data)
{
  debug_print (1, "Initializing audio (%s %s)...", audio_name (),
	       audio_version ());
  audio.float_mode = preferences_get_boolean (PREF_KEY_AUDIO_USE_FLOAT);
  idata_init (&audio.sample, NULL, NULL, NULL);
  audio.loop = FALSE;
  audio.path = NULL;
  audio.status = AUDIO_STATUS_STOPPED;
  audio.volume_change_callback = volume_change_callback;
  audio.ready_callback = audio_ready_callback;
  controllable_init (&audio.control.controllable);
  audio.control.callback = NULL;
  audio.sel_start = -1;
  audio.sel_end = -1;

  audio_init_int ();
}

void
audio_destroy ()
{
  debug_print (1, "Destroying audio...");

  audio_stop_playback ();
  audio_stop_recording ();
  audio_reset_sample ();

  g_mutex_lock (&audio.control.controllable.mutex);
  audio_destroy_int ();
  g_mutex_unlock (&audio.control.controllable.mutex);

  controllable_clear (&audio.control.controllable);
}

void
audio_reset_sample ()
{
  debug_print (1, "Resetting sample...");

  g_mutex_lock (&audio.control.controllable.mutex);
  idata_free (&audio.sample);
  audio.pos = 0;
  g_free (audio.path);
  audio.path = NULL;
  audio.release_frames = 0;
  audio.status = AUDIO_STATUS_STOPPED;
  g_mutex_unlock (&audio.control.controllable.mutex);
}

void
audio_prepare (enum audio_status status)
{
  g_mutex_lock (&audio.control.controllable.mutex);
  audio.pos = audio.sel_end - audio.sel_start ? audio.sel_start : 0;
  audio.release_frames = 0;
  audio.status = status;
  g_mutex_unlock (&audio.control.controllable.mutex);
}

static guint32
audio_detect_start ()
{
  guint32 start_frame = 0;
  guint8 *data, *prev_data;
  struct sample_info *sample_info = audio.sample.info;

//Search audio data
  data = audio.sample.content->data;
  for (guint32 i = 0; i < sample_info->frames; i++)
    {
      for (gint j = 0; j < sample_info->channels; j++)
	{
	  gboolean above_threshold;
	  if (audio.float_mode)
	    {
	      gfloat v = *((gfloat *) data);
	      above_threshold = fabsf (v) >= AUDIO_SILENCE_THRESHOLD;
	    }
	  else
	    {
	      gint16 v = *((gint16 *) data);
	      above_threshold =
		fabsf (v) >= SHRT_MAX * AUDIO_SILENCE_THRESHOLD;
	    }
	  if (above_threshold)
	    {
	      start_frame = i;
	      debug_print (1, "Detected signal at %d", start_frame);
	      goto search_previous_zero;
	    }
	  data += AUDIO_SAMPLE_SIZE;
	}
    }

search_previous_zero:
  data = audio.sample.content->data +
    start_frame * SAMPLE_INFO_FRAME_SIZE (sample_info);
  prev_data = data - SAMPLE_INFO_FRAME_SIZE (sample_info);
  for (guint32 i = start_frame; i >= 1; i--)
    {
      for (gint j = 0; j < sample_info->channels; j++)
	{
	  gfloat curr, prev;
	  if (audio.float_mode)
	    {
	      curr = *((gfloat *) data);
	      prev = *((gfloat *) prev_data);
	    }
	  else
	    {
	      curr = *((gint16 *) data);
	      prev = *((gint16 *) prev_data);
	    }
	  if ((curr > 0 && prev < 0) || (curr < 0 && prev > 0))
	    {
	      start_frame = i - 1;
	      goto end;
	    }
	  data += AUDIO_SAMPLE_SIZE;
	  prev_data += AUDIO_SAMPLE_SIZE;
	}
      data -= SAMPLE_INFO_FRAME_SIZE (sample_info);
      prev_data -= SAMPLE_INFO_FRAME_SIZE (sample_info);
    }

end:
  data = audio.sample.content->data +
    start_frame * SAMPLE_INFO_FRAME_SIZE (sample_info);
  for (gint j = 0; j < sample_info->channels; j++)
    {
      if (audio.float_mode)
	{
	  *((gfloat *) data) = 0;
	}
      else
	{
	  *((gint16 *) data) = 0;
	}
      data += AUDIO_SAMPLE_SIZE;
    }

  debug_print (1, "Detected start at frame %d", start_frame);

  return start_frame;
}

static void
audio_delete_range_no_lock (guint32 start, guint32 length)
{
  guint bytes_per_frame, index, len;
  struct sample_info *sample_info = audio.sample.info;

  bytes_per_frame = SAMPLE_INFO_FRAME_SIZE (sample_info);
  index = start * bytes_per_frame;
  len = length * bytes_per_frame;

  debug_print (2, "Deleting range from %d with len %d...", index, len);
  g_byte_array_remove_range (audio.sample.content, index, len);

  sample_info->frames -= length;

  if (sample_info->loop_start >= audio.sel_end)
    {
      sample_info->loop_start -= length;
    }
  else if (sample_info->loop_start >= audio.sel_start &&
	   sample_info->loop_start < audio.sel_end)
    {
      sample_info->loop_start = sample_info->frames - 1;
    }

  if (sample_info->loop_end >= audio.sel_end)
    {
      sample_info->loop_end -= length;
    }
  else if (sample_info->loop_end >= audio.sel_start &&
	   sample_info->loop_end < audio.sel_end)
    {
      sample_info->loop_end = sample_info->frames - 1;
    }

  audio.sel_start = -1;
  audio.sel_end = -1;
}

void
audio_delete_range (guint32 start, guint32 length)
{
  g_mutex_lock (&audio.control.controllable.mutex);
  audio_delete_range_no_lock (start, length);
  g_mutex_unlock (&audio.control.controllable.mutex);
}

static void
audio_normalize ()
{
  guint8 *data;
  gdouble v, ratio, ratiop, ration, maxp = 0, minn = 0;
  struct sample_info *sample_info = audio.sample.info;
  guint32 samples = sample_info->frames * sample_info->channels;

  data = audio.sample.content->data;
  for (gint i = 0; i < samples; i++)
    {
      if (audio.float_mode)
	{
	  v = *((gfloat *) data);
	  data += sizeof (gfloat);
	}
      else
	{
	  v = *((gint16 *) data);
	  data += sizeof (gint16);
	}

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

  if (audio.float_mode)
    {
      ratiop = 1.0 / maxp;
      ration = -1.0 / minn;
    }
  else
    {
      ratiop = SHRT_MAX / maxp;
      ration = SHRT_MIN / minn;
    }

  ratio = ratiop < ration ? ratiop : ration;

  debug_print (1, "Normalizing to %f...", ratio);

  data = audio.sample.content->data;
  for (gint i = 0; i < samples; i++)
    {
      if (audio.float_mode)
	{
	  *((gfloat *) data) = (gfloat) (*((gfloat *) data) * ratio);
	  data += sizeof (gfloat);
	}
      else
	{
	  *((gint16 *) data) = (gint16) (*((gint16 *) data) * ratio);
	  data += sizeof (gint16);
	}
    }
}

void
audio_finish_recording ()
{
  guint32 start;
  struct sample_info *sample_info = audio.sample.info;
  guint record = !(audio.record_options & RECORD_MONITOR_ONLY);

  g_mutex_lock (&audio.control.controllable.mutex);
  audio.status = AUDIO_STATUS_STOPPED;
  sample_info->frames =
    audio.sample.content->len / SAMPLE_INFO_FRAME_SIZE (sample_info);
  sample_info->loop_start = sample_info->frames - 1;
  sample_info->loop_end = sample_info->loop_start;
  if (record)
    {
      audio_normalize ();
      start = audio_detect_start ();
      audio_delete_range_no_lock (0, start);
    }
  if (audio.monitor)
    {
      audio.monitor (audio.monitor_data, 0.0);
    }
  g_mutex_unlock (&audio.control.controllable.mutex);
}
