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

GSList *preferences;
static GHashTable *preferences_hashtable;

static gpointer
preferences_get_boolean_value (const gpointer in, gboolean def_value)
{
  gboolean *out = g_malloc (sizeof (gboolean));
  if (in)
    {
      *out = *(gboolean *) in;
    }
  else
    {
      *out = def_value;
    }
  return out;
}

gpointer
preferences_get_boolean_value_true (const gpointer b)
{
  return preferences_get_boolean_value (b, TRUE);
}

gpointer
preferences_get_boolean_value_false (const gpointer b)
{
  return preferences_get_boolean_value (b, FALSE);
}

gpointer
preferences_get_int_value (const gpointer in, gint max, gint min, gint def)
{
  gint *out = g_malloc (sizeof (gint));
  if (in)
    {
      *out = *(gint *) in;
      if (*out > max || *out < min)
	{
	  *out = def;
	}
    }
  else
    {
      *out = def;
    }
  return out;
}

static void
preferences_set_value (const struct preference *p, JsonBuilder *builder)
{
  gpointer v = g_hash_table_lookup (preferences_hashtable, p->key);

  json_builder_set_member_name (builder, p->key);

  if (p->type == PREFERENCE_TYPE_BOOLEAN)
    {
      json_builder_add_boolean_value (builder, *(gboolean *) v);
    }
  else if (p->type == PREFERENCE_TYPE_INT)
    {
      json_builder_add_int_value (builder, *(gint *) v);
    }
  else if (p->type == PREFERENCE_TYPE_STRING)
    {
      json_builder_add_string_value (builder, (gchar *) v);
    }
  else
    {
      error_print ("Illegal type");	//This should never happen as the application exists when loading the values for the same reason.
    }
}

gint
preferences_save ()
{
  gchar *preferences_path;
  JsonBuilder *builder;
  JsonGenerator *gen;
  JsonNode *root;
  gchar *json;
  GSList *l;

  preferences_path = get_user_dir (CONF_DIR);
  if (g_mkdir_with_parents (preferences_path,
			    S_IFDIR | S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH |
			    S_IXOTH))
    {
      error_print ("Error wile creating directory `%s'", preferences_path);
      return 1;
    }

  g_free (preferences_path);
  preferences_path = get_user_dir (CONF_DIR PREFERENCES_FILE);

  debug_print (1, "Saving preferences to '%s'...", preferences_path);

  builder = json_builder_new ();

  json_builder_begin_object (builder);

  l = preferences;
  while (l)
    {
      const struct preference *p = l->data;
      debug_print (1, "Saving preference '%s'...", p->key);
      preferences_set_value (p, builder);
      l = l->next;
    }

  json_builder_end_object (builder);

  gen = json_generator_new ();
  root = json_builder_get_root (builder);
  json_generator_set_root (gen, root);
  json = json_generator_to_data (gen, NULL);

  file_save_data (preferences_path, (guint8 *) json, strlen (json));

  g_free (json);
  json_node_free (root);
  g_object_unref (gen);
  g_object_unref (builder);
  g_free (preferences_path);

  return 0;
}

static gpointer
preferences_get_value (const struct preference *p, JsonReader *reader)
{
  gpointer v;

  if (json_reader_read_member (reader, p->key))
    {
      if (p->type == PREFERENCE_TYPE_BOOLEAN)
	{
	  gboolean b = json_reader_get_boolean_value (reader);
	  v = p->get_value (&b);
	}
      else if (p->type == PREFERENCE_TYPE_INT)
	{
	  gint i = json_reader_get_int_value (reader);
	  v = p->get_value (&i);
	}
      else if (p->type == PREFERENCE_TYPE_STRING)
	{
	  gchar *s = (gchar *) json_reader_get_string_value (reader);
	  v = p->get_value (s);
	}
      else
	{
	  error_print ("Illegal type");
	  exit (EXIT_FAILURE);
	}
    }
  else
    {
      v = p->get_value (NULL);
    }
  json_reader_end_member (reader);

  return v;
}

gint
preferences_load ()
{
  GError *error;
  JsonReader *reader;
  GSList *l;
  JsonParser *parser = json_parser_new ();
  gchar *preferences_file = get_user_dir (CONF_DIR PREFERENCES_FILE);

  //Keys need to be static constants defined only in one place
  preferences_hashtable = g_hash_table_new_full (g_direct_hash,
						 g_direct_equal, NULL,
						 g_free);

  error = NULL;
  json_parser_load_from_file (parser, preferences_file, &error);
  if (error)
    {
      debug_print (1, "Error wile loading preferences from `%s': %s",
		   preferences_file, error->message);
      g_error_free (error);
      g_object_unref (parser);
      g_free (preferences_file);

      l = preferences;
      while (l)
	{
	  gpointer v;
	  const struct preference *p = l->data;
	  debug_print (1, "Initializing preference '%s'...", p->key);
	  v = p->get_value (NULL);
	  g_hash_table_insert (preferences_hashtable, p->key, v);
	  l = l->next;
	}

      return 0;
    }

  debug_print (1, "Loading preferences from '%s'...", preferences_file);

  reader = json_reader_new (json_parser_get_root (parser));

  l = preferences;
  while (l)
    {
      gpointer v;
      const struct preference *p = l->data;
      debug_print (1, "Loading preference '%s'...", p->key);
      v = preferences_get_value (p, reader);
      g_hash_table_insert (preferences_hashtable, p->key, v);
      l = l->next;
    }

  g_object_unref (reader);
  g_object_unref (parser);

  g_free (preferences_file);

  return 0;
}

void
preferences_free ()
{
  g_hash_table_unref (preferences_hashtable);
}

gboolean
preferences_get_boolean (const gchar *key)
{
  gboolean *v = g_hash_table_lookup (preferences_hashtable, key);
  return *v;
}

gint
preferences_get_int (const gchar *key)
{
  gint *v = g_hash_table_lookup (preferences_hashtable, key);
  return *v;
}

const gchar *
preferences_get_string (const gchar *key)
{
  return (gchar *) g_hash_table_lookup (preferences_hashtable, key);
}

void
preferences_set_boolean (const gchar *key, gboolean v)
{
  gboolean *p = g_malloc (sizeof (gint));
  *p = v;
  g_hash_table_insert (preferences_hashtable, (gpointer) key, p);
}

void
preferences_set_int (const gchar *key, gint v)
{
  gint *p = g_malloc (sizeof (gint));
  *p = v;
  g_hash_table_insert (preferences_hashtable, (gpointer) key, p);
}

void
preferences_set_string (const char *key, gchar *v)
{
  g_hash_table_insert (preferences_hashtable, (gpointer) key, v);
}
