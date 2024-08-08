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

#include <samplerate.h>
#include <math.h>
#include <errno.h>
#include "sample.h"
#include "connectors/microfreak_sample.h"

#define LOAD_BUFFER_LEN (32 * KIB)

#define JUNK_CHUNK_ID "JUNK"
#define SMPL_CHUNK_ID "smpl"

#define HEADERS_SPACE 4096	//Gross estimation for the sample (WAV) headers

static const gchar *ELEKTROID_AUDIO_LOCAL_EXTS[] =
  { "wav", "ogg", "aiff", "flac", MICROFREAK_PWAVETABLE_EXT,
  MICROFREAK_ZWAVETABLE_EXT, MICROFREAK_PSAMPLE_EXT,
  MICROFREAK_ZSAMPLE_EXT,
#if !defined(__linux__) || HAVE_SNDFILE_MP3
  "mp3",
#endif
  NULL
};

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
sample_write_audio_file_data (struct idata *idata,
			      struct g_byte_array_io_data *wave,
			      struct job_control *control, guint32 format)
{
  SF_INFO sf_info;
  SNDFILE *sndfile;
  sf_count_t frames, total;
  struct SF_CHUNK_INFO junk_chunk_info;
  struct SF_CHUNK_INFO smpl_chunk_info;
  struct smpl_chunk_data smpl_chunk_data;
  GByteArray *sample = idata->content;
  struct sample_info *sample_info = idata->info;

  frames = sample->len / SAMPLE_INFO_FRAME_SIZE (sample_info);
  debug_print (1, "Frames: %" PRIu64 "; sample rate: %d; channels: %d",
	       frames, sample_info->rate, sample_info->channels);
  debug_print (1, "Loop start at %d; loop end at %d",
	       sample_info->loop_start, sample_info->loop_end);

  memset (&sf_info, 0, sizeof (sf_info));
  sf_info.samplerate = sample_info->rate;
  sf_info.channels = sample_info->channels;
  sf_info.format = format;

  sndfile = sf_open_virtual (&G_BYTE_ARRAY_IO, SFM_WRITE, &sf_info, wave);
  if (!sndfile)
    {
      error_print ("%s", sf_strerror (sndfile));
      return -1;
    }

  strcpy (junk_chunk_info.id, JUNK_CHUNK_ID);
  junk_chunk_info.id_size = strlen (JUNK_CHUNK_ID);
  junk_chunk_info.datalen = sizeof (JUNK_CHUNK_DATA);
  junk_chunk_info.data = (void *) JUNK_CHUNK_DATA;
  if (sf_set_chunk (sndfile, &junk_chunk_info) != SF_ERR_NO_ERROR)
    {
      error_print ("%s", sf_strerror (sndfile));
    }

  smpl_chunk_data.manufacturer = 0;
  smpl_chunk_data.product = 0;
  smpl_chunk_data.sample_period = 1e9 / sample_info->rate;
  smpl_chunk_data.midi_unity_note = GUINT32_TO_LE (sample_info->midi_note);
  smpl_chunk_data.midi_pitch_fraction = 0;
  smpl_chunk_data.smpte_format = 0;
  smpl_chunk_data.smpte_offset = 0;
  smpl_chunk_data.num_sampler_loops = 1;
  smpl_chunk_data.sampler_data = 0;
  smpl_chunk_data.sample_loop.cue_point_id = 0;
  smpl_chunk_data.sample_loop.type = GUINT32_TO_LE (sample_info->loop_type);
  smpl_chunk_data.sample_loop.start = GUINT32_TO_LE (sample_info->loop_start);
  smpl_chunk_data.sample_loop.end = GUINT32_TO_LE (sample_info->loop_end);
  smpl_chunk_data.sample_loop.fraction = 0;
  smpl_chunk_data.sample_loop.play_count = 0;

  strcpy (smpl_chunk_info.id, SMPL_CHUNK_ID);
  smpl_chunk_info.id_size = strlen (SMPL_CHUNK_ID);
  smpl_chunk_info.datalen = sizeof (struct smpl_chunk_data);
  smpl_chunk_info.data = &smpl_chunk_data;
  if (sf_set_chunk (sndfile, &smpl_chunk_info) != SF_ERR_NO_ERROR)
    {
      error_print ("%s", sf_strerror (sndfile));
    }

  if ((sample_info->format & SF_FORMAT_SUBMASK) == SF_FORMAT_PCM_16)
    {
      total = sf_writef_short (sndfile, (gint16 *) sample->data, frames);
    }
  else if ((sample_info->format & SF_FORMAT_SUBMASK) == SF_FORMAT_FLOAT)
    {
      total = sf_writef_float (sndfile, (gfloat *) sample->data, frames);
    }
  else if ((sample_info->format & SF_FORMAT_SUBMASK) == SF_FORMAT_PCM_32)
    {
      total = sf_writef_int (sndfile, (gint32 *) sample->data, frames);
    }
  else
    {
      error_print ("Invalid sample format. Using short...");
      total = sf_writef_short (sndfile, (gint16 *) sample->data, frames);
    }

  sf_close (sndfile);

  if (total != frames)
    {
      error_print ("Unexpected frames while writing to file (%" PRIu64 " != %"
		   PRIu64 ")", total, frames);
      return -1;
    }

  return 0;
}

gint
sample_get_memfile_from_sample (struct idata *sample, struct idata *memfile,
				struct job_control *control, guint32 format)
{
  gint err;
  GByteArray *content;
  struct g_byte_array_io_data data;
  struct sample_info *sample_info = sample->info;
  guint frame_size = FRAME_SIZE (sample_info->channels,
				 format & SF_FORMAT_SUBMASK);

  content = g_byte_array_sized_new (sample->content->len * frame_size +
				    HEADERS_SPACE);
  idata_init (memfile, content, strdup (sample->name), NULL);
  data.pos = 0;
  data.array = content;

  err = sample_write_audio_file_data (sample, &data, control, format);
  if (err)
    {
      idata_free (memfile);
    }

  return err;
}

gint
sample_save_to_file (const gchar *path, struct idata *sample,
		     struct job_control *control, guint32 format)
{
  gint err;
  struct idata file;

  err = sample_get_memfile_from_sample (sample, &file, control, format);
  if (err)
    {
      return err;
    }

  err = file_save (path, &file, control);
  idata_free (&file);

  return err;
}

static void
audio_multichannel_to_mono_short (gshort *input, gshort *output, gint size,
				  gint channels)
{
  gint32 i, j, v;

  debug_print (2, "Converting short values to mono...");

  for (i = 0; i < size; i++)
    {
      v = 0;
      for (j = 0; j < channels; j++)
	{
	  v += input[i * channels + j];
	}
      v *= MONO_MIX_GAIN (channels);
      output[i] = v;
    }
}

static void
audio_multichannel_to_mono_float (gfloat *input, gfloat *output, gint size,
				  gint channels)
{
  gfloat v;
  gint i, j;

  debug_print (2, "Converting float values to mono...");

  for (i = 0; i < size; i++)
    {
      v = 0;
      for (j = 0; j < channels; j++)
	{
	  v += input[i * channels + j];
	}
      v *= MONO_MIX_GAIN (channels);
      output[i] = v;
    }
}

static void
audio_multichannel_to_mono_int (gint32 *input, gint32 *output, gint size,
				gint channels)
{
  gint32 v;
  gint i, j;

  debug_print (2, "Converting int values to mono...");

  for (i = 0; i < size; i++)
    {
      v = 0;
      for (j = 0; j < channels; j++)
	{
	  v += input[i * channels + j];
	}
      v *= MONO_MIX_GAIN (channels);
      output[i] = v;
    }
}

static void
audio_mono_to_stereo_short (gshort *input, gshort *output, gint size)
{
  debug_print (2, "Converting short values to stereo...");

  for (gint i = 0; i < size; i++, input++)
    {
      *output = *input;
      output++;
      *output = *input;
      output++;
    }
}

static void
audio_mono_to_stereo_float (gfloat *input, gfloat *output, gint size)
{
  debug_print (2, "Converting float values to stereo...");

  for (gint i = 0; i < size; i++, input++)
    {
      *output = *input;
      output++;
      *output = *input;
      output++;
    }
}

static void
audio_mono_to_stereo_int (gint32 *input, gint32 *output, gint size)
{
  debug_print (2, "Converting int values to stereo...");

  for (gint i = 0; i < size; i++, input++)
    {
      *output = *input;
      output++;
      *output = *input;
      output++;
    }
}

static void
sample_set_sample_info (struct sample_info *sample_info, SNDFILE *sndfile,
			SF_INFO *sf_info)
{
  struct SF_CHUNK_INFO chunk_info;
  SF_CHUNK_ITERATOR *chunk_iter;
  struct smpl_chunk_data smpl_chunk_data;
  gboolean disable_loop = FALSE;

  sample_info->channels = sf_info->channels;
  sample_info->rate = sf_info->samplerate;
  sample_info->frames = sf_info->frames;
  sample_info->format = sf_info->format;

  strcpy (chunk_info.id, SMPL_CHUNK_ID);
  chunk_info.id_size = strlen (SMPL_CHUNK_ID);
  chunk_iter = sf_get_chunk_iterator (sndfile, &chunk_info);

  if (chunk_iter)
    {
      chunk_info.datalen = sizeof (struct smpl_chunk_data);
      memset (&smpl_chunk_data, 0, chunk_info.datalen);
      debug_print (2, "%s chunk found (%d B)", SMPL_CHUNK_ID,
		   chunk_info.datalen);
      chunk_info.data = &smpl_chunk_data;
      sf_get_chunk_data (chunk_iter, &chunk_info);
      sample_info->loop_start =
	GUINT32_FROM_LE (smpl_chunk_data.sample_loop.start);
      sample_info->loop_end =
	GUINT32_FROM_LE (smpl_chunk_data.sample_loop.end);
      sample_info->loop_type =
	GUINT32_FROM_LE (smpl_chunk_data.sample_loop.type);
      sample_info->midi_note =
	GUINT32_FROM_LE (smpl_chunk_data.midi_unity_note);
      if (sample_info->loop_start >= sample_info->frames)
	{
	  debug_print (2, "Bad loop start");
	  disable_loop = TRUE;
	}
      if (sample_info->loop_end >= sample_info->frames)
	{
	  debug_print (2, "Bad loop end");
	  disable_loop = TRUE;
	}

      while (chunk_iter)
	{
	  chunk_iter = sf_next_chunk_iterator (chunk_iter);
	}
    }
  else
    {
      disable_loop = TRUE;
      sample_info->midi_note = 0;
    }
  if (disable_loop)
    {
      sample_info->loop_start = sample_info->frames - 1;
      sample_info->loop_end = sample_info->loop_start;
      sample_info->loop_type = 0;
    }

  debug_print (2, "Loop start at %d, loop end at %d",
	       sample_info->loop_start, sample_info->loop_end);
}

static gint
sample_load_sample_info_libsndfile (const gchar *path,
				    struct sample_info *sample_info)
{
  SF_INFO sf_info;
  SNDFILE *sndfile;
  FILE *file;
  gint err = 0;

  file = fopen (path, "rb");
  if (!file)
    {
      return -errno;
    }

  sndfile = sf_open_virtual (&FILE_IO, SFM_READ, &sf_info, file);
  if (!sndfile)
    {
      error_print ("Error while reading %s: %s", path, sf_strerror (sndfile));
      err = -1;
      goto end;
    }

  sample_set_sample_info (sample_info, sndfile, &sf_info);

end:
  fclose (file);
  return err;
}

static gint
sample_load_microfreak_sample (const gchar *path, struct idata *sample,
			       struct job_control *control)
{
  gint err;
  const gchar *ext = filename_get_ext (path);

  if (!strcmp (MICROFREAK_PWAVETABLE_EXT, ext))
    {
      err = microfreak_pwavetable_load (path, sample, control);
    }
  else if (!strcmp (MICROFREAK_ZWAVETABLE_EXT, ext))
    {
      err = microfreak_zwavetable_load (path, sample, control);
    }
  else if (!strcmp (MICROFREAK_PSAMPLE_EXT, ext))
    {
      err = microfreak_psample_load (path, sample, control);
    }
  else if (!strcmp (MICROFREAK_ZSAMPLE_EXT, ext))
    {
      err = microfreak_zsample_load (path, sample, control);
    }
  else
    {
      err = -1;
    }

  return err;
}

static gboolean
sample_microfreak_filename (const gchar *path)
{
  const gchar *ext = filename_get_ext (path);

  return !strcmp (MICROFREAK_PWAVETABLE_EXT, ext) ||
    !strcmp (MICROFREAK_ZWAVETABLE_EXT, ext) ||
    !strcmp (MICROFREAK_PSAMPLE_EXT, ext) ||
    !strcmp (MICROFREAK_ZSAMPLE_EXT, ext);
}

gint
sample_load_sample_info (const gchar *path, struct sample_info *sample_info)
{
  gint err;

  memset (sample_info, 0, sizeof (struct sample_info));

  if (sample_microfreak_filename (path))
    {
      struct idata aux;
      struct job_control control;

      control.active = TRUE;
      g_mutex_init (&control.mutex);

      err = sample_load_microfreak_sample (path, &aux, &control);
      if (err)
	{
	  return err;
	}

      memcpy (sample_info, aux.info, sizeof (struct sample_info));
      idata_free (&aux);
    }
  else
    {
      err = sample_load_sample_info_libsndfile (path, sample_info);
    }

  return err;
}

static void
sample_info_fix_frame_values (struct sample_info *sample_info)
{
  if (sample_info->loop_start >= sample_info->frames)
    {
      sample_info->loop_start = sample_info->frames - 1;
    }
  if (sample_info->loop_end >= sample_info->frames)
    {
      sample_info->loop_end = sample_info->frames - 1;
    }
}

static gint
sample_load_libsndfile (void *data, SF_VIRTUAL_IO *sf_virtual_io,
			struct job_control *control, struct idata *idata,
			const struct sample_info *sample_info_req,
			struct sample_info *sample_info_src,
			sample_load_cb cb, gpointer cb_data,
			const gchar *name)
{
  SF_INFO sf_info;
  SNDFILE *sndfile;
  SRC_DATA src_data;
  SRC_STATE *src_state;
  void *buffer_input;
  void *buffer_input_multi;
  void *buffer_input_mono;
  void *buffer_input_stereo;
  void *buffer_i;		//For gint16 or gint32
  gfloat *buffer_f;
  void *buffer_output;
  gint err = 0, resampled_buffer_len, frames;
  gboolean active, rounding_fix;
  gdouble ratio;
  guint bytes_per_sample, bytes_per_frame;
  guint32 read_frames, actual_frames = 0;
  GByteArray *sample = NULL;
  struct sample_info *sample_info;

  sf_info.format = 0;
  sndfile = sf_open_virtual (sf_virtual_io, SFM_READ, &sf_info, data);
  if (!sndfile)
    {
      error_print ("%s", sf_strerror (sndfile));
      return -1;
    }

  sample_set_sample_info (sample_info_src, sndfile, &sf_info);

  sample_info = g_malloc (sizeof (struct sample_info));

  sample_info->midi_note = sample_info_src->midi_note;
  sample_info->loop_type = sample_info_src->loop_type;
  sample_info->channels = sample_info_req->channels ?
    sample_info_req->channels : sample_info_src->channels;
  sample_info->rate = sample_info_req->rate ? sample_info_req->rate :
    sample_info_src->rate;
  sample_info->format = sample_info_req->format ? sample_info_req->format :
    SF_FORMAT_PCM_16;
  if (sample_info->format != SF_FORMAT_PCM_16 &&
      sample_info->format != SF_FORMAT_PCM_32 &&
      sample_info->format != SF_FORMAT_FLOAT)
    {
      error_print ("Invalid sample format. Using short...");
      sample_info->format = SF_FORMAT_PCM_16;
    }

  bytes_per_frame = SAMPLE_INFO_FRAME_SIZE (sample_info);
  bytes_per_sample = SAMPLE_SIZE (sample_info->format);

  //Set scale factor. See http://www.mega-nerd.com/libsndfile/api.html#note2
  if ((sample_info_src->format & SF_FORMAT_FLOAT) == SF_FORMAT_FLOAT ||
      (sample_info_src->format & SF_FORMAT_DOUBLE) == SF_FORMAT_DOUBLE)
    {
      debug_print (2,
		   "Setting scale factor to ensure correct integer readings...");
      sf_command (sndfile, SFC_SET_SCALE_FLOAT_INT_READ, NULL, SF_TRUE);
    }

  buffer_input_multi = g_malloc (LOAD_BUFFER_LEN *
				 FRAME_SIZE (sample_info_src->channels,
					     sample_info->format));
  buffer_input_mono = g_malloc (LOAD_BUFFER_LEN * bytes_per_sample);
  buffer_input_stereo = g_malloc (LOAD_BUFFER_LEN * 2 * bytes_per_sample);

  ratio = sample_info->rate / (double) sample_info_src->rate;
  src_data.src_ratio = ratio;

  src_data.output_frames = ceil (LOAD_BUFFER_LEN * src_data.src_ratio);
  resampled_buffer_len = src_data.output_frames * sample_info->channels;
  buffer_i = g_malloc (resampled_buffer_len * bytes_per_sample);
  src_data.data_out = g_malloc (resampled_buffer_len * sizeof (gfloat));

  if (sample_info->format == SF_FORMAT_PCM_16 ||
      sample_info->format == SF_FORMAT_PCM_32)
    {
      buffer_f = g_malloc (LOAD_BUFFER_LEN * sample_info->channels *
			   sizeof (gfloat));
      src_data.data_in = buffer_f;
      buffer_output = buffer_i;
    }
  else
    {
      buffer_f = NULL;
      buffer_output = src_data.data_out;
    }

  src_state = src_new (SRC_SINC_BEST_QUALITY, sample_info->channels, &err);
  if (err)
    {
      error_print ("Error while creating the resampler: %s",
		   src_strerror (err));
      goto cleanup;
    }

  active = TRUE;
  if (control)
    {
      g_mutex_lock (&control->mutex);
      active = control->active;
    }
  sample_info->frames = ceil (sample_info_src->frames * ratio);	//Upper bound estimation. The actual amount is updated later.
  sample_info->loop_start = round (sample_info_src->loop_start * ratio);
  sample_info->loop_end = round (sample_info_src->loop_end * ratio);
  sample_info_fix_frame_values (sample_info);

  sample = g_byte_array_sized_new (sample_info->frames * bytes_per_frame);
  idata_init (idata, sample, strdup (name), sample_info);
  if (control)
    {
      g_mutex_unlock (&control->mutex);
    }

  debug_print (2, "Loading sample (%d frames)...", sample_info_src->frames);

  read_frames = 0;
  while (read_frames < sample_info_src->frames && active)
    {
      debug_print (2, "Loading %d channels buffer...", sample_info->channels);

      if (sample_info->format == SF_FORMAT_FLOAT)
	{
	  frames = sf_readf_float (sndfile, (gfloat *) buffer_input_multi,
				   LOAD_BUFFER_LEN);
	}
      else if (sample_info->format == SF_FORMAT_PCM_32)
	{
	  frames = sf_readf_int (sndfile, (gint32 *) buffer_input_multi,
				 LOAD_BUFFER_LEN);
	}
      else
	{
	  frames = sf_readf_short (sndfile, (gint16 *) buffer_input_multi,
				   LOAD_BUFFER_LEN);
	}
      read_frames += frames;

      if (sample_info->channels == sample_info_src->channels)
	{
	  buffer_input = buffer_input_multi;
	}
      else
	{
	  if (sample_info->format == SF_FORMAT_FLOAT)
	    {
	      audio_multichannel_to_mono_float (buffer_input_multi,
						buffer_input_mono,
						frames,
						sample_info_src->channels);
	    }
	  else if (sample_info->format == SF_FORMAT_PCM_32)
	    {
	      audio_multichannel_to_mono_int (buffer_input_multi,
					      buffer_input_mono,
					      frames,
					      sample_info_src->channels);
	    }
	  else
	    {
	      audio_multichannel_to_mono_short (buffer_input_multi,
						buffer_input_mono,
						frames,
						sample_info_src->channels);
	    }

	  if (sample_info->channels == 1)
	    {
	      buffer_input = buffer_input_mono;
	    }
	  else
	    {
	      if (sample_info->format == SF_FORMAT_FLOAT)
		{
		  audio_mono_to_stereo_float (buffer_input_mono,
					      buffer_input_stereo, frames);
		}
	      else if (sample_info->format == SF_FORMAT_PCM_32)
		{
		  audio_mono_to_stereo_int (buffer_input_mono,
					    buffer_input_stereo, frames);
		}
	      else
		{
		  audio_mono_to_stereo_short (buffer_input_mono,
					      buffer_input_stereo, frames);
		}
	      buffer_input = buffer_input_stereo;
	    }
	}

      if (sample_info->rate == sample_info_src->rate)
	{
	  if (control)
	    {
	      g_mutex_lock (&control->mutex);
	    }
	  g_byte_array_append (sample, (guint8 *) buffer_input,
			       frames * bytes_per_frame);
	  actual_frames += frames;
	  if (control)
	    {
	      g_mutex_unlock (&control->mutex);
	    }
	}
      else
	{
	  debug_print (2, "Resampling %d channels with ratio %f...",
		       sample_info->channels, src_data.src_ratio);

	  src_data.end_of_input = frames < LOAD_BUFFER_LEN ? SF_TRUE : 0;
	  src_data.input_frames = frames;

	  if (sample_info->format == SF_FORMAT_FLOAT)
	    {
	      src_data.data_in = buffer_input;
	    }
	  else if (sample_info->format == SF_FORMAT_PCM_32)
	    {
	      src_int_to_float_array (buffer_input, buffer_f, frames *
				      sample_info->channels);
	    }
	  else
	    {
	      src_short_to_float_array (buffer_input, buffer_f, frames *
					sample_info->channels);
	    }

	  err = src_process (src_state, &src_data);
	  if (err)
	    {
	      error_print ("Error while resampling: %s", src_strerror (err));
	      break;
	    }

	  if (control)
	    {
	      g_mutex_lock (&control->mutex);
	    }

	  if (sample_info->format == SF_FORMAT_PCM_32)
	    {
	      src_float_to_int_array (src_data.data_out, buffer_i,
				      src_data.output_frames_gen *
				      sample_info->channels);
	    }
	  if (sample_info->format == SF_FORMAT_PCM_16)
	    {
	      src_float_to_short_array (src_data.data_out, buffer_i,
					src_data.output_frames_gen *
					sample_info->channels);
	    }
	  g_byte_array_append (sample, (guint8 *) buffer_output,
			       src_data.output_frames_gen * bytes_per_frame);
	  actual_frames += src_data.output_frames_gen;
	  if (control)
	    {
	      g_mutex_unlock (&control->mutex);
	    }
	}

      if (control)
	{
	  cb (control, read_frames * 1.0 / sample_info_src->frames, cb_data);
	  active = job_control_get_active_lock (control);
	}
    }

  src_delete (src_state);

cleanup:
  g_free (buffer_input_multi);
  g_free (buffer_input_mono);
  g_free (buffer_input_stereo);
  g_free (buffer_i);
  if (sample_info->format == SF_FORMAT_PCM_16 ||
      sample_info->format == SF_FORMAT_PCM_32)
    {
      g_free (buffer_f);
    }
  g_free (src_data.data_out);

  sf_close (sndfile);

  if (!sample)
    {
      g_free (sample_info);
      return err;
    }

  if (control)
    {
      g_mutex_lock (&control->mutex);
    }
  if (!active || !sample_info->frames || err)
    {
      idata_free (idata);
      cb (control, 0, cb_data);
      g_mutex_unlock (&control->mutex);
      return -1;
    }
  // This removes the additional samples added by the estimation above.
  if (sample_info->frames > actual_frames)
    {
      rounding_fix = TRUE;
      sample_info->frames = actual_frames;
      sample_info_fix_frame_values (sample_info);
      sample->len = sample_info->frames * bytes_per_frame;
    }
  if (control)
    {
      //It there was a rounding fix in the previous lines, the call is needed to detect the end of the loading process.
      if (rounding_fix)
	{
	  cb (control, 1.0, cb_data);
	}
      g_mutex_unlock (&control->mutex);
    }

  return 0;
}

gint
sample_load_from_memfile (struct idata *memfile, struct idata *sample,
			  struct job_control *control,
			  const struct sample_info *sample_info_req,
			  struct sample_info *sample_info_src)
{
  struct g_byte_array_io_data data;
  data.pos = 0;
  data.array = memfile->content;
  return sample_load_libsndfile (&data, &G_BYTE_ARRAY_IO, control, sample,
				 sample_info_req, sample_info_src,
				 job_control_set_sample_progress_no_sync,
				 NULL, memfile->name);
}

// Reloads the input into the output following all the requirements.

gint
sample_reload (struct idata *input, struct idata *output,
	       struct job_control *control,
	       const struct sample_info *sample_info_req, sample_load_cb cb,
	       gpointer cb_data)
{
  gint err;
  struct idata aux;
  struct sample_info sample_info_src;
  struct g_byte_array_io_data data;

  err = sample_get_memfile_from_sample (input, &aux, NULL,
					SF_FORMAT_WAV | SF_FORMAT_PCM_16);
  if (err)
    {
      return err;
    }

  data.pos = 0;
  data.array = aux.content;
  err = sample_load_libsndfile (&data, &G_BYTE_ARRAY_IO, control, output,
				sample_info_req, &sample_info_src, cb,
				cb_data, input->name);
  idata_free (&aux);

  return err;
}

gint
sample_load_from_file_full (const gchar *path, struct idata *sample,
			    struct job_control *control,
			    const struct sample_info *sample_info_req,
			    struct sample_info *sample_info_src,
			    sample_load_cb cb, gpointer cb_data)
{
  gint err;
  if (sample_microfreak_filename (path))
    {
      struct idata aux;

      err = sample_load_microfreak_sample (path, &aux, control);
      if (err)
	{
	  return err;
	}

      memcpy (sample_info_src, aux.info, sizeof (struct sample_info));
      err = sample_reload (&aux, sample, control, sample_info_req, cb,
			   cb_data);
      idata_free (&aux);
    }
  else
    {
      gchar *name;
      FILE *file = fopen (path, "rb");
      if (!file)
	{
	  return -errno;
	}
      name = g_path_get_basename (path);
      filename_remove_ext (name);
      err = sample_load_libsndfile (file, &FILE_IO, control, sample,
				    sample_info_req, sample_info_src, cb,
				    cb_data, name);
      g_free (name);
      fclose (file);
    }

  return err;
}

gint
sample_load_from_file (const gchar *path, struct idata *sample,
		       struct job_control *control,
		       const struct sample_info *sample_info_req,
		       struct sample_info *sample_info_src)
{
  return sample_load_from_file_full (path, sample, control,
				     sample_info_req, sample_info_src,
				     job_control_set_sample_progress_no_sync,
				     NULL);
}

GSList *
sample_get_sample_extensions ()
{
  GSList *exts = NULL;
  const gchar **e = ELEKTROID_AUDIO_LOCAL_EXTS;
  while (*e)
    {
      exts = g_slist_append (exts, strdup (*e));
      e++;
    }
  return exts;
}

const gchar *
sample_get_format (struct sample_info *sample_info)
{
  switch (sample_info->format & SF_FORMAT_TYPEMASK)
    {
    case SF_FORMAT_WAV:
      return "WAV";
    case SF_FORMAT_AIFF:
      return "AIFF";
    case SF_FORMAT_AU:
      return "Au";
    case SF_FORMAT_FLAC:
      return "FLAC";
    case SF_FORMAT_OGG:
      return "Ogg";
#if !defined(__linux__) || HAVE_SNDFILE_MP3
    case SF_FORMAT_MPEG:
      return "MPEG";
#endif
    default:
      return "?";
    }
}

const gchar *
sample_get_subtype (struct sample_info *sample_info)
{
  switch (sample_info->format & SF_FORMAT_SUBMASK)
    {
    case SF_FORMAT_PCM_S8:
      return "s8";
    case SF_FORMAT_PCM_16:
      return "s16";
    case SF_FORMAT_PCM_24:
      return "s24";
    case SF_FORMAT_PCM_32:
      return "s32";
    case SF_FORMAT_PCM_U8:
      return "u8";
    case SF_FORMAT_FLOAT:
      return "f32";
    case SF_FORMAT_DOUBLE:
      return "f64";
    default:
      return "?";
    }
}
