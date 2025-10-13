/*
 *   tags_window.c
 *   Copyright (C) 2025 David García Goñi <dagargo@gmail.com>
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

#include "tags_window.h"
#include "audio.h"
#include "editor.h"
#include "utils.h"

// This uses the same separator as the IKEY in the LIST INFO chunk.
// Structured here but will be alphabetically sorted in the editor.
#define TAGS_INSTRUMENTS       "kick; snare; clap; tom; percussion; hi-hat; cymbal"
#define TAGS_DURATION          "loop; one-shot"
#define TAGS_SOUND_DESCRIPTION "ambient; nature; percussion; electronic; acoustic; string; pluck; stab; noise"
#define TAGS_EMOTIONS          "hard; soft; dark; bright; moody; gloomy"

static GtkWindow *window;
static GtkWidget *flow_box;
static GtkWidget *accept_button;
static GtkWidget *cancel_button;
static GHashTable *tags;
static enum tag_source tag_source;

static void
tags_window_cancel (GtkWidget *object, gpointer data)
{
  if (tags)
    {
      g_hash_table_unref (tags);
      tags = NULL;
    }
  gtk_widget_hide (GTK_WIDGET (window));
}

static void
tags_window_save (GtkWidget *object, gpointer data)
{
  struct sample_info *sample_info = audio.sample.info;

  if (g_hash_table_size (tags))
    {
      gchar *ikey = tags_to_ikey_format (tags);
      sample_info_set_tag (sample_info, SAMPLE_INFO_TAG_IKEY, ikey);
    }
  else
    {
      sample_info_set_tag (sample_info, SAMPLE_INFO_TAG_IKEY, NULL);
    }

  editor_set_dirty (TRUE);
  editor_update_tags ();

  tags_window_cancel (NULL, NULL);
}

static gboolean
tags_window_delete (GtkWidget *widget, GdkEvent *event, gpointer data)
{
  tags_window_cancel (NULL, NULL);
  return TRUE;
}

static gboolean
tags_window_key_press (GtkWidget *widget, GdkEventKey *event, gpointer data)
{
  if (event->keyval == GDK_KEY_Escape)
    {
      tags_window_cancel (NULL, NULL);
      return TRUE;
    }
  return FALSE;
}

void
tags_window_init (GtkBuilder *builder)
{
  window = GTK_WINDOW (gtk_builder_get_object (builder, "tags_window"));
  flow_box =
    GTK_WIDGET (gtk_builder_get_object (builder, "tags_window_flow_box"));
  accept_button =
    GTK_WIDGET (gtk_builder_get_object
		(builder, "tags_window_accept_button"));
  cancel_button =
    GTK_WIDGET (gtk_builder_get_object
		(builder, "tags_window_cancel_button"));

  g_signal_connect (accept_button, "clicked",
		    G_CALLBACK (tags_window_save), NULL);
  g_signal_connect (cancel_button, "clicked",
		    G_CALLBACK (tags_window_cancel), NULL);
  g_signal_connect (GTK_WIDGET (window), "delete-event",
		    G_CALLBACK (tags_window_delete), NULL);
  g_signal_connect (GTK_WIDGET (window), "key_press_event",
		    G_CALLBACK (tags_window_key_press), NULL);
}

static gchar *
tags_get_label_css (enum tag_source tag_source)
{
  switch (tag_source)
    {
    case TAG_SOURCE_LOCAL:
      return "local_tag_label";
    case TAG_SOURCE_REMOTE:
      return "remote_tag_label";
    default:
      return "none_tag_label";
    }
}

GtkWidget *
tags_label_new (const gchar *name, enum tag_source tag_source)
{
  GtkWidget *tag = gtk_label_new (name);
  const gchar *class = tags_get_label_css (tag_source);
  GtkStyleContext *context = gtk_widget_get_style_context (tag);

  gtk_widget_set_visible (tag, TRUE);
  gtk_widget_set_valign (tag, GTK_ALIGN_CENTER);
  gtk_style_context_add_class (context, class);

  return tag;
}

static gchar *
tags_get_toggle_css (enum tag_source tag_source)
{
  switch (tag_source)
    {
    case TAG_SOURCE_LOCAL:
      return "local_tag_button";
    case TAG_SOURCE_REMOTE:
      return "remote_tag_button";
    default:
      return "none_tag_button";
    }
}

static void
tags_toggle_data_closure_notify (gpointer data, GClosure *closure)
{
  g_free (data);
}

static void
tags_toggle_button_clicked (GtkWidget *button, gpointer data)
{
  gchar *tag = data;
  gboolean active = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (button));

  debug_print (2, "Setting tag '%s' to %d...", tag, active);
  if (active)
    {
      g_hash_table_add (tags, g_strdup (tag));
    }
  else
    {
      g_hash_table_remove (tags, tag);
    }
}

static GtkWidget *
tags_toggle_new (const gchar *tag, enum tag_source tag_source)
{
  GtkWidget *toggle = gtk_toggle_button_new_with_label (tag);
  const gchar *class = tags_get_toggle_css (tag_source);
  GtkStyleContext *context = gtk_widget_get_style_context (toggle);

  gtk_widget_set_visible (toggle, TRUE);
  gtk_widget_set_valign (toggle, GTK_ALIGN_CENTER);
  gtk_style_context_add_class (context, class);

  g_signal_connect_data (toggle, "clicked",
			 G_CALLBACK (tags_toggle_button_clicked),
			 strdup (tag),
			 tags_toggle_data_closure_notify, G_CONNECT_DEFAULT);

  return toggle;
}

void
tags_window_open (enum tag_source tag_source_)
{
  const gchar *ikey_tags;
  GHashTable *sample_tags, *editor_tags, *all_tags;
  struct sample_info *sample_info = audio.sample.info;

  tag_source = tag_source_;

  tags = g_hash_table_new (g_str_hash, g_str_equal);

  ikey_tags = sample_info_get_tag (sample_info, SAMPLE_INFO_TAG_IKEY);
  sample_tags = ikey_format_to_tags (ikey_tags);
  editor_tags =
    ikey_format_to_tags (TAGS_INSTRUMENTS "; " TAGS_DURATION "; "
			 TAGS_SOUND_DESCRIPTION "; " TAGS_EMOTIONS);
  all_tags = g_hash_table_new (g_str_hash, g_str_equal);

  tags_add (all_tags, sample_tags);
  tags_add (all_tags, editor_tags);

  g_hash_table_unref (editor_tags);

  GList *keys = g_hash_table_get_keys (all_tags);
  keys = g_list_sort (keys, (GCompareFunc) g_strcmp0);

  tags_clear_container (flow_box);

  GList *tag = keys;
  while (tag)
    {
      gboolean active;
      GtkWidget *tag_button;
      const gchar *tag_name = tag->data;

      debug_print (2, "Adding tag for '%s'...", tag_name);

      active = g_hash_table_contains (sample_tags, tag_name);
      tag_button = tags_toggle_new (tag_name, tag_source);
      gtk_flow_box_insert (GTK_FLOW_BOX (flow_box), tag_button, -1);
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (tag_button), active);

      tag = g_list_next (tag);
    }
  g_list_free (keys);

  g_hash_table_unref (all_tags);
  g_hash_table_unref (sample_tags);

  gtk_widget_show (GTK_WIDGET (window));
}

void
tags_window_destroy ()
{
  debug_print (1, "Destroying tags window...");
  tags_window_cancel (NULL, NULL);
  gtk_widget_destroy (GTK_WIDGET (window));
}

static void
utils_gtk_container_remove (GtkWidget *widget, gpointer data)
{
  gtk_container_remove (GTK_CONTAINER (data), widget);
}

void
tags_clear_container (GtkWidget *container)
{
  gtk_container_foreach (GTK_CONTAINER (container),
			 utils_gtk_container_remove, container);
}
