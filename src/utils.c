/*
 *   utils.c
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

#include <stdio.h>
#include <wordexp.h>
#include <errno.h>
#include "utils.h"

#define DEBUG_SHORT_HEX_LEN 64
#define DEBUG_FULL_HEX_THRES 3

#define KIB 1024

#define DEVICES_FILE "/devices.json"

#define DEV_TAG_ID "id"
#define DEV_TAG_NAME "name"
#define DEV_TAG_ALIAS "alias"
#define DEV_TAG_FILESYSTEMS "filesystems"
#define DEV_TAG_STORAGE "storage"

gint debug_level;

static guint
get_max_message_length (guint msg_len)
{
  guint len;

  if (debug_level >= DEBUG_FULL_HEX_THRES)
    {
      len = msg_len;
    }
  else
    {
      len = msg_len > DEBUG_SHORT_HEX_LEN ? DEBUG_SHORT_HEX_LEN : msg_len;
    }

  return len;
}

gchar *
debug_get_hex_data (gint level, guint8 * data, guint len)
{
  gint i;
  guint8 *b;
  guint size;
  guint bytes_shown;
  guint extra;
  gchar *str;
  gchar *next;

  if (level >= DEBUG_FULL_HEX_THRES)
    {
      bytes_shown = len;
      extra = 0;
    }
  else
    {
      if (len > DEBUG_SHORT_HEX_LEN)
	{
	  bytes_shown = DEBUG_SHORT_HEX_LEN;
	  extra = 3;
	}
      else
	{
	  bytes_shown = len;
	  extra = 0;
	}
    }
  size = bytes_shown * 3 + extra;
  str = malloc (sizeof (char) * size);

  b = data;
  next = str;

  sprintf (next, "%02x", *b);
  next += 2;
  b++;

  i = 1;
  while (i < get_max_message_length (len))
    {
      sprintf (next, " %02x", *b);
      next += 3;
      b++;
      i++;
    }

  if (level < DEBUG_FULL_HEX_THRES && len > DEBUG_SHORT_HEX_LEN)
    {
      sprintf (next, "...");
      next += 3;
    }

  return str;
}

gchar *
debug_get_hex_msg (const GByteArray * msg)
{
  return debug_get_hex_data (debug_level, msg->data, msg->len);
}

gchar *
chain_path (const gchar * parent, const gchar * child)
{
  gchar *pathname = malloc (PATH_MAX);
  if (strcmp (parent, "/") == 0)
    {
      snprintf (pathname, PATH_MAX, "/%s", child);
    }
  else
    {
      snprintf (pathname, PATH_MAX, "%s/%s", parent, child);
    }

  return pathname;
}

void
remove_ext (char *name)
{
  gint namelen = strlen (name);
  gchar *dot = &name[namelen];
  gint i = namelen;

  while (*dot != '.' && i > 0)
    {
      dot--;
      i--;
    }
  *dot = 0;
}

const gchar *
get_ext (const gchar * name)
{
  int namelen = strlen (name) - 1;
  int i = namelen;
  const gchar *ext = &name[namelen];

  while (i > 0 && *(ext - 1) != '.')
    {
      ext--;
      i--;
    }

  if (i == 0 && name[0] != '.')
    {
      return NULL;
    }
  else
    {
      return ext;
    }
}

gchar *
get_expanded_dir (const char *exp)
{
  wordexp_t exp_result;
  size_t n;
  gchar *exp_dir = malloc (PATH_MAX);

  wordexp (exp, &exp_result, 0);
  n = PATH_MAX - 1;
  strncpy (exp_dir, exp_result.we_wordv[0], n);
  exp_dir[PATH_MAX - 1] = 0;
  wordfree (&exp_result);

  return exp_dir;
}

char *
get_local_startup_path (const gchar * local_dir)
{
  DIR *dir;
  gchar *startup_path = NULL;

  if (local_dir)
    {
      dir = opendir (local_dir);
      if (dir)
	{
	  startup_path = realpath (local_dir, NULL);
	}
      else
	{
	  error_print ("Unable to open dir '%s'\n", local_dir);
	}
      closedir (dir);
    }

  if (!startup_path)
    {
      startup_path = get_expanded_dir ("~");
    }

  debug_print (1, "Using '%s' as local dir...\n", startup_path);

  return startup_path;
}

void
free_msg (gpointer msg)
{
  g_byte_array_free ((GByteArray *) msg, TRUE);
}

gchar *
get_item_name (struct item *item)
{
  return strdup (item->name);
}

gchar *
get_item_index (struct item *item)
{
  gchar *index;

  if (item->type == ELEKTROID_DIR)
    {
      index = get_item_name (item);
    }
  else
    {
      index = malloc (LABEL_MAX);
      snprintf (index, LABEL_MAX, "%d", item->index);
    }

  return index;
}

guint
next_item_iterator (struct item_iterator *iter)
{
  return iter->next (iter);
}

void
free_item_iterator (struct item_iterator *iter)
{
  if (iter->free)
    {
      iter->free (iter->data);
    }
}

gint
copy_item_iterator (struct item_iterator *dst, struct item_iterator *src,
		    gboolean cached)
{
  return src->copy (dst, src, cached);
}

gint
load_file (const char *path, GByteArray * array, struct job_control *control)
{
  FILE *file;
  long size;
  gint res;

  file = fopen (path, "rb");

  if (!file)
    {
      return -errno;
    }

  res = 0;

  if (fseek (file, 0, SEEK_END))
    {
      error_print ("Unexpected value\n");
      res = -errno;
      goto end;
    }

  size = ftell (file);
  rewind (file);

  g_byte_array_set_size (array, size);

  if (fread (array->data, 1, size, file) == size)
    {
      debug_print (1, "%zu bytes read\n", size);
    }
  else
    {
      error_print ("Error while reading from file %s\n", path);
      res = -errno;
    }

end:
  fclose (file);
  return res;
}

gint
save_file_char (const gchar * path, const guint8 * data, ssize_t len)
{
  gint res;
  long bytes;
  FILE *file;

  file = fopen (path, "w");

  if (!file)
    {
      return -errno;
    }

  debug_print (1, "Saving file %s...\n", path);

  res = 0;
  bytes = fwrite (data, 1, len, file);
  if (bytes == len)
    {
      debug_print (1, "%zu bytes written\n", bytes);
    }
  else
    {
      error_print ("Error while writing to file %s\n", path);
      res = -EIO;
    }

  fclose (file);

  return res;
}

gint
save_file (const gchar * path, GByteArray * array,
	   struct job_control *control)
{
  return save_file_char (path, array->data, array->len);
}

gchar *
get_human_size (guint size, gboolean with_space)
{
  gchar *label = malloc (LABEL_MAX);
  gchar *space = with_space ? " " : "";

  if (size < KIB)
    {
      snprintf (label, LABEL_MAX, "%d%sB", size, space);
    }
  else if (size < KIB * KIB)
    {
      snprintf (label, LABEL_MAX, "%.2f%sKiB", size / (double) KIB, space);
    }
  else if (size < KIB * KIB * KIB)
    {
      snprintf (label, LABEL_MAX, "%.2f%sMiB", size / (double) (KIB * KIB),
		space);
    }
  else
    {
      snprintf (label, LABEL_MAX, "%.2f%sGiB",
		size / (double) (KIB * KIB * KIB), space);
    }

  return label;
}

void
set_job_control_progress (struct job_control *control, gdouble p)
{
  gdouble v = (double) control->part / control->parts + p / control->parts;
  control->callback (v);
}

gint
load_device_desc (struct connector_device_desc *device_desc, guint8 id)
{
  gint err, devices;
  JsonParser *parser;
  JsonReader *reader;
  gchar *devices_filename;
  GError *error = NULL;

  parser = json_parser_new ();

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
  return err;
}
