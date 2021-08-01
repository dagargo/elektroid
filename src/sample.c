/*
 *   sample.c
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

#include "../config.h"
#include <sndfile.h>
#include <samplerate.h>
#include <inttypes.h>
#include "sample.h"
#include "utils.h"

size_t
sample_save (GByteArray * sample, gchar * path)
{
  SF_INFO sf_info;
  SNDFILE *sndfile;
  int total;

  debug_print (1, "Saving file %s...\n", path);

  sf_info.format = 0;
  sf_info.samplerate = 48000;
  sf_info.channels = 1;
  sf_info.format = SF_FORMAT_WAV | SF_FORMAT_PCM_16;
  sndfile = sf_open (path, SFM_WRITE, &sf_info);
  if (!sndfile)
    {
      error_print ("%s\n", sf_strerror (sndfile));
      return -1;
    }

  total = sf_write_short (sndfile, (gint16 *) sample->data, sample->len >> 1);

  sf_close (sndfile);

  return total << 1;
}

static void
audio_multichannel_to_mono (gshort * input, gshort * output, gint size,
			    gint channels)
{
  int i, j, v;

  debug_print (2, "Converting to mono...\n");

  for (i = 0; i < size; i++)
    {
      v = 0;
      for (j = 0; j < channels; j++)
	{
	  v += input[i * channels + j];
	}
      v /= channels;
      output[i] = v;
    }
}

size_t
sample_load (GByteArray * sample, GMutex * mutex, gint * frames, gchar * path,
	     gboolean * active, void (*progress) (gdouble))
{
  SF_INFO sf_info;
  SNDFILE *sndfile;
  SRC_DATA src_data;
  SRC_STATE *src_state;
  gint16 *buffer_input;
  gint16 *buffer_input_multi;
  gint16 *buffer_input_mono;
  gint16 *buffer_s;
  gfloat *buffer_f;
  gint err;
  gint resampled_buffer_len;
  gint f, frames_read;
  gboolean load_active;

  debug_print (1, "Loading file %s...\n", path);

  if (mutex)
    {
      g_mutex_lock (mutex);
    }
  g_byte_array_set_size (sample, 0);
  if (mutex)
    {
      g_mutex_unlock (mutex);
    }

  sf_info.format = 0;
  sndfile = sf_open (path, SFM_READ, &sf_info);
  if (!sndfile)
    {
      error_print ("%s\n", sf_strerror (sndfile));
      return -1;
    }

  //Set scale factor. See http://www.mega-nerd.com/libsndfile/api.html#note2
  if ((sf_info.format & SF_FORMAT_FLOAT) == SF_FORMAT_FLOAT ||
      (sf_info.format & SF_FORMAT_DOUBLE) == SF_FORMAT_DOUBLE)
    {
      debug_print (2,
		   "Setting scale factor to ensure correct integer readings...\n");
      sf_command (sndfile, SFC_SET_SCALE_FLOAT_INT_READ, NULL, SF_TRUE);
    }

  buffer_input_multi =
    malloc (LOAD_BUFFER_LEN * sf_info.channels * sizeof (gint16));
  buffer_input_mono = malloc (LOAD_BUFFER_LEN * sizeof (gint16));

  buffer_f = malloc (LOAD_BUFFER_LEN * sizeof (gfloat));
  src_data.data_in = buffer_f;
  src_data.src_ratio = 48000.0 / sf_info.samplerate;

  resampled_buffer_len = LOAD_BUFFER_LEN * src_data.src_ratio;
  buffer_s = malloc (resampled_buffer_len * sizeof (gint16));
  src_data.data_out = malloc (resampled_buffer_len * sizeof (gfloat));

  src_state = src_new (SRC_SINC_BEST_QUALITY, 1, &err);
  if (err)
    {
      goto cleanup;
    }

  if (frames)
    {
      *frames = sf_info.frames * src_data.src_ratio;
    }

  debug_print (2, "Loading sample (%" PRId64 ")...\n", sf_info.frames);

  f = 0;
  if (mutex)
    {
      g_mutex_lock (mutex);
    }
  load_active = (!active || *active);
  if (mutex)
    {
      g_mutex_unlock (mutex);
    }
  while (f < sf_info.frames && load_active)
    {
      debug_print (2, "Loading buffer...\n");

      frames_read =
	sf_readf_short (sndfile, buffer_input_multi, LOAD_BUFFER_LEN);

      if (frames_read < LOAD_BUFFER_LEN)
	{
	  src_data.end_of_input = SF_TRUE;
	}
      else
	{
	  src_data.end_of_input = 0;
	}
      src_data.input_frames = frames_read;
      f += frames_read;

      if (sf_info.channels == 1)
	{
	  buffer_input = buffer_input_multi;
	}
      else
	{
	  audio_multichannel_to_mono (buffer_input_multi, buffer_input_mono,
				      frames_read, sf_info.channels);
	  buffer_input = buffer_input_mono;
	}

      if (sf_info.samplerate == 48000)
	{
	  if (mutex)
	    {
	      g_mutex_lock (mutex);
	    }
	  g_byte_array_append (sample, (guint8 *) buffer_input,
			       frames_read << 1);
	  if (mutex)
	    {
	      g_mutex_unlock (mutex);
	    }
	}
      else
	{
	  src_short_to_float_array (buffer_input, buffer_f, frames_read);
	  src_data.output_frames = src_data.input_frames * src_data.src_ratio;
	  err = src_process (src_state, &src_data);
	  debug_print (2, "Resampling...\n");
	  if (err)
	    {
	      debug_print (2, "Error %s\n", src_strerror (err));
	      break;
	    }
	  src_float_to_short_array (src_data.data_out, buffer_s,
				    src_data.output_frames_gen);
	  if (mutex)
	    {
	      g_mutex_lock (mutex);
	    }
	  g_byte_array_append (sample, (guint8 *) buffer_s,
			       src_data.output_frames_gen << 1);
	  if (mutex)
	    {
	      g_mutex_unlock (mutex);
	    }
	}

      if (progress)
	{
	  progress (f * 1.0 / sf_info.frames);
	}

      if (mutex)
	{
	  g_mutex_lock (mutex);
	}
      load_active = (!active || *active);
      if (mutex)
	{
	  g_mutex_unlock (mutex);
	}
    }

  src_delete (src_state);

  if (err)
    {
      error_print ("Error while preparing resampling: %s\n",
		   src_strerror (err));
    }

  if (mutex)
    {
      g_mutex_lock (mutex);
    }
  if (!active || *active)
    {
      if (progress)
	{
	  progress (1.0);
	}
    }
  else
    {
      g_byte_array_set_size (sample, 0);
    }
  if (mutex)
    {
      g_mutex_unlock (mutex);
    }

cleanup:
  free (buffer_input_multi);
  free (buffer_input_mono);
  free (buffer_s);
  free (buffer_f);
  free (src_data.data_out);

  sf_close (sndfile);

  return sample->len;
}
