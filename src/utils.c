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
  gchar *dot = &name[namelen - 1];
  gint i = namelen - 1;

  while (i > 0)
    {
      if (*dot == '.')
	{
	  *dot = 0;
	  break;
	}
      dot--;
      i--;
    }
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
      snprintf (index, LABEL_MAX, "%d", item->id);
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

inline gint
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
get_human_size (gint64 size, gboolean with_space)
{
  gchar *label = malloc (LABEL_MAX);
  gchar *space = with_space ? " " : "";

  if (size < 0)
    {
      label = strdup ("");
    }
  else if (size < KIB)
    {
      snprintf (label, LABEL_MAX, "%ld%sB", size, space);
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
