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

#define MIN_TIME_UNTIL_DIALOG_RESPONSE_US 1000000
#define PROGRESS_BAR_UPDATE_TIME_MS 100
#define PROGRESS_WAIT_TO_END_REFRESH_TIME_US (PROGRESS_BAR_UPDATE_TIME_MS * 2 * 1000)

static GtkWindow *window;
static GtkWidget *bar;
static GtkWidget *label;
static GtkWidget *cancel_button;
static GThread *thread;
static struct controllable controllable;
static progress_window_runner runner;
static progress_window_consumer consumer;
static progress_window_cancel_cb cancel_cb;
static gpointer data;
static gint64 start;
static gdouble fraction;
static enum progress_type type;

void
progress_window_set_fraction (gdouble fraction_)
{
  g_mutex_lock (&controllable.mutex);
  fraction = fraction_;
  g_mutex_unlock (&controllable.mutex);
}

static gboolean
progress_window_set_label_cb (gpointer data)
{
  gchar *label_text = data;
  gtk_label_set_text (GTK_LABEL (label), label_text);
  g_free (label_text);
  return FALSE;
}

void
progress_window_set_label (const gchar *label_text)
{
  g_idle_add (progress_window_set_label_cb, strdup (label_text));
}

void
progress_window_set_active (gboolean active)
{
  controllable_set_active (&controllable, active);
}

gboolean
progress_window_is_active ()
{
  return controllable_is_active (&controllable);
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
  gboolean active;

  g_mutex_lock (&controllable.mutex);
  active = controllable.active;
  controllable.active = FALSE;
  g_mutex_unlock (&controllable.mutex);

  if (active)
    {
      debug_print (1, "Cancelling progress window...");
      usleep (PROGRESS_WAIT_TO_END_REFRESH_TIME_US);	//Needed to ensure refresh has ended and will not interfere with the cancelling label.
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

  status = sysex_transfer_get_status (sysex_transfer, &controllable);

  switch (status)
    {
    case SYSEX_TRANSFER_STATUS_WAITING:
      text = _("Waiting...");
      break;
    case SYSEX_TRANSFER_STATUS_SENDING:
      text = _("Sending...");
      break;
    case SYSEX_TRANSFER_STATUS_RECEIVING:
      text = _("Receiving...");
      break;
    default:
      text = "";
    }

  gtk_label_set_text (GTK_LABEL (label), text);

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

//This is used to avoiding the window to be opened a closed too fast.
static void
progress_window_sleep_until_min_time ()
{
  gint64 diff = g_get_monotonic_time () - start;
  if (diff < MIN_TIME_UNTIL_DIALOG_RESPONSE_US)
    {
      usleep (MIN_TIME_UNTIL_DIALOG_RESPONSE_US - diff);
    }
}

static gboolean
progress_window_run_refresh ()
{
  gboolean active_;
  gdouble fraction_;

  if (type == PROGRESS_TYPE_PULSE)
    {
      progress_window_update_pulse ();
    }
  else if (type == PROGRESS_TYPE_SYSEX_TRANSFER)
    {
      progress_window_update_sysex_transfer ();	//This sets the label and it's read and used later.
    }

  g_mutex_lock (&controllable.mutex);
  active_ = controllable.active;
  fraction_ = fraction;
  g_mutex_unlock (&controllable.mutex);

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
  progress_window_sleep_until_min_time ();
  progress_window_set_active (FALSE);
  g_idle_add (progress_window_end, NULL);
  return NULL;
}

void
progress_window_open (progress_window_runner runner_,
		      progress_window_consumer consumer_,
		      progress_window_cancel_cb cancel_cb_,
		      gpointer data_, enum progress_type type_,
		      const gchar *name, const gchar *label_text,
		      gboolean cancellable)
{
  runner = runner_;
  consumer = consumer_;
  cancel_cb = cancel_cb_;
  data = data_;
  type = type_;
  start = g_get_monotonic_time ();

  controllable.active = TRUE;

  progress_window_start_refresh ();

  gtk_window_set_title (window, name);
  gtk_label_set_text (GTK_LABEL (label), label_text);
  gtk_widget_set_visible (cancel_button, cancellable);
  gtk_widget_set_visible (GTK_WIDGET (window), TRUE);

  debug_print (1, "Creating progress thread...");
  thread = g_thread_new ("progress thread", progress_window_thread_runner,
			 NULL);
}

void
progress_window_destroy ()
{
  debug_print (1, "Destroying progress window...");
  if (thread)
    {
      progress_window_cancel ();
      //The thread will be joined by the main iteration as the thread runner idle-add a function for this.
      while (thread)
	{
	  gtk_main_iteration_do (TRUE);
	}
    }
  gtk_widget_destroy (GTK_WIDGET (window));
}
