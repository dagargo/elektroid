/*
 *   regpref.c
 *   Copyright (C) 2024 David García Goñi <dagargo@gmail.com>
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

#include "regpref.h"
#include "backend.h"
#include "connectors/elektron.h"

#define PREF_DEFAULT_GRID_LENGTH 16
#define PREF_MAX_GRID_LENGTH 64
#define PREF_MIN_GRID_LENGTH 2

#define PREF_DEFAULT_AUDIO_BUF_LENGTH 256
#define PREF_MAX_AUDIO_BUF_LENGTH 4096
#define PREF_MIN_AUDIO_BUF_LENGTH 256

static gpointer
regpref_get_grid (const gpointer grid)
{
  return preferences_get_int_value (grid, PREF_MAX_GRID_LENGTH,
				    PREF_MIN_GRID_LENGTH,
				    PREF_DEFAULT_GRID_LENGTH);
}

static gpointer
regpref_get_audio_buffer_length (const gpointer grid)
{
  return preferences_get_int_value (grid, PREF_MAX_AUDIO_BUF_LENGTH,
				    PREF_MIN_AUDIO_BUF_LENGTH,
				    PREF_DEFAULT_AUDIO_BUF_LENGTH);
}

static gpointer
regpref_get_home (const gpointer home)
{
  return home ? g_strdup (home) : get_user_dir (NULL);
}

static const struct preference PREF_LOCAL_DIR = {
  .key = PREF_KEY_LOCAL_DIR,
  .type = PREFERENCE_TYPE_STRING,
  .get_value = regpref_get_home
};

static const struct preference PREF_REMOTE_DIR = {
  .key = PREF_KEY_REMOTE_DIR,
  .type = PREFERENCE_TYPE_STRING,
  .get_value = regpref_get_home
};

const struct preference PREF_SHOW_REMOTE = {
  .key = PREF_KEY_SHOW_REMOTE,
  .type = PREFERENCE_TYPE_BOOLEAN,
  .get_value = preferences_get_boolean_value_true
};

static const struct preference PREF_AUTOPLAY = {
  .key = PREF_KEY_AUTOPLAY,
  .type = PREFERENCE_TYPE_BOOLEAN,
  .get_value = preferences_get_boolean_value_true
};

static const struct preference PREF_MIX = {
  .key = PREF_KEY_MIX,
  .type = PREFERENCE_TYPE_BOOLEAN,
  .get_value = preferences_get_boolean_value_false
};

static const struct preference PREF_SHOW_GRID = {
  .key = PREF_KEY_SHOW_GRID,
  .type = PREFERENCE_TYPE_BOOLEAN,
  .get_value = preferences_get_boolean_value_false
};

static const struct preference PREF_GRID_LENGTH = {
  .key = PREF_KEY_GRID_LENGTH,
  .type = PREFERENCE_TYPE_INT,
  .get_value = regpref_get_grid
};

static const struct preference PREF_PLAY_WHILE_LOADING = {
  .key = PREF_KEY_PLAY_WHILE_LOADING,
  .type = PREFERENCE_TYPE_BOOLEAN,
  .get_value = preferences_get_boolean_value_true
};

static const struct preference PREF_AUDIO_BUFFER_LEN = {
  .key = PREF_KEY_AUDIO_BUFFER_LEN,
  .type = PREFERENCE_TYPE_INT,
  .get_value = regpref_get_audio_buffer_length
};

void
regpref_register ()
{
  gslist_fill (&preferences, &PREF_LOCAL_DIR, &PREF_REMOTE_DIR,
	       &PREF_SHOW_REMOTE, &PREF_AUTOPLAY, &PREF_MIX, &PREF_SHOW_GRID,
	       &PREF_GRID_LENGTH, &PREF_PLAY_WHILE_LOADING,
	       &PREF_AUDIO_BUFFER_LEN, &PREF_BE_STOP_WHEN_CONNECTING,
	       &PREF_ELEKTRON_LOAD_SOUND_TAGS, NULL);
}

void
regpref_unregister ()
{
  g_slist_free (g_steal_pointer (&preferences));
}
