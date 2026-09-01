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
#include "elektroid.h"
#include "maction.h"
#include "progress_window.h"

#define SYSEX_FILTER "*." BE_SYSEX_EXT

extern GtkWindow *main_window;
extern struct browser remote_browser;

struct ma_backend_tx_sysex_common_data
{
  void *data;
  struct controllable controllable;
};

struct ma_backend_send_sysex_file_data
{
  gchar *filename;
  gint err;
};

struct ma_backend_rx_sysex_data
{
  struct sysex_transfer sysex_transfer;
  struct controllable controllable;
};

static gboolean
ma_backend_send_sysex_file_show_error (gpointer user_data)
{
  struct ma_backend_send_sysex_file_data *data = user_data;
  elektroid_show_error_msg (_("Error while loading “%s”: %s."),
			    data->filename, g_strerror (-data->err));
  g_free (data->filename);
  g_free (data);
  return FALSE;
}

static gint
ma_backend_send_sysex_file (GFile *file, t_sysex_transfer f,
			    struct controllable *controllable)
{
  gint err;
  struct idata idata;
  struct sysex_transfer sysex_transfer;
  gchar *filename = g_file_get_path (file);

  err = file_load (filename, &idata, NULL);
  g_free (filename);
  if (!err)
    {
      sysex_transfer_init_tx (&sysex_transfer, idata_steal (&idata));
      err = f (remote_browser.backend, &sysex_transfer, controllable);
      sysex_transfer_clear (&sysex_transfer);
    }
  if (err && err != -ECANCELED)
    {
      struct ma_backend_send_sysex_file_data *data =
	g_malloc (sizeof (struct ma_backend_send_sysex_file_data));
      data->filename = strdup (filename);
      data->err = err;
      g_idle_add (ma_backend_send_sysex_file_show_error, data);
    }
  return err;
}

static void
ma_backend_tx_sysex_files_runner (gpointer user_data)
{
  gint err;
  guint pos = 0;
  GSList *filename;
  struct ma_backend_tx_sysex_common_data *data = user_data;

  err = 0;
  while (err != -ECANCELED)
    {
      GFile *file = g_list_model_get_item (data->data, pos);
      if (!file)
	{
	  break;
	}
      err = ma_backend_send_sysex_file (file, backend_tx_sysex,
					&data->controllable);
      filename = filename->next;
      //The device may have sent some messages in response so we skip all these.
      backend_rx_drain (remote_browser.backend);
      g_usleep (BE_REST_TIME_US);
      pos++;
    }
}

static void
ma_backend_os_upgrade_runner (gpointer user_data)
{
  gint err;
  struct ma_backend_tx_sysex_common_data *data = user_data;
  GFile *file = data->data;
  err = ma_backend_send_sysex_file (file,
				    remote_browser.backend->upgrade_os,
				    &data->controllable);
  if (err < 0)
    {
      elektroid_check_backend ();
    }
}

static void
ma_backend_tx_sysex_consumer (gpointer user_data)
{
  struct ma_backend_tx_sysex_common_data *data = user_data;
  g_object_unref (data->data);
  //runners already free sysex_transfer->raw
  controllable_clear (&data->controllable);
  g_free (data);
}

static void
ma_backend_os_upgrade_consumer (gpointer data)
{
  ma_backend_tx_sysex_consumer (data);
  elektroid_refresh_devices ();
}

static void
ma_backend_tx_sysex_cancel (gpointer user_data)
{
  struct ma_backend_tx_sysex_common_data *data = user_data;
  controllable_set_active (&data->controllable, FALSE);
}

static void
ma_backend_open_os_upgrade_selected (GObject *source_object,
				     GAsyncResult *res, gpointer user_data)
{
  GError *error = NULL;
  GtkFileDialog *dialog = GTK_FILE_DIALOG (source_object);
  GFile *file = gtk_file_dialog_open_finish (dialog, res, &error);
  if (!error)
    {
      struct ma_backend_tx_sysex_common_data *data =
	g_malloc (sizeof (struct ma_backend_tx_sysex_common_data));

      data->data = file;
      controllable_init (&data->controllable);

      progress_window_open (ma_backend_os_upgrade_runner,
			    ma_backend_os_upgrade_consumer,
			    ma_backend_tx_sysex_cancel, data,
			    PROGRESS_TYPE_SYSEX_TRANSFER, _("Sending SysEx"),
			    "", TRUE);
    }
}

static void
ma_backend_open_tx_sysex_selected (GObject *source_object,
				   GAsyncResult *res, gpointer user_data)
{
  GError *error = NULL;
  GtkFileDialog *dialog = GTK_FILE_DIALOG (source_object);
  GListModel *files = gtk_file_dialog_open_multiple_finish (dialog, res,
							    &error);
  if (!error)
    {
      struct ma_backend_tx_sysex_common_data *data =
	g_malloc (sizeof (struct ma_backend_tx_sysex_common_data));

      data->data = files;
      controllable_init (&data->controllable);

      progress_window_open (ma_backend_tx_sysex_files_runner,
			    ma_backend_tx_sysex_consumer,
			    ma_backend_tx_sysex_cancel, data,
			    PROGRESS_TYPE_SYSEX_TRANSFER, _("Sending SysEx"),
			    "", TRUE);
    }
}

static void
ma_backend_rx_sysex_consumer_selected (GObject *source_object,
				       GAsyncResult *res, gpointer user_data)
{
  GError *error = NULL;
  struct ma_backend_rx_sysex_data *data = user_data;
  GtkFileDialog *dialog = GTK_FILE_DIALOG (source_object);
  GFile *file = gtk_file_dialog_save_finish (dialog, res, &error);
  if (error)
    {
      sysex_transfer_clear (&data->sysex_transfer);
    }
  else
    {
      gint err;
      struct idata idata;
      gchar *filename_w_ext;
      gchar *filename = g_file_get_path (file);
      const gchar *ext = filename_get_ext (filename);

      if (strcmp (ext, BE_SYSEX_EXT) != 0)
	{
	  filename_w_ext = g_strconcat (filename, "." BE_SYSEX_EXT, NULL);
	  g_free (filename);
	  filename = filename_w_ext;
	}

      idata_init (&idata, sysex_transfer_steal (&data->sysex_transfer), NULL,
		  NULL, NULL);

      err = file_save (filename, &idata, NULL);
      if (err)
	{
	  elektroid_show_error_msg (_("Error while saving “%s”: %s."),
				    filename, g_strerror (-err));
	}

      idata_clear (&idata);
      g_free (filename);
    }

  controllable_clear (&data->controllable);
  g_free (data);
}

static GtkFileDialog *
ma_backend_tx_dialog_new (const gchar *title, const gchar *accept_label)
{
  GtkFileDialog *dialog = gtk_file_dialog_new ();
  gtk_file_dialog_set_title (dialog, title);
  gtk_file_dialog_set_modal (dialog, TRUE);
  gtk_file_dialog_set_accept_label (dialog, accept_label);
  gtk_file_dialog_set_initial_folder (dialog,
				      g_file_new_for_path (g_get_home_dir
							   ()));
  GtkFileFilter *filter = gtk_file_filter_new ();
  gtk_file_filter_set_name (filter, _("SysEx Files"));
  gtk_file_filter_add_pattern (filter, SYSEX_FILTER);
  gtk_file_dialog_set_default_filter (dialog, filter);
  return dialog;
}

static void
ma_backend_rx_sysex_consumer (gpointer user_data)
{
  GtkFileDialog *dialog;
  GtkFileChooser *chooser;
  GtkFileFilter *filter;
  struct ma_backend_rx_sysex_data *data = user_data;
  GtkFileChooserAction action = GTK_FILE_CHOOSER_ACTION_SAVE;

  if (!data->sysex_transfer.err)
    {
      GtkFileDialog *dialog =
	ma_backend_tx_dialog_new (_("Received SysEx"), _("_Save"));
      gtk_file_dialog_save (dialog, main_window, NULL,
			    ma_backend_rx_sysex_consumer_selected, data);
      g_object_unref (dialog);
    }
  else
    {
      controllable_clear (&data->controllable);
      sysex_transfer_clear (&data->sysex_transfer);
      g_free (data);
    }
}

static void
ma_backend_rx_sysex_runner (gpointer user_data)
{
  gint err;
  gchar *text;
  struct ma_backend_rx_sysex_data *data = user_data;

  //This doesn't need to be synchronized because the GUI doesn't allow concurrent access when receiving SysEx in batch mode.
  backend_rx_drain (remote_browser.backend);

  err = backend_rx_sysex (remote_browser.backend, &data->sysex_transfer,
			  &data->controllable);
  if (err)
    {
      elektroid_check_backend ();
    }
  else
    {
      text = debug_get_hex_msg (data->sysex_transfer.raw);
      debug_print (1, "SysEx message received (%d): %s",
		   data->sysex_transfer.raw->len, text);
      g_free (text);
    }
}

static void
ma_backend_open_os_upgrade (GSimpleAction *simple_action, GVariant *parameter,
			    gpointer user_data)
{
  GtkFileDialog *dialog =
    ma_backend_tx_dialog_new (_("Open SysEx"), _("_Open"));
  gtk_file_dialog_open (dialog, main_window, NULL,
			ma_backend_open_os_upgrade_selected, NULL);
  g_object_unref (dialog);
}

static void
ma_backend_open_tx_sysex (GSimpleAction *simple_action, GVariant *parameter,
			  gpointer user_data)
{
  GtkFileDialog *dialog =
    ma_backend_tx_dialog_new (_("Open SysEx"), _("_Open"));
  gtk_file_dialog_open_multiple (dialog, main_window, NULL,
				 ma_backend_open_tx_sysex_selected, NULL);
  g_object_unref (dialog);
}

static void
ma_backend_rx_sysex_cancel (gpointer user_data)
{
  struct ma_backend_rx_sysex_data *data = user_data;
  controllable_set_active (&data->controllable, FALSE);
}

static void
ma_backend_open_rx_sysex (GSimpleAction *simple_action, GVariant *parameter,
			  gpointer user_data)
{
  struct ma_backend_rx_sysex_data *data =
    g_malloc (sizeof (struct ma_backend_rx_sysex_data));

  controllable_init (&data->controllable);
  sysex_transfer_init_rx (&data->sysex_transfer, BE_SYSEX_TIMEOUT_MS, TRUE);

  progress_window_open (ma_backend_rx_sysex_runner,
			ma_backend_rx_sysex_consumer,
			ma_backend_rx_sysex_cancel, data,
			PROGRESS_TYPE_SYSEX_TRANSFER, _("Receiving SysEx"),
			"", TRUE);
}

static const GActionEntry BACKEND_ENTRIES[] = {
  {"backend_open_os_upgrade", ma_backend_open_os_upgrade, NULL, NULL, NULL},
  {"backend_open_rx_sysex", ma_backend_open_rx_sysex, NULL, NULL, NULL},
  {"backend_open_tx_sysex", ma_backend_open_tx_sysex, NULL, NULL, NULL}
};

void
ma_backend_init (GtkApplication *app)
{
  g_action_map_add_action_entries (G_ACTION_MAP (app), BACKEND_ENTRIES,
				   G_N_ELEMENTS (BACKEND_ENTRIES), app);
}

struct maction *
ma_backend_os_upgrade_builder (struct maction_context *context)
{
  struct maction *ma = NULL;
  if (remote_browser.backend->upgrade_os)
    {
      ma = g_malloc (sizeof (struct maction));
      ma->name = _("OS _Upgrade");
      ma->action_name = "app.backend_open_os_upgrade";
    }
  return ma;
}

struct maction *
ma_backend_rx_sysex_builder ()
{
  struct maction *ma = NULL;
  if (remote_browser.backend->type == BE_TYPE_MIDI)
    {
      ma = g_malloc (sizeof (struct maction));
      ma->name = _("_Receive SysEx");
      ma->action_name = "app.backend_open_rx_sysex";
    }
  return ma;
}

struct maction *
ma_backend_tx_sysex_builder ()
{
  struct maction *ma = NULL;
  if (remote_browser.backend->type == BE_TYPE_MIDI)
    {
      ma = g_malloc (sizeof (struct maction));
      ma->name = _("_Send SysEx");
      ma->action_name = "app.backend_open_tx_sysex";
    }
  return ma;
}
