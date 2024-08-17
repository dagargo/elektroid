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

#define PREF_KEY_AUTOPLAY "autoplay"
#define PREF_KEY_MIX "mix"
#define PREF_KEY_LOCAL_DIR "localDir"
#define PREF_KEY_REMOTE_DIR "remoteDir"	//Only used in system filesystems.
#define PREF_KEY_SHOW_REMOTE "showRemote"
#define PREF_KEY_SHOW_GRID "showGrid"
#define PREF_KEY_GRID_LENGTH "gridLength"

gint preferences_save ();

gint preferences_load ();

void preferences_free ();

gboolean preferences_get_boolean (const gchar * key);

gint preferences_get_int (const gchar * key);

const gchar *preferences_get_string (const gchar * key);

void preferences_set_boolean (const gchar * key, gboolean v);

void preferences_set_int (const gchar * key, gint v);

void preferences_set_string (const gchar * key, gchar * s);

#endif
