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
#if !defined(__linux__)
#include <dirent.h>
#endif
#include <errno.h>
#include "utils.h"

#define DEBUG_SHORT_HEX_LEN 64
#define DEBUG_FULL_HEX_THRES 5

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
debug_get_hex_data (gint level, guint8 *data, guint len)
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
  if (!size)
    {
      return NULL;
    }

  str = g_malloc (sizeof (char) * size);
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
debug_get_hex_msg (const GByteArray *msg)
{
  return debug_get_hex_data (debug_level, msg->data, msg->len);
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
filename_get_ext (const gchar *name)
{
  int namelen = strlen (name);
  const gchar *ext = &name[namelen], *p = name;

  for (guint i = 0; i < namelen; i++, p++)
    {
      if (*p == '.')
	{
	  i++;
	  p++;
	  ext = p;
	}
    }

  return ext;
}

gchar *
get_user_dir (const char *rel_conf_path)
{
  const gchar *home = getenv ("HOME");
  gchar *input = rel_conf_path ? g_strconcat (home, rel_conf_path, NULL) :
    strdup (home);
  gchar *output = path_translate (PATH_SYSTEM, input);
  g_free (input);
  return output;
}

char *
get_system_startup_path (const gchar *local_dir)
{
  DIR *dir;
  gchar *startup_path = NULL;

  if (local_dir)
    {
      dir = opendir (local_dir);
      if (dir)
	{
	  startup_path = strdup (local_dir);
	}
      else
	{
	  error_print ("Unable to open dir '%s'", local_dir);
	}
      closedir (dir);
    }

  if (!startup_path)
    {
      startup_path = get_user_dir (NULL);
    }

  debug_print (1, "Using '%s' as local dir...", startup_path);

  return startup_path;
}

void
free_msg (gpointer msg)
{
  g_byte_array_free ((GByteArray *) msg, TRUE);
}

gint
file_load (const char *path, struct idata *idata, struct job_control *control)
{
  FILE *f;
  size_t size;
  gint res;
  GByteArray *array;

  f = fopen (path, "rb");

  if (!f)
    {
      return -errno;
    }

  res = 0;

  if (fseek (f, 0, SEEK_END))
    {
      error_print ("Unexpected value");
      res = -errno;
      goto end;
    }

  size = ftell (f);
  rewind (f);

  array = g_byte_array_sized_new (size);
  array->len = size;

  if (fread (array->data, 1, size, f) == size)
    {
      gchar *name = g_path_get_basename (path);
      remove_ext (name);
      idata_init (idata, array, strdup (name), NULL);
      g_free (name);
      debug_print (1, "%zu B read", size);
    }
  else
    {
      error_print ("Error while reading from file %s", path);
      g_byte_array_free (array, TRUE);
      res = -errno;
    }

end:
  fclose (f);
  return res;
}

gint
file_save_data (const gchar *path, const guint8 *data, ssize_t len)
{
  gint res;
  size_t bytes;
  FILE *file;

  file = fopen (path, "wb");

  if (!file)
    {
      return -errno;
    }

  debug_print (1, "Saving file %s...", path);

  res = 0;
  bytes = fwrite (data, 1, len, file);
  if (bytes == len)
    {
      debug_print (1, "%zu B written", bytes);
    }
  else
    {
      error_print ("Error while writing to file %s", path);
      res = -EIO;
    }

  fclose (file);

  return res;
}

gint
file_save (const gchar *path, struct idata *idata,
	   struct job_control *control)
{
  return file_save_data (path, idata->content->data, idata->content->len);
}

gchar *
get_human_size (gint64 size, gboolean with_space)
{
  gchar *label = g_malloc (LABEL_MAX);
  gchar *space = with_space ? " " : "";

  if (size < 0)
    {
      *label = 0;
    }
  else if (size < KIB)
    {
      snprintf (label, LABEL_MAX, "%" PRId64 "%sB", size, space);
    }
  else if (size < KIB * KIB)
    {
      snprintf (label, LABEL_MAX, "%.4g%sKiB", size / (double) KIB, space);
    }
  else if (size < KIB * KIB * KIB)
    {
      snprintf (label, LABEL_MAX, "%.4g%sMiB", size / (double) (KIB * KIB),
		space);
    }
  else
    {
      snprintf (label, LABEL_MAX, "%.4g%sGiB",
		size / (double) (KIB * KIB * KIB), space);
    }

  return label;
}

static inline void
job_control_set_progress_value (struct job_control *control, gdouble p)
{
  if (control->parts)
    {
      if (control->part == control->parts)
	{
	  control->progress = 1.0;
	}
      else
	{
	  control->progress =
	    (control->part / (double) control->parts) +
	    (p / (double) control->parts);
	}
    }
  else
    {
      control->progress = 0.0;
    }
}

void
job_control_set_sample_progress_no_sync (struct job_control *control,
					 gdouble p, gpointer data)
{
  job_control_set_progress_value (control, p);

  if (control->callback)
    {
      control->callback (control);
    }
}

void
job_control_set_progress (struct job_control *control, gdouble p)
{
  g_mutex_lock (&control->mutex);
  job_control_set_progress_value (control, p);
  g_mutex_unlock (&control->mutex);

  if (control->callback)
    {
      control->callback (control);
    }
}

gboolean
file_matches_extensions (const gchar *name, const GSList *extensions)
{
  const gchar *extension;
  const GSList *e = extensions;

  if (!e)
    {
      return TRUE;
    }

  extension = filename_get_ext (name);
  if (!*extension)
    {
      return FALSE;
    }

  while (e)
    {
      if (!strcasecmp (extension, e->data))
	{
	  return TRUE;
	}
      e = e->next;
    }

  return FALSE;
}

static inline const gchar *
path_get_separator (enum path_type type)
{
  const gchar *sep;
  if (type == PATH_SYSTEM)
    {
#if defined(__MINGW32__) | defined(__MINGW64__)
      sep = "\\";
#else
      sep = "/";
#endif
    }
  else
    {
      sep = "/";
    }
  return sep;
}

gchar *
path_chain (enum path_type type, const gchar *parent, const gchar *child)
{
  const gchar *sep = path_get_separator (type);
  return g_build_path (sep, parent, child, NULL);
}

//Translate from internal path to system path.

gchar *
path_translate (enum path_type type, const gchar *input)
{
  gchar *output, *o;
  const gchar *i;
  const gchar *sep = path_get_separator (type);

  if (!strcmp (sep, "/"))
    {
      return strdup (input);
    }

  output = g_malloc (strlen (input) * 2 + 1);	//Worst case scenario
  i = input;
  o = output;
  while (*i)
    {
      if (*i == '/')
	{
	  *o = 0;
	  strcat (o, sep);
	  o += strlen (sep);
	}
      else
	{
	  *o = *i;
	  o++;
	}
      i++;
    }
  *o = 0;
  return output;
}

//These two functions are needed as g_filename_to_uri and g_uri_to_filename
//depend on the local system and therefore can not be used for BE_TYPE_MIDI
//under MSYS2.

gchar *
path_filename_from_uri (enum path_type type, gchar *uri)
{
  if (type == PATH_SYSTEM)
    {
      return g_filename_from_uri (uri, NULL, NULL);
    }
  const gchar *filename = &uri[7];	//Skip "file://".
  return g_uri_unescape_string (filename, ":/");
}

gchar *
path_filename_to_uri (enum path_type type, gchar *filename)
{
  if (type == PATH_SYSTEM)
    {
      return g_filename_to_uri (filename, NULL, NULL);
    }
  gchar *uri = g_strconcat ("file://", filename, NULL);
  gchar *escaped_uri = g_uri_escape_string (uri, ":/", FALSE);
  g_free (uri);
  return escaped_uri;
}

void
g_slist_fill (GSList **list, ...)
{
  gpointer v;
  va_list argptr;

  *list = NULL;
  va_start (argptr, list);
  while ((v = va_arg (argptr, gpointer)))
    {
      *list = g_slist_append (*list, v);
    }
  va_end (argptr);
}

void
idata_init (struct idata *idata, GByteArray *content, gchar *name, void *info)
{
  idata->content = content;
  idata->name = name;
  idata->info = info;
}

void
idata_free (struct idata *idata)
{
  if (idata->content)
    {
      g_byte_array_free (idata->content, TRUE);
      idata->content = NULL;
    }
  g_free (idata->name);
  idata->name = NULL;
  g_free (idata->info);
  idata->info = NULL;
}

GByteArray *
idata_steal (struct idata *idata)
{
  g_free (idata->name);
  g_free (idata->info);
  return idata->content;
}

gboolean
job_control_get_active_lock (struct job_control *control)
{
  gboolean active;

  g_mutex_lock (&control->mutex);
  active = control->active;
  g_mutex_unlock (&control->mutex);

  return active;
}

void
job_control_set_active_lock (struct job_control *control, gboolean active)
{
  g_mutex_lock (&control->mutex);
  control->active = active;
  g_mutex_unlock (&control->mutex);
}
