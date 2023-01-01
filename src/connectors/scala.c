/*
 *   scala.h
 *   Copyright (C) 2022 David García Goñi <dagargo@gmail.com>
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

#include <math.h>
#include "scala.h"
#include "utils.h"

#define SCALA_FILE_LINE_SEPARATOR "\x0d\x0a"
#define SCALA_FILE_COMMENT_CHAR '!'
#define SCALA_ELEKTROID_NOTES 12
#define SCALA_MIDI_TUNING_NAME_LEN 16

static const guint8 SCALA_MIDI_TUNING_HEADER[] = { 0xf0, 0x7e, 0x7f, 8, 6 };

static gint
scl_parser_get_pitch (gchar * line, gdouble * val)
{
  gint err = 0;
  gdouble num, den;
  gboolean dot;
  gchar *c, *rem, *denrem;

  c = line;
  dot = FALSE;
  while (*c)
    {
      if (*c == '.')
	{
	  dot = TRUE;
	  break;
	}
      c++;
    }

  if (dot)
    {
      *val = g_ascii_strtod (line, NULL);
    }
  else
    {
      num = g_ascii_strtoull (line, &rem, 10);
      if (num == 0 && !strcmp (line, rem))
	{
	  return -EINVAL;
	}
      if (*rem != '/')
	{
	  return -EINVAL;
	}
      rem++;
      den = g_ascii_strtoull (rem, &denrem, 10);
      if (num == 0 && !strcmp (rem, denrem))
	{
	  return -EINVAL;
	}
      *val = num / (gdouble) den;
    }
  return err;
}

static gchar **
scl_parser_get_next_line (gchar ** lines)
{
  while (*lines && (*lines)[0] == SCALA_FILE_COMMENT_CHAR)
    {
      lines++;
    }
  return lines;
}

gint
scl_init_scala_from_bytes (struct scala *scala, GByteArray * input)
{
  gint err = 0;
  gchar **line, **lines, *rem;
  guint64 notes;

  if (!input->len)
    {
      return -EINVAL;
    }

  lines = g_strsplit ((gchar *) input->data, SCALA_FILE_LINE_SEPARATOR, -1);
  line = scl_parser_get_next_line (lines);
  if (!*line)
    {
      err = -EINVAL;
      goto end;
    }
  snprintf (scala->desc, SCALA_DESC_MAX_LEN, "%s", *line);
  debug_print (2, "Scala description: %s\n", scala->desc);

  line++;
  line = scl_parser_get_next_line (line);
  if (!*line)
    {
      err = -EINVAL;
      goto end;
    }
  scala->notes = g_ascii_strtoull (*line, &rem, 10);
  if (scala->notes < 0 || (!scala->notes && !strcmp (*line, rem))
      || scala->notes > SCALA_NOTES_MAX)
    {
      err = -ERANGE;
      goto end;
    }
  debug_print (2, "Scala notes: %" G_GUINT64_FORMAT "\n", scala->notes);

  notes = 0;
  for (gint i = 0; i < scala->notes; i++)
    {
      line++;
      line = scl_parser_get_next_line (line);
      if (!*line)
	{
	  err = -EINVAL;
	  goto end;
	}
      err = scl_parser_get_pitch (*line, &scala->pitches[i]);
      if (err)
	{
	  goto end;
	}
      debug_print (2, "Scala pitch %d: %f\n", i, scala->pitches[i]);
      notes++;
    }

  if (scala->notes != notes)
    {
      err = -EINVAL;
    }

end:
  g_strfreev (lines);
  return err;
}

static double
scala_get_cents_from_ratio (double ratio)
{
  return 1200.0 * log (ratio) / log (2);
}

GByteArray *
scl_get_2_byte_tuning_msg_from_scala_file (GByteArray * input, guint8 bank,
					   guint8 tuning)
{
  gint err;
  guint8 *b, cksum;
  guint len;
  GByteArray *msg;
  struct scala scala;

  err = scl_init_scala_from_bytes (&scala, input);
  if (err)
    {
      return NULL;
    }

  msg = g_byte_array_sized_new (256);
  g_byte_array_append (msg, SCALA_MIDI_TUNING_HEADER,
		       sizeof (SCALA_MIDI_TUNING_HEADER));
  g_byte_array_append (msg, &bank, 1);
  g_byte_array_append (msg, &tuning, 1);

  len = strlen (scala.desc);
  if (len > SCALA_MIDI_TUNING_NAME_LEN)
    {
      len = SCALA_MIDI_TUNING_NAME_LEN;
    }
  g_byte_array_append (msg, (guint8 *) scala.desc, len);
  while (len < SCALA_MIDI_TUNING_NAME_LEN)
    {
      g_byte_array_append (msg, (guint8 *) " ", 1);
      len++;
    }

  for (gint i = 0; i < scala.notes; i++)
    {
      double pitch, cents, diff;
      guint value;
      guint8 msb, lsb;
      if (i == 0)
	{
	  pitch = 1.0;
	  cents = 0.0;
	  diff = 0.0;
	}
      else
	{
	  pitch = scala.pitches[i - 1];
	  cents = scala_get_cents_from_ratio (pitch);
	  diff = cents - i * 100;
	}
      value = (diff + 100.0) / .012207;
      msb = (value >> 7) & 0x7f;
      lsb = value & 0x7f;
      debug_print (2,
		   "Adding note %d (pitch %.6f, cents %.2f, diff %.2f, value %d, MSB %02x, LSB %02x)...\n",
		   i, pitch, cents, diff, value, msb, lsb);
      g_byte_array_append (msg, (guint8 *) & msb, 1);
      g_byte_array_append (msg, (guint8 *) & lsb, 1);
    }

  cksum = 0;
  b = &msg->data[1];
  for (gint i = 1; i < msg->len; i++, b++)
    {
      cksum ^= *b;
    }
  cksum &= 0x7f;
  g_byte_array_append (msg, &cksum, 1);
  g_byte_array_append (msg, (guint8 *) "\xf7", 1);

  return msg;
}
