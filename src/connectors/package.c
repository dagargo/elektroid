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
#define PKG_TAG_SAMPLES "Samples"
#define PKG_TAG_FILE_NAME "FileName"
#define PKG_TAG_FILE_SIZE "FileSize"
#define PKG_TAG_HASH "Hash"
#define PKG_VAL_FILE_TYPE_PRJ "Project"
#define PKG_VAL_FILE_TYPE_SND "Sound"
#define PKG_VAL_FILE_TYPE_UNK "Unknown"

#define MAN_TAG_SAMPLE_REFS "sample_references"
#define MAN_TAG_HASH "hash"
#define MAN_TAG_SIZE "size"

#define MAX_PACKAGE_LEN (64 * 1024 * 1024)
#define MAX_MANIFEST_LEN (128 * 1024)
#define MANIFEST_FILENAME "manifest.json"

const struct sample_params ELEKTRON_SAMPLE_PARAMS = {
  .samplerate = ELEKTRON_SAMPLE_RATE,
  .channels = ELEKTRON_SAMPLE_CHANNELS
};

static gint
package_add_resource (struct package *pkg,
		      struct package_resource *pkg_resource, gboolean new)
{
  zip_source_t *sample_source;
  zip_int64_t index;
  zip_error_t zerror;

  debug_print (1, "Adding file %s to zip (%d B)...\n", pkg_resource->path,
	       pkg_resource->data->len);
  sample_source =
    zip_source_buffer_create (pkg_resource->data->data,
			      pkg_resource->data->len, 0, &zerror);
  if (!sample_source)
    {
      error_print ("Error while creating file source: %s\n",
		   zip_error_strerror (&zerror));
      zip_error_fini (&zerror);
      return -1;
    }

  index =
    zip_file_add (pkg->zip, pkg_resource->path, sample_source,
		  ZIP_FL_OVERWRITE | ZIP_FL_ENC_UTF_8);
  if (index < 0)
    {
      error_print ("Error while adding file: %s\n",
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
package_begin (struct package *pkg, gchar * name, const gchar * fw_version,
	       const struct device_desc *device_desc, enum package_type type)
{
  zip_error_t zerror;
  pkg->resources = NULL;
  pkg->buff = g_malloc (MAX_PACKAGE_LEN);
  pkg->name = name;
  pkg->fw_version = strdup (fw_version);
  pkg->device_desc = device_desc;
  pkg->type = type;

  debug_print (1, "Creating zip buffer...\n");

  zip_error_init (&zerror);
  pkg->zip_source =
    zip_source_buffer_create (pkg->buff, MAX_PACKAGE_LEN, 0, &zerror);
  if (!pkg->zip_source)
    {
      error_print ("Error while creating zip source: %s\n",
		   zip_error_strerror (&zerror));
      zip_error_fini (&zerror);
      g_free (pkg->buff);
      return -1;
    }

  pkg->zip = zip_open_from_source (pkg->zip_source, ZIP_TRUNCATE, &zerror);
  if (!pkg->zip)
    {
      error_print ("Error while creating in memory zip: %s\n",
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
  json_builder_add_string_value (builder,
				 pkg->type & PKG_FILE_TYPE_SOUND ?
				 PKG_VAL_FILE_TYPE_SND : pkg->type &
				 PKG_FILE_TYPE_PROJECT ? PKG_VAL_FILE_TYPE_PRJ
				 : PKG_VAL_FILE_TYPE_UNK);

  if (pkg->type != PKG_FILE_TYPE_PRESET)
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
package_end (struct package *pkg, GByteArray * out)
{
  int ret = 0;
  zip_stat_t zstat;

  ret = package_add_manifest (pkg);
  if (ret)
    {
      error_print ("Error while formatting %s\n", MANIFEST_FILENAME);
      return ret;
    }

  debug_print (1, "Writing zip to buffer...\n");
  if (zip_close (pkg->zip))
    {
      error_print ("Error while creating in memory zip: %s\n",
		   zip_error_strerror (zip_get_error (pkg->zip)));
      return -1;
    }

  zip_source_stat (pkg->zip_source, &zstat);
  debug_print (1, "%" PRIu64 " B written to package\n", zstat.comp_size);

  zip_source_open (pkg->zip_source);
  g_byte_array_set_size (out, zstat.comp_size);
  zip_source_read (pkg->zip_source, out->data, zstat.comp_size);
  zip_source_close (pkg->zip_source);

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
package_open (struct package *pkg, GByteArray * data,
	      const struct device_desc *device_desc)
{
  gint ret;
  zip_error_t zerror;
  zip_file_t *manifest_file;
  zip_stat_t zstat;

  debug_print (1, "Opening zip stream...\n");

  zip_error_init (&zerror);
  pkg->zip_source = zip_source_buffer_create (data->data, data->len, 0,
					      &zerror);
  if (!pkg->zip_source)
    {
      error_print ("Error while creating zip source: %s\n",
		   zip_error_strerror (&zerror));
      zip_error_fini (&zerror);
      return -1;
    }

  pkg->zip = zip_open_from_source (pkg->zip_source, ZIP_RDONLY, &zerror);
  if (!pkg->zip)
    {
      error_print ("Error while creating in memory zip: %s\n",
		   zip_error_strerror (&zerror));
      zip_error_fini (&zerror);
      zip_source_free (pkg->zip_source);
      return -1;
    }

  ret = zip_stat (pkg->zip, MANIFEST_FILENAME, ZIP_FL_ENC_STRICT, &zstat);
  if (ret)
    {
      error_print ("Error while loading '%s': %s\n", MANIFEST_FILENAME,
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

  return ret;
}

void
package_close (struct package *pkg)
{
  zip_source_close (pkg->zip_source);
  package_destroy (pkg);
}

gint
package_receive_pkg_resources (struct package *pkg,
			       const gchar * payload_path,
			       struct job_control *control,
			       struct backend *backend,
			       fs_remote_file_op download_data,
			       fs_remote_file_op download_sample)
{
  gint ret, i, elements;
  JsonParser *parser;
  JsonReader *reader;
  gint64 hash, size;
  GError *error;
  gchar *sample_path, *metadata_path;
  struct package_resource *pkg_resource;
  GByteArray *wave, *payload, *metadata, *sample;
  GString *package_resource_path;

  metadata_path = path_chain (PATH_INTERNAL, payload_path, ".metadata");
  debug_print (1, "Getting metadata from %s...\n", metadata_path);
  metadata = g_byte_array_new ();
  control->parts = 130;		// 128 sample slots, metadata and main.
  control->part = 0;
  set_job_control_progress (control, 0.0);
  ret = download_data (backend, metadata_path, metadata, control);
  if (ret)
    {
      debug_print (1, "Metadata file not available\n");
      control->parts = 1;
      goto get_payload;
    }

  control->part++;

  parser = json_parser_new ();
  if (!json_parser_load_from_data
      (parser, (gchar *) metadata->data, metadata->len, &error))
    {
      error_print ("Unable to parse stream: %s. Continuing...",
		   error->message);
      g_clear_error (&error);
      control->parts = 2;
      goto get_payload;
    }

  reader = json_reader_new (json_parser_get_root (parser));
  if (!reader)
    {
      error_print ("Unable to read from parser. Continuing...");
      control->parts = 2;
      goto get_payload;
    }

  if (!json_reader_read_member (reader, MAN_TAG_SAMPLE_REFS))
    {
      debug_print (1, "Member '%s' not found\n", MAN_TAG_SAMPLE_REFS);
      control->parts = 2;
      goto get_payload;
    }

  if (!json_reader_is_array (reader))
    {
      error_print ("Member '%s' is not an array. Continuing...\n",
		   MAN_TAG_SAMPLE_REFS);
      control->parts = 2;
      goto cleanup_reader;
    }

  elements = json_reader_count_elements (reader);
  if (!elements)
    {
      debug_print (1, "No samples found\n");
      control->parts = 2;
      goto cleanup_reader;
    }

  sample = g_byte_array_new ();
  control->parts = 2 + elements;
  set_job_control_progress (control, 0.0);
  for (i = 0; i < elements; i++, control->part++)
    {
      if (!json_reader_read_element (reader, i))
	{
	  error_print ("Cannot read element %d. Continuing...\n", i);
	  continue;
	}
      if (!json_reader_read_member (reader, MAN_TAG_HASH))
	{
	  error_print ("Cannot read member '%s'. Continuing...\n",
		       MAN_TAG_HASH);
	  continue;
	}
      hash = json_reader_get_int_value (reader);
      json_reader_end_element (reader);

      if (!json_reader_read_member (reader, MAN_TAG_SIZE))
	{
	  error_print ("Cannot read member '%s'. Continuing...\n",
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
	  debug_print (1, "Sample not found. Skipping...\n");
	  continue;
	}

      debug_print (1, "Hash: %" PRIu64 "; size: %" PRIu64 "; path: %s\n",
		   hash, size, sample_path);
      debug_print (1, "Getting sample %s...\n", sample_path);
      g_byte_array_set_size (sample, 0);
      if (download_sample (backend, sample_path, sample, control))
	{
	  g_free (sample_path);
	  error_print ("Error while downloading sample. Continuing...\n");
	  continue;
	}

      wave = g_byte_array_new ();
      ret = sample_get_wav_from_array (sample, wave, control);
      if (ret)
	{
	  error_print
	    ("Error while converting sample to wave file. Continuing...\n");
	  g_byte_array_free (wave, TRUE);
	  g_free (sample_path);
	  continue;
	}

      pkg_resource = g_malloc (sizeof (struct package_resource));
      pkg_resource->type = PKG_RES_TYPE_SAMPLE;
      pkg_resource->data = wave;
      pkg_resource->hash = hash;
      pkg_resource->size = size;
      package_resource_path = g_string_new (NULL);
      g_string_append_printf (package_resource_path, "%s%s.wav",
			      PKG_TAG_SAMPLES, sample_path);
      pkg_resource->path = package_resource_path->str;
      g_string_free (package_resource_path, FALSE);
      if (package_add_resource (pkg, pkg_resource, TRUE))
	{
	  package_free_package_resource (pkg_resource);
	  error_print ("Error while packaging sample\n");
	  continue;
	}
    }

  g_byte_array_free (sample, TRUE);
cleanup_reader:
  g_object_unref (reader);
  g_object_unref (parser);
get_payload:
  g_byte_array_free (metadata, TRUE);
  debug_print (1, "Getting payload from %s...\n", payload_path);
  payload = g_byte_array_new ();
  ret = download_data (backend, payload_path, payload, control);
  if (ret)
    {
      error_print ("Error while downloading payload\n");
      ret = -1;
    }
  else
    {
      pkg_resource = g_malloc (sizeof (struct package_resource));
      pkg_resource->type = PKG_RES_TYPE_PAYLOAD;
      pkg_resource->data = payload;
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
package_send_pkg_resources (struct package *pkg, const gchar * payload_path,
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
  GError *error;
  zip_stat_t zstat;
  zip_error_t zerror;
  zip_file_t *zip_file;
  GByteArray *wave, *raw;
  struct package_resource *pkg_resource;

  zip_error_init (&zerror);

  parser = json_parser_new ();
  if (!json_parser_load_from_data
      (parser, (gchar *) pkg->manifest->data->data, pkg->manifest->data->len,
       &error))
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
      error_print ("No '%s' found\n", PKG_TAG_PAYLOAD);
      ret = -1;
      goto cleanup_reader;
    }
  pkg->name = strdup (json_reader_get_string_value (reader));
  json_reader_end_element (reader);

  if (zip_stat (pkg->zip, pkg->name, ZIP_FL_ENC_STRICT, &zstat))
    {
      error_print ("Error while loading '%s': %s\n", MANIFEST_FILENAME,
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

  control->parts = 129;		// 128 sample slots and main.
  control->part = 0;
  ret = upload_data (backend, payload_path, pkg_resource->data, control);
  if (ret)
    {
      error_print ("Error while uploading payload to '%s'\n", payload_path);
      goto cleanup_reader;
    }
  control->part++;

  if (!json_reader_read_member (reader, PKG_TAG_FIRMWARE_VERSION))
    {
      error_print ("No '%s' found\n", PKG_TAG_FIRMWARE_VERSION);
      ret = -1;
      goto cleanup_reader;
    }
  pkg->fw_version = strdup (json_reader_get_string_value (reader));
  json_reader_end_element (reader);

  if (!json_reader_read_member (reader, PKG_TAG_FILE_TYPE))
    {
      error_print ("No '%s' found\n", PKG_TAG_FILE_TYPE);
      ret = -1;
      goto cleanup_reader;
    }
  file_type = json_reader_get_string_value (reader);
  json_reader_end_element (reader);

  if (strcmp (file_type, PKG_VAL_FILE_TYPE_SND) == 0)
    {
      pkg->type = PKG_FILE_TYPE_SOUND;
    }
  else if (strcmp (file_type, PKG_VAL_FILE_TYPE_PRJ) == 0)
    {
      pkg->type = PKG_FILE_TYPE_PROJECT;
    }
  else
    {
      pkg->type = PKG_FILE_TYPE_NONE;
      debug_print (1, "Invalid '%s': %s\n", PKG_TAG_FILE_TYPE, file_type);
    }

  if (!json_reader_read_member (reader, PKG_TAG_PRODUCT_TYPE))
    {
      error_print ("No '%s' found\n", PKG_TAG_PRODUCT_TYPE);
      ret = 0;
      goto cleanup_reader;
    }
  if (!json_reader_is_array (reader))
    {
      error_print ("Member '%s' is not an array\n", PKG_TAG_PRODUCT_TYPE);
      ret = -1;
      goto cleanup_reader;
    }
  if (!json_reader_count_elements (reader))
    {
      error_print ("No product types found\n");
      ret = 0;
      goto cleanup_reader;
    }
  if (!json_reader_read_element (reader, 0))
    {
      ret = -1;
      goto cleanup_reader;
    }
  product_type = atoi (json_reader_get_string_value (reader));
  debug_print (1, "ProductType: %" PRId64 "\n", product_type);
  if (pkg->device_desc->id != product_type)
    {
      debug_print (1, "Incompatible product type. Continuing...\n");
    }
  json_reader_end_element (reader);
  json_reader_end_element (reader);

  if (!json_reader_read_member (reader, PKG_TAG_SAMPLES))
    {
      control->parts = 1;	// Only payload and it's done.
      control->part = 0;
      set_job_control_progress (control, 1.0);
      goto cleanup_reader;
    }

  if (!json_reader_is_array (reader))
    {
      error_print ("Member '%s' is not an array. Skipping samples...\n",
		   PKG_TAG_SAMPLES);
      ret = -1;
      goto cleanup_reader;
    }

  wave = g_byte_array_sized_new (zstat.size);
  raw = g_byte_array_sized_new (MAX_PACKAGE_LEN);
  elements = json_reader_count_elements (reader);
  control->parts = elements + 1;
  control->part = 1;
  for (i = 0; i < elements; i++, control->part++)
    {
      guint frames;

      json_reader_read_element (reader, i);
      json_reader_read_member (reader, PKG_TAG_FILE_NAME);
      sample_path = json_reader_get_string_value (reader);
      json_reader_end_element (reader);
      json_reader_end_element (reader);

      if (zip_stat (pkg->zip, sample_path, ZIP_FL_ENC_STRICT, &zstat))
	{
	  error_print ("Error while loading '%s': %s\n",
		       MANIFEST_FILENAME, zip_error_strerror (&zerror));
	  zip_error_fini (&zerror);
	  ret = -1;
	  continue;
	}

      g_byte_array_set_size (wave, zstat.size);
      zip_file = zip_fopen (pkg->zip, sample_path, 0);
      zip_fread (zip_file, wave->data, zstat.size);
      wave->len = zstat.size;
      zip_fclose (zip_file);

      raw->len = 0;
      if (sample_load_from_array (wave, raw, control,
				  &ELEKTRON_SAMPLE_PARAMS, &frames))
	{
	  error_print ("Error while loading '%s': %s\n",
		       sample_path, zip_error_strerror (&zerror));
	  continue;
	}

      pkg_resource = g_malloc (sizeof (struct package_resource));
      pkg_resource->type = PKG_RES_TYPE_SAMPLE;
      pkg_resource->data = g_byte_array_sized_new (raw->len);
      pkg_resource->data->len = raw->len;
      memcpy (pkg_resource->data->data, raw->data, raw->len);
      pkg_resource->path = strdup (sample_path);

      pkg->resources = g_list_append (pkg->resources, pkg_resource);

      //We remove the "Samples" at the beggining of the full zip path...
      dev_sample_path = strdup (&sample_path[7]);
      //... And the extension.
      remove_ext (dev_sample_path);
      ret = elektron_upload_sample_part (backend, dev_sample_path,
					 pkg_resource->data, control);
      g_free (dev_sample_path);
      g_free (control->data);
      control->data = NULL;
      if (ret)
	{
	  error_print ("Error while uploading sample to '%s'\n",
		       &sample_path[7]);
	  continue;
	}
    }

  g_byte_array_free (wave, TRUE);
  g_byte_array_free (raw, TRUE);

cleanup_reader:
  g_object_unref (reader);
cleanup_parser:
  g_object_unref (parser);
  return ret;
}
