/*
 *   config.c
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
#include <config.h>
#include <wordexp.h>
#include "utils.h"

#define CONF_DIR "~/.config/elektroid"
#define CONF_FILE "/preferences"

#define MEMBER_AUTOPLAY "autoplay"
#define MEMBER_LOCALDIR "localDir"

gint
config_save (struct config *config)
{
  size_t n;
  gchar *config_path;
  JsonBuilder *builder;
  JsonGenerator *gen;
  JsonNode *root;
  gchar *json;

  config_path = get_expanded_dir (CONF_DIR);
  if (g_mkdir_with_parents (config_path,
			    S_IFDIR | S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH |
			    S_IXOTH))
    {
      return 1;
    }

  n = PATH_MAX - strlen (config_path) - 1;
  strncat (config_path, CONF_FILE, n);
  config_path[PATH_MAX - 1] = 0;

  debug_print (1, "Saving preferences to '%s'...\n", config_path);

  builder = json_builder_new ();

  json_builder_begin_object (builder);

  json_builder_set_member_name (builder, MEMBER_AUTOPLAY);
  json_builder_add_boolean_value (builder, config->autoplay);

  json_builder_set_member_name (builder, MEMBER_LOCALDIR);
  json_builder_add_string_value (builder, config->local_dir);

  json_builder_end_object (builder);

  gen = json_generator_new ();
  root = json_builder_get_root (builder);
  json_generator_set_root (gen, root);
  json = json_generator_to_data (gen, NULL);

  save_file_char (config_path, (guint8 *) json, strlen (json));

  g_free (json);
  json_node_free (root);
  g_object_unref (gen);
  g_object_unref (builder);
  g_free (config_path);

  return 0;
}

gint
config_load (struct config *config)
{
  size_t n;
  GError *error;
  JsonReader *reader;
  JsonParser *parser = json_parser_new ();
  gchar *config_file = get_expanded_dir (CONF_DIR CONF_FILE);

  error = NULL;
  json_parser_load_from_file (parser, config_file, &error);
  if (error)
    {
      error_print ("Error wile loading preferences from `%s': %s\n",
		   CONF_DIR CONF_FILE, error->message);
      g_error_free (error);
      g_object_unref (parser);
      config->autoplay = TRUE;
      config->local_dir = get_expanded_dir ("~");
      return 0;
    }

  reader = json_reader_new (json_parser_get_root (parser));
  json_reader_read_member (reader, MEMBER_AUTOPLAY);
  config->autoplay = json_reader_get_boolean_value (reader);
  json_reader_end_member (reader);
  json_reader_read_member (reader, MEMBER_LOCALDIR);
  config->local_dir = malloc (PATH_MAX);
  n = PATH_MAX - 1;
  strncpy (config->local_dir, json_reader_get_string_value (reader), n);
  config->local_dir[PATH_MAX - 1] = 0;
  json_reader_end_member (reader);
  g_object_unref (reader);
  g_object_unref (parser);

  g_free (config_file);

  return 0;
}

void
config_free (struct config *config)
{
  g_free (config->local_dir);
}
