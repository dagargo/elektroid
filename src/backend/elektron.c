/*
 *   elektron.h
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

#include "elektron.h"

#define DEVICES_FILE "/devices.json"

#define DEV_TAG_ID "id"
#define DEV_TAG_NAME "name"
#define DEV_TAG_ALIAS "alias"
#define DEV_TAG_FILESYSTEMS "filesystems"
#define DEV_TAG_STORAGE "storage"

gchar *transfer_devices_filename = NULL;

gint
elektron_load_device_desc (struct device_desc *device_desc, guint8 id)
{
  gint err, devices;
  JsonParser *parser;
  JsonReader *reader;
  gchar *devices_filename;
  GError *error = NULL;

  parser = json_parser_new ();

  if (transfer_devices_filename)
    {
      devices_filename = strdup (transfer_devices_filename);

      if (!json_parser_load_from_file (parser, devices_filename, &error))
	{
	  error_print ("%s", error->message);
	  g_clear_error (&error);
	  err = -ENODEV;
	  goto cleanup_parser;
	}
    }
  else
    {
      devices_filename = get_expanded_dir (CONF_DIR DEVICES_FILE);

      if (!json_parser_load_from_file (parser, devices_filename, &error))
	{
	  debug_print (1, "%s\n", error->message);
	  g_clear_error (&error);

	  g_free (devices_filename);
	  devices_filename = strdup (DATADIR DEVICES_FILE);

	  debug_print (1, "Falling back to %s...\n", devices_filename);

	  if (!json_parser_load_from_file (parser, devices_filename, &error))
	    {
	      error_print ("%s", error->message);
	      g_clear_error (&error);
	      err = -ENODEV;
	      goto cleanup_parser;
	    }
	}
    }

  reader = json_reader_new (json_parser_get_root (parser));
  if (!reader)
    {
      error_print ("Unable to read from parser");
      err = -ENODEV;
      goto cleanup_parser;
    }

  if (!json_reader_is_array (reader))
    {
      error_print ("Not an array\n");
      err = -ENODEV;
      goto cleanup_reader;
    }

  devices = json_reader_count_elements (reader);
  if (!devices)
    {
      debug_print (1, "No devices found\n");
      err = -ENODEV;
      goto cleanup_reader;
    }

  err = -ENODEV;
  for (int i = 0; i < devices; i++)
    {
      if (!json_reader_read_element (reader, i))
	{
	  error_print ("Cannot read element %d. Continuing...\n", i);
	  continue;
	}

      if (!json_reader_read_member (reader, DEV_TAG_ID))
	{
	  error_print ("Cannot read member '%s'. Continuing...\n",
		       DEV_TAG_ID);
	  continue;
	}
      device_desc->id = json_reader_get_int_value (reader);
      json_reader_end_member (reader);

      if (device_desc->id != id)
	{
	  json_reader_end_element (reader);
	  continue;
	}

      err = 0;
      debug_print (1, "Device %d found\n", id);

      if (!json_reader_read_member (reader, DEV_TAG_NAME))
	{
	  error_print ("Cannot read member '%s'. Stopping...\n",
		       DEV_TAG_NAME);
	  json_reader_end_element (reader);
	  err = -ENODEV;
	  break;
	}
      device_desc->name = strdup (json_reader_get_string_value (reader));
      json_reader_end_member (reader);

      if (!json_reader_read_member (reader, DEV_TAG_ALIAS))
	{
	  error_print ("Cannot read member '%s'. Stopping...\n",
		       DEV_TAG_ALIAS);
	  json_reader_end_element (reader);
	  err = -ENODEV;
	  break;
	}
      device_desc->alias = strdup (json_reader_get_string_value (reader));
      json_reader_end_member (reader);

      if (!json_reader_read_member (reader, DEV_TAG_FILESYSTEMS))
	{
	  error_print ("Cannot read member '%s'. Stopping...\n",
		       DEV_TAG_FILESYSTEMS);
	  json_reader_end_element (reader);
	  err = -ENODEV;
	  break;
	}
      device_desc->filesystems = json_reader_get_int_value (reader);
      json_reader_end_member (reader);

      if (!json_reader_read_member (reader, DEV_TAG_STORAGE))
	{
	  error_print ("Cannot read member '%s'. Stopping...\n",
		       DEV_TAG_STORAGE);
	  json_reader_end_element (reader);
	  err = -ENODEV;
	  break;
	}
      device_desc->storage = json_reader_get_int_value (reader);
      json_reader_end_member (reader);

      break;
    }

cleanup_reader:
  g_object_unref (reader);
cleanup_parser:
  g_object_unref (parser);
  g_free (devices_filename);
  if (err)
    {
      device_desc->id = -1;
    }
  return err;
}
