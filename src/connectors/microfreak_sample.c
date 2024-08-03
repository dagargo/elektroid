/*
 *   microfreak_sample.c
 *   Copyright (C) 2024 David García Goñi <dagargo@gmail.com>
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

#include <zip.h>
#include "utils.h"
#include "microfreak_sample.h"

#define MICROFREAK_SERIALIZATION_TAG "serialization::archive"
#define MICROFREAK_ZEROS "000000000000000000"

#define MICROFREAK_WAVETABLE_HEADER "DEVBUILD"
#define MICROFREAK_SAMPLE_HEADER "94"

gint
microfreak_serialize_object (GByteArray *output, const gchar *version,
			     const gchar *name, guint8 p0, guint8 p3,
			     guint8 p5, guint8 *data, guint datalen)
{
  gchar aux[LABEL_MAX];
  guint namelen = strlen (name);
  gint8 *v;
  guint len;

  debug_print (2, "Serializing object '%s'...", name);

  len = strlen (MICROFREAK_SERIALIZATION_TAG);
  snprintf (aux, LABEL_MAX, "%d ", len);
  g_byte_array_append (output, (guint8 *) aux, strlen (aux));
  g_byte_array_append (output, (guint8 *) MICROFREAK_SERIALIZATION_TAG, len);

  snprintf (aux, LABEL_MAX, " %d", 10);
  g_byte_array_append (output, (guint8 *) aux, strlen (aux));
  snprintf (aux, LABEL_MAX, " %d", 0);
  g_byte_array_append (output, (guint8 *) aux, strlen (aux));
  snprintf (aux, LABEL_MAX, " %d", 4);
  g_byte_array_append (output, (guint8 *) aux, strlen (aux));

  len = strlen (version);
  snprintf (aux, LABEL_MAX, " %d ", len);
  g_byte_array_append (output, (guint8 *) aux, strlen (aux));
  g_byte_array_append (output, (guint8 *) version, len);

  snprintf (aux, LABEL_MAX, " %d ", namelen);
  g_byte_array_append (output, (guint8 *) aux, strlen (aux));
  g_byte_array_append (output, (guint8 *) name, namelen);

  snprintf (aux, LABEL_MAX, " %d", p0);
  g_byte_array_append (output, (guint8 *) aux, strlen (aux));
  snprintf (aux, LABEL_MAX, " %d", 0);
  g_byte_array_append (output, (guint8 *) aux, strlen (aux));
  snprintf (aux, LABEL_MAX, " %d", 0);
  g_byte_array_append (output, (guint8 *) aux, strlen (aux));

  len = strlen (MICROFREAK_ZEROS);
  snprintf (aux, LABEL_MAX, " %d ", len);
  g_byte_array_append (output, (guint8 *) aux, strlen (aux));
  g_byte_array_append (output, (guint8 *) MICROFREAK_ZEROS, len);

  snprintf (aux, LABEL_MAX, " %d", p3);
  g_byte_array_append (output, (guint8 *) aux, strlen (aux));
  snprintf (aux, LABEL_MAX, " %d", 0);
  g_byte_array_append (output, (guint8 *) aux, strlen (aux));
  snprintf (aux, LABEL_MAX, " %d", p5);
  g_byte_array_append (output, (guint8 *) aux, strlen (aux));

  snprintf (aux, LABEL_MAX, " %d", datalen);
  g_byte_array_append (output, (guint8 *) aux, strlen (aux));

  v = (gint8 *) data;
  for (guint i = 0; i < datalen; i++, v++)
    {
      snprintf (aux, LABEL_MAX, " %d", *v);
      g_byte_array_append (output, (guint8 *) aux, strlen (aux));
    }

  g_byte_array_append (output, (guint8 *) "\x0a", 1);

  return 0;
}

static gint
microfreak_deserialize_read_value (guint8 **position, gint64 *value)
{
  gchar *p = (gchar *) * position;
  gchar *endp;

  *value = g_ascii_strtoll (p, &endp, 10);
  if (p == endp)
    {
      return -EINVAL;
    }

  *position = (guint8 *) endp;
  return 0;
}

static gint
microfreak_deserialize_check_value (guint8 **position, gint64 test)
{
  gint64 value;
  gint err = microfreak_deserialize_read_value (position, &value);
  if (err || value != test)
    {
      return -EINVAL;
    }
  return 0;
}

gint
microfreak_deserialize_object (GByteArray *input, const gchar *header,
			       gchar *name, guint8 *p0, guint8 *p3,
			       guint8 *p5, guint8 *data, gint64 *datalen)
{
  gint64 v;
  gint err;
  guint len;
  guint8 *p;
  GByteArray *safe;

  //As this relies on string readings, the input data needs to be NULL
  //terminated to handle errors properly.

  len = input->len + 1;
  safe = g_byte_array_sized_new (len);
  g_byte_array_append (safe, input->data, input->len);
  g_byte_array_append (safe, (guint8 *) "\x00", 1);
  p = safe->data;

  len = strlen (MICROFREAK_SERIALIZATION_TAG);
  err = microfreak_deserialize_check_value (&p, len);
  if (err)
    {
      goto end;
    }

  p++;
  err = memcmp (p, (guint8 *) MICROFREAK_SERIALIZATION_TAG, len);
  if (err)
    {
      err = -EINVAL;
      goto end;
    }

  p += len;
  err = microfreak_deserialize_check_value (&p, 10);
  if (err)
    {
      goto end;
    }

  err = microfreak_deserialize_check_value (&p, 0);
  if (err)
    {
      goto end;
    }

  err = microfreak_deserialize_check_value (&p, 4);
  if (err)
    {
      goto end;
    }

  err = microfreak_deserialize_read_value (&p, &v);
  if (err)
    {
      goto end;
    }

  p += v + 1;
  err = microfreak_deserialize_read_value (&p, &v);
  if (err)
    {
      goto end;
    }

  p++;
  memcpy (name, p, v);
  name[v] = 0;

  debug_print (2, "Deserializing object '%s'...", name);

  p += v;
  err = microfreak_deserialize_read_value (&p, &v);
  if (err)
    {
      goto end;
    }
  *p0 = v;

  err = microfreak_deserialize_check_value (&p, 0);
  if (err)
    {
      goto end;
    }

  err = microfreak_deserialize_check_value (&p, 0);
  if (err)
    {
      goto end;
    }

  err = microfreak_deserialize_check_value (&p, 18);
  if (err)
    {
      goto end;
    }

  p++;
  len = strlen (MICROFREAK_ZEROS);
  if (memcmp (p, (guint8 *) MICROFREAK_ZEROS, len))
    {
      err = -EINVAL;
      goto end;
    }

  p += len + 1;
  err = microfreak_deserialize_read_value (&p, &v);
  if (err)
    {
      goto end;
    }
  *p3 = v;

  err = microfreak_deserialize_check_value (&p, 0);
  if (err)
    {
      goto end;
    }

  err = microfreak_deserialize_read_value (&p, &v);
  if (err)
    {
      goto end;
    }
  *p5 = v;

  err = microfreak_deserialize_read_value (&p, &v);
  if (err)
    {
      goto end;
    }
  *datalen = v;

  for (guint i = 0; i < *datalen; i++, data++)
    {
      err = microfreak_deserialize_read_value (&p, &v);
      if (err)
	{
	  goto end;
	}
      *data = v;
    }

  //A byte 0x0d appears in wavetables and samples when exported from Arturia
  //MIDI Control Center but not when downloading wavetables.
  if (*p == 0x0d)
    {
      p++;
    }

  //A byte 0x0a is always present at the end.
  if (*p != 0x0a)
    {
      err = -EINVAL;
    }

end:
  g_byte_array_free (safe, TRUE);
  return err;
}

void
microfreak_init_sample_info (struct sample_info *sample_info, guint32 frames)
{
  sample_info->midi_note = 0;
  sample_info->loop_type = 0;
  sample_info->channels = 1;
  sample_info->rate = MICROFREAK_SAMPLERATE;
  sample_info->format = SF_FORMAT_PCM_16;
  sample_info->frames = frames;
  sample_info->loop_start = 0;
  sample_info->loop_end = sample_info->frames - 1;
}

struct sample_info *
microfreak_new_sample_info (guint32 frames)
{
  struct sample_info *sample_info = g_malloc (sizeof (struct sample_info));
  microfreak_init_sample_info (sample_info, frames);
  return sample_info;
}

gint
microfreak_deserialize_sample (struct idata *sample, struct idata *serialized,
			       const gchar *header)
{
  gchar name[MICROFREAK_WAVETABLE_NAME_LEN];
  guint8 p0, p3, p5;
  gint64 datalen;
  gint err;
  struct sample_info *sample_info;
  GByteArray *data = g_byte_array_sized_new (2 * MIB);	//Enough for 24 s samples or wavetables

  err = microfreak_deserialize_object (serialized->content, header, name, &p0,
				       &p3, &p5, data->data, &datalen);
  if (err)
    {
      g_byte_array_free (data, TRUE);
      return err;
    }

  data->len = datalen;

  sample_info = microfreak_new_sample_info (datalen / 2);
  idata_init (sample, data, strdup (name), sample_info);

  return 0;
}

gint
microfreak_zobject_save (const gchar *path, struct idata *zobject,
			 struct job_control *control, const gchar *name)
{
  gint err = 0, index;
  zip_t *archive;
  zip_error_t zerror;
  zip_source_t *source;
  GByteArray *array = zobject->content;

  zip_error_init (&zerror);

  archive = zip_open (path, ZIP_CREATE, &err);
  if (!archive)
    {
      zip_error_init_with_code (&zerror, err);
      error_print ("Error while saving zip file: %s",
		   zip_error_strerror (&zerror));
      zip_error_fini (&zerror);
      return -EIO;
    }

  source = zip_source_buffer (archive, array->data, array->len, 0);
  if (!source)
    {
      error_print ("Error while creating source buffer: %s",
		   zip_strerror (archive));
      err = -EIO;
      goto end;
    }

  //Any name works as long as its a number, an underscore and additional characters without spaces.
  index = zip_file_add (archive, name, source, ZIP_FL_OVERWRITE);
  if (index < 0)
    {
      error_print ("Error while adding to file: %s", zip_strerror (archive));
      err = -EIO;
      goto end;
    }

  if (zip_close (archive))
    {
      error_print ("Error while saving zip file: %s",
		   zip_error_strerror (&zerror));
      zip_error_fini (&zerror);
      err = -EIO;
    }

end:
  if (err)
    {
      zip_discard (archive);
    }
  return err;
}

gint
microfreak_zobject_load (const char *path, struct idata *zobject,
			 struct job_control *control)
{
  gint err = 0;
  zip_t *archive;
  zip_stat_t zstat;
  zip_error_t zerror;
  zip_file_t *zip_file = NULL;
  GByteArray *array;

  archive = zip_open (path, ZIP_RDONLY, &err);
  if (!archive)
    {
      zip_error_init_with_code (&zerror, err);
      error_print ("Error while opening zip file: %s",
		   zip_error_strerror (&zerror));
      zip_error_fini (&zerror);
      return -EIO;
    }

  array = g_byte_array_new ();
  if (zip_get_num_entries (archive, 0) != 1)
    {
      err = -EIO;
      goto end;
    }

  zip_file = zip_fopen_index (archive, 0, 0);
  if (!zip_file)
    {
      err = -EIO;
      goto end;
    }

  if (zip_stat_index (archive, 0, ZIP_FL_ENC_STRICT, &zstat))
    {
      err = -EIO;
      goto end;
    }

  g_byte_array_set_size (array, zstat.size);
  zip_fread (zip_file, array->data, zstat.size);

end:
  if (zip_file)
    {
      zip_fclose (zip_file);
    }

  err = zip_close (archive) ? -EIO : 0;
  if (err)
    {
      g_byte_array_free (array, TRUE);
    }
  else
    {
      idata_init (zobject, array, NULL, NULL);
    }

  return err;
}

gint
microfreak_zsample_load (const gchar *path, struct idata *sample,
			 struct job_control *control)
{
  gint err;
  struct idata aux;

  err = microfreak_zobject_load (path, &aux, control);
  if (err)
    {
      return err;
    }

  err = microfreak_deserialize_sample (sample, &aux,
				       MICROFREAK_SAMPLE_HEADER);

  idata_free (&aux);
  return err;
}

gint
microfreak_psample_load (const gchar *path, struct idata *sample,
			 struct job_control *control)
{
  gint err;
  struct idata aux;

  err = file_load (path, &aux, control);
  if (err)
    {
      return err;
    }

  err = microfreak_deserialize_sample (sample, &aux,
				       MICROFREAK_SAMPLE_HEADER);

  idata_free (&aux);
  return err;
}

gint
microfreak_serialize_sample (struct idata *serialized,
			     struct idata *wavetable, const gchar *header)
{
  gint err;
  GByteArray *data = g_byte_array_sized_new (MICROFREAK_WAVETABLE_SIZE * 8);

  err = microfreak_serialize_object (data, header, wavetable->name, 1, 0, 1,
				     wavetable->content->data,
				     wavetable->content->len);
  if (err)
    {
      g_byte_array_free (data, TRUE);
    }
  else
    {
      idata_init (serialized, data, NULL, NULL);
    }

  return err;
}

gint
microfreak_serialize_wavetable (struct idata *serialized,
				struct idata *wavetable)
{

  return microfreak_serialize_sample (serialized, wavetable,
				      MICROFREAK_WAVETABLE_HEADER);
}

gint
microfreak_deserialize_wavetable (struct idata *wavetable,
				  struct idata *serialized)
{
  struct sample_info *sample_info;
  gint err;

  err = microfreak_deserialize_sample (wavetable, serialized,
				       MICROFREAK_WAVETABLE_HEADER);
  if (err)
    {
      return err;
    }

  sample_info = wavetable->info;
  if (sample_info->frames != MICROFREAK_WAVETABLE_LEN)
    {
      idata_free (wavetable);
      return -EINVAL;
    }

  return 0;
}

gint
microfreak_pwavetable_load (const gchar *path, struct idata *wavetable,
			    struct job_control *control)
{
  gint err;
  struct idata aux;

  err = file_load (path, &aux, control);
  if (err)
    {
      return err;
    }

  err = microfreak_deserialize_wavetable (wavetable, &aux);

  idata_free (&aux);
  return err;
}

gint
microfreak_zwavetable_load (const gchar *path, struct idata *wavetable,
			    struct job_control *control)
{
  gint err;
  struct idata aux;

  err = microfreak_zobject_load (path, &aux, control);
  if (err)
    {
      return err;
    }

  err = microfreak_deserialize_wavetable (wavetable, &aux);

  idata_free (&aux);
  return err;
}

gint
microfreak_pwavetable_save (const gchar *path, struct idata *wavetable,
			    struct job_control *control)
{
  gint err;
  struct idata aux;

  err = microfreak_serialize_wavetable (&aux, wavetable);
  if (err)
    {
      goto cleanup;
    }

  err = file_save (path, &aux, control);

cleanup:
  idata_free (&aux);
  return err;
}

gint
microfreak_zwavetable_save (const gchar *path, struct idata *wavetable,
			    struct job_control *control)
{
  gint err;
  struct idata aux;

  err = microfreak_serialize_sample (&aux, wavetable,
				     MICROFREAK_WAVETABLE_HEADER);
  if (err)
    {
      goto cleanup;
    }

  err = microfreak_zobject_save (path, &aux, control, "0_wavetable");

cleanup:
  idata_free (&aux);
  return err;
}
