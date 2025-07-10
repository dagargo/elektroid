/*
 *   backend.c
 *   Copyright (C) 2022 David García Goñi <dagargo@gmail.com>
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
#include "browser.h"
#include "maction.h"
#include "progress_window.h"

#define SYSEX_FILTER "*." BE_SYSEX_EXT

struct backend_tx_sysex_common_data
{
  struct sysex_transfer sysex_transfer;
  GSList *filenames;
};

struct backend_tx_sysex_common_funcs
{
  progress_window_runner runner;
  progress_window_consumer consumer;
};

struct backend_send_sysex_file_data
{
  gchar *filename;
  gint err;
};

extern GtkWindow *main_window;
extern struct browser remote_browser;

void elektroid_show_error_msg (const char *format, ...);
gboolean elektroid_check_backend ();
void elektroid_refresh_devices ();

static gboolean
backend_send_sysex_file_show_error (gpointer user_data)
{
  struct backend_send_sysex_file_data *data = user_data;
  elektroid_show_error_msg (_("Error while loading “%s”: %s."),
			    data->filename, g_strerror (-data->err));
  g_free (data->filename);
  g_free (data);
  return FALSE;
}

static gint
backend_send_sysex_file (struct sysex_transfer *sysex_transfer,
			 const gchar *filename, t_sysex_transfer f)
{
  gint err;
  struct idata idata;

  err = file_load (filename, &idata, NULL);
  if (!err)
    {
      sysex_transfer->raw = idata.content;
      //If the trasfer is too fast, there is not time to cancel it.
      err = f (remote_browser.backend, sysex_transfer);
      idata_free (&idata);
    }
  if (err && err != -ECANCELED)
    {
      struct backend_send_sysex_file_data *data =
	g_malloc (sizeof (struct backend_send_sysex_file_data));
      data->filename = strdup (filename);
      data->err = err;
      g_idle_add (backend_send_sysex_file_show_error, data);
    }
  return err;
}

static void
backend_tx_sysex_files_runner (gpointer user_data)
{
  gint err;
  GSList *filename;
  struct backend_tx_sysex_common_data *data = user_data;

  err = 0;
  filename = data->filenames;
  while (err != -ECANCELED && filename)
    {
      err = backend_send_sysex_file (&data->sysex_transfer, filename->data,
				     backend_tx_sysex);
      filename = filename->next;
      //The device may have sent some messages in response so we skip all these.
      backend_rx_drain (remote_browser.backend);
      usleep (BE_REST_TIME_US);
    }
}

static void
backend_upgrade_os_runner (gpointer user_data)
{
  gint err;
  struct backend_tx_sysex_common_data *data = user_data;
  GSList *filenames = data->filenames;

  err = backend_send_sysex_file (&data->sysex_transfer, filenames->data,
				 remote_browser.backend->upgrade_os);
  if (err < 0)
    {
      elektroid_check_backend ();
    }
}

static void
backend_tx_sysex_consumer (gpointer user_data)
{
  struct backend_tx_sysex_common_data *data = user_data;
  g_slist_free_full (g_steal_pointer (&data->filenames), g_free);
  //runners already free sysex_transfer->raw
  g_mutex_clear (&data->sysex_transfer.mutex);
  g_free (data);
}

static void
backend_upgrade_os_consumer (gpointer data)
{
  backend_tx_sysex_consumer (data);
  elektroid_refresh_devices (FALSE);
}

static void
backend_tx_sysex_cancel (gpointer user_data)
{
  struct backend_tx_sysex_common_data *data = user_data;
  sysex_transfer_cancel (&data->sysex_transfer);
}

static void
backend_tx_sysex_common_response (GtkDialog *dialog, gint response_id,
				  gpointer user_data)
{
  struct backend_tx_sysex_common_funcs *funcs = user_data;

  if (response_id == GTK_RESPONSE_ACCEPT)
    {
      GtkFileChooser *chooser = GTK_FILE_CHOOSER (dialog);
      struct backend_tx_sysex_common_data *data =
	g_malloc (sizeof (struct backend_tx_sysex_common_data));

      g_mutex_init (&data->sysex_transfer.mutex);
      data->sysex_transfer.active = TRUE;
      data->sysex_transfer.status = SENDING;
      data->sysex_transfer.timeout = BE_SYSEX_TIMEOUT_MS;

      data->filenames = gtk_file_chooser_get_filenames (chooser);

      progress_window_open (funcs->runner, funcs->consumer,
			    backend_tx_sysex_cancel, data,
			    PROGRESS_TYPE_SYSEX_TRANSFER, _("Sending SysEx"),
			    "", TRUE);
    }

  gtk_widget_destroy (GTK_WIDGET (dialog));
  g_free (funcs);
}


static void
backend_tx_sysex_common (progress_window_runner runner,
			 progress_window_consumer consumer, gboolean multiple)
{
  GtkWidget *dialog;
  GtkFileChooser *chooser;
  GtkFileFilter *filter;
  struct backend_tx_sysex_common_funcs *funcs =
    g_malloc (sizeof (struct backend_tx_sysex_common_funcs));

  funcs->runner = runner;
  funcs->consumer = consumer;

  dialog = gtk_file_chooser_dialog_new (_("Open SysEx"), main_window,
					GTK_FILE_CHOOSER_ACTION_OPEN,
					_("_Cancel"),
					GTK_RESPONSE_CANCEL,
					_("_Open"),
					GTK_RESPONSE_ACCEPT, NULL);
  chooser = GTK_FILE_CHOOSER (dialog);
  filter = gtk_file_filter_new ();
  gtk_file_filter_set_name (filter, _("SysEx Files"));
  gtk_file_filter_add_pattern (filter, SYSEX_FILTER);
  gtk_file_chooser_add_filter (chooser, filter);
  gtk_file_chooser_set_current_folder (chooser, g_get_home_dir ());
  gtk_file_chooser_set_select_multiple (chooser, multiple);

  g_signal_connect (dialog, "response",
		    G_CALLBACK (backend_tx_sysex_common_response), funcs);

  gtk_widget_set_visible (dialog, TRUE);
}

static void
backend_rx_sysex_consumer_response (GtkDialog *dialog, gint response_id,
				    gpointer user_data)
{
  gint err;
  struct idata idata;
  gchar *filename;
  gchar *filename_w_ext;
  const gchar *ext;
  struct sysex_transfer *sysex_transfer = user_data;

  if (response_id == GTK_RESPONSE_ACCEPT)
    {
      GtkFileChooser *chooser = GTK_FILE_CHOOSER (dialog);

      filename = gtk_file_chooser_get_filename (chooser);
      ext = filename_get_ext (filename);

      if (strcmp (ext, BE_SYSEX_EXT) != 0)
	{
	  filename_w_ext = g_strconcat (filename, "." BE_SYSEX_EXT, NULL);
	  g_free (filename);
	  filename = filename_w_ext;

	  if (g_file_test (filename, G_FILE_TEST_EXISTS))
	    {
	      gtk_file_chooser_set_filename (GTK_FILE_CHOOSER (chooser),
					     filename);
	      g_free (filename);
	      filename = NULL;
	      return;
	    }
	}

      idata.content = sysex_transfer->raw;

      err = file_save (filename, &idata, NULL);
      if (err)
	{
	  elektroid_show_error_msg (_("Error while saving “%s”: %s."),
				    filename, g_strerror (-err));
	}

      g_free (filename);
    }

  gtk_widget_destroy (GTK_WIDGET (dialog));
  g_byte_array_free (sysex_transfer->raw, TRUE);
  g_mutex_clear (&sysex_transfer->mutex);
  g_free (sysex_transfer);
}

static void
backend_rx_sysex_consumer (gpointer data)
{
  GtkWidget *dialog;
  GtkFileChooser *chooser;
  GtkFileFilter *filter;
  struct sysex_transfer *sysex_transfer = data;
  GtkFileChooserAction action = GTK_FILE_CHOOSER_ACTION_SAVE;

  if (!sysex_transfer->err)
    {
      dialog = gtk_file_chooser_dialog_new (_("Save SysEx"), main_window,
					    action, _("_Cancel"),
					    GTK_RESPONSE_CANCEL, _("_Save"),
					    GTK_RESPONSE_ACCEPT, NULL);
      chooser = GTK_FILE_CHOOSER (dialog);
      gtk_file_chooser_set_do_overwrite_confirmation (chooser, TRUE);
      gtk_file_chooser_set_current_name (chooser, _("Received SysEx"));

      gtk_file_chooser_set_create_folders (chooser, TRUE);

      filter = gtk_file_filter_new ();
      gtk_file_filter_set_name (filter, _("SysEx Files"));
      gtk_file_filter_add_pattern (filter, SYSEX_FILTER);
      gtk_file_chooser_add_filter (chooser, filter);
      gtk_file_chooser_set_current_folder (chooser, g_get_home_dir ());

      gtk_file_chooser_set_filter (GTK_FILE_CHOOSER (dialog), filter);

      g_signal_connect (dialog, "response",
			G_CALLBACK (backend_rx_sysex_consumer_response),
			sysex_transfer);

      gtk_widget_set_visible (dialog, TRUE);
    }
  else
    {
      g_mutex_clear (&sysex_transfer->mutex);
      g_free (sysex_transfer);
    }
}

static void
backend_rx_sysex_runner (gpointer data)
{
  gint err;
  gchar *text;
  struct sysex_transfer *sysex_transfer = data;

  //This needs to be initialized to an error as the cancelation might happend before it's set.
  sysex_transfer->err = -ECANCELED;

  //This doesn't need to be synchronized because the GUI doesn't allow concurrent access when receiving SysEx in batch mode.

  backend_rx_drain (remote_browser.backend);

  //This is needed as the call to backend_rx_drain might have been the one being cancelled.
  if (!sysex_transfer_is_active (sysex_transfer))
    {
      return;
    }

  err = backend_rx_sysex (remote_browser.backend, sysex_transfer);
  if (err)
    {
      elektroid_check_backend (FALSE);
    }
  else
    {
      text = debug_get_hex_msg (sysex_transfer->raw);
      debug_print (1, "SysEx message received (%d): %s",
		   sysex_transfer->raw->len, text);
      g_free (text);
    }
}

static void
os_upgrade_callback (GtkWidget *object, gpointer data)
{
  backend_tx_sysex_common (backend_upgrade_os_runner,
			   backend_upgrade_os_consumer, FALSE);
}

static void
tx_sysex_callback (GtkWidget *object, gpointer data)
{
  backend_tx_sysex_common (backend_tx_sysex_files_runner,
			   backend_tx_sysex_consumer, TRUE);
}

static void
backend_rx_sysex_cancel (gpointer data)
{
  struct sysex_transfer *sysex_transfer = data;
  sysex_transfer_cancel (sysex_transfer);
}

static void
backend_rx_sysex_callback (GtkWidget *object, gpointer data)
{
  struct sysex_transfer *sysex_transfer =
    g_malloc (sizeof (struct sysex_transfer));

  g_mutex_init (&sysex_transfer->mutex);
  sysex_transfer->status = WAITING;
  sysex_transfer->active = TRUE;
  sysex_transfer->timeout = BE_SYSEX_TIMEOUT_MS;
  sysex_transfer->batch = TRUE;

  progress_window_open (backend_rx_sysex_runner, backend_rx_sysex_consumer,
			backend_rx_sysex_cancel, sysex_transfer,
			PROGRESS_TYPE_SYSEX_TRANSFER, _("Receiving SysEx"),
			"", TRUE);
}

struct maction *
backend_maction_os_upgrade_builder (struct maction_context *context)
{
  struct maction *ma = NULL;
  if (remote_browser.backend->upgrade_os)
    {
      ma = g_malloc (sizeof (struct maction));
      ma->type = MACTION_BUTTON;
      ma->name = _("OS _Upgrade");
      ma->sensitive = TRUE;
      ma->callback = G_CALLBACK (os_upgrade_callback);
    }
  return ma;
}

struct maction *
backend_maction_rx_sysex_builder (struct maction_context *context)
{
  struct maction *ma = NULL;
  if (remote_browser.backend->type == BE_TYPE_MIDI)
    {
      ma = g_malloc (sizeof (struct maction));
      ma->type = MACTION_BUTTON;
      ma->name = _("_Receive SysEx");
      ma->sensitive = TRUE;
      ma->callback = G_CALLBACK (backend_rx_sysex_callback);
    }
  return ma;
}

struct maction *
backend_maction_tx_sysex_builder (struct maction_context *context)
{
  struct maction *ma = NULL;
  if (remote_browser.backend->type == BE_TYPE_MIDI)
    {
      ma = g_malloc (sizeof (struct maction));
      ma->type = MACTION_BUTTON;
      ma->name = _("_Send SysEx");
      ma->sensitive = TRUE;
      ma->callback = G_CALLBACK (tx_sysex_callback);
    }
  return ma;
}
