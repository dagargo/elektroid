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
#include "utils.h"

struct audio audio;
static struct controllable audio_initializing_controllable;

#define AUDIO_FRAMES_TO_MONITOR (audio.rate / 10)
#define AUDIO_SILENCE_THRESHOLD 0.01

#define AUDIO_SLEEP_US 200000

void audio_init_int ();
void audio_destroy_int ();
const gchar *audio_name ();
const gchar *audio_version ();

static inline gboolean
audio_is_recording (guint record_options)
{
  return (record_options & RECORD_MONITOR_ONLY) == 0;
}

static inline gboolean
audio_is_monitoring (guint record_options)
{
  return !audio_is_recording (record_options);
}

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

gboolean
audio_is_stopped ()
{
  enum audio_status s;
  g_mutex_lock (&audio.control.controllable.mutex);
  s = audio.status;
  g_mutex_unlock (&audio.control.controllable.mutex);
  return s == AUDIO_STATUS_STOPPED;
}

void
audio_write_to_output (void *buffer, gint frames)
{
  size_t size;
  guint8 *dst, *src, *data;
  guint bytes_per_frame;
  gboolean end, stopping = FALSE;
  struct sample_info *sample_info;
  gboolean selection_mode;

  g_mutex_lock (&audio.control.controllable.mutex);

  sample_info = audio.sample.info;

  if (!sample_info)
    {
      goto end;
    }

  debug_print (2, "Writing %d frames...", frames);

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

  if (audio.cursor_notifier)
    {
      audio.cursor_notifier (audio.pos);
    }

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
      if (audio.cursor_notifier)
	{
	  audio.cursor_notifier (-1);
	}
    }

  g_mutex_unlock (&audio.control.controllable.mutex);

  if (audio.release_frames > AUDIO_BUF_FRAMES)
    {
      audio_stop_playback ();
    }
}

guint32
audio_get_used_frames (guint32 *bytes_per_frame)
{
  guint32 bpf;
  struct sample_info *sample_info = audio.sample.info;
  bpf = SAMPLE_INFO_FRAME_SIZE (sample_info);
  if (bytes_per_frame)
    {
      *bytes_per_frame = bpf;
    }
  return audio.sample.content->len / bpf;
}

void
audio_read_from_input (void *buffer, gint frames)
{
  gboolean last;
  guint8 *src, *dst;
  gint16 ls16, rs16;
  gfloat lm, rm, lf32, rf32;
  static gint monitor_frames = 0;
  guint32 recorded_frames, remaining_frames, recording_frames,
    bytes_per_frame;
  struct sample_info *sample_info;

  g_mutex_lock (&audio.control.controllable.mutex);

  if (!audio.record_options)
    {
      g_mutex_unlock (&audio.control.controllable.mutex);
      return;
    }

  if (audio_is_recording (audio.record_options))
    {
      debug_print (2, "Reading %d frames (recording)...", frames);

      sample_info = audio.sample.info;
      recorded_frames = audio_get_used_frames (&bytes_per_frame);
      remaining_frames = sample_info->frames - recorded_frames;
      if (remaining_frames <= frames)
	{
	  last = TRUE;
	  recording_frames = remaining_frames;
	}
      else
	{
	  last = FALSE;
	  recording_frames = frames;
	}

      dst = audio.sample.content->data + audio.sample.content->len;
      audio.sample.content->len += recording_frames * bytes_per_frame;
    }
  else
    {
      debug_print (2, "Reading %d frames (monitoring)...", frames);

      recording_frames = frames;
      dst = NULL;
      last = FALSE;
    }

  src = buffer;
  for (gint i = 0; i < recording_frames; i++)
    {
      if (audio.float_mode)
	{
	  lf32 = *((gfloat *) src);
	  src += sizeof (gfloat);

	  rf32 = *((gfloat *) src);
	  src += sizeof (gfloat);

	  lm = lf32;
	  rm = rf32;

	  if (audio_is_recording (audio.record_options))
	    {
	      if (audio.record_options & RECORD_LEFT)
		{
		  *((gfloat *) dst) = lf32;
		  dst += sizeof (gfloat);
		}
	      if (audio.record_options & RECORD_RIGHT)
		{
		  *((gfloat *) dst) = rf32;
		  dst += sizeof (gfloat);
		}
	    }
	}
      else
	{
	  ls16 = *((gint16 *) src);
	  src += sizeof (gint16);

	  rs16 = *((gint16 *) src);
	  src += sizeof (gint16);

	  lm = ls16;
	  rm = rs16;

	  if (audio_is_recording (audio.record_options))
	    {
	      if (audio.record_options & RECORD_LEFT)
		{
		  *((gint16 *) dst) = ls16;
		  dst += sizeof (gint16);
		}
	      if (audio.record_options & RECORD_RIGHT)
		{
		  *((gint16 *) dst) = rs16;
		  dst += sizeof (gint16);
		}
	    }
	}

      if (lm > audio.monitor_level_l)
	{
	  audio.monitor_level_l = lm;
	}

      if (rm > audio.monitor_level_l)
	{
	  audio.monitor_level_r = rm;
	}
    }

  monitor_frames += frames;
  if (audio.monitor_notifier && monitor_frames >= AUDIO_FRAMES_TO_MONITOR)
    {
      if (audio.float_mode)
	{
	  audio.monitor_notifier (audio.monitor_data, audio.monitor_level_l,
				  audio.monitor_level_r);
	}
      else
	{
	  audio.monitor_notifier (audio.monitor_data,
				  -audio.monitor_level_l / (gdouble) SHRT_MIN,
				  -audio.monitor_level_r /
				  (gdouble) SHRT_MIN);
	}
      monitor_frames -= AUDIO_FRAMES_TO_MONITOR;
      audio.monitor_level_l = 0;
      audio.monitor_level_r = 0;
    }
  g_mutex_unlock (&audio.control.controllable.mutex);

  if (last)
    {
      audio_stop_recording ();
    }
}

void
audio_reset_record_buffer (guint record_options,
			   audio_monitor_notifier monitor_notifier,
			   void *monitor_data)
{
  guint size;
  GByteArray *content;
  struct sample_info *si;

  if (audio_is_recording (record_options))
    {
      debug_print (1, "Resetting record buffer...");

      si = sample_info_new (TRUE);
      si->frames = audio.rate * MAX_RECORDING_TIME_S;
      si->loop_start = si->frames - 1;
      si->loop_end = si->loop_start;
      si->rate = audio.rate;
      si->format = audio.float_mode ? SF_FORMAT_FLOAT : SF_FORMAT_PCM_16;
      si->channels = (record_options & RECORD_STEREO) == 3 ? 2 : 1;

      size = si->frames * SAMPLE_INFO_FRAME_SIZE (si);
      content = g_byte_array_sized_new (size);
      memset (content->data, 0, size);	//Needed for the recording drawing
    }
  else
    {
      content = NULL;
      si = NULL;
    }
  g_mutex_lock (&audio.control.controllable.mutex);
  idata_free (&audio.sample);
  idata_init (&audio.sample, content, NULL, si,
	      si == NULL ? NULL : sample_info_free);
  audio.pos = 0;
  audio.record_options = record_options;
  audio.monitor_notifier = monitor_notifier;
  audio.monitor_data = monitor_data;
  audio.monitor_level_l = 0;
  audio.monitor_level_r = 0;
  g_mutex_unlock (&audio.control.controllable.mutex);
}

void
audio_init (audio_ready_callback ready_callback,
	    audio_volume_change_callback volume_change_callback)
{
  debug_print (1, "Initializing audio (%s %s)...", audio_name (),
	       audio_version ());
  audio.float_mode = preferences_get_boolean (PREF_KEY_AUDIO_USE_FLOAT);
  idata_init (&audio.sample, NULL, NULL, NULL, NULL);
  audio.loop = FALSE;
  audio.path = NULL;
  audio.status = AUDIO_STATUS_STOPPED;
  audio.ready_callback = ready_callback;
  audio.volume_change_callback = volume_change_callback;
  controllable_init (&audio.control.controllable);
  audio.control.callback = NULL;
  audio.sel_start = -1;
  audio.sel_end = -1;
  audio.record_options = 0;

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

static gboolean
audio_zero_crossing_slope (gfloat prev, gfloat next,
			   enum audio_zero_crossing_slope slope)
{
  switch (slope)
    {
    case AUDIO_ZERO_CROSSING_SLOPE_POSITIVE:
      if (prev < 0 && next > 0)
	{
	  return TRUE;
	}
      break;
    case AUDIO_ZERO_CROSSING_SLOPE_NEGATIVE:
      if (prev > 0 && next < 0)
	{
	  return TRUE;
	}
      break;
    case AUDIO_ZERO_CROSSING_SLOPE_ANY:
      if ((prev < 0 && next > 0) || (prev > 0 && next < 0))
	{
	  return TRUE;
	}
      break;
    default:
      error_print ("Slope not implemented");
    }

  return FALSE;
}

static gboolean
audio_zero_crossing_any_channel (struct sample_info *sample_info,
				 guint8 *prev_data, guint8 *next_data,
				 enum audio_zero_crossing_slope slope)
{
  gboolean float_mode = SAMPLE_INFO_IS_FLOAT (sample_info);
  guint sample_size = SAMPLE_INFO_SAMPLE_SIZE (sample_info);

  for (gint i = 0; i < sample_info->channels; i++)
    {
      gfloat prev, next;
      if (float_mode)
	{
	  prev = *((gfloat *) prev_data);
	  next = *((gfloat *) next_data);
	}
      else
	{
	  prev = *((gint16 *) prev_data);
	  next = *((gint16 *) next_data);
	}
      if (audio_zero_crossing_slope (prev, next, slope))
	{
	  return TRUE;
	}
      prev_data += sample_size;
      next_data += sample_size;
    }

  return FALSE;
}

guint32
audio_get_next_zero_crossing (struct idata *sample, guint32 frame,
			      enum audio_zero_crossing_slope slope)
{
  guint8 *prev_data, *next_data;
  struct sample_info *sample_info = sample->info;
  guint frame_size = SAMPLE_INFO_FRAME_SIZE (sample_info);

  for (guint32 i = frame; i < sample_info->frames - 1; i++)
    {
      prev_data = sample->content->data + i * frame_size;
      next_data = prev_data + frame_size;
      if (audio_zero_crossing_any_channel (sample_info, prev_data, next_data,
					   slope))
	{
	  return i + 1;
	}
    }

  return frame;
}

guint32
audio_get_prev_zero_crossing (struct idata *sample, guint32 frame,
			      enum audio_zero_crossing_slope slope)
{
  guint8 *prev_data, *next_data;
  struct sample_info *sample_info = sample->info;
  guint frame_size = SAMPLE_INFO_FRAME_SIZE (sample_info);

  for (guint32 i = frame; i >= 1; i--)
    {
      next_data = sample->content->data + i * frame_size;
      prev_data = next_data - frame_size;
      if (audio_zero_crossing_any_channel (sample_info, prev_data, next_data,
					   slope))
	{
	  return i - 1;
	}
    }

  return frame;
}

static guint32
audio_detect_start (struct idata *sample)
{
  guint32 start_frame = 0;
  guint8 *data;
  struct sample_info *sample_info = sample->info;
  guint frame_size = SAMPLE_INFO_FRAME_SIZE (sample_info);
  gboolean float_mode = SAMPLE_INFO_IS_FLOAT (sample_info);
  guint sample_size = SAMPLE_INFO_SAMPLE_SIZE (sample_info);

  // Search audio data
  data = sample->content->data;
  for (guint32 i = 0; i < sample_info->frames; i++)
    {
      for (gint j = 0; j < sample_info->channels; j++)
	{
	  gboolean above_threshold;
	  if (float_mode)
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
	  data += sample_size;
	}
    }

search_previous_zero:
  start_frame = audio_get_prev_zero_crossing (sample, start_frame,
					      AUDIO_ZERO_CROSSING_SLOPE_ANY);

  data = sample->content->data + start_frame * frame_size;
  for (gint j = 0; j < sample_info->channels; j++)
    {
      if (float_mode)
	{
	  *((gfloat *) data) = 0;
	}
      else
	{
	  *((gint16 *) data) = 0;
	}
      data += sample_size;
    }

  debug_print (1, "Detected start at frame %d", start_frame);

  return start_frame;
}

void
audio_delete_range (struct idata *sample, guint32 start, guint32 length)
{
  guint index, len;
  struct sample_info *sample_info = sample->info;
  guint frame_size = SAMPLE_INFO_FRAME_SIZE (sample_info);

  index = start * frame_size;
  len = length * frame_size;

  debug_print (2, "Deleting range from %d with len %d...", index, len);
  g_byte_array_remove_range (sample->content, index, len);

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
audio_normalize (struct idata *sample, guint32 start, guint32 length)
{
  guint8 *data;
  gdouble v, ratio, ratiop, ration, maxp = 0, minn = 0;
  struct sample_info *sample_info = sample->info;
  guint samples = length * sample_info->channels;
  guint frame_size = SAMPLE_INFO_FRAME_SIZE (sample_info);
  gboolean float_mode = SAMPLE_INFO_IS_FLOAT (sample_info);

  data = sample->content->data + start * frame_size;
  for (gint i = 0; i < samples; i++)
    {
      if (float_mode)
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

  if (float_mode)
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

  data = sample->content->data + start * frame_size;
  for (gint i = 0; i < samples; i++)
    {
      if (float_mode)
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
  struct sample_info *sample_info;

  g_mutex_lock (&audio.control.controllable.mutex);
  audio.status = AUDIO_STATUS_STOPPED;
  if (audio_is_recording (audio.record_options))
    {
      sample_info = audio.sample.info;
      sample_info->frames =
	audio.sample.content->len / SAMPLE_INFO_FRAME_SIZE (sample_info);
      sample_info->loop_start = sample_info->frames - 1;
      sample_info->loop_end = sample_info->loop_start;

      debug_print (1, "Finishing recording (%d frames read)...",
		   sample_info->frames);

      audio_normalize (&audio.sample, 0, sample_info->frames);
      start = audio_detect_start (&audio.sample);
      audio_delete_range (&audio.sample, 0, start);
    }
  else
    {
      debug_print (1, "Finishing monitoring...");
    }
  if (audio.monitor_notifier)
    {
      audio.monitor_notifier (audio.monitor_data, 0, 0);
    }
  g_mutex_unlock (&audio.control.controllable.mutex);
}

void
audio_init_ready_callback ()
{
  controllable_set_active (&audio_initializing_controllable, FALSE);
}

void
audio_init_and_wait ()
{
  controllable_init (&audio_initializing_controllable);
  controllable_set_active (&audio_initializing_controllable, TRUE);
  audio_init (audio_init_ready_callback, NULL);
  while (controllable_is_active (&audio_initializing_controllable))
    {
      usleep (AUDIO_SLEEP_US);
    }
  controllable_clear (&audio_initializing_controllable);
  debug_print (1, "Audio initialized");
}

// This consumes the sample parameter.

void
audio_set_play_and_wait (struct idata *sample, struct task_control *control)
{
  guint32 pos;
  gdouble progress;
  gboolean active = TRUE;
  struct sample_info *sample_info;
  GDestroyNotify free_info;

  audio_reset_sample ();

  g_mutex_lock (&audio.control.controllable.mutex);
  // Full steal of sample
  sample_info = sample->info;
  sample->info = NULL;
  free_info = sample->free_info;
  sample->free_info = NULL;
  idata_init (&audio.sample, idata_steal (sample), NULL, sample_info,
	      free_info);
  audio.control.callback = NULL;
  audio.sel_start = -1;
  audio.sel_end = -1;
  g_mutex_unlock (&audio.control.controllable.mutex);

  audio_start_playback (NULL);

  sample_info = audio.sample.info;
  while (!audio_is_stopped () && active)
    {
      usleep (AUDIO_SLEEP_US);
      if (control)
	{
	  g_mutex_lock (&audio.control.controllable.mutex);
	  pos = audio.pos;
	  g_mutex_unlock (&audio.control.controllable.mutex);
	  progress = pos / (gdouble) sample_info->frames;
	  task_control_set_progress (control, progress);
	  active = controllable_is_active (&control->controllable);
	}
    }
}

void
audio_record_and_wait (guint32 options, struct task_control *control)
{
  guint32 frames;
  gdouble progress;
  struct sample_info *sample_info;

  audio_start_recording (options, NULL, NULL);

  sample_info = audio.sample.info;
  while (!audio_is_stopped ())
    {
      usleep (AUDIO_SLEEP_US);
      if (control)
	{
	  g_mutex_lock (&audio.control.controllable.mutex);
	  frames = audio_get_used_frames (NULL);
	  g_mutex_unlock (&audio.control.controllable.mutex);
	  progress = frames / (gdouble) sample_info->frames;
	  task_control_set_progress (control, progress);
	}
    }
}
