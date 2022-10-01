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
#include <math.h>
#include "sample.h"

#define JUNK_CHUNK_ID "JUNK"
#define SMPL_CHUNK_ID "smpl"

static const gchar *ELEKTROID_AUDIO_LOCAL_EXTS[] =
  { "wav", "ogg", "aiff", "flac", NULL };

static const gchar *ELEKTROID_AUDIO_LOCAL_EXTS_MP3[] =
  { "wav", "ogg", "aiff", "flac", "mp3", NULL };

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

static SF_VIRTUAL_IO G_BYTE_ARRAY_IO = {
  .get_filelen = get_filelen_byte_array_io,
  .seek = seek_byte_array_io,
  .read = read_byte_array_io,
  .write = write_byte_array_io,
  .tell = tell_byte_array_io
};

static sf_count_t
get_filelen_file_io (void *user_data)
{
  long fileSize, position;
  FILE *file = user_data;
  position = fseek (file, 0, SEEK_END);
  fileSize = ftell (file);
  fseek (file, position, SEEK_SET);
  return fileSize;
}

static sf_count_t
seek_file_io (sf_count_t offset, int whence, void *user_data)
{
  FILE *file = user_data;
  return fseek (file, offset, whence);
}

static sf_count_t
read_file_io (void *ptr, sf_count_t count, void *user_data)
{
  FILE *file = user_data;
  return fread (ptr, 1, count, file);
}

static sf_count_t
write_file_io (const void *ptr, sf_count_t count, void *user_data)
{
  FILE *file = user_data;
  return fwrite (ptr, 1, count, file);
}

static sf_count_t
tell_file_io (void *user_data)
{
  FILE *file = user_data;
  return ftell (file);
}

static SF_VIRTUAL_IO FILE_IO = {
  .get_filelen = get_filelen_file_io,
  .seek = seek_file_io,
  .read = read_file_io,
  .write = write_file_io,
  .tell = tell_file_io
};

static gint
sample_get_wave_data (GByteArray * sample, struct job_control *control,
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

  frames = sample->len >> sample_info->channels;
  debug_print (1, "Frames: %ld; sample rate: %d; channels: %d\n", frames,
	       sample_info->samplerate, sample_info->channels);
  debug_print (1, "Loop start at %d; loop end at %d\n",
	       sample_info->loopstart, sample_info->loopend);

  memset (&sf_info, 0, sizeof (sf_info));
  sf_info.samplerate = sample_info->samplerate;
  sf_info.channels = sample_info->channels;
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
  smpl_chunk_data.sample_period = 1e9 / sample_info->samplerate;
  smpl_chunk_data.midi_unity_note = 60;
  smpl_chunk_data.midi_pitch_fraction = 0;
  smpl_chunk_data.smpte_format = 0;
  smpl_chunk_data.smpte_offset = 0;
  smpl_chunk_data.num_sampler_loops = 1;
  smpl_chunk_data.sampler_data = 0;
  smpl_chunk_data.sample_loop.cue_point_id = 0;
  smpl_chunk_data.sample_loop.type = sample_info->looptype;
  smpl_chunk_data.sample_loop.start = sample_info->loopstart;
  smpl_chunk_data.sample_loop.end = sample_info->loopend;
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

  total = sf_writef_short (sndfile, (gint16 *) sample->data, frames);
  sf_close (sndfile);

  if (total != frames)
    {
      error_print ("Unexpected frames while writing to file (%ld != %ld)\n",
		   total, frames);
      return -1;
    }

  return 0;
}

gint
sample_get_wav_from_array (GByteArray * sample, GByteArray * wave,
			   struct job_control *control)
{
  struct g_byte_array_io_data data;
  data.pos = 0;
  data.array = wave;
  return sample_get_wave_data (sample, control, &data);
}

gint
sample_save_from_array (const gchar * path, GByteArray * sample,
			struct job_control *control)
{
  GByteArray *wave = g_byte_array_new ();
  gint ret = sample_get_wav_from_array (sample, wave, control);
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

// If control->data is NULL, then a new struct sample_info * is created and control->data points to it.
// In case of failure, if control->data is NULL is freed.
// Franes is the amount of frames after resampling. This value is set before the loading has terminated.

static gint
sample_load_raw (void *data, SF_VIRTUAL_IO * sf_virtual_io,
		 struct job_control *control, GByteArray * sample,
		 const struct sample_params *sample_params, guint * frames)
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
  gint err, resampled_buffer_len, f, frames_read, channels, samplerate;
  gboolean active;
  gdouble ratio;
  struct sample_info *sample_info;
  struct smpl_chunk_data smpl_chunk_data;
  gboolean disable_loop = FALSE;

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
  sndfile = sf_open_virtual (sf_virtual_io, SFM_READ, &sf_info, data);
  if (!sndfile)
    {
      error_print ("%s\n", sf_strerror (sndfile));
      return -1;
    }

  strcpy (chunk_info.id, SMPL_CHUNK_ID);
  chunk_info.id_size = strlen (SMPL_CHUNK_ID);
  chunk_iter = sf_get_chunk_iterator (sndfile, &chunk_info);
  sample_info = control->data;
  if (!control->data)
    {
      sample_info = g_malloc (sizeof (struct sample_info));
    }

  channels = sample_params->channels == 2 && sf_info.channels == 2 ? 2 : 1;
  samplerate =
    sample_params->samplerate ? sample_params->
    samplerate : sf_info.samplerate;

  if (control)
    {
      g_mutex_lock (&control->mutex);
    }
  sample_info->channels = sf_info.channels;
  sample_info->samplerate = sf_info.samplerate;
  sample_info->frames = sf_info.frames;
  if (control)
    {
      g_mutex_unlock (&control->mutex);
    }

  if (chunk_iter)
    {
      chunk_info.datalen = sizeof (struct smpl_chunk_data);
      memset (&smpl_chunk_data, 0, chunk_info.datalen);
      chunk_info.data = &smpl_chunk_data;
      sf_get_chunk_data (chunk_iter, &chunk_info);

      if ((sf_info.format & SF_FORMAT_TYPEMASK) == SF_FORMAT_WAV)
	{
	  switch (sf_info.format & SF_FORMAT_SUBMASK)
	    {
	    case SF_FORMAT_PCM_S8:
	      sample_info->bitdepth = 8;
	      break;
	    case SF_FORMAT_PCM_16:
	      sample_info->bitdepth = 16;
	      break;
	    case SF_FORMAT_PCM_24:
	      sample_info->bitdepth = 24;
	      break;
	    case SF_FORMAT_PCM_32:
	      sample_info->bitdepth = 32;
	      break;
	    default:
	      sample_info->bitdepth = 0;
	    }
	}
      else
	{
	  sample_info->bitdepth = 0;
	}
      sample_info->loopstart = le32toh (smpl_chunk_data.sample_loop.start);
      sample_info->loopend = le32toh (smpl_chunk_data.sample_loop.end);
      sample_info->looptype = le32toh (smpl_chunk_data.sample_loop.type);
      if (sample_info->loopstart >= sf_info.frames)
	{
	  debug_print (2, "Bad loop start\n");
	  disable_loop = TRUE;
	}
      if (sample_info->loopend >= sf_info.frames)
	{
	  debug_print (2, "Bad loop end\n");
	  disable_loop = TRUE;
	}
    }
  else
    {
      disable_loop = TRUE;
    }
  if (disable_loop)
    {
      sample_info->loopstart = sf_info.frames - 1;
      sample_info->loopend = sample_info->loopstart;
      sample_info->looptype = 0;
    }
  sample_info->bitdepth = 16;

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

  buffer_f = malloc (LOAD_BUFFER_LEN * channels * sizeof (gfloat));
  src_data.data_in = buffer_f;
  ratio = samplerate / (double) sf_info.samplerate;
  src_data.src_ratio = ratio;

  src_data.output_frames = ceil (LOAD_BUFFER_LEN * src_data.src_ratio);
  resampled_buffer_len = src_data.output_frames * channels;
  buffer_s = malloc (resampled_buffer_len * sizeof (gint16));
  src_data.data_out = malloc (resampled_buffer_len * sizeof (gfloat));

  src_state = src_new (SRC_SINC_BEST_QUALITY, channels, &err);
  if (err)
    {
      goto cleanup;
    }

  *frames = sf_info.frames * src_data.src_ratio;

  if (samplerate != sf_info.samplerate)
    {
      debug_print (2, "Loop start at %d, loop end at %d before resampling\n",
		   sample_info->loopstart, sample_info->loopend);
      sample_info->loopstart = round (sample_info->loopstart * ratio);
      sample_info->loopend = round (sample_info->loopend * ratio);
      debug_print (2, "Loop start at %d, loop end at %d after resampling\n",
		   sample_info->loopstart, sample_info->loopend);
    }

  debug_print (2, "Loading sample (%" PRId64 " frames)...\n", sf_info.frames);

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

  f = 0;
  while (f < sf_info.frames && active)
    {
      debug_print (2, "Loading %d channels buffer...\n", channels);
      frames_read = sf_readf_short (sndfile, buffer_input_multi,
				    LOAD_BUFFER_LEN);
      f += frames_read;

      if (channels == sf_info.channels)	// 1 <= channels <= 2
	{
	  buffer_input = buffer_input_multi;
	}
      else
	{
	  audio_multichannel_to_mono (buffer_input_multi, buffer_input_mono,
				      frames_read, sf_info.channels);
	  buffer_input = buffer_input_mono;
	}

      if (samplerate == sf_info.samplerate)
	{
	  if (control)
	    {
	      g_mutex_lock (&control->mutex);
	    }
	  g_byte_array_append (sample, (guint8 *) buffer_input,
			       frames_read << channels);
	  if (control)
	    {
	      g_mutex_unlock (&control->mutex);
	    }
	}
      else
	{
	  src_data.end_of_input = frames_read < LOAD_BUFFER_LEN ? SF_TRUE : 0;
	  src_data.input_frames = frames_read;

	  src_short_to_float_array (buffer_input, buffer_f,
				    frames_read * channels);
	  debug_print (2, "Resampling %d channels with ratio %f...\n",
		       channels, src_data.src_ratio);
	  err = src_process (src_state, &src_data);
	  if (err)
	    {
	      g_byte_array_set_size (sample, 0);
	      error_print ("Error while resampling: %s\n",
			   src_strerror (err));
	      break;
	    }
	  src_float_to_short_array (src_data.data_out, buffer_s,
				    src_data.output_frames_gen * channels);
	  if (control)
	    {
	      g_mutex_lock (&control->mutex);
	    }
	  g_byte_array_append (sample, (guint8 *) buffer_s,
			       src_data.output_frames_gen << channels);
	  if (control)
	    {
	      g_mutex_unlock (&control->mutex);
	    }
	}

      if (control)
	{
	  g_mutex_lock (&control->mutex);
	  set_job_control_progress_no_sync (control,
					    f * 1.0 / sf_info.frames);
	  active = control->active;
	  g_mutex_unlock (&control->mutex);
	}
    }

  src_delete (src_state);

  if (control)
    {
      g_mutex_lock (&control->mutex);

      if (control->active)
	{
	  set_job_control_progress_no_sync (control, 1.0);
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

  if (sample->len)
    {
      if (!control->data)
	{
	  control->data = sample_info;
	}
      // This removes the additional samples added by the resampler due to rounding.
      g_byte_array_set_size (sample, *frames << channels);
      return 0;
    }
  else
    {
      if (!control->data)
	{
	  g_free (sample_info);
	}
      return -1;
    }
}

gint
sample_load_from_array (GByteArray * wave, GByteArray * sample,
			struct job_control *control,
			const struct sample_params *sample_params,
			guint * frames)
{
  struct g_byte_array_io_data data;
  data.pos = 0;
  data.array = wave;
  return sample_load_raw (&data, &G_BYTE_ARRAY_IO, control, sample,
			  sample_params, frames);
}

gint
sample_load_from_file (const gchar * path, GByteArray * sample,
		       struct job_control *control,
		       const struct sample_params *sample_params,
		       guint * frames)
{
  FILE *file = fopen (path, "rb");
  if (!file)
    {
      return errno;
    }
  gint err = sample_load_raw (file, &FILE_IO, control, sample, sample_params,
			      frames);
  fclose (file);
  return err;
}

static gboolean
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

const gchar **
sample_get_sample_extensions ()
{
  if (sample_is_mp3_supported ())
    {
      return ELEKTROID_AUDIO_LOCAL_EXTS_MP3;
    }
  return ELEKTROID_AUDIO_LOCAL_EXTS;
}
