/*
 *   preferences.c
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

#include <sys/stat.h>
#include <glib.h>
#include <json-glib/json-glib.h>
#include "preferences.h"
#include "utils.h"

#define PREFERENCES_FILE "/preferences.json"

#define MEMBER_AUTOPLAY "autoplay"
#define MEMBER_MIX "mix"
#define MEMBER_LOCALDIR "localDir"
#define MEMBER_SHOWREMOTE "showRemote"

gint
preferences_save (struct preferences *preferences)
{
  gchar *preferences_path;
  JsonBuilder *builder;
  JsonGenerator *gen;
  JsonNode *root;
  gchar *json;

  preferences_path = get_user_dir (CONF_DIR);
  if (g_mkdir_with_parents (preferences_path,
			    S_IFDIR | S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH |
			    S_IXOTH))
    {
      error_print ("Error wile creating directory `%s'\n", preferences_path);
      return 1;
    }

  g_free (preferences_path);
  preferences_path = get_user_dir (CONF_DIR PREFERENCES_FILE);

  debug_print (1, "Saving preferences to '%s'...\n", preferences_path);

  builder = json_builder_new ();

  json_builder_begin_object (builder);

  json_builder_set_member_name (builder, MEMBER_AUTOPLAY);
  json_builder_add_boolean_value (builder, preferences->autoplay);

  json_builder_set_member_name (builder, MEMBER_MIX);
  json_builder_add_boolean_value (builder, preferences->mix);

  json_builder_set_member_name (builder, MEMBER_SHOWREMOTE);
  json_builder_add_boolean_value (builder, preferences->show_remote);

  json_builder_set_member_name (builder, MEMBER_LOCALDIR);
  json_builder_add_string_value (builder, preferences->local_dir);

  json_builder_end_object (builder);

  gen = json_generator_new ();
  root = json_builder_get_root (builder);
  json_generator_set_root (gen, root);
  json = json_generator_to_data (gen, NULL);

  save_file_char (preferences_path, (guint8 *) json, strlen (json));

  g_free (json);
  json_node_free (root);
  g_object_unref (gen);
  g_object_unref (builder);
  g_free (preferences_path);

  return 0;
}

gint
preferences_load (struct preferences *preferences)
{
  GError *error;
  JsonReader *reader;
  JsonParser *parser = json_parser_new ();
  gchar *preferences_file = get_user_dir (CONF_DIR PREFERENCES_FILE);

  error = NULL;
  json_parser_load_from_file (parser, preferences_file, &error);
  if (error)
    {
      debug_print (1, "Error wile loading preferences from `%s': %s\n",
		   preferences_file, error->message);
      g_error_free (error);
      g_object_unref (parser);
      g_free (preferences_file);
      preferences->autoplay = TRUE;
      preferences->mix = TRUE;
      preferences->show_remote = TRUE;
      preferences->local_dir = get_user_dir (NULL);
      return 0;
    }

  debug_print (1, "Loading preferences from '%s'...\n", preferences_file);

  reader = json_reader_new (json_parser_get_root (parser));

  if (json_reader_read_member (reader, MEMBER_AUTOPLAY))
    {
      preferences->autoplay = json_reader_get_boolean_value (reader);
    }
  else
    {
      preferences->autoplay = TRUE;
    }
  json_reader_end_member (reader);

  if (json_reader_read_member (reader, MEMBER_MIX))
    {
      preferences->mix = json_reader_get_boolean_value (reader);
    }
  else
    {
      preferences->mix = TRUE;
    }
  json_reader_end_member (reader);

  if (json_reader_read_member (reader, MEMBER_SHOWREMOTE))
    {
      preferences->show_remote = json_reader_get_boolean_value (reader);
    }
  else
    {
      preferences->show_remote = TRUE;
    }
  json_reader_end_member (reader);

  if (json_reader_read_member (reader, MEMBER_LOCALDIR) &&
      g_file_test (json_reader_get_string_value (reader),
		   (G_FILE_TEST_EXISTS | G_FILE_TEST_IS_DIR)))
    {
      preferences->local_dir =
	g_strdup (json_reader_get_string_value (reader));
    }
  else
    {
      preferences->local_dir = get_user_dir (NULL);
    }
  json_reader_end_member (reader);

  g_object_unref (reader);
  g_object_unref (parser);

  g_free (preferences_file);

  return 0;
}

void
preferences_free (struct preferences *preferences)
{
  g_free (preferences->local_dir);
}
