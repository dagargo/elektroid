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
#include "utils.h"

#define DEBUG_LEVEL1_LEN 16

static gchar
connector_get_printable_char (gchar c)
{
  return c >= 0x20 && c < 0x80 ? c : '.';
}

static guint
get_max_message_length (guint msg_len)
{
  guint len;

  if (debug_level > 1)
    {
      len = msg_len;
    }
  else
    {
      len = msg_len > DEBUG_LEVEL1_LEN ? DEBUG_LEVEL1_LEN : msg_len;
    }

  return len;
}

static void
print_dots_if_needed (guint msg_len)
{
  if (debug_level == 1 && msg_len > DEBUG_LEVEL1_LEN)
    {
      debug_print (1, "...");
    }
}

void
debug_print_ascii_msg (const GByteArray * msg)
{
  gint i = 0;
  guint8 *data = msg->data;

  while (i < get_max_message_length (msg->len))
    {
      debug_print (1, "%c", connector_get_printable_char (*data));
      data++;
      i++;
    }
  print_dots_if_needed (msg->len);
  debug_print (1, "\n");
}

void
debug_print_hex_msg (const GByteArray * msg)
{
  gint i = 0;
  guint8 *data = msg->data;

  while (i < get_max_message_length (msg->len))
    {
      if (i > 0)
	{
	  debug_print (1, " ");
	}
      debug_print (1, "%02x", *data);
      data++;
      i++;
    }
  print_dots_if_needed (msg->len);
  debug_print (1, "\n");
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
