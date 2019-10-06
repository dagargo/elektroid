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

extern int debug;

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

short *
audio_load_sample_in_mono (SNDFILE * sndfile, int channels, sf_count_t frames,
			   gint * running, void (*progress) (gdouble))
{
  short *frame;
  float sum;
  int i, j, k;
  short *buffer = malloc (sizeof (short) * frames);

  if (channels == 1)
    sf_readf_short (sndfile, buffer, frames);
  else
    {
      frame = malloc (sizeof (short) * channels);

      for (i = 0, k = 0; i < frames; i++, k++)
	{
	  sf_readf_short (sndfile, frame, 1);
	  sum = 0;
	  for (j = 0; j < channels; j++)
	    {
	      sum += frame[j];
	    }
	  buffer[i] = sum / (float) channels;
	  if (progress && k == PA_BUFFER_LEN)
	    {
	      progress (i * 1.0 / frames);
	      k = 0;
	    }
	}

      progress (i * 1.0 / frames);
      free (frame);
    }

  return buffer;
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

size_t
audio_load (struct audio *audio, char *path, gint * running,
	    void (*progress) (gdouble))
{
  struct stat path_stat;
  SF_INFO sf_info;
  SNDFILE *sndfile;
  SRC_DATA src_data;
  int frames;
  short *buffer_s;
  int err;
  float *buffer_f;
  int resampled_buffer_len;

  g_array_set_size (audio->sample, 0);

  if (path == NULL || stat (path, &path_stat) != 0
      || !S_ISREG (path_stat.st_mode))
    {
      return 0;
    }

  sf_info.format = 0;
  sndfile = sf_open (path, SFM_READ, &sf_info);
  if (!sndfile)
    {
      fprintf (stderr, __FILE__ ": sf_open() failed: %s\n",
	       sf_strerror (sndfile));
      return 0;
    }

  frames = sf_info.frames;

  //TODO: check for too long samples before loading

  buffer_s =
    audio_load_sample_in_mono (sndfile, sf_info.channels, frames, running,
			       progress);

  if (!*running)
    {
      g_array_set_size (audio->sample, 0);
      goto cleanup;
    }

  if (sf_info.samplerate != 48000)
    {
      buffer_f = malloc (frames * sizeof (float));
      src_short_to_float_array (buffer_s, buffer_f, frames);

      src_data.data_in = buffer_f;
      src_data.input_frames = frames;
      src_data.src_ratio = 48000.0 / sf_info.samplerate;
      resampled_buffer_len = frames * src_data.src_ratio;
      src_data.data_out = malloc (resampled_buffer_len * sizeof (float));
      src_data.output_frames = resampled_buffer_len;
      err = src_simple (&src_data, SRC_SINC_BEST_QUALITY, 1);
      if (err)
	{
	  fprintf (stderr, __FILE__ ": src_simple() failed: %s\n",
		   src_strerror (err));
	}

      free (buffer_s);
      free (buffer_f);
      frames = src_data.output_frames_gen;
      buffer_s = malloc (frames * sizeof (short));
      src_float_to_short_array (src_data.data_out, buffer_s, frames);

      free (src_data.data_out);
    }

  g_array_append_vals (audio->sample, buffer_s, frames);

cleanup:
  free (buffer_s);
  sf_close (sndfile);
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
