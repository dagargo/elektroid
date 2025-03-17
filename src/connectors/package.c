/*
 *   package.c
 *   Copyright (C) 2021 David García Goñi <dagargo@gmail.com>
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

#include <stdio.h>
#include <unistd.h>
#include <json-glib/json-glib.h>
#include "package.h"
#include "utils.h"
#include "sample.h"
#include "elektron.h"

#define PKG_TAG_FORMAT_VERSION "FormatVersion"
#define PKG_TAG_PRODUCT_TYPE "ProductType"
#define PKG_TAG_PAYLOAD "Payload"
#define PKG_TAG_FILE_TYPE "FileType"
#define PKG_TAG_FIRMWARE_VERSION "FirmwareVersion"
#define PKG_TAG_METAINFO "MetaInfo"
#define PKG_TAG_TAGS "Tags"
#define PKG_TAG_SAMPLES "Samples"
#define PKG_TAG_FILE_NAME "FileName"
#define PKG_TAG_FILE_SIZE "FileSize"
#define PKG_TAG_HASH "Hash"
#define PKG_VAL_FILE_TYPE_PRJ "Project"
#define PKG_VAL_FILE_TYPE_SND "Sound"
#define PKG_VAL_FILE_TYPE_PST "Preset"
#define PKG_VAL_FILE_TYPE_UNK "Unknown"

#define MAN_TAG_SAMPLE_REFS "sample_references"
#define MAN_TAG_HASH "hash"
#define MAN_TAG_SIZE "size"

#define METADATA_TAG_SAMPLE_REFS "sound_tags"

#define MAX_PACKAGE_LEN (512 * 1024 * 1024)
#define MAX_MANIFEST_LEN (128 * 1024)
#define MANIFEST_FILENAME "manifest.json"

static GSList *
package_get_tags_from_snd_metadata_int (JsonReader *reader)
{
  gint elements;
  GSList *tags = NULL;

  if (!json_reader_read_member (reader, METADATA_TAG_SAMPLE_REFS))
    {
      debug_print (1, "Member '%s' not found", METADATA_TAG_SAMPLE_REFS);
      return NULL;
    }

  if (!json_reader_is_array (reader))
    {
      error_print ("Member '%s' is not an array. Continuing...",
		   METADATA_TAG_SAMPLE_REFS);
      goto end;
    }

  elements = json_reader_count_elements (reader);
  if (!elements)
    {
      debug_print (1, "No tags found");
      return NULL;
    }

  for (gint i = 0; i < elements; i++)
    {
      const gchar *tag;

      if (!json_reader_read_element (reader, i))
	{
	  error_print ("Cannot read element %d. Continuing...", i);
	  continue;
	}

      tag = json_reader_get_string_value (reader);
      tags = g_slist_append (tags, strdup (tag));
      json_reader_end_element (reader);
    }

end:
  json_reader_end_element (reader);

  return tags;
}

GSList *
package_get_tags_from_snd_metadata (GByteArray *metadata)
{
  JsonParser *parser;
  JsonReader *reader;
  GError *error = NULL;
  GSList *tags = NULL;

  parser = json_parser_new ();
  if (!json_parser_load_from_data (parser, (gchar *) metadata->data,
				   metadata->len, &error))
    {
      error_print ("Unable to parse stream: %s. Continuing...",
		   error->message);
      g_clear_error (&error);
      goto cleanup_parser;
    }

  reader = json_reader_new (json_parser_get_root (parser));
  if (!reader)
    {
      error_print ("Unable to read from parser. Continuing...");
      goto cleanup_parser;
    }

  tags = package_get_tags_from_snd_metadata_int (reader);

  g_object_unref (reader);

cleanup_parser:
  g_object_unref (parser);

  return tags;
}

static gint
package_add_resource (struct package *pkg,
		      struct package_resource *pkg_resource, gboolean new)
{
  zip_source_t *sample_source;
  zip_int64_t index;
  zip_error_t zerror;

  debug_print (1, "Adding file %s to zip (%d B)...", pkg_resource->path,
	       pkg_resource->data->len);
  sample_source = zip_source_buffer_create (pkg_resource->data->data,
					    pkg_resource->data->len, 0,
					    &zerror);
  if (!sample_source)
    {
      error_print ("Error while creating file source: %s",
		   zip_error_strerror (&zerror));
      zip_error_fini (&zerror);
      return -1;
    }

  index = zip_file_add (pkg->zip, pkg_resource->path, sample_source,
			ZIP_FL_OVERWRITE | ZIP_FL_ENC_UTF_8);
  if (index < 0)
    {
      error_print ("Error while adding file: %s",
		   zip_error_strerror (zip_get_error (pkg->zip)));
      zip_source_free (sample_source);
      return -1;
    }

  if (new)
    {
      pkg->resources = g_list_append (pkg->resources, pkg_resource);
    }

  return 0;
}


gint
package_begin (struct package *pkg, gchar *name, const gchar *fw_version,
	       const struct device_desc *device_desc, enum package_type type)
{
  zip_error_t zerror;
  pkg->resources = NULL;
  pkg->buff = g_malloc (MAX_PACKAGE_LEN);
  pkg->name = name;
  pkg->fw_version = strdup (fw_version);
  pkg->device_desc = device_desc;
  pkg->type = type;

  debug_print (1, "Creating zip buffer...");

  zip_error_init (&zerror);
  pkg->zip_source = zip_source_buffer_create (pkg->buff, MAX_PACKAGE_LEN, 0,
					      &zerror);
  if (!pkg->zip_source)
    {
      error_print ("Error while creating zip source: %s",
		   zip_error_strerror (&zerror));
      zip_error_fini (&zerror);
      g_free (pkg->buff);
      return -1;
    }

  pkg->zip = zip_open_from_source (pkg->zip_source, ZIP_TRUNCATE, &zerror);
  if (!pkg->zip)
    {
      error_print ("Error while creating in memory zip: %s",
		   zip_error_strerror (&zerror));
      zip_error_fini (&zerror);
      zip_source_free (pkg->zip_source);
      g_free (pkg->buff);
      return -1;
    }

  zip_source_keep (pkg->zip_source);

  pkg->manifest = g_malloc (sizeof (struct package_resource));
  pkg->manifest->type = PKG_RES_TYPE_MANIFEST;
  pkg->manifest->data = g_byte_array_sized_new (MAX_MANIFEST_LEN);	//We need this because we can not resize later.
  pkg->manifest->path = strdup (MANIFEST_FILENAME);
  package_add_resource (pkg, pkg->manifest, TRUE);

  return 0;
}

static const gchar *
package_get_file_type (enum package_type type)
{
  if (type & PKG_FILE_TYPE_DATA_PROJECT)
    {
      return PKG_VAL_FILE_TYPE_PRJ;
    }
  else if (type & PKG_FILE_TYPE_DATA_SOUND)
    {
      return PKG_VAL_FILE_TYPE_SND;
    }
  else if (type & PKG_FILE_TYPE_DATA_PRESET)
    {
      return PKG_VAL_FILE_TYPE_PST;
    }
  else
    {
      return PKG_VAL_FILE_TYPE_UNK;
    }
}

static gint
package_add_manifest (struct package *pkg)
{
  JsonBuilder *builder;
  JsonGenerator *gen;
  JsonNode *root;
  gchar *json;
  gint len;
  gchar *val = g_malloc (LABEL_MAX);
  GList *resource;
  gboolean samples_found = FALSE;
  struct package_resource *pkg_resource;

  builder = json_builder_new ();

  json_builder_begin_object (builder);

  json_builder_set_member_name (builder, PKG_TAG_FORMAT_VERSION);
  json_builder_add_string_value (builder, "1.0");

  json_builder_set_member_name (builder, PKG_TAG_PRODUCT_TYPE);
  json_builder_begin_array (builder);
  snprintf (val, LABEL_MAX, "%d", pkg->device_desc->id);
  json_builder_add_string_value (builder, val);
  json_builder_end_array (builder);

  json_builder_set_member_name (builder, PKG_TAG_PAYLOAD);
  json_builder_add_string_value (builder, pkg->name);

  json_builder_set_member_name (builder, PKG_TAG_FILE_TYPE);
  json_builder_add_string_value (builder, package_get_file_type (pkg->type));

  if (pkg->type != PKG_FILE_TYPE_RAW_PRESET)
    {
      json_builder_set_member_name (builder, PKG_TAG_FIRMWARE_VERSION);
      json_builder_add_string_value (builder, pkg->fw_version);
    }

  for (resource = pkg->resources; resource; resource = resource->next)
    {
      pkg_resource = resource->data;
      if (pkg_resource->type == PKG_RES_TYPE_SAMPLE)
	{
	  samples_found = TRUE;
	  break;
	}
    }

  if (pkg->manifest->tags)	// PKG_FILE_TYPE_DATA_SOUND
    {
      json_builder_set_member_name (builder, PKG_TAG_METAINFO);
      json_builder_begin_object (builder);
      json_builder_set_member_name (builder, PKG_TAG_TAGS);
      json_builder_begin_array (builder);

      GSList *e = pkg->manifest->tags;

      while (e)
	{
	  gchar *tag = (gchar *) e->data;
	  json_builder_add_string_value (builder, tag);
	  e = e->next;
	}

      json_builder_end_array (builder);
      json_builder_end_object (builder);
    }

  if (samples_found)
    {
      json_builder_set_member_name (builder, PKG_TAG_SAMPLES);
      json_builder_begin_array (builder);
      for (resource = pkg->resources; resource; resource = resource->next)
	{
	  pkg_resource = resource->data;
	  if (pkg_resource->type == PKG_RES_TYPE_SAMPLE)
	    {
	      json_builder_begin_object (builder);

	      json_builder_set_member_name (builder, PKG_TAG_FILE_NAME);
	      json_builder_add_string_value (builder, pkg_resource->path);

	      json_builder_set_member_name (builder, PKG_TAG_FILE_SIZE);
	      json_builder_add_int_value (builder, pkg_resource->size);

	      json_builder_set_member_name (builder, PKG_TAG_HASH);
	      snprintf (val, LABEL_MAX, "%u", pkg_resource->hash);
	      json_builder_add_string_value (builder, val);

	      json_builder_end_object (builder);
	    }
	}
      json_builder_end_array (builder);
    }

  json_builder_end_object (builder);

  gen = json_generator_new ();
  g_object_set (gen, "pretty", TRUE, NULL);
  root = json_builder_get_root (builder);
  json_generator_set_root (gen, root);
  json = json_generator_to_data (gen, NULL);

  len = strlen (json);
  memcpy (pkg->manifest->data->data, json, len);
  pkg->manifest->data->len = len;
  package_add_resource (pkg, pkg->manifest, FALSE);

  g_free (json);
  json_node_free (root);
  g_object_unref (gen);
  g_object_unref (builder);
  g_free (val);

  return 0;
}

gint
package_end (struct package *pkg, struct idata *out)
{
  int ret = 0;
  zip_stat_t zstat;
  GByteArray *content;

  ret = package_add_manifest (pkg);
  if (ret)
    {
      error_print ("Error while formatting %s", MANIFEST_FILENAME);
      return ret;
    }

  debug_print (1, "Writing zip to buffer...");
  if (zip_close (pkg->zip))
    {
      error_print ("Error while creating in memory zip: %s",
		   zip_error_strerror (zip_get_error (pkg->zip)));
      return -1;
    }

  zip_source_stat (pkg->zip_source, &zstat);
  debug_print (1, "%" PRIu64 " B written to package", zstat.comp_size);

  zip_source_open (pkg->zip_source);
  content = g_byte_array_sized_new (zstat.comp_size);
  content->len = zstat.comp_size;
  zip_source_read (pkg->zip_source, content->data, zstat.comp_size);
  zip_source_close (pkg->zip_source);

  idata_init (out, content, NULL, NULL);

  return 0;
}

void
package_free_package_resource (gpointer data)
{
  struct package_resource *pkg_resource = data;
  g_byte_array_free (pkg_resource->data, TRUE);
  g_free (pkg_resource);
}

void
package_destroy (struct package *pkg)
{
  zip_source_free (pkg->zip_source);
  g_free (pkg->buff);
  g_free (pkg->name);
  g_free (pkg->fw_version);
  g_list_free_full (pkg->resources, package_free_package_resource);
}

gint
package_open (struct package *pkg, struct idata *idata,
	      const struct device_desc *device_desc)
{
  gint err;
  zip_error_t zerror;
  zip_file_t *manifest_file;
  zip_stat_t zstat;
  GByteArray *data = idata->content;

  debug_print (1, "Opening zip stream...");

  zip_error_init (&zerror);
  pkg->zip_source = zip_source_buffer_create (data->data, data->len, 0,
					      &zerror);
  if (!pkg->zip_source)
    {
      error_print ("Error while creating zip source: %s",
		   zip_error_strerror (&zerror));
      zip_error_fini (&zerror);
      return -1;
    }

  pkg->zip = zip_open_from_source (pkg->zip_source, ZIP_RDONLY, &zerror);
  if (!pkg->zip)
    {
      error_print ("Error while creating in memory zip: %s",
		   zip_error_strerror (&zerror));
      zip_error_fini (&zerror);
      zip_source_free (pkg->zip_source);
      return -1;
    }

  err = zip_stat (pkg->zip, MANIFEST_FILENAME, ZIP_FL_ENC_STRICT, &zstat);
  if (err)
    {
      error_print ("Error while loading '%s': %s", MANIFEST_FILENAME,
		   zip_error_strerror (&zerror));
      zip_error_fini (&zerror);
      zip_source_free (pkg->zip_source);
      zip_close (pkg->zip);
      return -1;
    }

  pkg->manifest = g_malloc (sizeof (struct package_resource));
  pkg->manifest->type = PKG_RES_TYPE_MANIFEST;
  pkg->manifest->data = g_byte_array_sized_new (zstat.size);
  pkg->manifest->path = strdup (MANIFEST_FILENAME);
  manifest_file = zip_fopen (pkg->zip, MANIFEST_FILENAME, 0);
  zip_fread (manifest_file, pkg->manifest->data->data, zstat.size);
  pkg->manifest->data->len = zstat.size;
  zip_fclose (manifest_file);

  pkg->resources = NULL;
  pkg->resources = g_list_append (pkg->resources, pkg->manifest);
  pkg->buff = NULL;
  pkg->name = NULL;
  pkg->fw_version = NULL;
  pkg->device_desc = device_desc;

  return 0;
}

void
package_close (struct package *pkg)
{
  zip_source_close (pkg->zip_source);
  package_destroy (pkg);
}

static gint
packaget_get_pkg_sample_slots (struct backend *backend)
{
  struct elektron_data *data = backend->data;
  gint slots = 127;		// slot 0 is never used
  if (data->device_desc.id == 42)	// Digitakt II has 8 banks (1026)
    {
      slots *= 8;
    }
  return slots;
}

gint
package_receive_pkg_resources (struct package *pkg,
			       const gchar *payload_path,
			       struct job_control *control,
			       struct backend *backend,
			       fs_remote_file_op download_data,
			       enum package_type type)
{
  gint ret, i, elements;
  JsonParser *parser;
  JsonReader *reader;
  gint64 hash, size;
  GError *error = NULL;
  gchar *sample_path, *metadata_path;
  struct package_resource *pkg_resource;
  GString *package_resource_path;
  struct idata metadata_file, payload_file, sample_file, file;
  struct elektron_data *data = backend->data;

  pkg->manifest->tags = NULL;

  if ((type == PKG_FILE_TYPE_DATA_PROJECT &&
      data->device_desc.id != ELEKTRON_ANALOG_RYTM_ID &&
      data->device_desc.id != ELEKTRON_DIGITAKT_ID &&
      data->device_desc.id != ELEKTRON_ANALOG_RYTM_MKII_ID &&
      data->device_desc.id != ELEKTRON_MODEL_SAMPLES_ID &&
      data->device_desc.id != ELEKTRON_DIGITAKT_II_ID) ||
      type == PKG_FILE_TYPE_DATA_PRESET)
    {
      goto get_payload;
    }

  metadata_path = path_chain (PATH_INTERNAL, payload_path,
			      FS_DATA_METADATA_FILE);
  debug_print (1, "Getting metadata from %s...", metadata_path);
  control->parts = 2 + packaget_get_pkg_sample_slots (backend);	// main, metadata and sample slots.
  control->part = 0;
  job_control_set_progress (control, 0.0);

  ret = download_data (backend, metadata_path, &metadata_file, control);
  if (ret)
    {
      debug_print (1, "Metadata file not available");
      control->parts = 1;
      goto get_payload;
    }

  control->part++;

  parser = json_parser_new ();
  if (!json_parser_load_from_data (parser,
				   (gchar *) metadata_file.content->data,
				   metadata_file.content->len, &error))
    {
      error_print ("Unable to parse stream: %s. Continuing...",
		   error->message);
      g_clear_error (&error);
      control->parts = 2;
      goto cleanup_parser;
    }

  reader = json_reader_new (json_parser_get_root (parser));
  if (!reader)
    {
      error_print ("Unable to read from parser. Continuing...");
      control->parts = 2;
      goto cleanup_parser;
    }

  if (type == PKG_FILE_TYPE_DATA_SOUND)
    {
      pkg->manifest->tags = package_get_tags_from_snd_metadata_int (reader);
    }
  else
    {
      pkg->manifest->tags = NULL;
    }

  if (!json_reader_read_member (reader, MAN_TAG_SAMPLE_REFS))
    {
      debug_print (1, "Member '%s' not found", MAN_TAG_SAMPLE_REFS);
      control->parts = 2;
      goto cleanup_reader;
    }

  if (!json_reader_is_array (reader))
    {
      error_print ("Member '%s' is not an array. Continuing...",
		   MAN_TAG_SAMPLE_REFS);
      control->parts = 2;
      goto cleanup_reader;
    }

  elements = json_reader_count_elements (reader);
  if (!elements)
    {
      debug_print (1, "No samples found");
      control->parts = 2;
      goto cleanup_reader;
    }

  control->parts = 2 + elements;
  job_control_set_progress (control, 0.0);
  for (i = 0; i < elements; i++, control->part++)
    {
      if (!json_reader_read_element (reader, i))
	{
	  error_print ("Cannot read element %d. Continuing...", i);
	  continue;
	}
      if (!json_reader_read_member (reader, MAN_TAG_HASH))
	{
	  error_print ("Cannot read member '%s'. Continuing...",
		       MAN_TAG_HASH);
	  continue;
	}
      hash = json_reader_get_int_value (reader);
      json_reader_end_element (reader);

      if (!json_reader_read_member (reader, MAN_TAG_SIZE))
	{
	  error_print ("Cannot read member '%s'. Continuing...",
		       MAN_TAG_SIZE);
	  continue;
	}
      size = json_reader_get_int_value (reader);
      json_reader_end_element (reader);

      json_reader_end_element (reader);

      sample_path = elektron_get_sample_path_from_hash_size (backend, hash,
							     size);
      if (!sample_path)
	{
	  debug_print (1, "Sample not found. Skipping...");
	  continue;
	}

      debug_print (1, "Hash: %" PRIu64 "; size: %" PRIu64 "; path: %s",
		   hash, size, sample_path);
      debug_print (1, "Getting sample %s...", sample_path);

      if (elektron_download_sample_part (backend, sample_path, &sample_file,
					 control))
	{
	  g_free (sample_path);
	  error_print ("Error while downloading sample. Continuing...");
	  continue;
	}

      ret = sample_get_memfile_from_sample (&sample_file, &file, control,
					    SF_FORMAT_WAV | SF_FORMAT_PCM_16);
      if (ret)
	{
	  error_print
	    ("Error while converting sample to wave file. Continuing...");
	  g_free (sample_path);
	  continue;
	}

      pkg_resource = g_malloc (sizeof (struct package_resource));
      pkg_resource->type = PKG_RES_TYPE_SAMPLE;
      pkg_resource->data = idata_steal (&file);
      pkg_resource->hash = hash;
      pkg_resource->size = size;
      package_resource_path = g_string_new (NULL);
      g_string_append_printf (package_resource_path, "%s%s.wav",
			      PKG_TAG_SAMPLES, sample_path);
      pkg_resource->path = g_string_free (package_resource_path, FALSE);
      if (package_add_resource (pkg, pkg_resource, TRUE))
	{
	  package_free_package_resource (pkg_resource);
	  error_print ("Error while packaging sample");
	  continue;
	}

      idata_free (&sample_file);
    }

cleanup_reader:
  g_object_unref (reader);
cleanup_parser:
  g_object_unref (parser);
  idata_free (&metadata_file);
get_payload:
  debug_print (1, "Getting payload from %s...", payload_path);
  ret = download_data (backend, payload_path, &payload_file, control);
  if (ret)
    {
      error_print ("Error while downloading payload");
    }
  else
    {
      pkg_resource = g_malloc (sizeof (struct package_resource));
      pkg_resource->type = PKG_RES_TYPE_PAYLOAD;
      pkg_resource->data = idata_steal (&payload_file);
      pkg_resource->path = strdup (pkg->name);
      if (package_add_resource (pkg, pkg_resource, TRUE))
	{
	  package_free_package_resource (pkg_resource);
	  ret = -1;
	}
    }

  return ret;
}

gint
package_send_pkg_resources (struct package *pkg, const gchar *payload_path,
			    struct job_control *control,
			    struct backend *backend,
			    fs_remote_file_op upload_data)
{
  gint elements, i, ret = 0;
  const gchar *file_type, *sample_path;
  gchar *dev_sample_path;
  gint64 product_type;
  JsonParser *parser;
  JsonReader *reader;
  GError *error = NULL;
  zip_stat_t zstat;
  zip_error_t zerror;
  zip_file_t *zip_file;
  struct package_resource *pkg_resource;
  struct idata file, sample, sample_file;

  zip_error_init (&zerror);

  parser = json_parser_new ();
  if (!json_parser_load_from_data (parser,
				   (gchar *) pkg->manifest->data->data,
				   pkg->manifest->data->len, &error))
    {
      error_print ("Unable to parse stream: %s", error->message);
      g_clear_error (&error);
      ret = -1;
      goto cleanup_parser;
    }

  reader = json_reader_new (json_parser_get_root (parser));
  if (!reader)
    {
      ret = -1;
      goto cleanup_parser;
    }

  if (!json_reader_read_member (reader, PKG_TAG_PAYLOAD))
    {
      error_print ("No '%s' found", PKG_TAG_PAYLOAD);
      ret = -1;
      goto cleanup_reader;
    }
  pkg->name = strdup (json_reader_get_string_value (reader));
  json_reader_end_element (reader);

  if (zip_stat (pkg->zip, pkg->name, ZIP_FL_ENC_STRICT, &zstat))
    {
      error_print ("Error while loading '%s': %s", MANIFEST_FILENAME,
		   zip_error_strerror (&zerror));
      zip_error_fini (&zerror);
      ret = -1;
      goto cleanup_reader;
    }

  pkg_resource = g_malloc (sizeof (struct package_resource));
  pkg_resource->type = PKG_RES_TYPE_PAYLOAD;
  pkg_resource->data = g_byte_array_sized_new (zstat.size);
  pkg_resource->path = strdup (pkg->name);
  zip_file = zip_fopen (pkg->zip, pkg->name, 0);
  zip_fread (zip_file, pkg_resource->data->data, zstat.size);
  pkg_resource->data->len = zstat.size;
  zip_fclose (zip_file);

  pkg->resources = g_list_append (pkg->resources, pkg_resource);

  control->parts = 1 + packaget_get_pkg_sample_slots (backend);	// main and sample slots
  control->part = 0;
  idata_init (&file, pkg_resource->data, NULL, NULL);
  ret = upload_data (backend, payload_path, &file, control);
  if (ret)
    {
      error_print ("Error while uploading payload to '%s'", payload_path);
      goto cleanup_reader;
    }
  control->part++;

  if (!json_reader_read_member (reader, PKG_TAG_FIRMWARE_VERSION))
    {
      error_print ("No '%s' found", PKG_TAG_FIRMWARE_VERSION);
      ret = -1;
      goto cleanup_reader;
    }
  pkg->fw_version = strdup (json_reader_get_string_value (reader));
  json_reader_end_element (reader);

  if (!json_reader_read_member (reader, PKG_TAG_FILE_TYPE))
    {
      error_print ("No '%s' found", PKG_TAG_FILE_TYPE);
      ret = -1;
      goto cleanup_reader;
    }
  file_type = json_reader_get_string_value (reader);
  json_reader_end_element (reader);

  if (strcmp (file_type, PKG_VAL_FILE_TYPE_SND) == 0)
    {
      pkg->type = PKG_FILE_TYPE_DATA_SOUND;
    }
  else if (strcmp (file_type, PKG_VAL_FILE_TYPE_PRJ) == 0)
    {
      pkg->type = PKG_FILE_TYPE_DATA_PROJECT;
    }
  else if (strcmp (file_type, PKG_VAL_FILE_TYPE_PST) == 0)
    {
      pkg->type = PKG_FILE_TYPE_DATA_PRESET;
    }
  else
    {
      pkg->type = PKG_FILE_TYPE_NONE;
      debug_print (1, "Invalid '%s': %s", PKG_TAG_FILE_TYPE, file_type);
    }

  if (!json_reader_read_member (reader, PKG_TAG_PRODUCT_TYPE))
    {
      error_print ("No '%s' found", PKG_TAG_PRODUCT_TYPE);
      ret = 0;
      goto cleanup_reader;
    }
  if (!json_reader_is_array (reader))
    {
      error_print ("Member '%s' is not an array", PKG_TAG_PRODUCT_TYPE);
      ret = -1;
      goto cleanup_reader;
    }
  if (!json_reader_count_elements (reader))
    {
      error_print ("No product types found");
      ret = 0;
      goto cleanup_reader;
    }
  if (!json_reader_read_element (reader, 0))
    {
      ret = -1;
      goto cleanup_reader;
    }
  product_type = atoi (json_reader_get_string_value (reader));
  debug_print (1, "ProductType: %" PRId64 "", product_type);
  if (pkg->device_desc->id != product_type)
    {
      debug_print (1, "Incompatible product type. Continuing...");
    }
  json_reader_end_element (reader);
  json_reader_end_element (reader);

  if (!json_reader_read_member (reader, PKG_TAG_SAMPLES))
    {
      control->parts = 1;	// Only payload and it's done.
      control->part = 0;
      job_control_set_progress (control, 1.0);
      goto cleanup_reader;
    }

  if (!json_reader_is_array (reader))
    {
      error_print ("Member '%s' is not an array. Skipping samples...",
		   PKG_TAG_SAMPLES);
      ret = -1;
      goto cleanup_reader;
    }

  //We are reusing the same sample_file. Let's be careful.
  idata_init (&sample_file, g_byte_array_sized_new (zstat.size), NULL, NULL);

  elements = json_reader_count_elements (reader);
  control->parts = elements + 1;
  control->part = 1;

  for (i = 0; i < elements; i++, control->part++)
    {
      struct sample_info sample_info_req, sample_info_src;
      sample_info_req.rate = ELEKTRON_SAMPLE_RATE;
      sample_info_req.channels = 0;	//Automatic
      sample_info_req.format = SF_FORMAT_PCM_16;

      json_reader_read_element (reader, i);
      json_reader_read_member (reader, PKG_TAG_FILE_NAME);
      sample_path = json_reader_get_string_value (reader);
      json_reader_end_element (reader);
      json_reader_end_element (reader);

      debug_print (2, "Uploading %s...", sample_path);

      if (zip_stat (pkg->zip, sample_path, ZIP_FL_ENC_STRICT, &zstat))
	{
	  error_print ("Error while loading '%s': %s",
		       MANIFEST_FILENAME, zip_error_strerror (&zerror));
	  zip_error_fini (&zerror);
	  ret = -1;
	  continue;
	}

      //We remove the "Samples" at the beggining of the full zip path...
      dev_sample_path = strdup (&sample_path[7]);
      //... And the extension.
      filename_remove_ext (dev_sample_path);
      sample_file.name = g_path_get_basename (dev_sample_path);

      g_byte_array_set_size (sample_file.content, zstat.size);
      zip_file = zip_fopen (pkg->zip, sample_path, 0);
      zip_fread (zip_file, sample_file.content->data, zstat.size);
      sample_file.content->len = zstat.size;
      zip_fclose (zip_file);

      if (sample_load_from_memfile (&sample_file, &sample, control,
				    &sample_info_req, &sample_info_src))
	{
	  error_print ("Error while loading '%s': %s",
		       sample_path, zip_error_strerror (&zerror));
	}
      else
	{
	  ret = elektron_upload_sample_part (backend, dev_sample_path,
					     &sample, control);

	  if (ret)
	    {
	      error_print ("Error while uploading sample to '%s'",
			   &sample_path[7]);
	    }

	  pkg_resource = g_malloc (sizeof (struct package_resource));
	  pkg_resource->type = PKG_RES_TYPE_SAMPLE;
	  pkg_resource->data = idata_steal (&sample);
	  pkg_resource->path = strdup (sample_path);

	  pkg->resources = g_list_append (pkg->resources, pkg_resource);
	}

      g_free (dev_sample_path);
      g_free (sample_file.name);
      sample_file.name = NULL;
    }

  idata_free (&sample_file);

cleanup_reader:
  g_object_unref (reader);
cleanup_parser:
  g_object_unref (parser);
  return ret;
}
