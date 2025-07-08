/*
 *   c
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
#include "connector.h"
#include "utils.h"
#include "progress_window.h"

#define MIN_TIME_UNTIL_DIALOG_RESPONSE 1e6
#define PROGRESS_BAR_UPDATE_TIME_MS 100
#define PROGRESS_WAIT_TO_END_REFRESH_TIME_US (PROGRESS_BAR_UPDATE_TIME_MS * 2 * 1000)

static GtkWindow *window;
static GtkWidget *bar;
static GtkWidget *label;
static GtkWidget *cancel_button;
static GThread *thread;
static gboolean active;
static GMutex mutex;
static progress_window_runner runner;
static progress_window_consumer consumer;
static progress_window_cancel_cb cancel_cb;
static gpointer data;
static gint64 start;
static gdouble fraction;
static const gchar *label_text;
static enum progress_type type;

void
progress_window_set_fraction (gdouble fraction_)
{
  g_mutex_lock (&mutex);
  fraction = fraction_;
  g_mutex_unlock (&mutex);
}

void
progress_window_set_label (const gchar *label_text_)
{
  g_mutex_lock (&mutex);
  label_text = label_text_;
  g_mutex_unlock (&mutex);
}

void
progress_window_set_active (gboolean active_)
{
  g_mutex_lock (&mutex);
  active = active_;
  g_mutex_unlock (&mutex);
}

gboolean
progress_window_is_active ()
{
  gboolean active_;

  g_mutex_lock (&mutex);
  active_ = active;
  g_mutex_unlock (&mutex);

  return active_;
}

static void
progress_window_join_thread ()
{
  debug_print (1, "Stopping SysEx thread...");

  if (thread)
    {
      g_thread_join (thread);
      thread = NULL;
    }
}

void
progress_window_cancel ()
{
  gboolean active_;

  g_mutex_lock (&mutex);
  active_ = active;
  active = FALSE;
  g_mutex_unlock (&mutex);

  if (active_)
    {
      debug_print (1, "Cancelling progress window...");
      usleep (PROGRESS_WAIT_TO_END_REFRESH_TIME_US);	//Needed to ensure refresh has ended and will not interfere with the cancelling label.
      progress_window_set_active (FALSE);
      gtk_label_set_text (GTK_LABEL (label), _("Cancelling..."));
      if (cancel_cb)
	{
	  cancel_cb (data);
	}
    }
}

static void
progress_window_cancel_clicked (GtkWidget *object, gpointer data_)
{
  progress_window_cancel ();
}

static void
progress_window_update_pulse ()
{
  gtk_progress_bar_pulse (GTK_PROGRESS_BAR (bar));
}

static void
progress_window_update_sysex_transfer ()
{
  const gchar *text;
  enum sysex_transfer_status status;
  struct sysex_transfer *sysex_transfer = data;

  g_mutex_lock (&sysex_transfer->mutex);
  status = sysex_transfer->status;
  g_mutex_unlock (&sysex_transfer->mutex);

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
  progress_window_set_label (text);

  progress_window_update_pulse ();
}

static gboolean
progress_window_delete (GtkWidget *widget, GdkEvent *event, gpointer data)
{
  progress_window_cancel ();
  return FALSE;
}

static gboolean
progress_window_key_press (GtkWidget *widget, GdkEventKey *event,
			   gpointer data)
{
  if (event->keyval == GDK_KEY_Escape)
    {
      progress_window_cancel ();
      return TRUE;
    }
  return FALSE;
}

void
progress_window_init (GtkBuilder *builder)
{
  window = GTK_WINDOW (gtk_builder_get_object (builder, "progress_window"));
  bar = GTK_WIDGET (gtk_builder_get_object (builder, "progress_window_bar"));
  label =
    GTK_WIDGET (gtk_builder_get_object (builder, "progress_window_label"));
  cancel_button =
    GTK_WIDGET (gtk_builder_get_object
		(builder, "progress_window_cancel_button"));

  g_signal_connect (GTK_WIDGET (window), "delete-event",
		    G_CALLBACK (progress_window_delete), NULL);
  g_signal_connect (GTK_WIDGET (window), "key_press_event",
		    G_CALLBACK (progress_window_key_press), NULL);
  g_signal_connect (cancel_button, "clicked",
		    G_CALLBACK (progress_window_cancel_clicked), NULL);
}

/**
 * This function guarantees that the time since start is at least the timeout.
 * This is needed when controlling a dialog from a thread because the dialog needs to be showed before the response is sent from the thread.
 */
static void
progress_window_usleep_since (gint64 timeout, gint64 start)
{
  gint64 diff = g_get_monotonic_time () - start;
  if (diff < timeout)
    {
      usleep (timeout - diff);
    }
}

static gboolean
progress_window_run_refresh ()
{
  gboolean active_;
  const gchar *label_text_;
  gdouble fraction_;

  if (type == PROGRESS_TYPE_PULSE)
    {
      progress_window_update_pulse ();
    }
  else if (type == PROGRESS_TYPE_SYSEX_TRANSFER)
    {
      progress_window_update_sysex_transfer ();	//This sets the label and it's read and used later.
    }

  g_mutex_lock (&mutex);
  active_ = active;
  label_text_ = label_text;
  fraction_ = fraction;
  g_mutex_unlock (&mutex);

  gtk_label_set_text (GTK_LABEL (label), label_text_);

  if (type == PROGRESS_TYPE_NO_AUTO)
    {
      gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (bar), fraction_);
    }

  return active_;
}

static void
progress_window_start_refresh ()
{
  g_timeout_add (PROGRESS_BAR_UPDATE_TIME_MS, progress_window_run_refresh,
		 NULL);
}

static gboolean
progress_window_end ()
{
  progress_window_join_thread ();

  if (consumer)
    {
      consumer (data);
    }

  gtk_widget_set_visible (GTK_WIDGET (window), FALSE);

  return FALSE;
}

static gpointer
progress_window_thread_runner (gpointer data_)
{
  runner (data);
  progress_window_usleep_since (MIN_TIME_UNTIL_DIALOG_RESPONSE, start);
  progress_window_set_active (FALSE);
  g_idle_add (progress_window_end, NULL);
  return NULL;
}

void
progress_window_open (progress_window_runner runner_,
		      progress_window_consumer consumer_,
		      progress_window_cancel_cb cancel_cb_,
		      gpointer data_, enum progress_type type_,
		      const gchar *name, const gchar *label_text_,
		      gboolean cancellable)
{
  runner = runner_;
  consumer = consumer_;
  cancel_cb = cancel_cb_;
  data = data_;
  type = type_;
  label_text = label_text_;
  start = g_get_monotonic_time ();

  active = TRUE;

  progress_window_start_refresh ();

  gtk_window_set_title (window, name);
  gtk_widget_set_visible (cancel_button, cancellable);
  gtk_widget_set_visible (GTK_WIDGET (window), TRUE);

  debug_print (1, "Creating progress thread...");
  thread = g_thread_new ("progress thread", progress_window_thread_runner,
			 NULL);
}

void
progress_window_destroy ()
{
  if (thread)
    {
      progress_window_cancel ();
    }
}
