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

#include "../config.h"
#include <sndfile.h>
#include <samplerate.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <glib.h>
#include <netinet/in.h>
#include "audio.h"
#include "utils.h"

#define PA_BUFFER_LEN 4800
#define LOAD_BUFFER_LEN 10000

void
audio_play (struct audio *audio)
{
  int err;
  int remaining;
  short *buffer;
  int buffer_len;
  int len;

  if (!audio->pa_s)
    {
      return;
    }

  audio->playing = 1;
  buffer = (short *) audio->sample->data;
  remaining = audio->sample->len;
  buffer_len =
    (audio->sample->len < PA_BUFFER_LEN ? audio->sample->len : PA_BUFFER_LEN);
  while (remaining > 0 && audio->playing)
    {
      len = remaining > buffer_len ? buffer_len : remaining;
      //PulseAudio API uses bytes instead of samples
      if (pa_simple_write (audio->pa_s, buffer, len * sizeof (short), &err) <
	  0)
	fprintf (stderr, __FILE__ ": pa_simple_write() failed: %s\n",
		 pa_strerror (err));
      remaining -= buffer_len;
      buffer += buffer_len;
    }
}

void
audio_stop (struct audio *audio)
{
  int err;

  if (!audio->pa_s)
    {
      return;
    }

  audio->playing = 0;

  if (pa_simple_flush (audio->pa_s, &err) < 0)
    fprintf (stderr, __FILE__ ": pa_simple_flush() failed: %s\n",
	     pa_strerror (err));

  if (pa_simple_drain (audio->pa_s, &err) < 0)
    fprintf (stderr, __FILE__ ": pa_simple_drain() failed: %s\n",
	     pa_strerror (err));

}

size_t
audio_save_file (char *path, GArray * sample)
{
  struct stat path_stat;
  SF_INFO sf_info;
  SNDFILE *sndfile;
  int total;

  if (path == NULL || (stat (path, &path_stat) == 0
		       && !S_ISREG (path_stat.st_mode)))
    {
      return -1;
    }

  sf_info.format = 0;
  sf_info.samplerate = 48000;
  sf_info.channels = 1;
  sf_info.format = SF_FORMAT_WAV | SF_FORMAT_PCM_16;
  sndfile = sf_open (path, SFM_WRITE, &sf_info);
  if (!sndfile)
    {
      fprintf (stderr, __FILE__ ": sf_open() failed: %s\n",
	       sf_strerror (sndfile));
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

  debug_print ("Converting to mono...\n");

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
audio_load (struct audio *audio, char *path, gint * running,
	    void (*progress) (gdouble))
{
  struct stat path_stat;
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

  g_array_set_size (audio->sample, 0);

  if (path == NULL || stat (path, &path_stat) != 0
      || !S_ISREG (path_stat.st_mode))
    {
      return 0;
    }

  debug_print ("Loading file %s...\n", path);

  sf_info.format = 0;
  sndfile = sf_open (path, SFM_READ, &sf_info);
  if (!sndfile)
    {
      fprintf (stderr, __FILE__ ": sf_open() failed: %s\n",
	       sf_strerror (sndfile));
      return 0;
    }

  //TODO: limit sample length or upload

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
      g_mutex_lock (&audio->load_mutex);
      g_array_set_size (audio->sample, 0);
      g_mutex_unlock (&audio->load_mutex);
      goto cleanup;
    }

  audio->len = sf_info.frames * src_data.src_ratio;

  debug_print ("Loading sample (%ld)...\n", sf_info.frames);

  f = 0;
  while (f < sf_info.frames && (!running || *running))
    {
      debug_print ("Loading buffer...\n");

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
	  g_mutex_lock (&audio->load_mutex);
	  g_array_append_vals (audio->sample, buffer_input, frames_read);
	  g_mutex_unlock (&audio->load_mutex);
	}
      else
	{
	  src_short_to_float_array (buffer_input, buffer_f, frames_read);
	  src_data.output_frames = src_data.input_frames * src_data.src_ratio;
	  err = src_process (src_state, &src_data);
	  debug_print ("Resampling...\n");
	  if (err)
	    {
	      debug_print ("Error %s\n", src_strerror (err));
	      break;
	    }
	  src_float_to_short_array (src_data.data_out, buffer_s,
				    src_data.output_frames_gen);
	  g_mutex_lock (&audio->load_mutex);
	  g_array_append_vals (audio->sample, buffer_s,
			       src_data.output_frames_gen);
	  g_mutex_unlock (&audio->load_mutex);
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
      g_mutex_lock (&audio->load_mutex);
      g_array_set_size (audio->sample, 0);
      g_mutex_unlock (&audio->load_mutex);
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

  if (running)
    {
      *running = 0;
    }

  return audio->sample->len;
}

int
audio_init (struct audio *audio)
{
  int err = 0;

  audio->sample = g_array_new (FALSE, FALSE, sizeof (short));

  pa_sample_spec pa_ss;
  pa_ss.format = PA_SAMPLE_S16LE;
  pa_ss.channels = 1;
  pa_ss.rate = 48000;
  audio->pa_s = pa_simple_new (NULL,	// Use the default server.
			       PACKAGE,	// Our application's name.
			       PA_STREAM_PLAYBACK, NULL,	// Use the default device.
			       PACKAGE,	// Description of our stream.
			       &pa_ss,	// Our sample format.
			       NULL,	// Use default channel map
			       NULL,	// Use default buffering attributes.
			       NULL	// Ignore error code.
    );

  if (!audio->pa_s)
    {
      fprintf (stderr, __FILE__ ": No pulseaudio.\n");
    }

  return err;
}

void
audio_destroy (struct audio *audio)
{
  audio_stop (audio);

  g_array_free (audio->sample, TRUE);

  if (audio->pa_s)
    {
      pa_simple_free (audio->pa_s);
    }
}

int
audio_check (struct audio *audio)
{
  return audio->pa_s ? 1 : 0;
}
