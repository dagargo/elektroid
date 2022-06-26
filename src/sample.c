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

struct smpl_chunk_data
{
  guint32 manufacturer;
  guint32 product;
  guint32 sample_period;
  guint32 midi_unity_note;
  guint32 midi_pitch_fraction;
  guint32 smpte_format;
  guint32 smpte_offset;
  guint32 num_sampler_loops;
  guint32 sampler_data;
  struct sample_loop
  {
    guint32 cue_point_id;
    guint32 type;
    guint32 start;
    guint32 end;
    guint32 fraction;
    guint32 play_count;
  } sample_loop;
};

struct g_byte_array_io_data
{
  GByteArray *array;
  guint pos;
};

static const guint8 JUNK_CHUNK_DATA[] = {
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0
};

static sf_count_t get_filelen_byte_array_io (void *);
static sf_count_t seek_byte_array_io (sf_count_t, int, void *);
static sf_count_t read_byte_array_io (void *, sf_count_t, void *);
static sf_count_t write_byte_array_io (const void *, sf_count_t, void *);
static sf_count_t tell_byte_array_io (void *);

static SF_VIRTUAL_IO G_BYTE_ARRAY_IO = {
  .get_filelen = get_filelen_byte_array_io,
  .seek = seek_byte_array_io,
  .read = read_byte_array_io,
  .write = write_byte_array_io,
  .tell = tell_byte_array_io
};

static sf_count_t
get_filelen_byte_array_io (void *user_data)
{
  struct g_byte_array_io_data *data = user_data;
  return data->array->len;
}

static sf_count_t
seek_byte_array_io (sf_count_t offset, int whence, void *user_data)
{
  struct g_byte_array_io_data *data = user_data;
  switch (whence)
    {
    case SEEK_SET:
      data->pos = offset;
      break;
    case SEEK_CUR:
      data->pos = data->pos + offset;
      break;
    case SEEK_END:
      data->pos = data->array->len + offset;
      break;
    default:
      break;
    };

  if (data->pos > data->array->len)
    {
      g_byte_array_set_size (data->array, data->pos);
    }
  return data->pos;
}

static sf_count_t
read_byte_array_io (void *ptr, sf_count_t count, void *user_data)
{
  struct g_byte_array_io_data *data = user_data;
  if (data->pos + count > data->array->len)
    {
      count = data->array->len - data->pos;
    }
  memcpy (ptr, data->array->data + data->pos, count);
  data->pos += count;
  return count;
}

static sf_count_t
write_byte_array_io (const void *ptr, sf_count_t count, void *user_data)
{
  struct g_byte_array_io_data *data = user_data;

  if (data->pos >= data->array->len)
    {
      g_byte_array_set_size (data->array, data->pos);
    }

  if (data->pos + count > data->array->len)
    {
      g_byte_array_set_size (data->array, data->pos + count);
    }

  memcpy (data->array->data + data->pos, (guint8 *) ptr, count);
  data->pos += count;
  return count;
}

static sf_count_t
tell_byte_array_io (void *user_data)
{
  struct g_byte_array_io_data *data = user_data;
  return data->pos;
}

static gint
sample_get_wave_data (GByteArray * sample,
		      struct job_control *control,
		      struct g_byte_array_io_data *wave)
{
  SF_INFO sf_info;
  SNDFILE *sndfile;
  sf_count_t frames, total;
  struct SF_CHUNK_INFO junk_chunk_info;
  struct SF_CHUNK_INFO smpl_chunk_info;
  struct smpl_chunk_data smpl_chunk_data;
  struct sample_info *sample_info = control->data;

  g_byte_array_set_size (wave->array, sample->len + 1024);	//We need space for the headers.
  wave->array->len = 0;

  debug_print (1, "Loop start at %d; loop end at %d\n",
	       sample_info->start, sample_info->end);

  memset (&sf_info, 0, sizeof (sf_info));
  sf_info.samplerate = sample_info->samplerate;
  sf_info.channels = 1;
  sf_info.format = SF_FORMAT_WAV | SF_FORMAT_PCM_16;
  sndfile = sf_open_virtual (&G_BYTE_ARRAY_IO, SFM_WRITE, &sf_info, wave);
  if (!sndfile)
    {
      error_print ("%s\n", sf_strerror (sndfile));
      return -1;
    }

  strcpy (junk_chunk_info.id, JUNK_CHUNK_ID);
  junk_chunk_info.id_size = strlen (JUNK_CHUNK_ID);
  junk_chunk_info.datalen = sizeof (JUNK_CHUNK_DATA);
  junk_chunk_info.data = (void *) JUNK_CHUNK_DATA;
  if (sf_set_chunk (sndfile, &junk_chunk_info) != SF_ERR_NO_ERROR)
    {
      error_print ("%s\n", sf_strerror (sndfile));
    }

  smpl_chunk_data.manufacturer = 0;
  smpl_chunk_data.product = 0;
  smpl_chunk_data.sample_period = 1000000000 / sample_info->samplerate;
  smpl_chunk_data.midi_unity_note = 60;
  smpl_chunk_data.midi_pitch_fraction = 0;
  smpl_chunk_data.smpte_format = 0;
  smpl_chunk_data.smpte_offset = 0;
  smpl_chunk_data.num_sampler_loops = 1;
  smpl_chunk_data.sampler_data = 0;
  smpl_chunk_data.sample_loop.cue_point_id = 0;
  smpl_chunk_data.sample_loop.type = ELEKTRON_LOOP_TYPE;
  smpl_chunk_data.sample_loop.start = sample_info->start;
  smpl_chunk_data.sample_loop.end = sample_info->end;
  smpl_chunk_data.sample_loop.fraction = 0;
  smpl_chunk_data.sample_loop.play_count = 0;

  strcpy (smpl_chunk_info.id, SMPL_CHUNK_ID);
  smpl_chunk_info.id_size = strlen (SMPL_CHUNK_ID);
  smpl_chunk_info.datalen = sizeof (struct smpl_chunk_data);
  smpl_chunk_info.data = &smpl_chunk_data;
  if (sf_set_chunk (sndfile, &smpl_chunk_info) != SF_ERR_NO_ERROR)
    {
      error_print ("%s\n", sf_strerror (sndfile));
    }

  frames = sample->len >> 1;
  total = sf_write_short (sndfile, (gint16 *) sample->data, frames);

  sf_close (sndfile);

  if (total != frames)
    {
      error_print ("Unexpected frames while writing to sample (%ld != %ld)\n",
		   total, frames);
      return -1;
    }

  return 0;
}

gint
sample_get_wave (GByteArray * sample, GByteArray * wave,
		 struct job_control *control)
{
  struct g_byte_array_io_data data;
  data.pos = 0;
  data.array = wave;
  return sample_get_wave_data (sample, control, &data);
}

gint
sample_save (const gchar * path, GByteArray * sample,
	     struct job_control *control)
{
  GByteArray *wave = g_byte_array_new ();
  gint ret = sample_get_wave (sample, wave, control);
  if (!ret)
    {
      ret = save_file (path, wave, control);
    }
  g_byte_array_free (wave, TRUE);
  return ret;
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

static gint
sample_load_raw_data (struct g_byte_array_io_data *wave,
		      struct job_control *control, GByteArray * sample,
		      guint * frames)
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
  gfloat *buffer_f;
  gint err;
  gint resampled_buffer_len;
  gint f, frames_read;
  gboolean active;
  struct sample_info *sample_info;
  struct smpl_chunk_data smpl_chunk_data;

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
  sndfile = sf_open_virtual (&G_BYTE_ARRAY_IO, SFM_READ, &sf_info, wave);
  if (!sndfile)
    {
      error_print ("%s\n", sf_strerror (sndfile));
      return -1;
    }

  strcpy (chunk_info.id, SMPL_CHUNK_ID);
  chunk_info.id_size = strlen (SMPL_CHUNK_ID);
  chunk_iter = sf_get_chunk_iterator (sndfile, &chunk_info);
  sample_info = control->data;
  if (chunk_iter)
    {
      chunk_info.datalen = sizeof (struct smpl_chunk_data);
      chunk_info.data = &smpl_chunk_data;
      sf_get_chunk_data (chunk_iter, &chunk_info);

      sample_info->start = le32toh (smpl_chunk_data.sample_loop.start);
      sample_info->end = le32toh (smpl_chunk_data.sample_loop.end);
    }
  else
    {
      sample_info->start = 0;
      sample_info->end = 0;
    }

  if (sample_info->samplerate < 0)
    {
      sample_info->samplerate = sf_info.samplerate;
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
  src_data.src_ratio =
    ((double) sample_info->samplerate) / sf_info.samplerate;

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

  if (sample_info->samplerate != sf_info.samplerate)
    {
      debug_print (2, "Loop start at %d, loop end at %d before resampling\n",
		   sample_info->start, sample_info->end);
      sample_info->start *= src_data.src_ratio;
      sample_info->end *= src_data.src_ratio;
      debug_print (2, "Loop start at %d, loop end at %d after resampling\n",
		   sample_info->start, sample_info->end);
    }

  debug_print (2, "Loading sample (%" PRId64 " frames)...\n", sf_info.frames);

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

      if (sample_info->samplerate == sf_info.samplerate)
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

static gint
sample_load_raw_frames (GByteArray * wave, GByteArray * sample,
			struct job_control *control, guint * frames)
{
  struct g_byte_array_io_data data;
  data.pos = 0;
  data.array = wave;
  return sample_load_raw_data (&data, control, sample, frames);
}

gint
sample_load_raw (GByteArray * wave, GByteArray * sample,
		 struct job_control *control)
{
  return sample_load_raw_frames (wave, sample, control, NULL);
}

gint
sample_load_with_frames (const gchar * path, GByteArray * sample,
			 struct job_control *control, guint * frames)
{
  GByteArray *wave = g_byte_array_new ();

  debug_print (1, "Loading file %s...\n", path);

  int ret = load_file (path, wave, control);
  if (!ret)
    {
      ret = sample_load_raw_frames (wave, sample, control, frames);
    }
  g_byte_array_free (wave, TRUE);
  return ret;
}

gint
sample_load (const gchar * path, GByteArray * sample,
	     struct job_control *control)
{
  return sample_load_with_frames (path, sample, control, NULL);
}

gboolean
sample_is_mp3_supported ()
{
  static char buffer[LABEL_MAX];

  sf_command (NULL, SFC_GET_LIB_VERSION, buffer, LABEL_MAX);
  debug_print (1, "libsndfile version: %s...\n", buffer);
  if (strverscmp (buffer, "libsndfile-1.1.0") >= 0)
    {
      return TRUE;
    }

  return FALSE;
}
