/*
 *   preferences_window.c
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

#include "backend.h"
#include "editor.h"
#include "preferences.h"
#include "preferences_window.h"
#include "regpref.h"

static GtkWindow *window;
static GtkWidget *audio_buffer_length_combo;
static GtkWidget *audio_use_float_switch;
static GtkWidget *play_sample_while_loading_switch;
static GtkWidget *show_playback_cursor_switch;
static GtkWidget *stop_device_when_connecting_switch;
static GtkWidget *elektron_load_sound_tags_switch;
static GtkWidget *save_button;
static GtkWidget *cancel_button;

static void
preferences_window_cancel (GtkWidget *object, gpointer data)
{
  gtk_widget_hide (GTK_WIDGET (window));
}

static gboolean
preferences_window_delete (GtkWidget *widget, GdkEvent *event, gpointer data)
{
  preferences_window_cancel (NULL, NULL);
  return FALSE;
}

static void
preferences_window_save (GtkWidget *object, gpointer data)
{
  GtkTreeIter iter;
  GValue x = G_VALUE_INIT;
  gboolean b, float_prev, float_post;
  gint i, j, buffer_len_prev, buffer_len_post;
  GtkTreeModel *model =
    gtk_combo_box_get_model (GTK_COMBO_BOX (audio_buffer_length_combo));

  buffer_len_prev = preferences_get_int (PREF_KEY_AUDIO_BUFFER_LEN);
  float_prev = preferences_get_boolean (PREF_KEY_AUDIO_USE_FLOAT);

  b = gtk_switch_get_active (GTK_SWITCH (audio_use_float_switch));
  preferences_set_boolean (PREF_KEY_AUDIO_USE_FLOAT, b);
  b = gtk_switch_get_active (GTK_SWITCH (play_sample_while_loading_switch));
  preferences_set_boolean (PREF_KEY_PLAY_WHILE_LOADING, b);
  b = gtk_switch_get_active (GTK_SWITCH (show_playback_cursor_switch));
  preferences_set_boolean (PREF_KEY_SHOW_PLAYBACK_CURSOR, b);
  b = gtk_switch_get_active (GTK_SWITCH (stop_device_when_connecting_switch));
  preferences_set_boolean (PREF_KEY_STOP_DEVICE_WHEN_CONNECTING, b);
  b = gtk_switch_get_active (GTK_SWITCH (elektron_load_sound_tags_switch));
  preferences_set_boolean (PREF_KEY_ELEKTRON_LOAD_SOUND_TAGS, b);

  i = gtk_combo_box_get_active (GTK_COMBO_BOX (audio_buffer_length_combo));

  gtk_tree_model_get_iter_first (model, &iter);
  for (j = 0; j < i; j++)
    {
      gtk_tree_model_iter_next (model, &iter);
    }

  gtk_tree_model_get_value (model, &iter, 0, &x);

  buffer_len_post = g_value_get_int (&x);

  preferences_set_int (PREF_KEY_AUDIO_BUFFER_LEN, buffer_len_post);
  g_value_unset (&x);

  float_post = preferences_get_boolean (PREF_KEY_AUDIO_USE_FLOAT);

  if (buffer_len_prev != buffer_len_post || float_prev != float_post)
    {
      editor_reset_audio ();
    }

  preferences_window_cancel (NULL, NULL);
}

static gboolean
preferences_window_key_press (GtkWidget *widget, GdkEventKey *event,
			      gpointer data)
{
  if (event->keyval == GDK_KEY_Escape)
    {
      preferences_window_cancel (NULL, NULL);
      return TRUE;
    }
  return FALSE;
}

void
preferences_window_open ()
{
  gboolean b;
  GtkTreeIter iter;
  gint i, buffer_len;
  GValue x = G_VALUE_INIT;
  GtkTreeModel *model =
    gtk_combo_box_get_model (GTK_COMBO_BOX (audio_buffer_length_combo));

  b = preferences_get_boolean (PREF_KEY_AUDIO_USE_FLOAT);
  gtk_switch_set_active (GTK_SWITCH (audio_use_float_switch), b);
  b = preferences_get_boolean (PREF_KEY_PLAY_WHILE_LOADING);
  gtk_switch_set_active (GTK_SWITCH (play_sample_while_loading_switch), b);
  b = preferences_get_boolean (PREF_KEY_SHOW_PLAYBACK_CURSOR);
  gtk_switch_set_active (GTK_SWITCH (show_playback_cursor_switch), b);
  b = preferences_get_boolean (PREF_KEY_STOP_DEVICE_WHEN_CONNECTING);
  gtk_switch_set_active (GTK_SWITCH (stop_device_when_connecting_switch), b);
  b = preferences_get_boolean (PREF_KEY_ELEKTRON_LOAD_SOUND_TAGS);
  gtk_switch_set_active (GTK_SWITCH (elektron_load_sound_tags_switch), b);

  buffer_len = preferences_get_int (PREF_KEY_AUDIO_BUFFER_LEN);

  gtk_tree_model_get_iter_first (model, &iter);

  i = 0;
  do
    {
      gtk_tree_model_get_value (model, &iter, 0, &x);
      if (g_value_get_int (&x) == buffer_len)
	{
	  gtk_combo_box_set_active (GTK_COMBO_BOX (audio_buffer_length_combo),
				    i);
	}
      i++;
      g_value_unset (&x);
    }
  while (gtk_tree_model_iter_next (model, &iter));

  gtk_widget_show (GTK_WIDGET (window));
}

void
preferences_window_init (GtkBuilder *builder)
{
  window =
    GTK_WINDOW (gtk_builder_get_object (builder, "preferences_window"));
  save_button =
    GTK_WIDGET (gtk_builder_get_object
		(builder, "preferences_window_save_button"));
  cancel_button =
    GTK_WIDGET (gtk_builder_get_object
		(builder, "preferences_window_cancel_button"));

  audio_buffer_length_combo =
    GTK_WIDGET (gtk_builder_get_object (builder,
					"audio_buffer_length_combo"));
  audio_use_float_switch =
    GTK_WIDGET (gtk_builder_get_object (builder, "audio_use_float_switch"));
  play_sample_while_loading_switch =
    GTK_WIDGET (gtk_builder_get_object (builder,
					"play_sample_while_loading_switch"));
  show_playback_cursor_switch =
    GTK_WIDGET (gtk_builder_get_object (builder,
					"show_playback_cursor_switch"));
  stop_device_when_connecting_switch =
    GTK_WIDGET (gtk_builder_get_object (builder,
					"stop_device_when_connecting_switch"));
  elektron_load_sound_tags_switch =
    GTK_WIDGET (gtk_builder_get_object (builder,
					"elektron_load_sound_tags_switch"));

  g_signal_connect (save_button, "clicked",
		    G_CALLBACK (preferences_window_save), NULL);
  g_signal_connect (cancel_button, "clicked",
		    G_CALLBACK (preferences_window_cancel), NULL);
  g_signal_connect (GTK_WIDGET (window), "delete-event",
		    G_CALLBACK (preferences_window_delete), NULL);
  g_signal_connect (GTK_WIDGET (window), "key_press_event",
		    G_CALLBACK (preferences_window_key_press), NULL);
}

void
preferences_window_destroy ()
{
  debug_print (1, "Destroying preferences window...");
  gtk_widget_destroy (GTK_WIDGET (window));
}
