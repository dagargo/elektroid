/*
 *   record_window.c
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

#include "audio.h"
#include "guirecorder.h"
#include "record_window.h"

static struct guirecorder guirecorder;

static GtkWindow *window;
static GtkWidget *record_button;
static GtkWidget *cancel_button;
static record_window_record_cb record_cb;
static record_window_cancel_cb cancel_cb;

static void
record_window_close ()
{
  audio_stop_recording ();	//Stop monitoring
  gtk_main_iteration_do (gtk_widget_get_visible (GTK_WIDGET (window)));	//Wait for guirecorder
  gtk_widget_hide (GTK_WIDGET (window));
}

static void
record_window_cancel (GtkWidget *object, gpointer data)
{
  cancel_cb ();
  record_window_close ();
}

static gboolean
record_window_delete (GtkWidget *widget, GdkEvent *event, gpointer data)
{
  record_window_cancel (NULL, NULL);
  return FALSE;
}

static void
record_window_record (GtkWidget *object, gpointer data)
{
  guint channel_mask = guirecorder_get_channel_mask (&guirecorder);
  record_window_close ();
  record_cb (channel_mask);
}

static gboolean
record_window_key_press (GtkWidget *widget, GdkEventKey *event, gpointer data)
{
  if (event->keyval == GDK_KEY_Escape)
    {
      record_window_cancel (NULL, NULL);
      return TRUE;
    }
  return FALSE;
}

void
record_window_open (guint32 fs_options, record_window_record_cb record_cb_,
		    record_window_cancel_cb cancel_cb_)
{
  guint options;

  record_cb = record_cb_;
  cancel_cb = cancel_cb_;

  guirecorder_set_channels_masks (&guirecorder, fs_options);
  options = guirecorder_get_channel_mask (&guirecorder);
  audio_start_recording (options | RECORD_MONITOR_ONLY,
			 guirecorder_monitor_notifier, &guirecorder);

  gtk_widget_show (GTK_WIDGET (window));
}

void
record_window_init (GtkBuilder *builder)
{
  window = GTK_WINDOW (gtk_builder_get_object (builder, "record_window"));
  record_button =
    GTK_WIDGET (gtk_builder_get_object
		(builder, "record_window_record_button"));
  cancel_button =
    GTK_WIDGET (gtk_builder_get_object
		(builder, "record_window_cancel_button"));

  guirecorder.channels_combo =
    GTK_WIDGET (gtk_builder_get_object
		(builder, "record_window_channels_combo"));
  guirecorder.channels_list_store =
    GTK_LIST_STORE (gtk_builder_get_object
		    (builder, "record_window_channels_list_store"));
  guirecorder.monitor_levelbar_l =
    GTK_LEVEL_BAR (gtk_builder_get_object
		   (builder, "record_window_monitor_levelbar_l"));
  guirecorder.monitor_levelbar_r =
    GTK_LEVEL_BAR (gtk_builder_get_object
		   (builder, "record_window_monitor_levelbar_r"));

  g_signal_connect (record_button, "clicked",
		    G_CALLBACK (record_window_record), NULL);
  g_signal_connect (cancel_button, "clicked",
		    G_CALLBACK (record_window_cancel), NULL);
  g_signal_connect (GTK_WIDGET (window), "delete-event",
		    G_CALLBACK (record_window_delete), NULL);
  g_signal_connect (GTK_WIDGET (window), "key_press_event",
		    G_CALLBACK (record_window_key_press), NULL);

  g_signal_connect (guirecorder.channels_combo, "changed",
		    G_CALLBACK (guirecorder_channels_changed), &guirecorder);
}

void
record_window_destroy ()
{
  debug_print (1, "Destroying record window...");
  record_window_close ();
  gtk_widget_destroy (GTK_WIDGET (window));
}
