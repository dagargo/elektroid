/*
 *   preferences.h
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

#ifndef PREFERENCES_H
#define PREFERENCES_H

#include <glib.h>

struct preferences
{
  gboolean autoplay;
  gboolean mix;
  gboolean show_remote;
  gchar *local_dir;
  gchar *remote_dir;
  gboolean show_grid;
  gint grid_length;
};

gint preferences_save (struct preferences *);

gint preferences_load (struct preferences *);

void preferences_free (struct preferences *);

#endif
