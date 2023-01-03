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
#define SCALA_OCTAVE_NOTES 12
#define SCALA_MIDI_TUNING_NAME_LEN 16
#define SCALA_OCTAVE_STEP_SIZE .012207
#define SCALA_C0_FREQ 8.1758
#define SCALA_MIDI_NOTES 128
#define SCALA_BULK_STEP_SIZE .0061

static const guint8 SCALA_MIDI_OCTAVE_TUNING_HEADER[] =
  { 0xf0, 0x7e, 0x7f, 8, 6 };
static const guint8 SCALA_MIDI_BULK_TUNING_HEADER[] =
  { 0xf0, 0x7e, 0x7f, 8, 1 };

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

static gdouble
scala_get_cents_from_ratio (gdouble ratio)
{
  return 1200.0 * log (ratio) / log (2);
}

static guint8
scl_get_nearest_note_below (gdouble f, gdouble * note_f)
{
  gdouble next, p = exp (log (2.0) / 12.0);
  guint8 n;
  *note_f = SCALA_C0_FREQ;
  for (n = 0; n < SCALA_MIDI_NOTES; n++)
    {
      next = *note_f * p;
      if (next > f)
	{
	  return n;
	}
      *note_f = next;
    }
  return n;
}

static void
scl_append_name_to_msg (struct scala *scala, GByteArray * msg)
{
  guint len = strlen (scala->desc);
  if (len > SCALA_MIDI_TUNING_NAME_LEN)
    {
      len = SCALA_MIDI_TUNING_NAME_LEN;
    }
  g_byte_array_append (msg, (guint8 *) scala->desc, len);
  while (len < SCALA_MIDI_TUNING_NAME_LEN)
    {
      g_byte_array_append (msg, (guint8 *) " ", 1);
      len++;
    }
}

static guint8
scl_get_cksum (guint8 * b, gint len)
{
  guint8 cksum = 0;
  for (gint i = 0; i < len; i++, b++)
    {
      cksum ^= *b;
    }
  cksum &= 0x7f;
  return cksum;
}

GByteArray *
scl_get_2_byte_octave_tuning_msg_from_scala_file (GByteArray * input,
						  guint8 bank, guint8 tuning)
{
  gint err;
  GByteArray *msg;
  guint8 cksum, msb, lsb;
  struct scala scala;

  err = scl_init_scala_from_bytes (&scala, input);
  if (err)
    {
      return NULL;
    }

  if (scala.notes != SCALA_OCTAVE_NOTES)
    {
      return NULL;
    }

  msg = g_byte_array_sized_new (512);
  g_byte_array_append (msg, SCALA_MIDI_OCTAVE_TUNING_HEADER,
		       sizeof (SCALA_MIDI_OCTAVE_TUNING_HEADER));
  g_byte_array_append (msg, &bank, 1);
  g_byte_array_append (msg, &tuning, 1);

  scl_append_name_to_msg (&scala, msg);

  for (guint8 i = 0; i < SCALA_OCTAVE_NOTES; i++)
    {
      double pitch, cents, diff;
      guint value;
      if (i == 0)
	{
	  pitch = scala.pitches[SCALA_OCTAVE_NOTES - 1] / 2.0;
	}
      else
	{
	  pitch = scala.pitches[i - 1];
	}
      cents = scala_get_cents_from_ratio (pitch);
      diff = cents - i * 100.0;
      value = (diff + 100.0) / SCALA_OCTAVE_STEP_SIZE;
      msb = (value >> 7) & 0x7f;
      lsb = value & 0x7f;
      debug_print (2,
		   "Note %d (pitch %.6f, cents %.2f, diff %.2f, value %d, MSB %02x, LSB %02x)...\n",
		   i, pitch, cents, diff, value, msb, lsb);
      g_byte_array_append (msg, (guint8 *) & msb, 1);
      g_byte_array_append (msg, (guint8 *) & lsb, 1);
    }

  cksum = scl_get_cksum (&msg->data[1], 46);
  g_byte_array_append (msg, &cksum, 1);
  g_byte_array_append (msg, (guint8 *) "\xf7", 1);

  return msg;
}

GByteArray *
scl_get_key_based_tuning_msg_from_scala_file (GByteArray * input,
					      guint8 tuning)
{
  gint err;
  guint8 cksum;
  GByteArray *msg;
  struct scala scala;
  guint8 note[SCALA_OCTAVE_NOTES];
  guint8 msb[SCALA_OCTAVE_NOTES];
  guint8 lsb[SCALA_OCTAVE_NOTES];

  err = scl_init_scala_from_bytes (&scala, input);
  if (err)
    {
      return NULL;
    }

  if (scala.notes != SCALA_OCTAVE_NOTES)
    {
      return NULL;
    }

  msg = g_byte_array_sized_new (512);
  g_byte_array_append (msg, SCALA_MIDI_BULK_TUNING_HEADER,
		       sizeof (SCALA_MIDI_BULK_TUNING_HEADER));
  g_byte_array_append (msg, &tuning, 1);

  scl_append_name_to_msg (&scala, msg);

  //Calculate pitches only for the first octave.
  for (guint8 i = 0; i < SCALA_OCTAVE_NOTES; i++)
    {
      double pitch, f, note_f, cents;
      guint value;
      if (i == 0)
	{
	  pitch = scala.pitches[SCALA_OCTAVE_NOTES - 1] / 2.0;
	}
      else
	{
	  pitch = scala.pitches[i - 1];
	}
      f = pitch * SCALA_C0_FREQ;
      note[i] = scl_get_nearest_note_below (f, &note_f);
      cents = scala_get_cents_from_ratio (f / note_f);
      value = cents / SCALA_BULK_STEP_SIZE;
      msb[i] = (value >> 7) & 0x7f;
      lsb[i] = value & 0x7f;
      debug_print (2,
		   "Note %d (pitch %.6f, note %d, cents %.2f, value %d, MSB %02x, LSB %02x)...\n",
		   i, pitch, note[i], cents, value, msb[i], lsb[i]);
    }

  //Replicate pitches for all the notes.
  for (guint8 i = 0; i < 128; i++)
    {
      gint pos = i % SCALA_OCTAVE_NOTES;
      gint octave = i / SCALA_OCTAVE_NOTES;
      guint8 n = note[pos] + octave * SCALA_OCTAVE_NOTES;
      g_byte_array_append (msg, (guint8 *) & n, 1);
      g_byte_array_append (msg, (guint8 *) & msb[pos], 1);
      g_byte_array_append (msg, (guint8 *) & lsb[pos], 1);
    }

  cksum = scl_get_cksum (&msg->data[1], 405);
  g_byte_array_append (msg, &cksum, 1);
  g_byte_array_append (msg, (guint8 *) "\xf7", 1);

  return msg;
}
