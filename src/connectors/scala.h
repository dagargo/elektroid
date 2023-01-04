/*
 *   scala.c
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

#include <glib.h>
#include "utils.h"

#define SCALA_DESC_MAX_LEN 1024
#define SCALA_NOTES_MAX 1024
#define SCALA_MIDI_NOTES 128
#define SCALA_TUNING_BANK_SIZE 408

struct scala
{
  gchar desc[SCALA_DESC_MAX_LEN];
  guint64 notes;
  gdouble pitches[SCALA_NOTES_MAX];
};

gint scl_init_scala_from_bytes (struct scala *scala, GByteArray * input);

gint scl_get_2_byte_octave_tuning_msg_from_scala_file (const char *path,
						       GByteArray * msg,
						       struct job_control
						       *control);

gint scl_get_key_based_tuning_msg_from_scala_file (const char *path,
						   GByteArray * msg,
						   struct job_control
						   *control);
