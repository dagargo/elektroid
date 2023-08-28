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

#include <sndfile.h>
#include <samplerate.h>
#include <math.h>
#include <errno.h>
#include "sample.h"

#define LOAD_BUFFER_LEN (32 * 1024)

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
  position = ftell (file);
  fseek (file, 0, SEEK_END);
  fileSize = ftell (file);
  fseek (file, position, SEEK_SET);
  return fileSize;
}

static sf_count_t
seek_file_io (sf_count_t offset, int whence, void *user_data)
{
  FILE *file = user_data;
  fseek (file, offset, whence);
  return ftell (file);
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

  frames = sample->len / (sample_info->channels * SAMPLE_SIZE);
  debug_print (1, "Frames: %" PRIu64 "; sample rate: %d; channels: %d\n",
	       frames, sample_info->rate, sample_info->channels);
  debug_print (1, "Loop start at %d; loop end at %d\n",
	       sample_info->loop_start, sample_info->loop_end);

  memset (&sf_info, 0, sizeof (sf_info));
  sf_info.samplerate = sample_info->rate;
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
  smpl_chunk_data.sample_period = 1e9 / sample_info->rate;
  smpl_chunk_data.midi_unity_note = sample_info->midi_note;
  smpl_chunk_data.midi_pitch_fraction = 0;
  smpl_chunk_data.smpte_format = 0;
  smpl_chunk_data.smpte_offset = 0;
  smpl_chunk_data.num_sampler_loops = 1;
  smpl_chunk_data.sampler_data = 0;
  smpl_chunk_data.sample_loop.cue_point_id = 0;
  smpl_chunk_data.sample_loop.type = sample_info->loop_type;
  smpl_chunk_data.sample_loop.start = sample_info->loop_start;
  smpl_chunk_data.sample_loop.end = sample_info->loop_end;
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
      error_print ("Unexpected frames while writing to file (%" PRIu64 " != %"
		   PRIu64 ")\n", total, frames);
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

static void
audio_mono_to_stereo (gshort * input, gshort * output, gint size)
{
  debug_print (2, "Converting to stereo...\n");

  for (gint i = 0; i < size; i++, input++)
    {
      *output = *input;
      output++;
      *output = *input;
      output++;
    }
}

static void
sample_set_sample_info (struct sample_info *sample_info, SNDFILE * sndfile,
			SF_INFO * sf_info)
{
  struct SF_CHUNK_INFO chunk_info;
  SF_CHUNK_ITERATOR *chunk_iter;
  struct smpl_chunk_data smpl_chunk_data;
  gboolean disable_loop = FALSE;

  sample_info->channels = sf_info->channels;
  sample_info->rate = sf_info->samplerate;
  sample_info->frames = sf_info->frames;

  switch (sf_info->format & SF_FORMAT_SUBMASK)
    {
    case SF_FORMAT_PCM_S8:
      sample_info->bits = 8;
      break;
    case SF_FORMAT_PCM_16:
      sample_info->bits = 16;
      break;
    case SF_FORMAT_PCM_24:
      sample_info->bits = 24;
      break;
    case SF_FORMAT_PCM_32:
      sample_info->bits = 32;
      break;
    default:
      sample_info->bits = 0;
    }

  strcpy (chunk_info.id, SMPL_CHUNK_ID);
  chunk_info.id_size = strlen (SMPL_CHUNK_ID);
  chunk_iter = sf_get_chunk_iterator (sndfile, &chunk_info);

  if (chunk_iter)
    {
      chunk_info.datalen = sizeof (struct smpl_chunk_data);
      memset (&smpl_chunk_data, 0, chunk_info.datalen);
      chunk_info.data = &smpl_chunk_data;
      sf_get_chunk_data (chunk_iter, &chunk_info);
      sample_info->loop_start = le32toh (smpl_chunk_data.sample_loop.start);
      sample_info->loop_end = le32toh (smpl_chunk_data.sample_loop.end);
      sample_info->loop_type = le32toh (smpl_chunk_data.sample_loop.type);
      sample_info->midi_note = le32toh (smpl_chunk_data.midi_unity_note);
      if (sample_info->loop_start >= sample_info->frames)
	{
	  debug_print (2, "Bad loop start\n");
	  disable_loop = TRUE;
	}
      if (sample_info->loop_end >= sample_info->frames)
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
      sample_info->loop_start = sample_info->frames - 1;
      sample_info->loop_end = sample_info->loop_start;
      sample_info->loop_type = 0;
    }

  debug_print (2, "Loop start at %d, loop end at %d\n",
	       sample_info->loop_start, sample_info->loop_end);
}

gint
sample_load_sample_info (const gchar * path, struct sample_info *sample_info)
{
  SF_INFO sf_info;
  SNDFILE *sndfile;
  FILE *file;

  memset (sample_info, 0, sizeof (struct sample_info));

  file = fopen (path, "rb");
  if (!file)
    {
      return -errno;
    }

  sndfile = sf_open_virtual (&FILE_IO, SFM_READ, &sf_info, file);
  if (!sndfile)
    {
      error_print ("Error while reading %s: %s\n", path,
		   sf_strerror (sndfile));
      return -1;
    }

  sample_set_sample_info (sample_info, sndfile, &sf_info);

  fclose (file);
  return 0;
}

// If control->data is NULL, then a new struct sample_info * is created and control->data points to it.
// In case of failure, if control->data is NULL is freed.

static gint
sample_load_raw (void *data, SF_VIRTUAL_IO * sf_virtual_io,
		 struct job_control *control, GByteArray * sample,
		 struct sample_info *sample_info_dst, sample_load_cb cb,
		 gpointer cb_data)
{
  SF_INFO sf_info;
  SNDFILE *sndfile;
  SRC_DATA src_data;
  SRC_STATE *src_state;
  gint16 *buffer_input;
  gint16 *buffer_input_multi;
  gint16 *buffer_input_mono;
  gint16 *buffer_input_stereo;
  gint16 *buffer_s;
  gfloat *buffer_f;
  gint err, resampled_buffer_len, f, frames_read;
  gboolean active;
  gdouble ratio;
  struct sample_info *sample_info_src;
  guint bytes_per_frame;

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

  sample_info_src = control->data;
  if (!sample_info_src)
    {
      sample_info_src = g_malloc (sizeof (struct sample_info));
    }

  if (control)
    {
      g_mutex_lock (&control->mutex);
    }

  sample_info_dst->channels = sample_info_dst->channels ?
    sample_info_dst->channels : sf_info.channels;
  sample_info_dst->rate = sample_info_dst->rate ? sample_info_dst->rate :
    sf_info.samplerate;
  sample_info_dst->bits = 16;

  bytes_per_frame = sample_info_dst->channels * SAMPLE_SIZE;

  sample_set_sample_info (sample_info_src, sndfile, &sf_info);

  sample_info_dst->loop_type = sample_info_src->loop_type;

  if (control)
    {
      g_mutex_unlock (&control->mutex);
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
    g_malloc (LOAD_BUFFER_LEN * BYTES_PER_FRAME (sample_info_src->channels));
  buffer_input_mono = g_malloc (LOAD_BUFFER_LEN * SAMPLE_SIZE);
  buffer_input_stereo = g_malloc (LOAD_BUFFER_LEN * 2 * SAMPLE_SIZE);

  buffer_f =
    g_malloc (LOAD_BUFFER_LEN * sample_info_dst->channels * sizeof (gfloat));
  src_data.data_in = buffer_f;
  ratio = sample_info_dst->rate / (double) sample_info_src->rate;
  src_data.src_ratio = ratio;

  src_data.output_frames = ceil (LOAD_BUFFER_LEN * src_data.src_ratio);
  resampled_buffer_len = src_data.output_frames * sample_info_dst->channels;
  buffer_s = g_malloc (resampled_buffer_len * SAMPLE_SIZE);
  src_data.data_out = g_malloc (resampled_buffer_len * sizeof (gfloat));

  src_state = src_new (SRC_SINC_BEST_QUALITY, sample_info_dst->channels,
		       &err);
  if (err)
    {
      goto cleanup;
    }

  if (control)
    {
      g_mutex_lock (&control->mutex);
    }
  sample_info_dst->frames = sample_info_src->frames * ratio;
  sample_info_dst->loop_start = round (sample_info_src->loop_start * ratio);
  sample_info_dst->loop_end = round (sample_info_src->loop_end * ratio);
  if (control)
    {
      g_mutex_unlock (&control->mutex);
    }

  debug_print (2, "Loading sample (%d frames)...\n", sample_info_src->frames);

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
  while (f < sample_info_src->frames && active)
    {
      debug_print (2, "Loading %d channels buffer...\n",
		   sample_info_dst->channels);
      frames_read = sf_readf_short (sndfile, buffer_input_multi,
				    LOAD_BUFFER_LEN);
      f += frames_read;

      if (sample_info_dst->channels == sample_info_src->channels)
	{
	  buffer_input = buffer_input_multi;
	}
      else
	{
	  audio_multichannel_to_mono (buffer_input_multi,
				      buffer_input_mono, frames_read,
				      sample_info_src->channels);
	  if (sample_info_dst->channels == 1)
	    {
	      buffer_input = buffer_input_mono;
	    }
	  else
	    {
	      audio_mono_to_stereo (buffer_input_mono,
				    buffer_input_stereo, frames_read);
	      buffer_input = buffer_input_stereo;
	    }
	}

      if (sample_info_dst->rate == sample_info_src->rate)
	{
	  if (control)
	    {
	      g_mutex_lock (&control->mutex);
	    }
	  g_byte_array_append (sample, (guint8 *) buffer_input,
			       frames_read * bytes_per_frame);
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
				    frames_read * sample_info_dst->channels);
	  debug_print (2, "Resampling %d channels with ratio %f...\n",
		       sample_info_dst->channels, src_data.src_ratio);
	  err = src_process (src_state, &src_data);
	  if (err)
	    {
	      g_byte_array_set_size (sample, 0);
	      error_print ("Error while resampling: %s\n",
			   src_strerror (err));
	      break;
	    }
	  src_float_to_short_array (src_data.data_out, buffer_s,
				    src_data.output_frames_gen *
				    sample_info_dst->channels);
	  if (control)
	    {
	      g_mutex_lock (&control->mutex);
	    }
	  g_byte_array_append (sample, (guint8 *) buffer_s,
			       src_data.output_frames_gen * bytes_per_frame);
	  if (control)
	    {
	      g_mutex_unlock (&control->mutex);
	    }
	}

      if (control)
	{
	  g_mutex_lock (&control->mutex);
	  cb (control, f * 1.0 / sample_info_src->frames, cb_data);
	  active = control->active;
	  g_mutex_unlock (&control->mutex);
	}
    }

  src_delete (src_state);

  if (control)
    {
      g_mutex_lock (&control->mutex);
      if (!control->active)
	{
	  g_byte_array_set_size (sample, 0);
	}
      g_mutex_unlock (&control->mutex);
    }

cleanup:
  g_free (buffer_input_multi);
  g_free (buffer_input_mono);
  g_free (buffer_input_stereo);
  g_free (buffer_s);
  g_free (buffer_f);
  g_free (src_data.data_out);

  sf_close (sndfile);

  if (sample->len)
    {
      if (!control->data)
	{
	  control->data = sample_info_src;
	}
      // This removes the additional samples added by the resampler due to rounding.
      g_byte_array_set_size (sample,
			     sample_info_dst->frames * bytes_per_frame);
      return 0;
    }
  else
    {
      if (!control->data)
	{
	  g_free (sample_info_src);
	}
      return -1;
    }
}

gint
sample_load_from_array (GByteArray * wave, GByteArray * sample,
			struct job_control *control,
			struct sample_info *sample_info_dst)
{
  struct g_byte_array_io_data data;
  data.pos = 0;
  data.array = wave;
  return sample_load_raw (&data, &G_BYTE_ARRAY_IO, control, sample,
			  sample_info_dst, set_job_control_progress_no_sync,
			  NULL);
}

gint
sample_load_from_file_with_cb (const gchar * path, GByteArray * sample,
			       struct job_control *control,
			       struct sample_info *sample_info_dst,
			       sample_load_cb cb, gpointer cb_data)
{
  FILE *file = fopen (path, "rb");
  if (!file)
    {
      return -errno;
    }
  gint err = sample_load_raw (file, &FILE_IO, control, sample,
			      sample_info_dst, cb, cb_data);
  fclose (file);
  return err;
}

gint
sample_load_from_file (const gchar * path, GByteArray * sample,
		       struct job_control *control,
		       struct sample_info *sample_info_dst)
{
  return sample_load_from_file_with_cb (path, sample, control,
					sample_info_dst,
					set_job_control_progress_no_sync,
					NULL);
}

static gboolean
sample_is_mp3_supported ()
{
#if defined(__linux__)
  static char buffer[LABEL_MAX];
  sf_command (NULL, SFC_GET_LIB_VERSION, buffer, LABEL_MAX);
  debug_print (1, "libsndfile version: %s...\n", buffer);
  if (strverscmp (buffer, "libsndfile-1.1.0") >= 0)
    {
      return TRUE;
    }

  return FALSE;
#else
  return TRUE;
#endif
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
