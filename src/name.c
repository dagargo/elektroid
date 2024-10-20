/*
 *   name.c
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

#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include "name.h"

struct name
{
  GtkWindow *window;
  GtkEntry *entry;
  GtkWidget *accept_button;
  GtkWidget *cancel_button;
  gchar *old_path;
  gpointer source;
  name_accept_cb accept_cb;
  gpointer cb_data;
};

struct name name;

static void
name_window_show (const gchar *title, gint max_len, const gchar *text,
		  gint sel_start, gint sel_end, gboolean sensitive)
{
  gtk_window_set_title (name.window, title);

  gtk_entry_set_max_length (name.entry, max_len);
  gtk_entry_buffer_set_text (gtk_entry_get_buffer (name.entry), text, -1);
  gtk_widget_grab_focus (GTK_WIDGET (name.entry));
  gtk_editable_select_region (GTK_EDITABLE (name.entry), sel_start, sel_end);
  gtk_widget_set_sensitive (name.accept_button, sensitive);

  gtk_widget_show (GTK_WIDGET (name.window));
}

void
name_edit_text (gpointer source, const gchar *title,
		gint max_len, const gchar *text, gint sel_start,
		gint sel_end, gboolean sensitive, name_accept_cb accept_cb,
		gpointer cb_data)
{
  name.source = source;
  name.accept_cb = accept_cb;
  name.cb_data = cb_data;

  name_window_show (title, max_len, text, sel_start, sel_end, sensitive);
}

void
name_new_text (gpointer source, const gchar *title, gint max_len,
	       name_accept_cb accept_cb)
{
  name.source = source;
  name.accept_cb = accept_cb;
  name.cb_data = NULL;

  name_window_show (title, max_len, "", 0, 0, FALSE);
}

static void
name_window_cancel (GtkWidget *object, gpointer data)
{
  gtk_widget_hide (GTK_WIDGET (name.window));
}

static gboolean
name_window_delete (GtkWidget *widget, GdkEvent *event, gpointer data)
{
  name_window_cancel (name.cancel_button, NULL);
  return TRUE;
}

static void
name_window_accept (GtkWidget *object, gpointer data)
{
  GtkEntryBuffer *buf = gtk_entry_get_buffer (name.entry);
  const gchar *text = gtk_entry_buffer_get_text (buf);
  name.accept_cb (name.source, text, name.cb_data);
  name_window_cancel (name.cancel_button, NULL);
}

static void
name_window_entry_changed (GtkWidget *object, gpointer data)
{
  GtkEntryBuffer *buf = gtk_entry_get_buffer (name.entry);
  const gchar *text = gtk_entry_buffer_get_text (buf);
  size_t len = strlen (text);
  gtk_widget_set_sensitive (name.accept_button, len > 0);
}

void
name_init (GtkBuilder *builder)
{
  name.window = GTK_WINDOW (gtk_builder_get_object (builder, "name_window"));
  name.accept_button =
    GTK_WIDGET (gtk_builder_get_object
		(builder, "name_window_accept_button"));
  name.cancel_button =
    GTK_WIDGET (gtk_builder_get_object
		(builder, "name_window_cancel_button"));
  name.entry =
    GTK_ENTRY (gtk_builder_get_object (builder, "name_window_entry"));

  g_signal_connect (name.entry, "changed",
		    G_CALLBACK (name_window_entry_changed), NULL);
  g_signal_connect (name.accept_button, "clicked",
		    G_CALLBACK (name_window_accept), NULL);
  g_signal_connect (name.cancel_button, "clicked",
		    G_CALLBACK (name_window_cancel), NULL);
  g_signal_connect (GTK_WIDGET (name.window), "delete-event",
		    G_CALLBACK (name_window_delete), NULL);
}
