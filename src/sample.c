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

#define JUNK_CHUNK_ID "JUNK"
#define SMPL_CHUNK_ID "smpl"
#define SMPL_CHUNK_LAST_FRAME_POS 0x30

static const guint8 JUNK_CHUNK_DATA[] = {
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0
};

static const guint8 SMPL_CHUNK_DATA[] = {
  0, 0, 0, 0, 0, 0, 0, 0, 0x61, 0x51, 0, 0, 60, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0x7f, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

gint
sample_save (const gchar * path, GByteArray * sample,
	     struct job_control *control)
{
  SF_INFO sf_info;
  SNDFILE *sndfile;
  sf_count_t frames, total;
  struct SF_CHUNK_INFO junk_chunk;
  struct SF_CHUNK_INFO smpl_chunk;

  debug_print (1, "Saving file '%s' (last frame loop %d)...\n", path,
	       *(guint32 *) control->data);

  sf_info.samplerate = 48000;
  sf_info.channels = 1;
  sf_info.format = SF_FORMAT_WAV | SF_FORMAT_PCM_16;
  sndfile = sf_open (path, SFM_WRITE, &sf_info);
  if (!sndfile)
    {
      error_print ("%s\n", sf_strerror (sndfile));
      return -1;
    }

  strcpy (junk_chunk.id, JUNK_CHUNK_ID);
  junk_chunk.id_size = strlen (JUNK_CHUNK_ID);
  junk_chunk.datalen = sizeof (JUNK_CHUNK_DATA);
  junk_chunk.data = (void *) JUNK_CHUNK_DATA;
  if (sf_set_chunk (sndfile, &junk_chunk) != SF_ERR_NO_ERROR)
    {
      error_print ("%s\n", sf_strerror (sndfile));
    }

  strcpy (smpl_chunk.id, SMPL_CHUNK_ID);
  smpl_chunk.id_size = strlen (SMPL_CHUNK_ID);
  smpl_chunk.datalen = sizeof (SMPL_CHUNK_DATA);
  smpl_chunk.data = malloc (sizeof (SMPL_CHUNK_DATA));
  memcpy (smpl_chunk.data, (void *) SMPL_CHUNK_DATA,
	  sizeof (SMPL_CHUNK_DATA));
  memcpy (&((guint8 *) smpl_chunk.data)[SMPL_CHUNK_LAST_FRAME_POS],
	  control->data, sizeof (guint32));
  if (sf_set_chunk (sndfile, &smpl_chunk) != SF_ERR_NO_ERROR)
    {
      error_print ("%s\n", sf_strerror (sndfile));
    }

  frames = sample->len >> 1;
  total = sf_write_short (sndfile, (gint16 *) sample->data, frames);

  sf_close (sndfile);

  if (total != frames)
    {
      error_print ("Unexpected frames while writing to sample\n");
      return -1;
    }

  return 0;
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

gint
sample_load_with_frames (const gchar * path, GByteArray * sample,
			 struct job_control *control, guint * frames)
{
  SF_INFO sf_info;
  SNDFILE *sndfile;
  SRC_DATA src_data;
  SRC_STATE *src_state;
  struct SF_CHUNK_INFO chunk_info;
  SF_CHUNK_ITERATOR *chunk_iter;
  gint16 *buffer_input;
  gint16 *buffer_input_multi;
  gint16 *buffer_input_mono;
  gint16 *buffer_s;
  guint32 *aux32;
  gfloat *buffer_f;
  gint err;
  gint resampled_buffer_len;
  gint f, frames_read;
  gboolean active;

  debug_print (1, "Loading file %s...\n", path);

  if (control)
    {
      g_mutex_lock (&control->mutex);
    }
  g_byte_array_set_size (sample, 0);
  if (control)
    {
      g_mutex_unlock (&control->mutex);
    }

  sf_info.format = 0;
  sndfile = sf_open (path, SFM_READ, &sf_info);
  if (!sndfile)
    {
      error_print ("%s\n", sf_strerror (sndfile));
      return -1;
    }

  strcpy (chunk_info.id, SMPL_CHUNK_ID);
  chunk_info.id_size = strlen (SMPL_CHUNK_ID);
  chunk_iter = sf_get_chunk_iterator (sndfile, &chunk_info);
  control->data = malloc (sizeof (guint32));
  if (chunk_iter)
    {
      chunk_info.datalen = PATH_MAX;
      chunk_info.data = malloc (PATH_MAX);
      sf_get_chunk_data (chunk_iter, &chunk_info);
      aux32 =
	(guint32 *) & ((guint8 *) chunk_info.data)[SMPL_CHUNK_LAST_FRAME_POS];
      *(guint32 *) control->data = *aux32;
      g_free (chunk_info.data);
    }
  else
    {
      *(guint32 *) control->data = 0;
    }
  debug_print (2, "Last frame loop: %d\n", *(guint32 *) control->data);

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
  if (control)
    {
      g_mutex_lock (&control->mutex);
      active = control->active;
      g_mutex_unlock (&control->mutex);
    }
  else
    {
      active = TRUE;
    }

  while (f < sf_info.frames && active)
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
	  if (control)
	    {
	      g_mutex_lock (&control->mutex);
	    }
	  g_byte_array_append (sample, (guint8 *) buffer_input,
			       frames_read << 1);
	  if (control)
	    {
	      g_mutex_unlock (&control->mutex);
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
	  if (control)
	    {
	      g_mutex_lock (&control->mutex);
	    }
	  g_byte_array_append (sample, (guint8 *) buffer_s,
			       src_data.output_frames_gen << 1);
	  if (control)
	    {
	      g_mutex_unlock (&control->mutex);
	    }
	}

      if (control)
	{
	  control->callback (f * 1.0 / sf_info.frames);
	  g_mutex_lock (&control->mutex);
	  active = control->active;
	  g_mutex_unlock (&control->mutex);
	}
    }

  src_delete (src_state);

  if (err)
    {
      error_print ("Error while preparing resampling: %s\n",
		   src_strerror (err));
    }

  if (control)
    {
      g_mutex_lock (&control->mutex);

      if (control->active)
	{
	  control->callback (1.0);
	}
      else
	{
	  g_byte_array_set_size (sample, 0);
	}

      g_mutex_unlock (&control->mutex);
    }

cleanup:
  free (buffer_input_multi);
  free (buffer_input_mono);
  free (buffer_s);
  free (buffer_f);
  free (src_data.data_out);

  sf_close (sndfile);

  if (!sample->len)
    {
      g_free (control->data);
    }

  return sample->len > 0 ? 0 : -1;
}

gint
sample_load (const gchar * path, GByteArray * array,
	     struct job_control *control)
{
  return sample_load_with_frames (path, array, control, NULL);
}
