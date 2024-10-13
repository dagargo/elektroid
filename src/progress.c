/*
 *   progress.c
 *   Copyright (C) 2023 David García Goñi <dagargo@gmail.com>
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

#include <glib/gi18n.h>
#include "progress.h"

#define MIN_TIME_UNTIL_DIALOG_RESPONSE 1e6
#define PROGRESS_BAR_UPDATE_TIME 100

struct progress progress;

static void
progress_join_thread ()
{
  debug_print (1, "Stopping SysEx thread...");

  if (progress.thread)
    {
      gpointer output = g_thread_join (progress.thread);
      if (progress.end_cb)
	{
	  progress.end_cb (progress.data, output, progress_is_active ());
	}
    }
  progress.thread = NULL;
}

static gboolean
progress_end_gsourcefunc (gpointer data)
{
  gtk_label_set_text (GTK_LABEL (progress.label), _("Cancelling..."));

  debug_print (1, "Stopping SysEx transfer...");
  g_mutex_lock (&progress.sysex_transfer.mutex);
  progress.sysex_transfer.active = FALSE;
  g_mutex_unlock (&progress.sysex_transfer.mutex);

  progress_join_thread ();

  gtk_widget_hide (GTK_WIDGET (progress.window));

  return FALSE;
}

static void
progress_window_cancel (GtkWidget *object, gpointer data)
{
  progress_end_gsourcefunc (NULL);
}

static gboolean
progress_window_delete (GtkWidget *widget, GdkEvent *event, gpointer data)
{
  progress_window_cancel (progress.cancel_button, NULL);
  return TRUE;
}

void
progress_set_fraction (gdouble fraction)
{
  gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (progress.bar), fraction);
}

gboolean
progress_is_active ()
{
  gboolean active;

  g_mutex_lock (&progress.sysex_transfer.mutex);
  active = progress.sysex_transfer.active;
  g_mutex_unlock (&progress.sysex_transfer.mutex);

  return active;
}

static gboolean
progress_pulse (gpointer data)
{
  gtk_progress_bar_pulse (GTK_PROGRESS_BAR (progress.bar));

  return progress_is_active ();
}

static gboolean
progress_update (gpointer data)
{
  gchar *text;
  enum sysex_transfer_status status;

  g_mutex_lock (&progress.sysex_transfer.mutex);
  status = progress.sysex_transfer.status;
  g_mutex_unlock (&progress.sysex_transfer.mutex);

  switch (status)
    {
    case WAITING:
      text = _("Waiting...");
      break;
    case SENDING:
      text = _("Sending...");
      break;
    case RECEIVING:
      text = _("Receiving...");
      break;
    default:
      text = "";
    }
  gtk_label_set_text (GTK_LABEL (progress.label), text);

  return progress_pulse (NULL);
}

void
progress_init (GtkBuilder *builder)
{
  progress.window =
    GTK_WINDOW (gtk_builder_get_object (builder, "progress_window"));
  progress.bar =
    GTK_WIDGET (gtk_builder_get_object (builder, "progress_window_bar"));
  progress.label =
    GTK_WIDGET (gtk_builder_get_object (builder, "progress_window_label"));
  progress.cancel_button =
    GTK_WIDGET (gtk_builder_get_object
		(builder, "progress_window_cancel_button"));

  g_signal_connect (progress.cancel_button, "clicked",
		    G_CALLBACK (progress_window_cancel), NULL);
  g_signal_connect (GTK_WIDGET (progress.window), "delete-event",
		    G_CALLBACK (progress_window_delete), NULL);
}

static void
progress_start_thread_gsourcefunc ()
{
  switch (progress.type)
    {
    case PROGRESS_TYPE_PULSE:
      g_timeout_add (PROGRESS_BAR_UPDATE_TIME, progress_pulse, NULL);
      break;
    case PROGRESS_TYPE_UPDATE:
      g_timeout_add (PROGRESS_BAR_UPDATE_TIME, progress_update, NULL);
      break;
    default:
      error_print ("Illegal progress type");
    }
}

void
progress_run (GThreadFunc f, enum progress_type type, gpointer user_data,
	      const gchar *name, const gchar *text, gboolean cancellable,
	      progress_end_cb end_cb)
{
  gtk_widget_set_visible (progress.cancel_button, cancellable);

  progress.data = user_data;
  progress.end_cb = end_cb;

  progress.type = type;
  progress_start_thread_gsourcefunc ();

  progress.start = g_get_monotonic_time ();
  gtk_window_set_title (progress.window, name);
  gtk_label_set_text (GTK_LABEL (progress.label), text);

  progress.sysex_transfer.active = TRUE;

  debug_print (1, "Creating progress thread...");
  progress.thread = g_thread_new ("progress thread", f, user_data);

  gtk_widget_show (GTK_WIDGET (progress.window));
}

// This function guarantees that the time since start is at least the timeout.
static void
progress_usleep_since (gint64 timeout, gint64 start)
{
  gint64 diff = g_get_monotonic_time () - start;
  if (diff < timeout)
    {
      usleep (timeout - diff);
    }
}

void
progress_end ()
{
  progress_usleep_since (MIN_TIME_UNTIL_DIALOG_RESPONSE, progress.start);
  g_idle_add (progress_end_gsourcefunc, NULL);
}
