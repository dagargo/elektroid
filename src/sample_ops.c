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
#include <samplerate.h>
#include "rubberband/rubberband-c.h"
#include "sample_ops.h"
#include "sample.h"

#define TIMESTRETCH_BUF_SIZE 4096

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

static void
sample_ops_timestretch_fill_buf_to_non_interleaved (struct sample_info
						    *sample_info,
						    guint32 buffer_frames,
						    const gfloat *input,
						    gfloat *output)
{
  for (gint i = 0; i < buffer_frames; i++)
    {
      for (gint c = 0; c < sample_info->channels; c++, input++)
	{
	  output[c * TIMESTRETCH_BUF_SIZE + i] = *input;
	}
    }
}

static void
sample_ops_timestretch_fill_buf_from_non_interleaved (struct sample_info
						      *sample_info,
						      guint32 buffer_frames,
						      const gfloat *input,
						      gfloat *output)
{
  for (gint c = 0; c < sample_info->channels; c++)
    {
      for (gint i = 0; i < buffer_frames; i++, output++)
	{
	  *output = input[c * TIMESTRETCH_BUF_SIZE + i];
	}
    }
}

gint
sample_ops_timestretch (struct idata *sample, double ratio)
{
  gfloat *input;
  GByteArray *output;
  RubberBandState rbs;
  gint input_frames, output_frames, output_size;
  struct sample_info *sample_info = sample->info;
  gfloat *input_non_int_buf, *output_non_int_buf;
  gboolean float_mode = SAMPLE_INFO_IS_FLOAT (sample_info);
  gint total_input_samples = sample_info->channels * sample_info->frames;
  gint estimated_output_frames = sample_info->frames * (ratio * 2);
  gint estimated_output_samples =
    sample_info->channels * estimated_output_frames;

  debug_print (1, "Timestretching to %f...", ratio);

  if (float_mode)
    {
      input = (gfloat *) g_byte_array_free (sample->content, FALSE);
    }
  else
    {
      input = (gfloat *) g_malloc (total_input_samples * sizeof (gfloat));
      src_short_to_float_array ((gint16 *) sample->content->data, input,
				total_input_samples);
      g_byte_array_free (sample->content, TRUE);
    }

  output =
    g_byte_array_sized_new (estimated_output_samples * sizeof (gfloat));

  input_non_int_buf =
    g_malloc (TIMESTRETCH_BUF_SIZE * sizeof (gfloat) * sample_info->channels);
  output_non_int_buf =
    g_malloc (TIMESTRETCH_BUF_SIZE * sizeof (gfloat) * sample_info->channels);

  rbs = rubberband_new (sample_info->rate, sample_info->channels,
			RubberBandOptionEngineFiner |
			RubberBandOptionChannelsTogether |
			RubberBandOptionWindowShort, ratio, 1.0);
  rubberband_set_time_ratio (rbs, ratio);

  debug_print (2, "Studying sample...");

  input_frames = 0;
  while (input_frames < sample_info->frames)
    {
      gint rem_input_frames = sample_info->frames - input_frames;
      gint len_input_frames = rem_input_frames > TIMESTRETCH_BUF_SIZE ?
	TIMESTRETCH_BUF_SIZE : rem_input_frames;

      gfloat *fi = &input[input_frames];
      sample_ops_timestretch_fill_buf_to_non_interleaved (sample_info,
							  len_input_frames,
							  fi,
							  input_non_int_buf);

      debug_print (2, "Studying %d frames (last == %d)...", len_input_frames,
		   rem_input_frames == len_input_frames);
      rubberband_study (rbs, (const float *const *) &input_non_int_buf,
			len_input_frames,
			rem_input_frames == len_input_frames);
      input_frames += len_input_frames;

      debug_print (2, "Studied input frames: %d", input_frames);
    }

  debug_print (2, "Study completed at %d...", input_frames);

  input_frames = 0;
  output_frames = 0;
  while (input_frames < sample_info->frames)
    {
      gint rem_input_frames = sample_info->frames - input_frames;
      gint len_input_frames = rem_input_frames > TIMESTRETCH_BUF_SIZE ?
	TIMESTRETCH_BUF_SIZE : rem_input_frames;

      gfloat *fi = &input[input_frames];
      sample_ops_timestretch_fill_buf_to_non_interleaved (sample_info,
							  len_input_frames,
							  fi,
							  input_non_int_buf);

      debug_print (2, "Processing %d frames (last == %d)...",
		   len_input_frames, rem_input_frames == len_input_frames);
      rubberband_process (rbs, (const float *const *) &input_non_int_buf,
			  len_input_frames,
			  rem_input_frames == len_input_frames);
      gint available = rubberband_available (rbs);
      gint rem_output_frames = available;
      debug_print (2, "Available frames in rubberband: %d", available);
      while (rem_output_frames)
	{
	  gint len_output_available =
	    rem_output_frames > TIMESTRETCH_BUF_SIZE ? TIMESTRETCH_BUF_SIZE :
	    rem_output_frames;
	  rubberband_retrieve (rbs, &output_non_int_buf,
			       len_output_available);
	  debug_print (2, "Retrieved %d frames", len_output_available);

	  sample_ops_timestretch_fill_buf_from_non_interleaved (sample_info,
								len_output_available,
								output_non_int_buf,
								input_non_int_buf);

	  g_byte_array_append (output, (guint8 *) input_non_int_buf,
			       len_output_available * sample_info->channels *
			       sizeof (gfloat));

	  rem_output_frames -= len_output_available;
	}

      input_frames += len_input_frames;
      output_frames += available;

      debug_print (2,
		   "Processed input frames: %d; generated output frames: %d",
		   input_frames, output_frames);
    }

  rubberband_delete (rbs);

  g_free (input_non_int_buf);
  g_free (output_non_int_buf);

  g_free (input);

  output_frames = input_frames * ratio;
  debug_print (2, "Truncating output frames to %d...", output_frames);

  output_size = SAMPLE_INFO_FRAME_SIZE (sample_info) * output_frames;
  if (float_mode)
    {
      sample->content = g_byte_array_new_take ((guint8 *) output,
					       output_size);
    }
  else
    {
      sample->content = g_byte_array_sized_new (output_size);
      sample->content->len = output_size;
      src_float_to_short_array ((gfloat *) output->data,
				(gint16 *) sample->content->data,
				output_frames * sample_info->channels);
      g_free (output);
    }

  sample_info->frames = output_frames;
  sample_info->loop_start *= ratio;
  sample_info->loop_end *= ratio;
  if (sample_info->tempo)
    {
      sample_info->tempo /= ratio;
    }

  return 0;
}
