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

static char
connector_get_printable_char (char c)
{
  return c >= 0x20 && c < 0x80 ? c : '.';
}

void
debug_print_ascii_msg (const GByteArray * msg)
{
  int i = 0;
  guint8 *data = msg->data;

  while (i < msg->len)
    {
      debug_print ("%c", connector_get_printable_char (*data));
      data++;
      i++;
    }
  debug_print ("\n");
}

void
debug_print_hex_msg (const GByteArray * msg)
{
  int i = 0;
  guint8 *data = msg->data;

  while (i < msg->len - 1)
    {
      if (debug)
	{
	  debug_print ("0x%02x, ", *data);
	}
      data++;
      i++;
    }
  debug_print ("0x%02x\n", *data);
}

char *
chain_path (const char *parent, const char *child)
{
  char *pathname = malloc (PATH_MAX);
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
