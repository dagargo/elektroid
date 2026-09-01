/*
 *   name_window.c
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

#include "name_window.h"
#include "utils.h"

static GtkWindow *window;
static GtkEntry *entry;
static GtkWidget *accept_button;
static GtkWidget *cancel_button;
static gpointer source;
static name_window_accept_cb accept_cb;

static void
name_window_show (const gchar *title, gint max_len, const gchar *text,
		  gint sel_start, gint sel_end, gboolean sensitive,
		  name_window_accept_cb accept_cb_, gpointer source_)
{
  accept_cb = accept_cb_;
  source = source_;

  gtk_window_set_title (window, title);

  gtk_entry_set_max_length (entry, max_len);
  gtk_editable_set_text (GTK_EDITABLE (entry), text);
  gtk_widget_grab_focus (GTK_WIDGET (entry));
  gtk_editable_select_region (GTK_EDITABLE (entry), sel_start, sel_end);
  gtk_widget_set_sensitive (accept_button, sensitive);

  gtk_widget_set_visible (GTK_WIDGET (window), TRUE);
}

void
name_window_edit_text (const gchar *title, gint max_len, const gchar *text,
		       gint sel_start, gint sel_end,
		       name_window_accept_cb accept_cb, gpointer source)
{
  name_window_show (title, max_len, text, sel_start, sel_end, TRUE, accept_cb,
		    source);
}

void
name_window_new_text (const gchar *title, gint max_len,
		      name_window_accept_cb accept_cb, gpointer source)
{
  name_window_show (title, max_len, "", 0, 0, FALSE, accept_cb, source);
}

static void
name_window_cancel (GtkWidget *object, gpointer data)
{
  gtk_widget_set_visible (GTK_WIDGET (window), FALSE);
}

static gboolean
name_window_close (GtkWindow *window, gpointer data)
{
  name_window_cancel (NULL, NULL);
  return TRUE;
}

static void
name_window_accept (GtkWidget *object, gpointer data)
{
  const gchar *text = gtk_editable_get_text (GTK_EDITABLE (entry));
  name_window_cancel (cancel_button, NULL);
  accept_cb (source, text);
}

static void
name_window_entry_changed (GtkWidget *object, gpointer data)
{
  const gchar *text = gtk_editable_get_text (GTK_EDITABLE (entry));
  gsize len = strlen (text);
  gtk_widget_set_sensitive (accept_button, len > 0);
}

static gboolean
name_window_on_key_pressed (GtkEventControllerKey *controller,
			    guint keyval, guint keycode,
			    GdkModifierType state, gpointer user_data)
{
  if (keyval == GDK_KEY_Escape)
    {
      gtk_window_close (window);
      return TRUE;
    }

  return FALSE;
}

void
name_window_init (GtkBuilder *builder)
{
  window = GTK_WINDOW (gtk_builder_get_object (builder, "name_window"));
  accept_button = GTK_WIDGET (gtk_builder_get_object
			      (builder, "name_window_accept_button"));
  cancel_button = GTK_WIDGET (gtk_builder_get_object
			      (builder, "name_window_cancel_button"));
  entry = GTK_ENTRY (gtk_builder_get_object (builder, "name_window_entry"));

  g_signal_connect (entry, "changed", G_CALLBACK (name_window_entry_changed),
		    NULL);
  g_signal_connect (accept_button, "clicked", G_CALLBACK (name_window_accept),
		    NULL);
  g_signal_connect (cancel_button, "clicked", G_CALLBACK (name_window_cancel),
		    NULL);
  g_signal_connect (GTK_WIDGET (window), "close-request",
		    G_CALLBACK (name_window_close), NULL);

  GtkEventController *key_controller = gtk_event_controller_key_new ();
  g_signal_connect (key_controller, "key-pressed",
		    G_CALLBACK (name_window_on_key_pressed), NULL);
  gtk_widget_add_controller (GTK_WIDGET (window), key_controller);
}

void
name_window_destroy ()
{
  debug_print (1, "Destroying name window...");
  gtk_window_destroy (GTK_WINDOW (window));
}
