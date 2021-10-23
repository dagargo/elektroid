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

#define MAX_PACKAGE_LEN (128 * 1024 * 1024)
#define MAX_MANIFEST_LEN (128 * 1024)
#define MANIFEST_FILENAME "manifest.json"

gint
package_begin (struct package *pkg, gchar * name, const gchar * fw_version,
	       const struct connector_device_desc *device_desc,
	       enum package_type type)
{
  zip_error_t zerror;
  pkg->resources = NULL;
  pkg->buff = g_malloc (MAX_PACKAGE_LEN);
  pkg->name = name;
  pkg->fw_version = fw_version;
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
  package_add_resource (pkg, pkg->manifest);

  return 0;
}

static gint
package_add_resource_replace (struct package *pkg,
			      struct package_resource *pkg_resource,
			      gint replace)
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

  if (!replace)
    {
      pkg->resources = g_list_append (pkg->resources, pkg_resource);
    }

  return 0;
}

gint
package_add_resource (struct package *pkg,
		      struct package_resource *pkg_resource)
{
  return package_add_resource_replace (pkg, pkg_resource, 0);
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

  json_builder_set_member_name (builder, "FormatVersion");
  json_builder_add_string_value (builder, "1.0");

  json_builder_set_member_name (builder, "ProductType");
  json_builder_begin_array (builder);
  snprintf (val, LABEL_MAX, "%d", pkg->device_desc->id);
  json_builder_add_string_value (builder, val);
  json_builder_end_array (builder);

  json_builder_set_member_name (builder, "Payload");
  json_builder_add_string_value (builder, pkg->name);

  json_builder_set_member_name (builder, "FileType");
  json_builder_add_string_value (builder,
				 pkg->type & PKG_FILE_TYPE_SOUND ?
				 "Sound" : pkg->type &
				 PKG_FILE_TYPE_PROJECT ? "Project" :
				 "Unknown");

  json_builder_set_member_name (builder, "FirmwareVersion");
  json_builder_add_string_value (builder, pkg->fw_version);

  if (pkg->device_desc->fss & FS_SAMPLES)
    {
      for (resource = pkg->resources; resource; resource = resource->next)
	{
	  pkg_resource = resource->data;
	  if (pkg_resource->type == PKG_RES_TYPE_SAMPLE)
	    {
	      samples_found = TRUE;
	      break;
	    }
	}
    }

  if (samples_found)
    {
      json_builder_set_member_name (builder, "Samples");
      json_builder_begin_array (builder);
      for (resource = pkg->resources; resource; resource = resource->next)
	{
	  pkg_resource = resource->data;
	  if (pkg_resource->type == PKG_RES_TYPE_SAMPLE)
	    {
	      json_builder_begin_object (builder);

	      json_builder_set_member_name (builder, "FileName");
	      json_builder_add_string_value (builder, pkg_resource->path);

	      json_builder_set_member_name (builder, "FileSize");
	      json_builder_add_int_value (builder, pkg_resource->size);

	      json_builder_set_member_name (builder, "Hash");
	      snprintf (val, LABEL_MAX, "%d", pkg_resource->hash);
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
  package_add_resource_replace (pkg, pkg->manifest, 1);

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
  zip_stat_t zst;

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

  zip_source_stat (pkg->zip_source, &zst);
  debug_print (1, "%ld B written to package\n", zst.comp_size);

  zip_source_open (pkg->zip_source);
  g_byte_array_set_size (out, zst.comp_size);
  zip_source_read (pkg->zip_source, out->data, zst.comp_size);
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
  g_list_free_full (pkg->resources, package_free_package_resource);
}
