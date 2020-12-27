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

#include <glib.h>
#include <stdio.h>
#include <wordexp.h>
#include "utils.h"

#define DEBUG_SHORT_HEX_LEN 16
#define DEBUG_FULL_HEX_THRES 3

#define REG_TYPE "emblem-music-symbolic"
#define DIR_TYPE "folder-visiting-symbolic"

int debug_level;

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
debug_get_hex_msg (const GByteArray * msg)
{
  gint i;
  guint8 *data;
  guint size;
  guint bytes_shown;
  guint extra;
  gchar *str;
  gchar *next;

  if (debug_level >= DEBUG_FULL_HEX_THRES)
    {
      bytes_shown = msg->len;
      extra = 0;
    }
  else
    {
      if (msg->len > DEBUG_SHORT_HEX_LEN)
	{
	  bytes_shown = DEBUG_SHORT_HEX_LEN;
	  extra = 3;
	}
      else
	{
	  bytes_shown = msg->len;
	  extra = 0;
	}
    }
  size = 2 + (bytes_shown - 1) * 3 + extra + 1;
  str = malloc (sizeof (char) * size);

  data = msg->data;
  next = str;

  sprintf (next, "%02x", *data);
  next += 2;
  data++;

  i = 1;
  while (i < get_max_message_length (msg->len))
    {
      sprintf (next, " %02x", *data);
      next += 3;
      data++;
      i++;
    }

  if (debug_level < DEBUG_FULL_HEX_THRES && msg->len > DEBUG_SHORT_HEX_LEN)
    {
      sprintf (next, "...");
      next += 3;
    }

  sprintf (next, "\n");

  return str;
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

const char *
get_ext (const char *name)
{
  int namelen = strlen (name) - 1;
  int i = namelen;
  const char *ext = &name[namelen];

  while (*(ext - 1) != '.' && i > 0)
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

char
get_type_from_inventory_icon (const char *icon)
{
  char type;

  if (strcmp (icon, REG_TYPE) == 0)
    {
      type = ELEKTROID_FILE;
    }
  else
    {
      type = ELEKTROID_DIR;
    }
  return type;
}

const char *
get_inventory_icon_from_type (char type)
{
  const char *icon;

  if (type == ELEKTROID_FILE)
    {
      icon = REG_TYPE;
    }
  else
    {
      icon = DIR_TYPE;
    }
  return icon;
}

char *
get_local_startup_path (const char *local_dir)
{
  DIR *dir;
  char *startup_path = NULL;
  wordexp_t exp_result;

  if (local_dir)
    {
      dir = opendir (local_dir);
      if (dir)
	{
	  startup_path = malloc (PATH_MAX);
	  realpath (local_dir, startup_path);
	}
      else
	{
	  fprintf (stderr, __FILE__ ": Unable to open dir %s\n", local_dir);
	}
      closedir (dir);
    }

  if (!startup_path)
    {
      wordexp ("~", &exp_result, 0);
      startup_path = malloc (PATH_MAX);
      strcpy (startup_path, exp_result.we_wordv[0]);
      wordfree (&exp_result);
    }

  debug_print (1, "Using %s as local dir...\n", startup_path);

  return startup_path;
}
