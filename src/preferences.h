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

//Preferences need to be here as need to be available from the CLI even though some are not accessible from the CLI.
#define PREF_KEY_LOCAL_DIR "localDir"
#define PREF_KEY_REMOTE_DIR "remoteDir"	//Only used in system filesystems.
#define PREF_KEY_SHOW_REMOTE "showRemote"
#define PREF_KEY_AUTOPLAY "autoplay"
#define PREF_KEY_MIX "mix"
#define PREF_KEY_SUBDIVISIONS "subdivisions"
#define PREF_KEY_PLAY_WHILE_LOADING "playSampleWhileLoading"
#define PREF_KEY_AUDIO_BUFFER_LEN "audioBufferLength"
#define PREF_KEY_AUDIO_USE_FLOAT "audioUseFloat"
#define PREF_KEY_SHOW_PLAYBACK_CURSOR "showPlaybackCursor"
#define PREF_KEY_TAGS_STRUCTURES "tagsStructures"
#define PREF_KEY_TAGS_INSTRUMENTS "tagsInstruments"
#define PREF_KEY_TAGS_GENRES "tagsGenres"
#define PREF_KEY_TAGS_OBJECTIVE_CHARS "tagsObjectiveCharacteristics"
#define PREF_KEY_TAGS_SUBJECTIVE_CHARS "tagsSubjectiveCharacteristics"

enum preference_type
{
  PREFERENCE_TYPE_BOOLEAN,
  PREFERENCE_TYPE_INT,
  PREFERENCE_TYPE_STRING
};

typedef gpointer (*preference_get_value_f) (const gpointer);

struct preference
{
  gchar *key;
  enum preference_type type;
  preference_get_value_f get_value;
};

extern GSList *preferences;
extern GHashTable *preferences_hashtable;

gpointer preferences_get_boolean_value_true (const gpointer b);

gpointer preferences_get_boolean_value_false (const gpointer b);

gpointer preferences_get_string_value_default (const gpointer s,
					       const gchar * defaults);

gpointer preferences_get_int_value (const gpointer in, gint max, gint min,
				    gint def);

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
