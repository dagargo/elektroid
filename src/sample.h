/*
 *   sample.h
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
#include <sys/types.h>
#include <sys/stat.h>
#include "utils.h"

#ifndef SAMPLE_H
#define SAMPLE_H

#define LOAD_BUFFER_LEN 4800	// In guint16 frames; 9600 B; 0.1 ms

gint sample_save (GByteArray *, const gchar *, struct job_control *);

gint sample_load (GByteArray *, const gchar *, struct job_control *);

gint sample_load_with_frames (GByteArray *, const gchar *,
			      struct job_control *, guint *);

#endif
