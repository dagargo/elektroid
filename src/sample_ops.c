/*
 *   sample_ops.c
 *   Copyright (C) 2026 David García Goñi <dagargo@gmail.com>
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
#include "sample_ops.h"
#include "sample.h"

#define SAMPLE_OPS_SILENCE_THRESHOLD 0.01

static gboolean
sample_ops_zero_crossing_slope (gfloat prev, gfloat next,
				enum sample_ops_zero_crossing_slope slope)
{
  switch (slope)
    {
    case SAMPLE_OPS_ZERO_CROSSING_SLOPE_POSITIVE:
      if (prev < 0 && next > 0)
	{
	  return TRUE;
	}
      break;
    case SAMPLE_OPS_ZERO_CROSSING_SLOPE_NEGATIVE:
      if (prev > 0 && next < 0)
	{
	  return TRUE;
	}
      break;
    case SAMPLE_OPS_ZERO_CROSSING_SLOPE_ANY:
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
sample_ops_zero_crossing_any_channel (struct sample_info *sample_info,
				      guint8 *prev_data, guint8 *next_data,
				      enum sample_ops_zero_crossing_slope
				      slope)
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
      if (sample_ops_zero_crossing_slope (prev, next, slope))
	{
	  return TRUE;
	}
      prev_data += sample_size;
      next_data += sample_size;
    }

  return FALSE;
}

guint32
sample_ops_get_next_zero_crossing (struct idata *sample, guint32 frame,
				   enum sample_ops_zero_crossing_slope slope)
{
  guint8 *prev_data, *next_data;
  struct sample_info *sample_info = sample->info;
  guint frame_size = SAMPLE_INFO_FRAME_SIZE (sample_info);

  for (guint32 i = frame; i < sample_info->frames - 1; i++)
    {
      prev_data = sample->content->data + i * frame_size;
      next_data = prev_data + frame_size;
      if (sample_ops_zero_crossing_any_channel
	  (sample_info, prev_data, next_data, slope))
	{
	  return i + 1;
	}
    }

  return frame;
}

guint32
sample_ops_get_prev_zero_crossing (struct idata *sample, guint32 frame,
				   enum sample_ops_zero_crossing_slope slope)
{
  guint8 *prev_data, *next_data;
  struct sample_info *sample_info = sample->info;
  guint frame_size = SAMPLE_INFO_FRAME_SIZE (sample_info);

  for (guint32 i = frame; i >= 1; i--)
    {
      next_data = sample->content->data + i * frame_size;
      prev_data = next_data - frame_size;
      if (sample_ops_zero_crossing_any_channel
	  (sample_info, prev_data, next_data, slope))
	{
	  return i - 1;
	}
    }

  return frame;
}

guint32
sample_ops_detect_start (struct idata *sample)
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
	      above_threshold = fabsf (v) >= SAMPLE_OPS_SILENCE_THRESHOLD;
	    }
	  else
	    {
	      gint16 v = *((gint16 *) data);
	      above_threshold =
		fabsf (v) >= SHRT_MAX * SAMPLE_OPS_SILENCE_THRESHOLD;
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
  start_frame = sample_ops_get_prev_zero_crossing (sample, start_frame,
						   SAMPLE_OPS_ZERO_CROSSING_SLOPE_ANY);

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
sample_ops_delete_range (struct idata *sample, guint32 start,
			 guint32 length, gint64 *sel_start, gint64 *sel_end)
{
  guint index, len;
  struct sample_info *sample_info = sample->info;
  guint frame_size = SAMPLE_INFO_FRAME_SIZE (sample_info);

  index = start * frame_size;
  len = length * frame_size;

  debug_print (2, "Deleting range from %d with len %d...", index, len);
  g_byte_array_remove_range (sample->content, index, len);

  sample_info->frames -= length;

  if (sample_info->loop_start >= *sel_end)
    {
      sample_info->loop_start -= length;
    }
  else if (sample_info->loop_start >= *sel_start &&
	   sample_info->loop_start < *sel_end)
    {
      sample_info->loop_start = sample_info->frames - 1;
    }

  if (sample_info->loop_end >= *sel_end)
    {
      sample_info->loop_end -= length;
    }
  else if (sample_info->loop_end >= *sel_start &&
	   sample_info->loop_end < *sel_end)
    {
      sample_info->loop_end = sample_info->frames - 1;
    }

  *sel_start = -1;
  *sel_end = -1;
}

void
sample_ops_normalize (struct idata *sample, guint32 start, guint32 length)
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
