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

struct progress_sysex_thread_data
{
  GThreadFunc f;
  gpointer data;
};

struct sysex_transfer sysex_transfer;
GtkDialog *progress_dialog;
GtkWidget *progress_bar;
GtkWidget *progress_label;
GtkWidget *progress_dialog_cancel_button;

static GThread *sysex_thread;

gpointer
progress_join_sysex_thread ()
{
  gpointer output = NULL;

  debug_print (1, "Stopping SysEx thread...\n");
  if (sysex_thread)
    {
      output = g_thread_join (sysex_thread);
    }
  sysex_thread = NULL;

  return output;
}

void
progress_stop_running_sysex (GtkDialog * dialog, gint response_id,
			     gpointer data)
{
  debug_print (1, "Stopping SysEx transfer...\n");
  g_mutex_lock (&sysex_transfer.mutex);
  sysex_transfer.active = FALSE;
  g_mutex_unlock (&sysex_transfer.mutex);
}

void
progress_stop_sysex_thread ()
{
  progress_stop_running_sysex (NULL, 0, NULL);
  progress_join_sysex_thread ();
}

void
progress_progress_dialog_close (gpointer data)
{
  gtk_label_set_text (GTK_LABEL (progress_label), _("Cancelling..."));
  gtk_dialog_response (GTK_DIALOG (progress_dialog), GTK_RESPONSE_CANCEL);
}

gboolean
progress_pulse (gpointer data)
{
  gboolean active;

  g_mutex_lock (&sysex_transfer.mutex);
  active = sysex_transfer.active;
  g_mutex_unlock (&sysex_transfer.mutex);

  gtk_progress_bar_pulse (GTK_PROGRESS_BAR (progress_bar));

  if (!active)
    {
      debug_print (1, "Stopping SysEx progress...\n");
    }

  return active;
}

gboolean
progress_update (gpointer data)
{
  gchar *text;
  enum sysex_transfer_status status;

  g_mutex_lock (&sysex_transfer.mutex);
  status = sysex_transfer.status;
  g_mutex_unlock (&sysex_transfer.mutex);

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
  gtk_label_set_text (GTK_LABEL (progress_label), text);

  return progress_pulse (NULL);
}

void
progress_init (GtkBuilder * builder)
{
  progress_dialog =
    GTK_DIALOG (gtk_builder_get_object (builder, "progress_dialog"));
  progress_dialog_cancel_button =
    GTK_WIDGET (gtk_builder_get_object
		(builder, "progress_dialog_cancel_button"));
  progress_bar =
    GTK_WIDGET (gtk_builder_get_object (builder, "progress_bar"));
  progress_label =
    GTK_WIDGET (gtk_builder_get_object (builder, "progress_label"));

  g_signal_connect (progress_dialog_cancel_button, "clicked",
		    G_CALLBACK (progress_progress_dialog_close), NULL);
  g_signal_connect (progress_dialog, "response",
		    G_CALLBACK (progress_stop_running_sysex), NULL);
}

static gboolean
elektroid_new_sysex_thread_gsourcefunc (gpointer user_data)
{
  struct progress_sysex_thread_data *data = user_data;
  debug_print (1, "Creating SysEx thread...\n");
  sysex_thread = g_thread_new ("sysex_thread", data->f, data->data);
  g_free (data);
  return FALSE;
}

//Using this before a call to gtk_dialog_run ensures that the threads starts after the dialog is being run.
void
progress_new_sysex_thread (GThreadFunc f, gpointer user_data)
{
  struct progress_sysex_thread_data *data =
    g_malloc (sizeof (struct progress_sysex_thread_data));
  data->f = f;
  data->data = user_data;
  g_idle_add (elektroid_new_sysex_thread_gsourcefunc, data);
}
