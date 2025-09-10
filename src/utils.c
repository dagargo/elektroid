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
#include <math.h>
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
filename_remove_ext (char *name)
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

gint
filename_get_lenght_without_ext (const gchar *name)
{
  gint sugg_sel_len = strlen (name);
  const gchar *ext = filename_get_ext (name);
  gint ext_len = strlen (ext);
  if (ext_len)
    {
      sugg_sel_len -= ext_len + 1;
    }

  return sugg_sel_len;
}

//The returned value is owned by the caller.
//As this is used from the code, rel_dir uses '/' always and needs to be converted.

gchar *
get_user_dir (const char *rel_dir)
{
  const gchar *home = g_get_home_dir ();
  if (rel_dir)
    {
      gchar *rel_dir_conv = path_translate (PATH_SYSTEM, rel_dir);
      gchar *dir = path_chain (PATH_SYSTEM, home, rel_dir_conv);
      g_free (rel_dir_conv);
      return dir;
    }
  else
    {
      return strdup (home);
    }
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
file_load (const char *path, struct idata *idata,
	   struct task_control *control)
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
      filename_remove_ext (name);
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
	   struct task_control *control)
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
  else if (size < KI)
    {
      snprintf (label, LABEL_MAX, "%" PRId64 "%sB", size, space);
    }
  else if (size < MI)
    {
      snprintf (label, LABEL_MAX, "%.4g%sKiB", size / (double) KI, space);
    }
  else if (size < GI)
    {
      snprintf (label, LABEL_MAX, "%.4g%sMiB", size / (double) MI, space);
    }
  else
    {
      snprintf (label, LABEL_MAX, "%.4g%sGiB", size / (double) GI, space);
    }

  return label;
}

void
task_control_set_progress_no_sync (struct task_control *control, gdouble p)
{
  if (control->parts)
    {
      if (control->part == control->parts)
	{
	  control->progress = 1.0;
	}
      else
	{
	  control->progress = (control->part + p) / (double) control->parts;
	}
    }
  else
    {
      control->progress = 0.0;
    }
}

void
task_control_set_progress (struct task_control *control, gdouble p)
{
  g_mutex_lock (&control->controllable.mutex);
  task_control_set_progress_no_sync (control, p);
  g_mutex_unlock (&control->controllable.mutex);

  if (control->callback)
    {
      control->callback (control);
    }
}

gboolean
filename_matches_exts (const gchar *name, const gchar **exts)
{
  const gchar *ext;
  const gchar **e = exts;

  if (!e)
    {
      return TRUE;
    }

  ext = filename_get_ext (name);
  if (!*ext)
    {
      return FALSE;
    }

  while (*e)
    {
      if (!strcasecmp (ext, *e))
	{
	  return TRUE;
	}
      e++;
    }

  return FALSE;
}

gboolean
filename_is_dir_or_matches_exts (const gchar *name, const gchar **exts)
{
  return g_file_test (name, G_FILE_TEST_IS_DIR) ||
    filename_matches_exts (name, exts);
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
gslist_fill (GSList **list, ...)
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

GByteArray *
idata_steal (struct idata *idata)
{
  GByteArray *content = idata->content;
  idata->content = NULL;
  g_free (idata->name);
  idata->name = NULL;
  g_free (idata->info);
  idata->info = NULL;
  return content;
}

void
idata_free (struct idata *idata)
{
  GByteArray *content = idata_steal (idata);
  if (content)
    {
      g_byte_array_free (content, TRUE);
    }
}

void
task_control_reset (struct task_control *control, gint parts)
{
  control->parts = parts;
  control->part = 0;
  task_control_set_progress (control, 0.0);
}

guint32
cents_to_midi_fraction (guint32 cents)
{
  return (guint32) round (cents * 256 / 100.0);
}

guint32
midi_fraction_to_cents (guint32 midi_fraction)
{
  return (guint32) round (midi_fraction * 100 / 256.0);
}

void
controllable_init (struct controllable *controllable)
{
  g_mutex_init (&controllable->mutex);
  controllable->active = TRUE;
}

void
controllable_clear (struct controllable *controllable)
{
  g_mutex_clear (&controllable->mutex);
}

void
controllable_set_active (struct controllable *controllable, gboolean active)
{
  g_mutex_lock (&controllable->mutex);
  controllable->active = active;
  g_mutex_unlock (&controllable->mutex);
}

gboolean
controllable_is_active (struct controllable *controllable)
{
  gboolean active;
  g_mutex_lock (&controllable->mutex);
  active = controllable->active;
  g_mutex_unlock (&controllable->mutex);
  return active;
}

gboolean
token_is_in_any_token (const gchar *token, gchar **tokens)
{
  for (guint j = 0; tokens[j]; j++)
    {
      if (g_strstr_len (tokens[j], -1, token) != NULL)
	{
	  return TRUE;
	}
    }
  return FALSE;
}

gboolean
token_is_in_text (const gchar *token, const gchar *text)
{
  gboolean found;
  gchar **tokens;
  gchar **alternates;

  tokens = g_str_tokenize_and_fold (text, NULL, &alternates);
  found = token_is_in_any_token (token, tokens);
  if (!found)
    {
      found = token_is_in_any_token (token, alternates);
    }

  g_strfreev (tokens);
  g_strfreev (alternates);

  return found;
}

static gint
command_set_parts_with_separator (const gchar *cmd, gchar separator,
				  gchar **connector, gchar **fs, gchar **op)
{
  gchar *aux;

  *connector = strdup (cmd);
  aux = strchr (*connector, separator);
  if (!aux)
    {
      g_free (*connector);
      return -EINVAL;
    }
  *aux = 0;
  aux++;

  *fs = strdup (aux);
  aux = strchr (*fs, separator);
  if (!aux)
    {
      g_free (*connector);
      g_free (*fs);
      return -EINVAL;
    }

  *aux = 0;
  aux++;

  *op = strdup (aux);

  return 0;
}

gint
command_set_parts (const gchar *cmd, gchar **connector, gchar **fs,
		   gchar **op)
{
  return command_set_parts_with_separator (cmd, strchr (cmd, ':') ? ':' : '-',
					   connector, fs, op);
}
