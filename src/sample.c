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

#define LOAD_BUFFER_LEN 10000

size_t
sample_save (GArray * sample, char *path)
{
  SF_INFO sf_info;
  SNDFILE *sndfile;
  int total;

  sf_info.format = 0;
  sf_info.samplerate = 48000;
  sf_info.channels = 1;
  sf_info.format = SF_FORMAT_WAV | SF_FORMAT_PCM_16;
  sndfile = sf_open (path, SFM_WRITE, &sf_info);
  if (!sndfile)
    {
      fprintf (stderr, "%s\n", sf_strerror (sndfile));
      return -1;
    }

  total = sf_write_short (sndfile, (short *) sample->data, sample->len);

  sf_close (sndfile);

  return total;
}

static void
audio_multichannel_to_mono (short *input, short *output, int size,
			    int channels)
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
sample_load (GArray * sample, GMutex * mutex, gint * frames, char *path,
	     gint * running, void (*progress) (gdouble))
{
  SF_INFO sf_info;
  SNDFILE *sndfile;
  SRC_DATA src_data;
  SRC_STATE *src_state;
  short *buffer_input;
  short *buffer_input_multi;
  short *buffer_input_mono;
  short *buffer_s;
  float *buffer_f;
  int err;
  int resampled_buffer_len;
  int f, frames_read;

  debug_print (2, "Loading file %s...\n", path);

  if (mutex)
    {
      g_mutex_lock (mutex);
    }
  g_array_set_size (sample, 0);
  if (mutex)
    {
      g_mutex_unlock (mutex);
    }

  sf_info.format = 0;
  sndfile = sf_open (path, SFM_READ, &sf_info);
  if (!sndfile)
    {
      fprintf (stderr, "%s\n", sf_strerror (sndfile));
      return -1;
    }

  buffer_input_multi =
    malloc (LOAD_BUFFER_LEN * sf_info.channels * sizeof (short));
  buffer_input_mono = malloc (LOAD_BUFFER_LEN * sizeof (short));

  buffer_f = malloc (LOAD_BUFFER_LEN * sizeof (float));
  src_data.data_in = buffer_f;
  src_data.src_ratio = 48000.0 / sf_info.samplerate;

  resampled_buffer_len = LOAD_BUFFER_LEN * src_data.src_ratio;
  buffer_s = malloc (resampled_buffer_len * sizeof (short));
  src_data.data_out = malloc (resampled_buffer_len * sizeof (float));

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
  while (f < sf_info.frames && (!running || *running))
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
	  g_array_append_vals (sample, buffer_input, frames_read);
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
	  g_array_append_vals (sample, buffer_s, src_data.output_frames_gen);
	  if (mutex)
	    {
	      g_mutex_unlock (mutex);
	    }
	}

      if (progress)
	{
	  progress (f * 1.0 / sf_info.frames);
	}

    }

  src_delete (src_state);

  if (err)
    {
      fprintf (stderr, __FILE__ ": src_process() failed: %s\n",
	       src_strerror (err));
    }

  if (running && !*running)
    {
      if (mutex)
	{
	  g_mutex_lock (mutex);
	}
      g_array_set_size (sample, 0);
      if (mutex)
	{
	  g_mutex_unlock (mutex);
	}
    }

  if (running && *running && progress)
    {
      progress (1.0);
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
