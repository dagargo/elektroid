/*
 *   elektroid.c
 *   Copyright (C) 2019 David García Goñi <dagargo@gmail.com>
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

#include "../config.h"
#include <limits.h>
#include <locale.h>
#include <gtk/gtk.h>
#include <glib-unix.h>
#include <glib/gi18n.h>
#include <glib/gprintf.h>
#define _GNU_SOURCE
#include <getopt.h>
#include "connector.h"
#include "browser.h"
#include "audio.h"
#include "sample.h"
#include "utils.h"
#include "notifier.h"
#include "local.h"

#define MAX_DRAW_X 10000

#define DUMP_TIMEOUT 2000
#define DND_TIMEOUT 1000

#define TEXT_URI_LIST_STD "text/uri-list"
#define TEXT_URI_LIST_ELEKTROID "text/uri-list-elektroid"

#define MSG_ERROR_MOVING "Error while moving from “%s” to “%s”: %s."
#define MSG_WARN_SAME_SRC_DST "Same source and destination path. Skipping...\n"

enum device_list_store_columns
{
  DEVICES_LIST_STORE_CARD_FIELD,
  DEVICES_LIST_STORE_NAME_FIELD
};

enum task_list_store_columns
{
  TASK_LIST_STORE_STATUS_FIELD,
  TASK_LIST_STORE_TYPE_FIELD,
  TASK_LIST_STORE_SRC_FIELD,
  TASK_LIST_STORE_DST_FIELD,
  TASK_LIST_STORE_PROGRESS_FIELD,
  TASK_LIST_STORE_STATUS_HUMAN_FIELD,
  TASK_LIST_STORE_TYPE_HUMAN_FIELD,
  TASK_LIST_STORE_REMOTE_FS_ID_FIELD
};

enum fs_list_store_columns
{
  FS_LIST_STORE_ID_FIELD,
  FS_LIST_STORE_ICON_FIELD,
  FS_LIST_STORE_NAME_FIELD
};

enum
{
  TARGET_STRING,
};

enum elektroid_task_type
{
  UPLOAD,
  DOWNLOAD
};

enum elektroid_task_status
{
  QUEUED,
  RUNNING,
  COMPLETED_OK,
  COMPLETED_ERROR,
  CANCELED
};

struct elektroid_transfer
{
  struct job_control control;
  gchar *src;			//Contains a path to a file
  gchar *dst;			//Contains a path to a file
  enum elektroid_task_status status;	//Contains the final status
  const struct fs_operations *fs_ops;	//Contains the fs_operations to use in this transfer
};

struct elektroid_sysex_transfer
{
  struct connector_sysex_transfer transfer;
  GByteArray *data;
};

struct elektroid_add_upload_task_data
{
  struct item_iterator *iter;
  gint32 index;
};

static gpointer elektroid_upload_task (gpointer);
static gpointer elektroid_download_task (gpointer);
static void elektroid_update_progress (gdouble);

static const struct option ELEKTROID_OPTIONS[] = {
  {"local-directory", 1, NULL, 'l'},
  {"verbose", 0, NULL, 'v'},
  {"help", 0, NULL, 'h'},
  {NULL, 0, NULL, 0}
};

static const gchar *ELEKTROID_FS_NAMES[] = {
  "samples", "data"
};

static const gchar *ELEKTROID_FS_ICONS[] = {
  FILE_ICON_WAVE, FILE_ICON_PATTERN
};

static gchar *ELEKTROID_AUDIO_LOCAL_EXTS[] =
  { "wav", "ogg", "aiff", "flac", NULL };

static GtkTargetEntry TARGET_ENTRIES_LOCAL_DST[] = {
  {TEXT_URI_LIST_ELEKTROID, GTK_TARGET_SAME_APP | GTK_TARGET_OTHER_WIDGET,
   TARGET_STRING},
  {TEXT_URI_LIST_STD, GTK_TARGET_SAME_APP | GTK_TARGET_SAME_WIDGET,
   TARGET_STRING},
  {TEXT_URI_LIST_STD, GTK_TARGET_OTHER_APP, TARGET_STRING}
};

static GtkTargetEntry TARGET_ENTRIES_LOCAL_SRC[] = {
  {TEXT_URI_LIST_STD, GTK_TARGET_SAME_APP | GTK_TARGET_OTHER_WIDGET,
   TARGET_STRING},
  {TEXT_URI_LIST_STD, GTK_TARGET_SAME_APP | GTK_TARGET_SAME_WIDGET,
   TARGET_STRING},
  {TEXT_URI_LIST_STD, GTK_TARGET_OTHER_APP, TARGET_STRING}
};

static GtkTargetEntry TARGET_ENTRIES_REMOTE_DST[] = {
  {TEXT_URI_LIST_STD, GTK_TARGET_SAME_APP | GTK_TARGET_OTHER_WIDGET,
   TARGET_STRING},
  {TEXT_URI_LIST_ELEKTROID, GTK_TARGET_SAME_APP | GTK_TARGET_SAME_WIDGET,
   TARGET_STRING},
  {TEXT_URI_LIST_STD, GTK_TARGET_OTHER_APP, TARGET_STRING}
};

static GtkTargetEntry TARGET_ENTRIES_REMOTE_SRC[] = {
  {TEXT_URI_LIST_ELEKTROID, GTK_TARGET_SAME_APP | GTK_TARGET_OTHER_WIDGET,
   TARGET_STRING},
  {TEXT_URI_LIST_ELEKTROID, GTK_TARGET_SAME_APP | GTK_TARGET_SAME_WIDGET,
   TARGET_STRING}
};

static GtkTargetEntry TARGET_ENTRIES_UP_BUTTON_DST[] = {
  {TEXT_URI_LIST_STD, GTK_TARGET_SAME_APP,
   TARGET_STRING},
  {TEXT_URI_LIST_STD, GTK_TARGET_OTHER_APP,
   TARGET_STRING},
  {TEXT_URI_LIST_ELEKTROID, GTK_TARGET_SAME_APP,
   TARGET_STRING}
};

static guint TARGET_ENTRIES_LOCAL_DST_N =
G_N_ELEMENTS (TARGET_ENTRIES_LOCAL_DST);
static guint TARGET_ENTRIES_LOCAL_SRC_N =
G_N_ELEMENTS (TARGET_ENTRIES_LOCAL_SRC);
static guint TARGET_ENTRIES_REMOTE_DST_N =
G_N_ELEMENTS (TARGET_ENTRIES_REMOTE_DST);
static guint TARGET_ENTRIES_REMOTE_SRC_N =
G_N_ELEMENTS (TARGET_ENTRIES_REMOTE_SRC);
static guint TARGET_ENTRIES_UP_BUTTON_DST_N =
G_N_ELEMENTS (TARGET_ENTRIES_UP_BUTTON_DST);

static struct browser remote_browser;
static struct browser local_browser;

static struct audio audio;
static struct connector connector;
static gboolean autoplay;

static GThread *load_thread = NULL;
static GThread *task_thread = NULL;
static GThread *sysex_thread = NULL;
static struct elektroid_transfer transfer;
static struct elektroid_sysex_transfer sysex_transfer;

static GThread *notifier_thread = NULL;
struct notifier notifier;

static GtkWidget *main_window;
static GtkAboutDialog *about_dialog;
static GtkDialog *name_dialog;
static GtkEntry *name_dialog_entry;
static GtkWidget *name_dialog_accept_button;
static GtkDialog *progress_dialog;
static GtkWidget *progress_dialog_cancel_button;
static GtkWidget *progress_bar;
static GtkWidget *progress_label;
static GtkWidget *rx_sysex_button;
static GtkWidget *tx_sysex_button;
static GtkWidget *os_upgrade_button;
static GtkWidget *about_button;
static GtkWidget *remote_box;
static GtkWidget *waveform_draw_area;
static GtkStatusbar *status_bar;
static GtkListStore *devices_list_store;
static GtkComboBox *devices_combo;
static GtkWidget *upload_menuitem;
static GtkWidget *local_audio_box;
static GtkWidget *local_play_menuitem;
static GtkWidget *local_open_menuitem;
static GtkWidget *local_show_menuitem;
static GtkWidget *local_add_dir_menuitem;
static GtkWidget *local_rename_menuitem;
static GtkWidget *local_delete_menuitem;
static GtkWidget *download_menuitem;
static GtkWidget *remote_add_dir_menuitem;
static GtkWidget *remote_rename_menuitem;
static GtkWidget *remote_delete_menuitem;
static GtkWidget *play_button;
static GtkWidget *stop_button;
static GtkWidget *volume_button;
static GtkListStore *task_list_store;
static GtkWidget *task_tree_view;
static GtkWidget *cancel_task_button;
static GtkWidget *remove_tasks_button;
static GtkWidget *clear_tasks_button;
static GtkListStore *fs_list_store;
static GtkComboBox *fs_combo;
static GtkTreeViewColumn *remote_tree_view_index_column;

static const gchar *
elektroid_get_fs_name (enum connector_fs selected)
{
  for (int fs = FS_SAMPLES, i = 0; fs <= FS_DATA; fs = fs << 1, i++)
    {
      if (fs == selected)
	{
	  return ELEKTROID_FS_NAMES[i];
	}
    }
  return NULL;
}

static void
elektroid_set_file_extensions_for_fs (gchar ** extensions[],
				      enum connector_fs sel_fs)
{
  const struct fs_operations *ops;

  if (*extensions != NULL && *extensions != ELEKTROID_AUDIO_LOCAL_EXTS)
    {
      g_free ((*extensions)[0]);
      g_free (*extensions);
    }
  if (sel_fs == FS_SAMPLES)
    {
      *extensions = ELEKTROID_AUDIO_LOCAL_EXTS;
    }
  else
    {
      ops = connector_get_fs_operations (sel_fs);
      *extensions = malloc (sizeof (gchar *) * 2);
      (*extensions)[0] = connector_get_full_ext (connector.device_desc, ops);
      (*extensions)[1] = NULL;
    }
}

static const gchar *
elektroid_get_inventory_icon_for_fs (enum connector_fs sel_fs)
{
  const gchar *icon = ELEKTROID_FS_ICONS[0];

  for (int fs = FS_SAMPLES, i = 0; fs <= FS_DATA; fs <<= 1, i++)
    {
      if (sel_fs == fs)
	{
	  icon = ELEKTROID_FS_ICONS[i];
	  break;
	}
    }
  return icon;
}

static void
show_error_msg (const char *format, ...)
{
  GtkWidget *dialog;
  gchar *msg;
  va_list args;

  va_start (args, format);
  g_vasprintf (&msg, format, args);
  dialog = gtk_message_dialog_new (GTK_WINDOW (main_window),
				   GTK_DIALOG_DESTROY_WITH_PARENT |
				   GTK_DIALOG_MODAL,
				   GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE, msg);
  gtk_dialog_run (GTK_DIALOG (dialog));
  gtk_widget_destroy (dialog);
  g_free (msg);
}

static void
elektroid_load_devices (gboolean auto_select)
{
  gint i;
  gint device_index;
  GArray *devices = connector_get_system_devices ();
  struct connector_system_device device;

  debug_print (1, "Loading devices...\n");

  gtk_list_store_clear (fs_list_store);
  gtk_list_store_clear (devices_list_store);

  for (i = 0; i < devices->len; i++)
    {
      device = g_array_index (devices, struct connector_system_device, i);
      gtk_list_store_insert_with_values (devices_list_store, NULL, -1,
					 DEVICES_LIST_STORE_CARD_FIELD,
					 device.card,
					 DEVICES_LIST_STORE_NAME_FIELD,
					 device.name, -1);
    }

  g_array_free (devices, TRUE);

  device_index = auto_select && i == 1 ? 0 : -1;
  debug_print (1, "Selecting device %d...\n", device_index);
  gtk_combo_box_set_active (devices_combo, device_index);

  if (device_index == -1)
    {
      local_browser.file_icon =
	elektroid_get_inventory_icon_for_fs (FS_SAMPLES);
      elektroid_set_file_extensions_for_fs (&local_browser.extensions,
					    FS_SAMPLES);

      gtk_widget_set_visible (local_audio_box, TRUE);
      gtk_tree_view_column_set_visible (remote_tree_view_index_column, FALSE);

      browser_load_dir (&local_browser);
    }
}

static void
elektroid_update_statusbar ()
{
  gchar *status;
  gchar *statfss_str;
  gint res;
  enum connector_storage storage;
  struct connector_storage_stats statfs;
  GString *statfss;

  gtk_statusbar_pop (status_bar, 0);

  if (connector_check (&connector))
    {
      statfss = g_string_new (NULL);

      if (connector.device_desc->fss & FS_SAMPLES)
	{
	  for (storage = STORAGE_PLUS_DRIVE; storage <= STORAGE_RAM;
	       storage <<= 1)
	    {
	      if (connector.device_desc->storages & storage)
		{
		  res =
		    connector_get_storage_stats (&connector,
						 storage, &statfs);
		  if (!res)
		    {
		      g_string_append_printf (statfss, " %s %.2f%%",
					      statfs.name,
					      connector_get_storage_stats_percent
					      (&statfs));
		    }
		}
	    }
	}
      statfss_str = g_string_free (statfss, FALSE);
      status = malloc (LABEL_MAX);
      snprintf (status, LABEL_MAX, _("Connected to %s%s"),
		connector.device_name, statfss_str);
      gtk_statusbar_push (status_bar, 0, status);
      free (status);
      g_free (statfss_str);
    }
  else
    {
      gtk_statusbar_push (status_bar, 0, _("Not connected"));
    }
}

static gboolean
elektroid_get_next_queued_task (GtkTreeIter * iter,
				enum elektroid_task_type *type,
				gchar ** src, gchar ** dst, gint * fs)
{
  enum elektroid_task_status status;
  gboolean found = FALSE;
  gboolean valid =
    gtk_tree_model_get_iter_first (GTK_TREE_MODEL (task_list_store), iter);

  while (valid)
    {
      if (type)
	{
	  gtk_tree_model_get (GTK_TREE_MODEL (task_list_store), iter,
			      TASK_LIST_STORE_STATUS_FIELD, &status,
			      TASK_LIST_STORE_TYPE_FIELD, type,
			      TASK_LIST_STORE_SRC_FIELD, src,
			      TASK_LIST_STORE_DST_FIELD, dst,
			      TASK_LIST_STORE_REMOTE_FS_ID_FIELD, fs, -1);
	}
      else
	{
	  gtk_tree_model_get (GTK_TREE_MODEL (task_list_store), iter,
			      TASK_LIST_STORE_STATUS_FIELD, &status, -1);
	}

      if (status == QUEUED)
	{
	  found = TRUE;
	  break;
	}
      valid =
	gtk_tree_model_iter_next (GTK_TREE_MODEL (task_list_store), iter);
    }

  return found;
}

static gboolean
elektroid_check_connector ()
{
  GtkListStore *list_store;
  GtkTreeIter iter;
  gboolean connected = connector_check (&connector);
  gboolean queued =
    elektroid_get_next_queued_task (&iter, NULL, NULL, NULL, NULL);

  gtk_widget_set_sensitive (remote_box, connected);
  gtk_widget_set_sensitive (rx_sysex_button, connected && !queued);
  gtk_widget_set_sensitive (tx_sysex_button, connected && !queued);
  gtk_widget_set_sensitive (os_upgrade_button, connected && !queued);

  if (!connected)
    {
      list_store =
	GTK_LIST_STORE (gtk_tree_view_get_model (remote_browser.view));
      gtk_entry_set_text (remote_browser.dir_entry, "");
      gtk_list_store_clear (list_store);

      elektroid_load_devices (FALSE);
    }

  elektroid_update_statusbar ();

  return connected;
}

static gboolean
elektroid_check_connector_bg (gpointer data)
{
  elektroid_check_connector ();

  return FALSE;
}

static void
browser_refresh_devices (GtkWidget * object, gpointer data)
{
  if (connector_check (&connector))
    {
      connector_destroy (&connector);
      elektroid_check_connector ();
    }
  elektroid_load_devices (FALSE);
}

static gpointer
elektroid_join_sysex_thread ()
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

static void
elektroid_cancel_running_sysex (GtkDialog * dialog, gint response_id,
				gpointer data)
{
  g_mutex_lock (&sysex_transfer.transfer.mutex);
  sysex_transfer.transfer.active = FALSE;
  g_mutex_unlock (&sysex_transfer.transfer.mutex);
}

static void
elektroid_stop_sysex_thread ()
{
  elektroid_cancel_running_sysex (NULL, 0, NULL);
  elektroid_join_sysex_thread ();
}

static void
elektroid_progress_dialog_end (gpointer data)
{
  elektroid_cancel_running_sysex (NULL, 0, NULL);
  gtk_dialog_response (GTK_DIALOG (progress_dialog), GTK_RESPONSE_CANCEL);
}

static gboolean
elektroid_update_sysex_progress (gpointer data)
{
  gchar *text;
  gboolean active;
  enum connector_sysex_transfer_status status;

  g_mutex_lock (&sysex_transfer.transfer.mutex);
  status = sysex_transfer.transfer.status;
  g_mutex_unlock (&sysex_transfer.transfer.mutex);

  switch (status)
    {
    case WAITING:
      text = _("Waiting...");
      break;
    case SENDING:
      text = _("Sending...");
      gtk_progress_bar_pulse (GTK_PROGRESS_BAR (progress_bar));
      break;
    case RECEIVING:
      text = _("Receiving...");
      gtk_progress_bar_pulse (GTK_PROGRESS_BAR (progress_bar));
      break;
    default:
      text = "";
    }
  gtk_label_set_text (GTK_LABEL (progress_label), text);

  g_mutex_lock (&sysex_transfer.transfer.mutex);
  active = sysex_transfer.transfer.active;
  g_mutex_unlock (&sysex_transfer.transfer.mutex);

  return active;
}

static gpointer
elektroid_rx_sysex_thread (gpointer data)
{
  gchar *text;
  GByteArray *msg;

  sysex_transfer.transfer.status = WAITING;
  sysex_transfer.transfer.active = TRUE;
  sysex_transfer.transfer.timeout = DUMP_TIMEOUT;
  sysex_transfer.transfer.batch = TRUE;

  g_timeout_add (100, elektroid_update_sysex_progress, NULL);

  connector_rx_drain (&connector);
  msg = connector_rx_sysex (&connector, &sysex_transfer.transfer);
  if (msg)
    {
      text = debug_get_hex_msg (msg);
      debug_print (1, "SysEx message received (%d): %s\n", msg->len, text);
      free (text);
    }

  gtk_dialog_response (GTK_DIALOG (progress_dialog), GTK_RESPONSE_ACCEPT);

  return msg;
}

static gboolean
elektroid_start_rx_thread (gpointer data)
{
  debug_print (1, "Creating rx SysEx thread...\n");
  sysex_thread =
    g_thread_new ("sysex_thread", elektroid_rx_sysex_thread, NULL);

  return FALSE;
}

static void
elektroid_rx_sysex (GtkWidget * object, gpointer data)
{
  GtkWidget *dialog;
  GtkFileChooser *chooser;
  GtkFileFilter *filter;
  gint res;
  gchar *filename;
  gchar *filename_w_ext;
  const gchar *ext;
  GByteArray *array;
  GtkFileChooserAction action = GTK_FILE_CHOOSER_ACTION_SAVE;

  g_idle_add (elektroid_start_rx_thread, NULL);

  gtk_window_set_title (GTK_WINDOW (progress_dialog), _("Receive SysEx"));
  res = gtk_dialog_run (GTK_DIALOG (progress_dialog));
  sysex_transfer.transfer.active = FALSE;
  gtk_widget_hide (GTK_WIDGET (progress_dialog));

  array = elektroid_join_sysex_thread ();

  if (res != GTK_RESPONSE_ACCEPT)
    {
      if (array)
	{
	  g_byte_array_free (array, TRUE);
	}
      return;
    }

  if (!array)
    {
      elektroid_check_connector ();
      return;
    }

  dialog = gtk_file_chooser_dialog_new (_("Save SysEx"),
					GTK_WINDOW (main_window),
					action,
					_("_Cancel"),
					GTK_RESPONSE_CANCEL,
					_("_Save"),
					GTK_RESPONSE_ACCEPT, NULL);
  chooser = GTK_FILE_CHOOSER (dialog);
  gtk_file_chooser_set_do_overwrite_confirmation (chooser, TRUE);
  gtk_file_chooser_set_current_name (chooser, _("Received SysEx"));

  gtk_file_chooser_set_create_folders (chooser, TRUE);

  filter = gtk_file_filter_new ();
  gtk_file_filter_set_name (filter, _("SysEx Files"));
  gtk_file_filter_add_pattern (filter, "*.syx");
  gtk_file_chooser_add_filter (chooser, filter);

  gtk_file_chooser_set_filter (GTK_FILE_CHOOSER (dialog), filter);

  while (gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_ACCEPT)
    {
      filename = gtk_file_chooser_get_filename (chooser);
      ext = get_ext (filename);

      if (ext == NULL || strcmp (ext, "syx") != 0)
	{
	  filename_w_ext = g_strconcat (filename, ".syx", NULL);
	  g_free (filename);
	  filename = filename_w_ext;

	  if (g_file_test (filename, G_FILE_TEST_EXISTS))
	    {
	      gtk_file_chooser_set_filename (GTK_FILE_CHOOSER (chooser),
					     filename);
	      g_free (filename);
	      filename = NULL;
	      continue;
	    }
	}
      break;
    }

  if (filename != NULL)
    {
      debug_print (1, "Saving SysEx file...\n");
      if (save_file (filename, array, NULL))
	{
	  show_error_msg (_("Error while saving “%s”: %s."),
			  filename, g_strerror (errno));
	}
      g_free (filename);
    }

  gtk_widget_destroy (dialog);
}

static gpointer
elektroid_tx_sysex_thread (gpointer data)
{
  gchar *text;
  gint *response = malloc (sizeof (gint));

  sysex_transfer.transfer.active = TRUE;
  sysex_transfer.transfer.timeout = SYSEX_TIMEOUT;

  g_timeout_add (100, elektroid_update_sysex_progress, NULL);

  *response =
    connector_tx_sysex (&connector, sysex_transfer.data,
			&sysex_transfer.transfer);
  if (*response >= 0)
    {
      text = debug_get_hex_msg (sysex_transfer.data);
      debug_print (1, "SysEx message sent (%d): %s\n",
		   sysex_transfer.data->len, text);
      free (text);
    }

  gtk_dialog_response (GTK_DIALOG (progress_dialog), GTK_RESPONSE_CANCEL);

  return response;
}

static gboolean
elektroid_start_tx_thread (gpointer data)
{
  debug_print (1, "Creating tx SysEx thread...\n");
  sysex_thread = g_thread_new ("sysex_thread", data, NULL);

  return FALSE;
}

static void
elektroid_tx_sysex_common (GThreadFunc tx_function)
{
  GtkWidget *dialog;
  GtkFileChooser *chooser;
  GtkFileFilter *filter;
  gint res;
  char *filename;
  gint *response;
  GtkFileChooserAction action = GTK_FILE_CHOOSER_ACTION_OPEN;

  dialog = gtk_file_chooser_dialog_new (_("Open SysEx"),
					GTK_WINDOW (main_window),
					action,
					_("_Cancel"),
					GTK_RESPONSE_CANCEL,
					_("_Open"),
					GTK_RESPONSE_ACCEPT, NULL);
  chooser = GTK_FILE_CHOOSER (dialog);
  filter = gtk_file_filter_new ();
  gtk_file_filter_set_name (filter, _("SysEx Files"));
  gtk_file_filter_add_pattern (filter, "*.syx");
  gtk_file_chooser_add_filter (chooser, filter);

  res = gtk_dialog_run (GTK_DIALOG (dialog));

  if (res == GTK_RESPONSE_ACCEPT)
    {
      filename = gtk_file_chooser_get_filename (chooser);
      gtk_widget_destroy (dialog);
      debug_print (1, "Opening SysEx file...\n");

      sysex_transfer.data = g_byte_array_new ();

      if (load_file (filename, sysex_transfer.data, NULL))
	{
	  show_error_msg (_("Error while loading “%s”: %s."),
			  filename, g_strerror (errno));
	  response = NULL;
	}
      else
	{
	  g_idle_add (elektroid_start_tx_thread, tx_function);

	  gtk_window_set_title (GTK_WINDOW (progress_dialog),
				_("Send SysEx"));
	  res = gtk_dialog_run (GTK_DIALOG (progress_dialog));

	  g_mutex_lock (&sysex_transfer.transfer.mutex);
	  sysex_transfer.transfer.active = FALSE;
	  g_mutex_unlock (&sysex_transfer.transfer.mutex);

	  gtk_widget_hide (GTK_WIDGET (progress_dialog));

	  response = elektroid_join_sysex_thread ();
	}

      g_byte_array_free (sysex_transfer.data, TRUE);


      if (*response < 0)
	{
	  elektroid_check_connector ();
	}

      free (response);
    }
  else
    {
      gtk_widget_destroy (dialog);
    }
}

static void
elektroid_tx_sysex (GtkWidget * object, gpointer data)
{
  elektroid_tx_sysex_common (elektroid_tx_sysex_thread);
}

static gpointer
elektroid_os_upgrade_thread (gpointer data)
{
  gint *response = malloc (sizeof (gint));

  sysex_transfer.transfer.active = TRUE;
  sysex_transfer.transfer.timeout = SYSEX_TIMEOUT;

  g_timeout_add (100, elektroid_update_sysex_progress, NULL);

  *response =
    connector_upgrade_os (&connector, sysex_transfer.data,
			  &sysex_transfer.transfer);

  gtk_dialog_response (GTK_DIALOG (progress_dialog), GTK_RESPONSE_CANCEL);

  return response;
}

static void
elektroid_upgrade_os (GtkWidget * object, gpointer data)
{
  elektroid_tx_sysex_common (elektroid_os_upgrade_thread);
  connector_destroy (&connector);
  elektroid_check_connector ();
}

static void
elektroid_show_about (GtkWidget * object, gpointer data)
{
  gtk_dialog_run (GTK_DIALOG (about_dialog));
  gtk_widget_hide (GTK_WIDGET (about_dialog));
}

static void
elektroid_controls_set_sensitive (gboolean sensitive)
{
  gtk_widget_set_sensitive (local_play_menuitem, sensitive);
  gtk_widget_set_sensitive (play_button, sensitive);
  gtk_widget_set_sensitive (stop_button, sensitive);
}

static gboolean
elektroid_update_ui_on_load (gpointer data)
{
  gboolean ready_to_play;

  g_mutex_lock (&audio.control.mutex);
  ready_to_play = audio.sample->len >> 1 >= LOAD_BUFFER_LEN
    || (!audio.control.active && audio.sample->len > 0);
  g_mutex_unlock (&audio.control.mutex);

  if (ready_to_play)
    {
      if (audio_check (&audio))
	{
	  elektroid_controls_set_sensitive (TRUE);
	}
      if (autoplay)
	{
	  audio_play (&audio);
	}
      return FALSE;
    }

  return TRUE;
}

static void
elektroid_delete_file (GtkTreeModel * model, GtkTreePath * tree_path,
		       struct browser *browser)
{
  GtkTreeIter iter;
  gchar *path;
  gchar *id_path;
  gint err;
  struct item *item;

  gtk_tree_model_get_iter (model, &iter, tree_path);
  item = browser_get_item (model, &iter);
  path = browser_get_item_path (browser, item);
  id_path = browser_get_item_id_path (browser, item);

  debug_print (1, "Deleting %s...\n", id_path);
  err = browser->fs_ops->delete (id_path, browser->data);
  if (err < 0)
    {
      show_error_msg (_("Error while deleting “%s”: %s."),
		      path, g_strerror (errno));
    }
  else
    {
      gtk_list_store_remove (GTK_LIST_STORE (model), &iter);
    }

  g_free (path);
  g_free (id_path);
  browser_free_item (item);
}

static void
elektroid_delete_files (GtkWidget * object, gpointer data)
{
  GtkTreeRowReference *reference;
  GList *list;
  GtkTreePath *tree_path;
  GtkTreeSelection *selection;
  GtkTreeModel *model;
  GtkWidget *dialog;
  GList *tree_path_list;
  GList *ref_list;
  gint confirmation;
  struct browser *browser = data;

  dialog =
    gtk_message_dialog_new (GTK_WINDOW (main_window),
			    GTK_DIALOG_DESTROY_WITH_PARENT | GTK_DIALOG_MODAL,
			    GTK_MESSAGE_ERROR, GTK_BUTTONS_NONE,
			    _
			    ("Are you sure you want to delete the selected items?"));
  gtk_dialog_add_buttons (GTK_DIALOG (dialog), _("_Cancel"),
			  GTK_RESPONSE_CANCEL, _("_Delete"),
			  GTK_RESPONSE_ACCEPT, NULL);
  gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_ACCEPT);
  confirmation = gtk_dialog_run (GTK_DIALOG (dialog));
  gtk_widget_destroy (dialog);
  if (confirmation != GTK_RESPONSE_ACCEPT)
    {
      return;
    }

  selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (browser->view));
  model = GTK_TREE_MODEL (gtk_tree_view_get_model (browser->view));
  tree_path_list = gtk_tree_selection_get_selected_rows (selection, &model);
  ref_list = NULL;

  for (list = tree_path_list; list != NULL; list = g_list_next (list))
    {
      reference = gtk_tree_row_reference_new (model, list->data);
      ref_list = g_list_append (ref_list, reference);
    }
  g_list_free_full (tree_path_list, (GDestroyNotify) gtk_tree_path_free);

  for (list = ref_list; list != NULL; list = g_list_next (list))
    {
      tree_path = gtk_tree_row_reference_get_path (list->data);
      elektroid_delete_file (model, tree_path, browser);
    }
  g_list_free_full (ref_list, (GDestroyNotify) gtk_tree_row_reference_free);

  browser_load_dir (browser);
}

static void
elektroid_rename_item (GtkWidget * object, gpointer data)
{
  char *old_path;
  char *new_path;
  int result;
  gint err;
  GtkTreeIter iter;
  struct item *item;
  struct browser *browser = data;
  GtkTreeModel *model =
    GTK_TREE_MODEL (gtk_tree_view_get_model (browser->view));

  browser_set_selected_row_iter (browser, &iter);
  item = browser_get_item (model, &iter);
  old_path = browser_get_item_id_path (browser, item);

  gtk_entry_set_text (name_dialog_entry, item->name);
  gtk_widget_grab_focus (GTK_WIDGET (name_dialog_entry));
  gtk_widget_set_sensitive (name_dialog_accept_button, FALSE);

  gtk_window_set_title (GTK_WINDOW (name_dialog), _("Rename"));

  result = GTK_RESPONSE_ACCEPT;

  err = -1;
  while (err < 0 && result == GTK_RESPONSE_ACCEPT)
    {
      result = gtk_dialog_run (GTK_DIALOG (name_dialog));

      if (result == GTK_RESPONSE_ACCEPT)
	{
	  new_path =
	    chain_path (browser->dir, gtk_entry_get_text (name_dialog_entry));

	  err = browser->fs_ops->move (old_path, new_path, &connector);

	  if (err < 0)
	    {
	      show_error_msg (_("Error while renaming to “%s”: %s."),
			      new_path, g_strerror (errno));
	    }
	  else
	    {
	      browser_load_dir (browser);
	    }

	  free (new_path);
	}
    }

  browser_free_item (item);
  g_free (old_path);
  gtk_widget_hide (GTK_WIDGET (name_dialog));
}

static gboolean
elektroid_drag_begin (GtkWidget * widget,
		      GdkDragContext * context, gpointer data)
{
  GtkTreeIter iter;
  GtkTreeSelection *selection;
  GtkTreeModel *model;
  GList *tree_path_list;
  GList *list;
  gchar *uri;
  gchar *path;
  struct item *item;
  struct browser *browser = data;

  selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (widget));
  model = GTK_TREE_MODEL (gtk_tree_view_get_model (GTK_TREE_VIEW (widget)));
  tree_path_list = gtk_tree_selection_get_selected_rows (selection, &model);

  browser->dnd_data = g_string_new ("");
  for (list = tree_path_list; list != NULL; list = g_list_next (list))
    {
      gtk_tree_model_get_iter (model, &iter, list->data);
      item = browser_get_item (model, &iter);
      path = browser_get_item_id_path (browser, item);
      browser_free_item (item);
      if (widget == GTK_WIDGET (local_browser.view))
	{
	  uri = g_filename_to_uri (path, NULL, NULL);
	}
      else if (widget == GTK_WIDGET (remote_browser.view))
	{
	  uri = chain_path ("file://", &path[1]);
	}
      else
	{
	  continue;
	}
      g_free (path);
      g_string_append (browser->dnd_data, uri);
      g_free (uri);
      g_string_append (browser->dnd_data, "\n");
    }
  g_list_free_full (tree_path_list, (GDestroyNotify) gtk_tree_path_free);
  browser->dnd = TRUE;

  debug_print (1, "Drag begin data:\n%s\n", browser->dnd_data->str);

  return FALSE;
}

static gboolean
elektroid_drag_end (GtkWidget * widget,
		    GdkDragContext * context, gpointer data)
{
  struct browser *browser = data;

  debug_print (1, "Drag end\n");

  g_string_free (browser->dnd_data, TRUE);
  browser->dnd = FALSE;

  return FALSE;
}

static gboolean
elektroid_selection_function_true (GtkTreeSelection * selection,
				   GtkTreeModel * model,
				   GtkTreePath * path,
				   gboolean path_currently_selected,
				   gpointer data)
{
  return TRUE;
}

static gboolean
elektroid_selection_function_false (GtkTreeSelection * selection,
				    GtkTreeModel * model,
				    GtkTreePath * path,
				    gboolean path_currently_selected,
				    gpointer data)
{
  return FALSE;
}

static gboolean
elektroid_button_press (GtkWidget * treeview, GdkEventButton * event,
			gpointer data)
{
  GtkTreePath *path;
  GtkTreeSelection *selection;
  struct browser *browser = data;

  selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (browser->view));
  gtk_tree_selection_set_select_function (selection,
					  elektroid_selection_function_true,
					  NULL, NULL);

  if (event->state & (GDK_SHIFT_MASK | GDK_CONTROL_MASK))
    {
      return FALSE;
    }

  if (event->button == GDK_BUTTON_PRIMARY)
    {
      gtk_tree_view_get_path_at_pos (browser->view, event->x, event->y,
				     &path, NULL, NULL, NULL);

      if (!path)
	{
	  gtk_tree_selection_unselect_all (selection);
	  return FALSE;
	}

      if (!gtk_tree_selection_path_is_selected (selection, path))
	{
	  gtk_tree_selection_unselect_all (selection);
	  gtk_tree_selection_select_path (selection, path);
	}
      gtk_tree_path_free (path);
      gtk_tree_selection_set_select_function (selection,
					      elektroid_selection_function_false,
					      NULL, NULL);
    }
  else if (event->button == GDK_BUTTON_SECONDARY)
    {
      selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (browser->view));
      gtk_tree_view_get_path_at_pos (browser->view,
				     event->x, event->y, &path, NULL, NULL,
				     NULL);

      if (!path)
	{
	  gtk_tree_selection_unselect_all (selection);
	  gtk_menu_popup_at_pointer (browser->menu, (GdkEvent *) event);
	  return FALSE;
	}

      if (!gtk_tree_selection_path_is_selected (selection, path))
	{
	  gtk_tree_selection_unselect_all (selection);
	  gtk_tree_selection_select_path (selection, path);
	}
      gtk_tree_path_free (path);
      gtk_menu_popup_at_pointer (browser->menu, (GdkEvent *) event);

      return TRUE;
    }

  return FALSE;
}

static gboolean
elektroid_button_release (GtkWidget * treeview, GdkEventButton * event,
			  gpointer data)
{
  GtkTreePath *path;
  GtkTreeSelection *selection;
  struct browser *browser = data;

  if (event->state & (GDK_SHIFT_MASK | GDK_CONTROL_MASK))
    {
      return FALSE;
    }

  if (event->button == GDK_BUTTON_PRIMARY)
    {
      selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (browser->view));
      gtk_tree_selection_set_select_function (selection,
					      elektroid_selection_function_true,
					      NULL, NULL);

      if (!browser->dnd)
	{
	  gtk_tree_view_get_path_at_pos (browser->view, event->x, event->y,
					 &path, NULL, NULL, NULL);

	  if (!path)
	    {
	      return FALSE;
	    }

	  if (browser_get_selected_items_count (browser) > 1)
	    {
	      gtk_tree_selection_unselect_all (selection);
	      gtk_tree_selection_select_path (selection, path);
	    }
	  gtk_tree_path_free (path);
	}

      return FALSE;
    }

  return FALSE;
}

static gboolean
elektroid_queue_draw_waveform ()
{
  gtk_widget_queue_draw (waveform_draw_area);
  return FALSE;
}

static void
elektroid_redraw_sample (gdouble percent)
{
  g_idle_add (elektroid_queue_draw_waveform, NULL);
}

static gpointer
elektroid_load_sample (gpointer path)
{
  g_mutex_lock (&audio.control.mutex);
  audio.control.active = TRUE;
  g_mutex_unlock (&audio.control.mutex);

  sample_load_with_frames (path, audio.sample, &audio.control, &audio.frames);

  g_mutex_lock (&audio.control.mutex);
  audio.control.active = FALSE;
  g_mutex_unlock (&audio.control.mutex);

  free (path);

  return NULL;
}

static void
elektroid_start_load_thread (gchar * path)
{
  debug_print (1, "Creating load thread...\n");

  load_thread = g_thread_new ("load_sample", elektroid_load_sample, path);

  g_timeout_add (100, elektroid_update_ui_on_load, NULL);
}

static void
elektroid_stop_load_thread ()
{
  debug_print (1, "Stopping load thread...\n");

  g_mutex_lock (&audio.control.mutex);
  audio.control.active = FALSE;
  g_mutex_unlock (&audio.control.mutex);

  if (load_thread)
    {
      g_thread_join (load_thread);
      load_thread = NULL;
    }
}

static void
elektroid_join_task_thread ()
{
  debug_print (2, "Joining task thread...\n");
  if (task_thread)
    {
      g_thread_join (task_thread);
      task_thread = NULL;
    }
}

static void
elektroid_stop_task_thread ()
{
  debug_print (1, "Stopping task thread...\n");

  g_mutex_lock (&transfer.control.mutex);
  transfer.control.active = FALSE;
  g_mutex_unlock (&transfer.control.mutex);

  elektroid_join_task_thread ();
}

static gboolean
elektroid_remote_check_selection (gpointer data)
{
  gint count = browser_get_selected_items_count (&remote_browser);
  gboolean dl_impl = remote_browser.fs_ops->download ? TRUE : FALSE;
  gboolean move_impl = remote_browser.fs_ops->move ? TRUE : FALSE;
  gboolean del_impl = remote_browser.fs_ops->delete ? TRUE : FALSE;

  gtk_widget_set_sensitive (download_menuitem, count > 0 && dl_impl);
  gtk_widget_set_sensitive (remote_rename_menuitem, count == 1 && move_impl);
  gtk_widget_set_sensitive (remote_delete_menuitem, count == 1 && del_impl);

  return FALSE;
}

static gboolean
elektroid_local_check_selection (gpointer data)
{
  GtkTreeIter iter;
  gchar *sample_path;
  GtkTreeModel *model;
  struct item *item;
  gint count = browser_get_selected_items_count (&local_browser);

  if (!remote_browser.fs_ops || remote_browser.fs_ops->fs == FS_SAMPLES)
    {
      audio_stop (&audio, TRUE);
      elektroid_stop_load_thread ();
      audio_reset_sample (&audio);
      gtk_widget_queue_draw (waveform_draw_area);
      elektroid_controls_set_sensitive (FALSE);

      if (count == 1)
	{
	  browser_set_selected_row_iter (&local_browser, &iter);
	  model =
	    GTK_TREE_MODEL (gtk_tree_view_get_model (local_browser.view));
	  item = browser_get_item (model, &iter);
	  if (item->type == ELEKTROID_FILE)
	    {
	      gtk_widget_set_sensitive (local_open_menuitem, TRUE);
	      sample_path = chain_path (local_browser.dir, item->name);
	      elektroid_start_load_thread (sample_path);
	    }
	  else
	    {
	      gtk_widget_set_sensitive (local_open_menuitem, FALSE);
	    }
	  browser_free_item (item);
	}
    }

  gtk_widget_set_sensitive (local_show_menuitem, count <= 1);
  gtk_widget_set_sensitive (local_rename_menuitem, count == 1);
  gtk_widget_set_sensitive (local_delete_menuitem, count > 0);
  gtk_widget_set_sensitive (upload_menuitem, count > 0
			    && remote_browser.fs_ops->upload);

  return FALSE;
}

static gboolean
elektroid_draw_waveform (GtkWidget * widget, cairo_t * cr, gpointer data)
{
  guint width, height;
  GdkRGBA color;
  GtkStyleContext *context;
  int i, x_widget, x_sample;
  double x_ratio, mid_y, value;
  short *sample;

  g_mutex_lock (&audio.control.mutex);

  context = gtk_widget_get_style_context (widget);
  width = gtk_widget_get_allocated_width (widget);
  height = gtk_widget_get_allocated_height (widget);
  mid_y = height / 2.0;
  gtk_render_background (context, cr, 0, 0, width, height);
  gtk_style_context_get_color (context, gtk_style_context_get_state (context),
			       &color);
  gdk_cairo_set_source_rgba (cr, &color);

  sample = (short *) audio.sample->data;
  x_ratio = audio.frames / (double) MAX_DRAW_X;
  for (i = 0; i < MAX_DRAW_X; i++)
    {
      x_sample = i * x_ratio;
      if (x_sample < audio.sample->len >> 1)
	{
	  x_widget = i * ((double) width) / MAX_DRAW_X;
	  value = mid_y - mid_y * (sample[x_sample] / (float) SHRT_MIN);
	  cairo_move_to (cr, x_widget, mid_y);
	  cairo_line_to (cr, x_widget, value);
	  cairo_stroke (cr);
	}
    }

  g_mutex_unlock (&audio.control.mutex);

  return FALSE;
}

static void
elektroid_show_clicked (GtkWidget * object, gpointer data)
{
  GtkTreeIter iter;
  GtkTreeModel *model;
  gchar *uri;
  GVariant *params, *result;
  GVariantBuilder builder;
  GFile *file;
  GDBusProxy *proxy;
  struct item *item;
  gchar *path = NULL;
  gboolean done = FALSE;
  gint count = browser_get_selected_items_count (&local_browser);

  if (count == 0)
    {
      path = chain_path (local_browser.dir, NULL);
    }
  else if (count == 1)
    {
      browser_set_selected_row_iter (&local_browser, &iter);
      model = GTK_TREE_MODEL (gtk_tree_view_get_model (local_browser.view));
      item = browser_get_item (model, &iter);
      path = chain_path (local_browser.dir, item->name);
      browser_free_item (item);
    }
  else
    {
      return;
    }

  file = g_file_new_for_path (path);
  g_free (path);
  uri = g_file_get_uri (file);
  g_object_unref (file);

  proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SESSION,
					 G_DBUS_PROXY_FLAGS_DO_NOT_CONNECT_SIGNALS
					 |
					 G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES,
					 NULL,
					 "org.freedesktop.FileManager1",
					 "/org/freedesktop/FileManager1",
					 "org.freedesktop.FileManager1",
					 NULL, NULL);
  if (proxy)
    {
      g_variant_builder_init (&builder, G_VARIANT_TYPE ("as"));
      g_variant_builder_add (&builder, "s", uri);

      params = g_variant_new ("(ass)", &builder, "");

      result = g_dbus_proxy_call_sync (proxy, "ShowItems",
				       params, G_DBUS_CALL_FLAGS_NONE,
				       -1, NULL, NULL);

      if (result != NULL)
	{
	  done = TRUE;
	  g_variant_unref (result);
	}

      g_object_unref (proxy);
    }

  if (!done)
    {
      g_app_info_launch_default_for_uri (uri, NULL, NULL);
    }

  g_free (uri);
}

static void
elektroid_open_clicked (GtkWidget * object, gpointer data)
{
  GtkTreeIter iter;
  GtkTreeModel *model;
  gchar *path;
  gchar *uri;
  GFile *file;
  struct item *item;

  browser_set_selected_row_iter (&local_browser, &iter);
  model = GTK_TREE_MODEL (gtk_tree_view_get_model (local_browser.view));
  item = browser_get_item (model, &iter);
  path = chain_path (local_browser.dir, item->name);
  browser_free_item (item);

  file = g_file_new_for_path (path);
  g_free (path);
  uri = g_file_get_uri (file);
  g_object_unref (file);

  g_app_info_launch_default_for_uri_async (uri, NULL, NULL, NULL, NULL);
  free (uri);
}

static void
elektroid_play_clicked (GtkWidget * object, gpointer data)
{
  audio_play (&audio);
}

static void
elektroid_stop_clicked (GtkWidget * object, gpointer data)
{
  audio_stop (&audio, TRUE);
}

static void
elektroid_loop_clicked (GtkWidget * object, gpointer data)
{
  audio.loop = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (object));
}

static gboolean
elektroid_autoplay_clicked (GtkWidget * object, gboolean state, gpointer data)
{
  autoplay = state;
  return FALSE;
}

static void
elektroid_set_volume (GtkScaleButton * button, gdouble value, gpointer data)
{
  audio_set_volume (&audio, value);
}

static void
elektroid_set_volume_callback (gdouble value)
{
  gtk_scale_button_set_value (GTK_SCALE_BUTTON (volume_button), value);
}

static void
elektroid_add_dir (GtkWidget * object, gpointer data)
{
  char *pathname;
  int result;
  gint err;
  struct browser *browser = data;

  gtk_entry_set_text (name_dialog_entry, "");
  gtk_widget_grab_focus (GTK_WIDGET (name_dialog_entry));
  gtk_widget_set_sensitive (name_dialog_accept_button, FALSE);

  gtk_window_set_title (GTK_WINDOW (name_dialog), _("Add Directory"));

  result = GTK_RESPONSE_ACCEPT;

  err = -1;
  while (err < 0 && result == GTK_RESPONSE_ACCEPT)
    {
      result = gtk_dialog_run (GTK_DIALOG (name_dialog));

      if (result == GTK_RESPONSE_ACCEPT)
	{
	  pathname =
	    chain_path (browser->dir, gtk_entry_get_text (name_dialog_entry));

	  err = browser->fs_ops->mkdir (pathname, &connector);

	  if (err < 0)
	    {
	      show_error_msg (_("Error while creating dir “%s”: %s."),
			      pathname, g_strerror (errno));
	    }
	  else
	    {
	      browser_load_dir (browser);
	    }

	  free (pathname);
	}
    }

  gtk_widget_hide (GTK_WIDGET (name_dialog));
}

static void
elektroid_accept_name (GtkWidget * object, gpointer data)
{
  gtk_dialog_response (name_dialog, GTK_RESPONSE_ACCEPT);
}

static void
elektroid_cancel_name (GtkWidget * object, gpointer data)
{
  gtk_dialog_response (name_dialog, GTK_RESPONSE_CANCEL);
}

static void
elektroid_name_dialog_entry_changed (GtkWidget * object, gpointer data)
{
  if (strlen (gtk_entry_get_text (name_dialog_entry)) > 0)
    {
      gtk_widget_set_sensitive (name_dialog_accept_button, TRUE);
    }
  else
    {
      gtk_widget_set_sensitive (name_dialog_accept_button, FALSE);
    }
}

static gboolean
elektroid_get_running_task (GtkTreeIter * iter)
{
  enum elektroid_task_status status;
  gboolean found = FALSE;
  gboolean valid =
    gtk_tree_model_get_iter_first (GTK_TREE_MODEL (task_list_store), iter);

  while (valid)
    {
      gtk_tree_model_get (GTK_TREE_MODEL (task_list_store), iter,
			  TASK_LIST_STORE_STATUS_FIELD, &status, -1);

      if (status == RUNNING)
	{
	  found = TRUE;
	  break;
	}

      valid =
	gtk_tree_model_iter_next (GTK_TREE_MODEL (task_list_store), iter);
    }

  return found;
}

static const gchar *
elektroid_get_human_task_status (enum elektroid_task_status status)
{
  switch (status)
    {
    case QUEUED:
      return _("Queued");
    case RUNNING:
      return _("Running");
    case COMPLETED_OK:
      return _("Completed");
    case COMPLETED_ERROR:
      return _("Completed with errors");
    case CANCELED:
      return _("Canceled");
    default:
      return _("Undefined");
    }
}

static const gchar *
elektroid_get_human_task_type (enum elektroid_task_type type)
{
  switch (type)
    {
    case UPLOAD:
      return _("Upload");
    case DOWNLOAD:
      return _("Download");
    default:
      return _("Undefined");
    }
}

static void
elektroid_stop_running_task (GtkWidget * object, gpointer data)
{
  g_mutex_lock (&transfer.control.mutex);
  transfer.control.active = FALSE;
  g_mutex_unlock (&transfer.control.mutex);
}

static gboolean
elektroid_task_is_queued (enum elektroid_task_status status)
{
  return (status == QUEUED);
}

static gboolean
elektroid_task_is_finished (enum elektroid_task_status status)
{
  return (status == COMPLETED_OK ||
	  status == COMPLETED_ERROR || status == CANCELED);
}

static gboolean
elektroid_check_task_buttons (gpointer data)
{
  enum elektroid_task_status status;
  gboolean queued = FALSE;
  gboolean finished = FALSE;
  GtkTreeIter iter;
  gboolean valid =
    gtk_tree_model_get_iter_first (GTK_TREE_MODEL (task_list_store), &iter);

  while (valid)
    {
      gtk_tree_model_get (GTK_TREE_MODEL (task_list_store), &iter,
			  TASK_LIST_STORE_STATUS_FIELD, &status, -1);

      if (elektroid_task_is_queued (status))
	{
	  queued = TRUE;
	}

      if (elektroid_task_is_finished (status))
	{
	  finished = TRUE;
	}

      valid =
	gtk_tree_model_iter_next (GTK_TREE_MODEL (task_list_store), &iter);
    }

  gtk_widget_set_sensitive (remove_tasks_button, queued);
  gtk_widget_set_sensitive (clear_tasks_button, finished);

  return FALSE;
}

static void
elektroid_cancel_all_tasks (GtkWidget * object, gpointer data)
{
  enum elektroid_task_status status;
  GtkTreeIter iter;
  gboolean valid =
    gtk_tree_model_get_iter_first (GTK_TREE_MODEL (task_list_store), &iter);
  const gchar *canceled = elektroid_get_human_task_status (CANCELED);

  while (valid)
    {
      gtk_tree_model_get (GTK_TREE_MODEL (task_list_store), &iter,
			  TASK_LIST_STORE_STATUS_FIELD, &status, -1);

      if (status == QUEUED)
	{
	  gtk_list_store_set (task_list_store, &iter,
			      TASK_LIST_STORE_STATUS_FIELD,
			      CANCELED,
			      TASK_LIST_STORE_STATUS_HUMAN_FIELD, canceled,
			      -1);
	  valid = gtk_list_store_iter_is_valid (task_list_store, &iter);
	}
      else
	{
	  valid =
	    gtk_tree_model_iter_next (GTK_TREE_MODEL (task_list_store),
				      &iter);
	}
    }

  elektroid_stop_running_task (NULL, NULL);
  elektroid_check_task_buttons (NULL);
}

static void
elektroid_remove_tasks_on_cond (gboolean (*selector)
				(enum elektroid_task_status))
{
  enum elektroid_task_status status;
  GtkTreeIter iter;
  gboolean valid =
    gtk_tree_model_get_iter_first (GTK_TREE_MODEL (task_list_store), &iter);

  while (valid)
    {
      gtk_tree_model_get (GTK_TREE_MODEL (task_list_store), &iter,
			  TASK_LIST_STORE_STATUS_FIELD, &status, -1);

      if (selector (status))
	{
	  gtk_list_store_remove (task_list_store, &iter);
	  valid = gtk_list_store_iter_is_valid (task_list_store, &iter);
	}
      else
	{
	  valid =
	    gtk_tree_model_iter_next (GTK_TREE_MODEL (task_list_store),
				      &iter);
	}
    }

  elektroid_check_task_buttons (NULL);
}

static void
elektroid_remove_queued_tasks (GtkWidget * object, gpointer data)
{
  elektroid_remove_tasks_on_cond (elektroid_task_is_queued);
}

static void
elektroid_clear_finished_tasks (GtkWidget * object, gpointer data)
{
  elektroid_remove_tasks_on_cond (elektroid_task_is_finished);
}

static gboolean
elektroid_complete_running_task (gpointer data)
{
  GtkTreeIter iter;
  const gchar *status = elektroid_get_human_task_status (transfer.status);

  if (elektroid_get_running_task (&iter))
    {
      gtk_list_store_set (task_list_store, &iter,
			  TASK_LIST_STORE_STATUS_FIELD,
			  transfer.status,
			  TASK_LIST_STORE_STATUS_HUMAN_FIELD, status, -1);
      elektroid_stop_running_task (NULL, NULL);
      g_free (transfer.src);
      g_free (transfer.dst);

      gtk_widget_set_sensitive (cancel_task_button, FALSE);
    }
  else
    {
      debug_print (1, "No task running. Skipping...\n");
    }

  return FALSE;
}

static gboolean
elektroid_run_next_task (gpointer data)
{
  GtkTreeIter iter;
  enum elektroid_task_type type;
  gchar *src;
  gchar *dst;
  gint fs;
  GtkTreePath *path;
  gboolean transfer_active;
  gboolean found =
    elektroid_get_next_queued_task (&iter, &type, &src, &dst, &fs);
  const gchar *status_human = elektroid_get_human_task_status (RUNNING);

  g_mutex_lock (&transfer.control.mutex);
  transfer_active = transfer.control.active;
  g_mutex_unlock (&transfer.control.mutex);

  if (!transfer_active && found)
    {
      gtk_list_store_set (task_list_store, &iter,
			  TASK_LIST_STORE_STATUS_FIELD, RUNNING,
			  TASK_LIST_STORE_STATUS_HUMAN_FIELD, status_human,
			  -1);
      path =
	gtk_tree_model_get_path (GTK_TREE_MODEL (task_list_store), &iter);
      gtk_tree_view_set_cursor (GTK_TREE_VIEW (task_tree_view), path, NULL,
				FALSE);
      gtk_tree_path_free (path);
      transfer.control.active = TRUE;
      transfer.control.callback = elektroid_update_progress;
      transfer.src = src;
      transfer.dst = dst;
      transfer.fs_ops = connector_get_fs_operations (fs);
      debug_print (1, "Running task type %d from %s to %s (%s)...\n", type,
		   transfer.src, transfer.dst, elektroid_get_fs_name (fs));

      if (type == UPLOAD)
	{
	  task_thread =
	    g_thread_new ("upload_task", elektroid_upload_task, NULL);
	}
      else if (type == DOWNLOAD)
	{
	  task_thread =
	    g_thread_new ("download_task", elektroid_download_task, NULL);
	}

      gtk_widget_set_sensitive (cancel_task_button, TRUE);
    }
  else
    {
      gtk_widget_set_sensitive (rx_sysex_button, TRUE);
      gtk_widget_set_sensitive (tx_sysex_button, TRUE);
      gtk_widget_set_sensitive (os_upgrade_button, TRUE);
    }

  elektroid_check_task_buttons (NULL);

  return FALSE;
}

static gpointer
elektroid_upload_task (gpointer data)
{
  gchar *dst_path;
  gchar *dst_dir;
  gint res;
  GByteArray *array;

  if (!transfer.fs_ops->upload)
    {
      transfer.status = COMPLETED_ERROR;
      debug_print (1, "Function upload not implemented\n");
      goto end;
    }

  debug_print (1, "Local path: %s\n", transfer.src);
  debug_print (1, "Remote path: %s\n", transfer.dst);

  array = g_byte_array_new ();

  res = transfer.fs_ops->load (transfer.src, array, &transfer.control);
  if (res)
    {
      error_print ("Error while loading file\n");
      transfer.status = COMPLETED_ERROR;
      goto end_cleanup;
    }

  debug_print (1, "Writing from file %s (filesystem %s)...\n", transfer.src,
	       elektroid_get_fs_name (transfer.fs_ops->fs));

  res = transfer.fs_ops->upload (transfer.dst, array, &transfer.control,
				 remote_browser.data);
  g_idle_add (elektroid_check_connector_bg, NULL);

  if (res && transfer.control.active)
    {
      error_print ("Error while uploading\n");
      transfer.status = COMPLETED_ERROR;
    }
  else
    {
      g_mutex_lock (&transfer.control.mutex);
      if (transfer.control.active)
	{
	  transfer.status = COMPLETED_OK;
	}
      else
	{
	  transfer.status = CANCELED;
	}
      g_mutex_unlock (&transfer.control.mutex);
    }

  if (!res && transfer.fs_ops == remote_browser.fs_ops)	//There is no need to refresh the local browser
    {
      dst_path = strdup (transfer.dst);
      dst_dir = dirname (dst_path);
      if (strcmp (dst_dir, remote_browser.dir) == 0)
	{
	  g_idle_add (browser_load_dir, &remote_browser);
	}
      g_free (dst_path);
    }

end_cleanup:
  g_byte_array_free (array, TRUE);
end:
  g_idle_add (elektroid_complete_running_task, NULL);
  g_idle_add (elektroid_run_next_task, NULL);

  return NULL;
}

static void
elektroid_add_task (enum elektroid_task_type type, const char *src,
		    const char *dst, gint remote_fs_id)
{
  const gchar *status_human = elektroid_get_human_task_status (QUEUED);
  const gchar *type_human = elektroid_get_human_task_type (type);

  gtk_list_store_insert_with_values (task_list_store, NULL, -1,
				     TASK_LIST_STORE_STATUS_FIELD, QUEUED,
				     TASK_LIST_STORE_TYPE_FIELD, type,
				     TASK_LIST_STORE_SRC_FIELD, src,
				     TASK_LIST_STORE_DST_FIELD, dst,
				     TASK_LIST_STORE_PROGRESS_FIELD, 0.0,
				     TASK_LIST_STORE_STATUS_HUMAN_FIELD,
				     status_human,
				     TASK_LIST_STORE_TYPE_HUMAN_FIELD,
				     type_human,
				     TASK_LIST_STORE_REMOTE_FS_ID_FIELD,
				     remote_fs_id, -1);

  gtk_widget_set_sensitive (remove_tasks_button, TRUE);
}

static void
elektroid_add_upload_task_path (gchar * rel_path, gchar * src_dir,
				gchar * dst_dir,
				struct item_iterator *remote_dir_iter,
				gint32 * index)
{
  gint32 children_index;
  gchar *path;
  gchar *dst_abs_dir;
  gchar *upload_path;
  struct item_iterator *iter;
  struct item_iterator *children_remote_item_iterator;
  gchar *dst_abs_path = chain_path (dst_dir, rel_path);
  gchar *src_abs_path = chain_path (src_dir, rel_path);

  iter = local_browser.fs_ops->readdir (src_abs_path, local_browser.data);
  if (!iter)
    {
      dst_abs_dir = dirname (dst_abs_path);
      upload_path =
	connector_get_upload_path (&connector, remote_dir_iter,
				   remote_browser.fs_ops, dst_abs_dir,
				   src_abs_path, index);
      elektroid_add_task (UPLOAD, src_abs_path, upload_path,
			  remote_browser.fs_ops->fs);
      goto cleanup_not_dir;
    }

  if (remote_browser.fs_ops->mkdir)
    {
      if (remote_browser.fs_ops->mkdir (dst_abs_path, remote_browser.data))
	{
	  error_print ("Error while creating remote %s dir\n", dst_abs_path);
	  goto cleanup;
	}

      if (!strchr (rel_path, '/'))
	{
	  browser_load_dir (&remote_browser);
	}
    }

  children_remote_item_iterator =
    remote_browser.fs_ops->readdir (dst_abs_path, remote_browser.data);
  if (children_remote_item_iterator)
    {
      children_index = 1;
      while (!next_item_iterator (iter))
	{
	  path = chain_path (rel_path, iter->item.name);
	  elektroid_add_upload_task_path (path, src_dir, dst_dir,
					  children_remote_item_iterator,
					  &children_index);
	  free (path);
	}

      free_item_iterator (children_remote_item_iterator);
    }


cleanup:
  free_item_iterator (iter);
cleanup_not_dir:
  g_free (dst_abs_path);
  g_free (src_abs_path);
}

static void
elektroid_add_upload_task (GtkTreeModel * model,
			   GtkTreePath * path,
			   GtkTreeIter * iter, gpointer userdata)
{
  struct item *item = browser_get_item (model, iter);
  struct elektroid_add_upload_task_data *data = userdata;
  elektroid_add_upload_task_path (item->name, local_browser.dir,
				  remote_browser.dir, data->iter,
				  &data->index);
  browser_free_item (item);
}

static void
elektroid_add_upload_tasks (GtkWidget * object, gpointer userdata)
{
  gboolean queued;
  GtkTreeIter iter;
  struct elektroid_add_upload_task_data data;
  GtkTreeSelection *selection =
    gtk_tree_view_get_selection (GTK_TREE_VIEW (local_browser.view));

  if (!gtk_tree_selection_count_selected_rows (selection))
    {
      return;
    }

  queued = elektroid_get_next_queued_task (&iter, NULL, NULL, NULL, NULL);

  data.index = 1;
  data.iter =
    remote_browser.fs_ops->readdir (remote_browser.dir, remote_browser.data);
  gtk_tree_selection_selected_foreach (selection,
				       elektroid_add_upload_task, &data);
  free_item_iterator (data.iter);

  gtk_widget_set_sensitive (rx_sysex_button, FALSE);
  gtk_widget_set_sensitive (tx_sysex_button, FALSE);
  gtk_widget_set_sensitive (os_upgrade_button, FALSE);

  if (!queued)
    {
      elektroid_run_next_task (NULL);
    }
}

static gpointer
elektroid_download_task (gpointer userdata)
{
  gint res;
  GByteArray *array;

  if (!transfer.fs_ops->download)
    {
      transfer.status = COMPLETED_ERROR;
      debug_print (1, "Function download not implemented\n");
      goto end;
    }

  array = g_byte_array_new ();

  debug_print (1, "Remote path: %s\n", transfer.src);
  debug_print (1, "Local path: %s\n", transfer.dst);

  res =
    transfer.fs_ops->download (transfer.src, array,
			       &transfer.control, remote_browser.data);
  g_idle_add (elektroid_check_connector_bg, NULL);

  g_mutex_lock (&transfer.control.mutex);
  if (res && transfer.control.active)
    {
      error_print ("Error while downloading\n");
      transfer.status = COMPLETED_ERROR;
    }
  else
    {
      if (transfer.control.active)
	{
	  debug_print (1, "Writing %d bytes to file %s (filesystem %s)...\n",
		       array->len, transfer.dst,
		       elektroid_get_fs_name (transfer.fs_ops->fs));

	  res =
	    transfer.fs_ops->save (transfer.dst, array, &transfer.control);
	  if (!res)
	    {
	      transfer.status = COMPLETED_OK;
	    }
	}
      else
	{
	  transfer.status = CANCELED;
	}

      g_byte_array_free (array, TRUE);

      g_mutex_unlock (&transfer.control.mutex);
    }

end:
  g_idle_add (elektroid_complete_running_task, NULL);
  g_idle_add (elektroid_run_next_task, NULL);

  return NULL;
}

static void
elektroid_add_download_task_path (gchar * rel_path, gchar * src_dir,
				  gchar * dst_dir,
				  struct item_iterator *remote_dir_iter)
{
  struct item_iterator *iter, *iter_copy;
  gchar *path, *id, *download_path, *dst_abs_dirc, *dst_abs_dir;
  gchar *src_abs_path = chain_path (src_dir, rel_path);
  gchar *dst_abs_path = chain_path (dst_dir, rel_path);

  iter = remote_browser.fs_ops->readdir (src_abs_path, remote_browser.data);
  if (!iter)
    {
      dst_abs_dirc = strdup (dst_abs_path);
      dst_abs_dir = dirname (dst_abs_dirc);
      download_path =
	connector_get_download_path (&connector, remote_dir_iter,
				     remote_browser.fs_ops, dst_abs_dir,
				     src_abs_path);
      elektroid_add_task (DOWNLOAD, src_abs_path, download_path,
			  remote_browser.fs_ops->fs);
      g_free (dst_abs_dirc);
      g_free (download_path);
      goto cleanup_not_dir;
    }

  if (local_browser.fs_ops->mkdir (dst_abs_path, NULL))
    {
      error_print ("Error while creating local %s dir\n", dst_abs_path);
      goto cleanup;
    }

  iter_copy = copy_item_iterator (iter);
  while (!next_item_iterator (iter))
    {
      id = remote_browser.fs_ops->getid (&iter->item);
      path = chain_path (rel_path, id);
      elektroid_add_download_task_path (path, src_dir, dst_dir, iter_copy);
      g_free (path);
      g_free (id);
    }
  free_item_iterator (iter_copy);

cleanup:
  free_item_iterator (iter);
cleanup_not_dir:
  free (dst_abs_path);
  free (src_abs_path);
}

static void
elektroid_add_download_task (GtkTreeModel * model,
			     GtkTreePath * path,
			     GtkTreeIter * iter, gpointer data)
{
  struct item *item = browser_get_item (model, iter);
  char *id = remote_browser.fs_ops->getid (item);

  elektroid_add_download_task_path (id, remote_browser.dir,
				    local_browser.dir, data);
  g_free (id);
  browser_free_item (item);
}

static void
elektroid_add_download_tasks (GtkWidget * object, gpointer data)
{
  gboolean queued;
  GtkTreeIter iter;
  struct item_iterator *item_iterator;
  GtkTreeSelection *selection =
    gtk_tree_view_get_selection (GTK_TREE_VIEW (remote_browser.view));

  if (!gtk_tree_selection_count_selected_rows (selection))
    {
      return;
    }

  queued = elektroid_get_next_queued_task (&iter, NULL, NULL, NULL, NULL);

  item_iterator =
    remote_browser.fs_ops->readdir (remote_browser.dir, remote_browser.data);
  gtk_tree_selection_selected_foreach (selection, elektroid_add_download_task,
				       item_iterator);
  free_item_iterator (item_iterator);

  gtk_widget_set_sensitive (rx_sysex_button, FALSE);
  gtk_widget_set_sensitive (tx_sysex_button, FALSE);
  gtk_widget_set_sensitive (os_upgrade_button, FALSE);

  if (!queued)
    {
      elektroid_run_next_task (NULL);
    }
}

static gboolean
elektroid_set_progress_value (gpointer data)
{
  GtkTreeIter iter;
  gdouble *value = data;

  if (elektroid_get_running_task (&iter))
    {
      gtk_list_store_set (task_list_store, &iter,
			  TASK_LIST_STORE_PROGRESS_FIELD,
			  100.0 * (*value), -1);
    }

  free (data);

  return FALSE;
}

static void
elektroid_update_progress (gdouble progress)
{
  gdouble *value = malloc (sizeof (gdouble));
  *value = progress;
  g_idle_add (elektroid_set_progress_value, value);
}

static gboolean
elektroid_common_key_press (GtkWidget * widget, GdkEventKey * event,
			    gpointer data)
{
  gint count;
  GtkAllocation allocation;
  GdkWindow *gdk_window;
  struct browser *browser = data;

  if (event->keyval == GDK_KEY_Menu)
    {
      count = browser_get_selected_items_count (browser);
      gtk_widget_get_allocation (GTK_WIDGET (browser->view), &allocation);
      gdk_window = gtk_widget_get_window (GTK_WIDGET (browser->view));
      gtk_menu_popup_at_rect (browser->menu, gdk_window, &allocation,
			      GDK_GRAVITY_CENTER, GDK_GRAVITY_NORTH_WEST,
			      NULL);
      return TRUE;
    }
  else if (event->keyval == GDK_KEY_F2)
    {
      count = browser_get_selected_items_count (browser);
      if (count == 1 && browser->fs_ops->rename)
	{
	  elektroid_rename_item (NULL, browser);
	}
      return TRUE;
    }
  else if (event->keyval == GDK_KEY_Delete)
    {
      if (browser_get_selected_items_count (browser) > 0
	  && browser->fs_ops->delete)
	{
	  elektroid_delete_files (NULL, browser);
	}
      return TRUE;
    }
  else if (event->state & GDK_CONTROL_MASK && event->keyval == GDK_KEY_r)
    {
      browser_load_dir (browser);
      return TRUE;
    }
  else if (event->state & GDK_CONTROL_MASK
	   && (event->keyval == GDK_KEY_U || event->keyval == GDK_KEY_u))
    {
      browser_go_up (NULL, browser);
      return TRUE;
    }
  else if (event->state & GDK_CONTROL_MASK
	   && event->state & GDK_SHIFT_MASK && (event->keyval == GDK_KEY_N
						|| event->keyval ==
						GDK_KEY_n))
    {
      elektroid_add_dir (NULL, browser);
      return TRUE;
    }
  else
    {
      return FALSE;
    }
}

static gboolean
elektroid_remote_key_press (GtkWidget * widget, GdkEventKey * event,
			    gpointer data)
{
  if (event->type == GDK_KEY_PRESS)
    {
      if (event->state & GDK_CONTROL_MASK && event->keyval == GDK_KEY_Left)
	{
	  if (remote_browser.fs_ops->download)
	    {
	      elektroid_add_download_tasks (NULL, NULL);
	    }
	  return TRUE;
	}
      else
	{
	  return elektroid_common_key_press (widget, event, data);
	}
    }

  return FALSE;
}

static gboolean
elektroid_local_key_press (GtkWidget * widget, GdkEventKey * event,
			   gpointer data)
{
  if (event->type == GDK_KEY_PRESS)
    {
      if (event->state & GDK_CONTROL_MASK && event->keyval == GDK_KEY_Right)
	{
	  if (remote_browser.fs_ops->upload)
	    {
	      elektroid_add_upload_tasks (NULL, NULL);
	    }
	  return TRUE;
	}
      else
	{
	  return elektroid_common_key_press (widget, event, data);
	}
    }

  return FALSE;
}

static void
elektroid_set_fs (GtkWidget * object, gpointer data)
{
  GtkTreeIter iter;
  GtkTreeSortable *sortable;
  GValue fsv = G_VALUE_INIT;
  enum connector_fs fs;

  if (gtk_combo_box_get_active_iter (fs_combo, &iter) == TRUE)
    {
      gtk_tree_model_get_value (GTK_TREE_MODEL (fs_list_store),
				&iter, FS_LIST_STORE_ID_FIELD, &fsv);
      fs = g_value_get_uint (&fsv);

      remote_browser.fs_ops = connector_get_fs_operations (fs);
      strcpy (remote_browser.dir, "/");
      browser_load_dir (&remote_browser);

      remote_browser.file_icon = elektroid_get_inventory_icon_for_fs (fs);
      local_browser.file_icon = elektroid_get_inventory_icon_for_fs (fs);
      elektroid_set_file_extensions_for_fs (&local_browser.extensions, fs);
      browser_load_dir (&local_browser);

      gtk_widget_set_sensitive (remote_browser.up_button,
				remote_browser.fs_ops->readdir != NULL);
      gtk_widget_set_sensitive (remote_browser.refresh_button,
				remote_browser.fs_ops->readdir != NULL);

      gtk_widget_set_visible (remote_add_dir_menuitem,
			      remote_browser.fs_ops->mkdir != NULL);
      gtk_widget_set_visible (remote_rename_menuitem,
			      remote_browser.fs_ops->rename != NULL);
      gtk_widget_set_visible (remote_delete_menuitem,
			      remote_browser.fs_ops->delete != NULL);
      gtk_widget_set_visible (local_audio_box, fs == FS_SAMPLES);
      gtk_tree_view_column_set_visible (remote_tree_view_index_column,
					fs == FS_DATA);
      if (fs == FS_DATA)
	{
	  audio_stop (&audio, TRUE);
	}

      sortable =
	GTK_TREE_SORTABLE (gtk_tree_view_get_model (remote_browser.view));

      if (fs == FS_SAMPLES)
	{
	  gtk_tree_sortable_set_sort_func (sortable,
					   BROWSER_LIST_STORE_NAME_FIELD,
					   browser_sort_samples, NULL, NULL);
	  gtk_tree_sortable_set_sort_column_id (sortable,
						BROWSER_LIST_STORE_NAME_FIELD,
						GTK_TREE_SORTABLE_DEFAULT_SORT_COLUMN_ID);
	}
      else if (fs == FS_DATA)
	{
	  gtk_tree_sortable_set_sort_func (sortable,
					   BROWSER_LIST_STORE_INDEX_FIELD,
					   browser_sort_data, NULL, NULL);
	  gtk_tree_sortable_set_sort_column_id (sortable,
						BROWSER_LIST_STORE_INDEX_FIELD,
						GTK_TREE_SORTABLE_DEFAULT_SORT_COLUMN_ID);
	}
    }
}

static void
elektroid_fill_fs_combo ()
{
  for (int fs = FS_SAMPLES, i = 0; fs <= FS_DATA; fs = fs << 1, i++)
    {
      if (connector.device_desc->fss & fs)
	{
	  gtk_list_store_insert_with_values (fs_list_store, NULL, -1,
					     FS_LIST_STORE_ID_FIELD,
					     fs,
					     FS_LIST_STORE_ICON_FIELD,
					     ELEKTROID_FS_ICONS[i],
					     FS_LIST_STORE_NAME_FIELD,
					     ELEKTROID_FS_NAMES[i], -1);
	}
    }

  debug_print (1, "Selecting first filesystem...\n");
  gtk_combo_box_set_active (fs_combo, 0);
}

static void
elektroid_set_device (GtkWidget * object, gpointer data)
{
  GtkTreeIter iter;
  GValue cardv = G_VALUE_INIT;
  guint card;

  if (gtk_combo_box_get_active_iter (devices_combo, &iter) == TRUE)
    {
      if (connector_check (&connector))
	{
	  connector_destroy (&connector);
	}

      gtk_tree_model_get_value (GTK_TREE_MODEL (devices_list_store),
				&iter, DEVICES_LIST_STORE_CARD_FIELD, &cardv);

      card = g_value_get_uint (&cardv);

      if (connector_init (&connector, card) < 0)
	{
	  error_print ("Error while connecting\n");
	}

      if (elektroid_check_connector ())
	{
	  elektroid_fill_fs_combo ();
	}
    }
}

static void
elektroid_dnd_received (GtkWidget * widget, GdkDragContext * context,
			gint x, gint y,
			GtkSelectionData * selection_data,
			guint info, guint time, gpointer userdata)
{
  gchar *data;
  gchar **uris;
  gchar *filename;
  gchar *path_basename;
  gchar *path_dirname;
  gchar *name;
  gchar *dir;
  gchar *dst_path;
  GtkTreeIter iter;
  gboolean queued;
  GdkAtom type;
  gchar *type_name;
  gint res;
  gint32 index = 1;
  struct item_iterator *remote_item_iterator;
  struct browser *browser = userdata;

  if (selection_data == NULL
      || !gtk_selection_data_get_length (selection_data)
      || info != TARGET_STRING)
    {
      goto end;
    }

  type = gtk_selection_data_get_data_type (selection_data);
  type_name = gdk_atom_name (type);

  data = (gchar *) gtk_selection_data_get_data (selection_data);
  debug_print (1, "DND received data (%s):\n%s\n", type_name, data);

  uris = g_uri_list_extract_uris (data);
  queued = elektroid_get_next_queued_task (&iter, NULL, NULL, NULL, NULL);

  remote_item_iterator =
    remote_browser.fs_ops->readdir (remote_browser.dir, remote_browser.data);

  for (int i = 0; uris[i] != NULL; i++)
    {
      filename = g_filename_from_uri (uris[i], NULL, NULL);
      path_basename = strdup (filename);
      path_dirname = strdup (filename);
      name = basename (path_basename);
      dir = dirname (path_dirname);

      if (widget == GTK_WIDGET (local_browser.view))
	{
	  if (strcmp (type_name, TEXT_URI_LIST_STD) == 0)
	    {
	      if (strcmp (dir, local_browser.dir))
		{
		  dst_path = chain_path (local_browser.dir, name);
		  res = local_browser.fs_ops->move (filename, dst_path, NULL);
		  if (res)
		    {
		      show_error_msg (_(MSG_ERROR_MOVING), filename,
				      dst_path, g_strerror (errno));
		    }
		  g_free (dst_path);
		}
	      else
		{
		  debug_print (1, MSG_WARN_SAME_SRC_DST);
		}
	    }
	  else if (strcmp (type_name, TEXT_URI_LIST_ELEKTROID) == 0)
	    {
	      elektroid_add_download_task_path (name, dir, local_browser.dir,
						remote_item_iterator);
	    }
	}
      else if (widget == GTK_WIDGET (remote_browser.view))
	{
	  if (strcmp (type_name, TEXT_URI_LIST_ELEKTROID) == 0)
	    {
	      if (strcmp (dir, remote_browser.dir))
		{
		  dst_path =
		    connector_get_upload_path (&connector,
					       remote_item_iterator,
					       remote_browser.fs_ops,
					       remote_browser.dir, name,
					       &index);
		  res =
		    remote_browser.fs_ops->move (filename, dst_path,
						 remote_browser.data);
		  if (res)
		    {
		      show_error_msg (_(MSG_ERROR_MOVING), filename,
				      dst_path, g_strerror (errno));
		    }
		  g_free (dst_path);
		  browser_load_dir (browser);
		}
	      else
		{
		  debug_print (1, MSG_WARN_SAME_SRC_DST);
		}
	    }
	  else if (strcmp (type_name, TEXT_URI_LIST_STD) == 0)
	    {
	      elektroid_add_upload_task_path (name, dir, remote_browser.dir,
					      remote_item_iterator, &index);
	    }
	}

      g_free (path_basename);
      g_free (path_dirname);
      g_free (filename);
    }

  free_item_iterator (remote_item_iterator);

  if (!queued)
    {
      elektroid_run_next_task (NULL);
    }

  g_strfreev (uris);

end:
  gtk_drag_finish (context, TRUE, TRUE, time);
}

static void
elektroid_dnd_get (GtkWidget * widget,
		   GdkDragContext * context,
		   GtkSelectionData * selection_data,
		   guint info, guint time, gpointer user_data)
{
  GdkAtom type;
  gchar *type_name;
  struct browser *browser = user_data;

  switch (info)
    {
    case TARGET_STRING:
      debug_print (1, "Creating DND data...\n");

      gtk_selection_data_set (selection_data,
			      gtk_selection_data_get_target
			      (selection_data), 8,
			      (guchar *) browser->dnd_data->str,
			      browser->dnd_data->len);

      type = gtk_selection_data_get_data_type (selection_data);
      type_name = gdk_atom_name (type);
      debug_print (1, "DND sent data (%s):\n%s\n", type_name,
		   browser->dnd_data->str);

      break;
    default:
      error_print ("DND type not supported\n");
    }
}

static gboolean
elektroid_drag_list_timeout (gpointer user_data)
{
  struct browser *browser = user_data;
  gchar *spath;

  spath = gtk_tree_path_to_string (browser->dnd_motion_path);
  debug_print (2, "Getting into path: %s...\n", spath);
  g_free (spath);

  browser_item_activated (browser->view, browser->dnd_motion_path, NULL,
			  browser);

  gtk_tree_path_free (browser->dnd_motion_path);
  browser->dnd_timeout_function_id = 0;
  browser->dnd_motion_path = NULL;
  return FALSE;
}

static gboolean
elektroid_drag_motion_list (GtkWidget * widget,
			    GdkDragContext * context,
			    gint wx, gint wy, guint time, gpointer user_data)
{
  GtkTreePath *path;
  GtkTreeModel *model;
  GtkTreeIter iter;
  gchar *spath;
  gint tx;
  gint ty;
  GtkTreeSelection *selection;
  struct item *item;
  struct browser *browser = user_data;

  gtk_tree_view_convert_widget_to_bin_window_coords
    (GTK_TREE_VIEW (widget), wx, wy, &tx, &ty);

  if (gtk_tree_view_get_path_at_pos
      (GTK_TREE_VIEW (widget), tx, ty, &path, NULL, NULL, NULL))
    {
      spath = gtk_tree_path_to_string (path);
      debug_print (2, "Drag motion path: %s\n", spath);
      g_free (spath);

      selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (browser->view));
      if (gtk_tree_selection_path_is_selected (selection, path))
	{
	  if (browser->dnd_timeout_function_id)
	    {
	      g_source_remove (browser->dnd_timeout_function_id);
	      browser->dnd_timeout_function_id = 0;
	    }
	  return TRUE;
	}

      model =
	GTK_TREE_MODEL (gtk_tree_view_get_model (GTK_TREE_VIEW (widget)));
      gtk_tree_model_get_iter (model, &iter, path);
      item = browser_get_item (model, &iter);

      if (item->type == ELEKTROID_DIR && (!browser->dnd_motion_path
					  || (browser->dnd_motion_path
					      &&
					      gtk_tree_path_compare
					      (browser->dnd_motion_path,
					       path))))
	{
	  if (browser->dnd_timeout_function_id)
	    {
	      g_source_remove (browser->dnd_timeout_function_id);
	      browser->dnd_timeout_function_id = 0;
	    }
	  browser->dnd_timeout_function_id =
	    g_timeout_add (DND_TIMEOUT, elektroid_drag_list_timeout, browser);
	}

      browser_free_item (item);
    }
  else
    {
      if (browser->dnd_timeout_function_id)
	{
	  g_source_remove (browser->dnd_timeout_function_id);
	  browser->dnd_timeout_function_id = 0;
	}

    }

  if (browser->dnd_motion_path)
    {
      gtk_tree_path_free (browser->dnd_motion_path);
      browser->dnd_motion_path = NULL;
    }

  browser->dnd_motion_path = path;

  return TRUE;
}

static void
elektroid_drag_leave_list (GtkWidget * widget,
			   GdkDragContext * context,
			   guint time, gpointer user_data)
{
  struct browser *browser = user_data;
  if (browser->dnd_timeout_function_id)
    {
      g_source_remove (browser->dnd_timeout_function_id);
      browser->dnd_timeout_function_id = 0;
    }
}

static gboolean
elektroid_drag_up_timeout (gpointer user_data)
{
  struct browser *browser = user_data;

  browser_go_up (NULL, browser);

  return TRUE;
}

static gboolean
elektroid_drag_motion_up (GtkWidget * widget,
			  GdkDragContext * context,
			  gint wx, gint wy, guint time, gpointer user_data)
{
  struct browser *browser = user_data;

  if (browser->dnd_timeout_function_id)
    {
      g_source_remove (browser->dnd_timeout_function_id);
      browser->dnd_timeout_function_id = 0;
    }
  browser->dnd_timeout_function_id =
    g_timeout_add (DND_TIMEOUT, elektroid_drag_up_timeout, browser);

  return TRUE;
}

static void
elektroid_drag_leave_up (GtkWidget * widget,
			 GdkDragContext * context,
			 guint time, gpointer user_data)
{
  struct browser *browser = user_data;
  if (browser->dnd_timeout_function_id)
    {
      g_source_remove (browser->dnd_timeout_function_id);
      browser->dnd_timeout_function_id = 0;
    }
}

static void
elektroid_notify_local_dir_change (struct browser *browser)
{
  notifier_set_dir (&notifier, browser->dir);
}

static void
elektroid_quit ()
{
  elektroid_stop_sysex_thread ();
  elektroid_stop_task_thread ();
  elektroid_stop_load_thread ();

  notifier.running = 0;
  notifier_close (&notifier);
  g_thread_join (notifier_thread);
  notifier_free (&notifier);

  debug_print (1, "Quitting GTK+...\n");
  gtk_main_quit ();
}

static gboolean
elektroid_delete_window (GtkWidget * widget, GdkEvent * event, gpointer data)
{
  elektroid_quit ();
  return FALSE;
}

static int
elektroid_run (int argc, char *argv[], gchar * local_dir)
{
  GtkBuilder *builder;
  GtkCssProvider *css_provider;
  GtkTreeSortable *sortable;
  GtkWidget *name_dialog_cancel_button;
  GtkWidget *refresh_devices_button;
  GtkWidget *hostname_label;
  GtkWidget *loop_button;
  GtkWidget *autoplay_switch;
  char *glade_file = malloc (PATH_MAX);
  char *css_file = malloc (PATH_MAX);
  char hostname[LABEL_MAX];

  if (snprintf
      (glade_file, PATH_MAX, "%s/%s/res/gui.glade", DATADIR,
       PACKAGE) >= PATH_MAX)
    {
      error_print ("Path too long\n");
      return -1;
    }

  if (snprintf
      (css_file, PATH_MAX, "%s/%s/res/gui.css", DATADIR, PACKAGE) >= PATH_MAX)
    {
      error_print ("Path too long\n");
      return -1;
    }

  gtk_init (&argc, &argv);
  builder = gtk_builder_new ();
  gtk_builder_add_from_file (builder, glade_file, NULL);
  free (glade_file);

  css_provider = gtk_css_provider_new ();
  gtk_css_provider_load_from_path (css_provider, css_file, NULL);
  gtk_style_context_add_provider_for_screen (gdk_screen_get_default (),
					     GTK_STYLE_PROVIDER
					     (css_provider),
					     GTK_STYLE_PROVIDER_PRIORITY_USER);
  free (css_file);

  main_window = GTK_WIDGET (gtk_builder_get_object (builder, "main_window"));
  gtk_window_resize (GTK_WINDOW (main_window), 1, 1);	//Compact window

  about_dialog =
    GTK_ABOUT_DIALOG (gtk_builder_get_object (builder, "about_dialog"));
  gtk_about_dialog_set_version (about_dialog, PACKAGE_VERSION);

  name_dialog = GTK_DIALOG (gtk_builder_get_object (builder, "name_dialog"));
  name_dialog_accept_button =
    GTK_WIDGET (gtk_builder_get_object
		(builder, "name_dialog_accept_button"));
  name_dialog_cancel_button =
    GTK_WIDGET (gtk_builder_get_object
		(builder, "name_dialog_cancel_button"));
  name_dialog_entry =
    GTK_ENTRY (gtk_builder_get_object (builder, "name_dialog_entry"));

  progress_dialog =
    GTK_DIALOG (gtk_builder_get_object (builder, "progress_dialog"));
  progress_dialog_cancel_button =
    GTK_WIDGET (gtk_builder_get_object
		(builder, "progress_dialog_cancel_button"));
  progress_bar =
    GTK_WIDGET (gtk_builder_get_object (builder, "progress_bar"));
  progress_label =
    GTK_WIDGET (gtk_builder_get_object (builder, "progress_label"));

  rx_sysex_button =
    GTK_WIDGET (gtk_builder_get_object (builder, "rx_sysex_button"));
  tx_sysex_button =
    GTK_WIDGET (gtk_builder_get_object (builder, "tx_sysex_button"));
  os_upgrade_button =
    GTK_WIDGET (gtk_builder_get_object (builder, "os_upgrade_button"));
  about_button =
    GTK_WIDGET (gtk_builder_get_object (builder, "about_button"));

  hostname_label =
    GTK_WIDGET (gtk_builder_get_object (builder, "hostname_label"));

  remote_box = GTK_WIDGET (gtk_builder_get_object (builder, "remote_box"));
  local_audio_box =
    GTK_WIDGET (gtk_builder_get_object (builder, "local_audio_box"));
  waveform_draw_area =
    GTK_WIDGET (gtk_builder_get_object (builder, "waveform_draw_area"));
  play_button = GTK_WIDGET (gtk_builder_get_object (builder, "play_button"));
  stop_button = GTK_WIDGET (gtk_builder_get_object (builder, "stop_button"));
  loop_button = GTK_WIDGET (gtk_builder_get_object (builder, "loop_button"));
  autoplay_switch =
    GTK_WIDGET (gtk_builder_get_object (builder, "autoplay_switch"));
  volume_button =
    GTK_WIDGET (gtk_builder_get_object (builder, "volume_button"));
  status_bar = GTK_STATUSBAR (gtk_builder_get_object (builder, "status_bar"));

  g_signal_connect (main_window, "delete-event",
		    G_CALLBACK (elektroid_delete_window), NULL);

  g_signal_connect (progress_dialog_cancel_button, "clicked",
		    G_CALLBACK (elektroid_progress_dialog_end), NULL);
  g_signal_connect (progress_dialog, "response",
		    G_CALLBACK (elektroid_cancel_running_sysex), NULL);

  g_signal_connect (rx_sysex_button, "clicked",
		    G_CALLBACK (elektroid_rx_sysex), NULL);
  g_signal_connect (tx_sysex_button, "clicked",
		    G_CALLBACK (elektroid_tx_sysex), NULL);
  g_signal_connect (os_upgrade_button, "clicked",
		    G_CALLBACK (elektroid_upgrade_os), NULL);
  g_signal_connect (about_button, "clicked",
		    G_CALLBACK (elektroid_show_about), NULL);

  g_signal_connect (name_dialog_accept_button, "clicked",
		    G_CALLBACK (elektroid_accept_name), NULL);
  g_signal_connect (name_dialog_cancel_button, "clicked",
		    G_CALLBACK (elektroid_cancel_name), NULL);
  g_signal_connect (name_dialog_entry, "changed",
		    G_CALLBACK (elektroid_name_dialog_entry_changed),
		    name_dialog_accept_button);

  g_signal_connect (waveform_draw_area, "draw",
		    G_CALLBACK (elektroid_draw_waveform), NULL);
  g_signal_connect (play_button, "clicked",
		    G_CALLBACK (elektroid_play_clicked), NULL);
  g_signal_connect (stop_button, "clicked",
		    G_CALLBACK (elektroid_stop_clicked), NULL);
  g_signal_connect (loop_button, "clicked",
		    G_CALLBACK (elektroid_loop_clicked), NULL);
  g_signal_connect (autoplay_switch, "state-set",
		    G_CALLBACK (elektroid_autoplay_clicked), NULL);
  g_signal_connect (volume_button, "value_changed",
		    G_CALLBACK (elektroid_set_volume), NULL);

  download_menuitem =
    GTK_WIDGET (gtk_builder_get_object (builder, "download_menuitem"));
  remote_add_dir_menuitem =
    GTK_WIDGET (gtk_builder_get_object (builder, "remote_add_dir_menuitem"));
  remote_rename_menuitem =
    GTK_WIDGET (gtk_builder_get_object (builder, "remote_rename_menuitem"));
  remote_delete_menuitem =
    GTK_WIDGET (gtk_builder_get_object (builder, "remote_delete_menuitem"));
  g_signal_connect (download_menuitem, "activate",
		    G_CALLBACK (elektroid_add_download_tasks), NULL);
  g_signal_connect (remote_add_dir_menuitem, "activate",
		    G_CALLBACK (elektroid_add_dir), &remote_browser);
  g_signal_connect (remote_rename_menuitem, "activate",
		    G_CALLBACK (elektroid_rename_item), &remote_browser);
  g_signal_connect (remote_delete_menuitem, "activate",
		    G_CALLBACK (elektroid_delete_files), &remote_browser);

  upload_menuitem =
    GTK_WIDGET (gtk_builder_get_object (builder, "upload_menuitem"));
  local_play_menuitem =
    GTK_WIDGET (gtk_builder_get_object (builder, "local_play_menuitem"));
  local_open_menuitem =
    GTK_WIDGET (gtk_builder_get_object (builder, "local_open_menuitem"));
  local_show_menuitem =
    GTK_WIDGET (gtk_builder_get_object (builder, "local_show_menuitem"));
  local_add_dir_menuitem =
    GTK_WIDGET (gtk_builder_get_object (builder, "local_add_dir_menuitem"));
  local_rename_menuitem =
    GTK_WIDGET (gtk_builder_get_object (builder, "local_rename_menuitem"));
  local_delete_menuitem =
    GTK_WIDGET (gtk_builder_get_object (builder, "local_delete_menuitem"));
  g_signal_connect (upload_menuitem, "activate",
		    G_CALLBACK (elektroid_add_upload_tasks), NULL);
  g_signal_connect (local_play_menuitem, "activate",
		    G_CALLBACK (elektroid_play_clicked), NULL);
  g_signal_connect (local_open_menuitem, "activate",
		    G_CALLBACK (elektroid_open_clicked), NULL);
  g_signal_connect (local_show_menuitem, "activate",
		    G_CALLBACK (elektroid_show_clicked), NULL);
  g_signal_connect (local_add_dir_menuitem, "activate",
		    G_CALLBACK (elektroid_add_dir), &local_browser);
  g_signal_connect (local_rename_menuitem, "activate",
		    G_CALLBACK (elektroid_rename_item), &local_browser);
  g_signal_connect (local_delete_menuitem, "activate",
		    G_CALLBACK (elektroid_delete_files), &local_browser);

  remote_browser = (struct browser)
  {
    .view =
      GTK_TREE_VIEW (gtk_builder_get_object (builder, "remote_tree_view")),
    .up_button =
      GTK_WIDGET (gtk_builder_get_object (builder, "remote_up_button")),
    .refresh_button =
      GTK_WIDGET (gtk_builder_get_object (builder, "remote_refresh_button")),
    .dir_entry =
      GTK_ENTRY (gtk_builder_get_object (builder, "remote_dir_entry")),
    .menu = GTK_MENU (gtk_builder_get_object (builder, "remote_menu")),
    .dir = malloc (PATH_MAX),
    .check_selection = elektroid_remote_check_selection,
    .file_icon = NULL,
    .fs_ops = connector_get_fs_operations (-1),
    .data = &connector,
    .notify_dir_change = NULL,
    .check_callback = elektroid_check_connector
  };
  remote_tree_view_index_column =
    GTK_TREE_VIEW_COLUMN (gtk_builder_get_object
			  (builder, "remote_tree_view_index_column"));

  g_signal_connect (gtk_tree_view_get_selection (remote_browser.view),
		    "changed", G_CALLBACK (browser_selection_changed),
		    &remote_browser);
  g_signal_connect (remote_browser.view, "row-activated",
		    G_CALLBACK (browser_item_activated), &remote_browser);
  g_signal_connect (remote_browser.up_button, "clicked",
		    G_CALLBACK (browser_go_up), &remote_browser);
  g_signal_connect (remote_browser.refresh_button, "clicked",
		    G_CALLBACK (browser_refresh), &remote_browser);
  g_signal_connect (remote_browser.view, "button-press-event",
		    G_CALLBACK (elektroid_button_press), &remote_browser);
  g_signal_connect (remote_browser.view, "button-release-event",
		    G_CALLBACK (elektroid_button_release), &remote_browser);
  g_signal_connect (remote_browser.view, "key-press-event",
		    G_CALLBACK (elektroid_remote_key_press), &remote_browser);
  g_signal_connect (remote_browser.view, "drag-begin",
		    G_CALLBACK (elektroid_drag_begin), &remote_browser);
  g_signal_connect (remote_browser.view, "drag-end",
		    G_CALLBACK (elektroid_drag_end), &remote_browser);
  g_signal_connect (remote_browser.view, "drag-data-get",
		    G_CALLBACK (elektroid_dnd_get), &remote_browser);
  g_signal_connect (remote_browser.view, "drag-data-received",
		    G_CALLBACK (elektroid_dnd_received), &remote_browser);
  g_signal_connect (remote_browser.view, "drag-motion",
		    G_CALLBACK (elektroid_drag_motion_list), &remote_browser);
  g_signal_connect (remote_browser.view, "drag-leave",
		    G_CALLBACK (elektroid_drag_leave_list), &remote_browser);
  g_signal_connect (remote_browser.up_button, "drag-motion",
		    G_CALLBACK (elektroid_drag_motion_up), &remote_browser);
  g_signal_connect (remote_browser.up_button, "drag-leave",
		    G_CALLBACK (elektroid_drag_leave_up), &remote_browser);

  gtk_drag_source_set ((GtkWidget *) remote_browser.view, GDK_BUTTON1_MASK,
		       TARGET_ENTRIES_REMOTE_SRC, TARGET_ENTRIES_REMOTE_SRC_N,
		       GDK_ACTION_COPY);
  gtk_drag_dest_set ((GtkWidget *) remote_browser.view, GTK_DEST_DEFAULT_ALL,
		     TARGET_ENTRIES_REMOTE_DST, TARGET_ENTRIES_REMOTE_DST_N,
		     GDK_ACTION_COPY);
  gtk_drag_dest_set ((GtkWidget *) remote_browser.up_button,
		     GTK_DEST_DEFAULT_MOTION | GTK_DEST_DEFAULT_HIGHLIGHT,
		     TARGET_ENTRIES_UP_BUTTON_DST,
		     TARGET_ENTRIES_UP_BUTTON_DST_N, GDK_ACTION_COPY);

  local_browser = (struct browser)
  {
    .view =
      GTK_TREE_VIEW (gtk_builder_get_object (builder, "local_tree_view")),
    .up_button =
      GTK_WIDGET (gtk_builder_get_object (builder, "local_up_button")),
    .refresh_button =
      GTK_WIDGET (gtk_builder_get_object (builder, "local_refresh_button")),
    .dir_entry =
      GTK_ENTRY (gtk_builder_get_object (builder, "local_dir_entry")),
    .menu = GTK_MENU (gtk_builder_get_object (builder, "local_menu")),
    .dir = malloc (PATH_MAX),
    .check_selection = elektroid_local_check_selection,
    .file_icon = elektroid_get_inventory_icon_for_fs (FS_SAMPLES),
    .extensions = NULL,
    .fs_ops = &FS_LOCAL_OPERATIONS,
    .data = NULL,
    .notify_dir_change = elektroid_notify_local_dir_change,
    .check_callback = NULL
  };

  g_signal_connect (gtk_tree_view_get_selection (local_browser.view),
		    "changed", G_CALLBACK (browser_selection_changed),
		    &local_browser);
  g_signal_connect (local_browser.view, "row-activated",
		    G_CALLBACK (browser_item_activated), &local_browser);
  g_signal_connect (local_browser.up_button, "clicked",
		    G_CALLBACK (browser_go_up), &local_browser);
  g_signal_connect (local_browser.refresh_button, "clicked",
		    G_CALLBACK (browser_refresh), &local_browser);
  g_signal_connect (local_browser.view, "button-press-event",
		    G_CALLBACK (elektroid_button_press), &local_browser);
  g_signal_connect (local_browser.view, "button-release-event",
		    G_CALLBACK (elektroid_button_release), &local_browser);
  g_signal_connect (local_browser.view, "key-press-event",
		    G_CALLBACK (elektroid_local_key_press), &local_browser);
  g_signal_connect (local_browser.view, "drag-begin",
		    G_CALLBACK (elektroid_drag_begin), &local_browser);
  g_signal_connect (local_browser.view, "drag-end",
		    G_CALLBACK (elektroid_drag_end), &local_browser);
  g_signal_connect (local_browser.view, "drag-data-get",
		    G_CALLBACK (elektroid_dnd_get), &local_browser);
  g_signal_connect (local_browser.view, "drag-data-received",
		    G_CALLBACK (elektroid_dnd_received), &local_browser);
  g_signal_connect (local_browser.view, "drag-motion",
		    G_CALLBACK (elektroid_drag_motion_list), &local_browser);
  g_signal_connect (local_browser.view, "drag-leave",
		    G_CALLBACK (elektroid_drag_leave_list), &local_browser);
  g_signal_connect (local_browser.up_button, "drag-motion",
		    G_CALLBACK (elektroid_drag_motion_up), &local_browser);
  g_signal_connect (local_browser.up_button, "drag-leave",
		    G_CALLBACK (elektroid_drag_leave_up), &local_browser);

  sortable = GTK_TREE_SORTABLE (gtk_tree_view_get_model (local_browser.view));
  gtk_tree_sortable_set_sort_func (sortable, BROWSER_LIST_STORE_NAME_FIELD,
				   browser_sort_samples, NULL, NULL);
  gtk_tree_sortable_set_sort_column_id (sortable,
					BROWSER_LIST_STORE_NAME_FIELD,
					GTK_TREE_SORTABLE_DEFAULT_SORT_COLUMN_ID);

  gtk_drag_source_set ((GtkWidget *) local_browser.view, GDK_BUTTON1_MASK,
		       TARGET_ENTRIES_LOCAL_SRC, TARGET_ENTRIES_LOCAL_SRC_N,
		       GDK_ACTION_MOVE);
  gtk_drag_dest_set ((GtkWidget *) local_browser.view, GTK_DEST_DEFAULT_ALL,
		     TARGET_ENTRIES_LOCAL_DST, TARGET_ENTRIES_LOCAL_DST_N,
		     GDK_ACTION_COPY);
  gtk_drag_dest_set ((GtkWidget *) local_browser.up_button,
		     GTK_DEST_DEFAULT_MOTION | GTK_DEST_DEFAULT_HIGHLIGHT,
		     TARGET_ENTRIES_UP_BUTTON_DST,
		     TARGET_ENTRIES_UP_BUTTON_DST_N, GDK_ACTION_COPY);

  audio_init (&audio, elektroid_set_volume_callback, elektroid_redraw_sample);

  devices_list_store =
    GTK_LIST_STORE (gtk_builder_get_object (builder, "devices_list_store"));
  devices_combo =
    GTK_COMBO_BOX (gtk_builder_get_object (builder, "devices_combo"));
  refresh_devices_button =
    GTK_WIDGET (gtk_builder_get_object (builder, "refresh_devices_button"));
  g_signal_connect (devices_combo, "changed",
		    G_CALLBACK (elektroid_set_device), NULL);
  g_signal_connect (refresh_devices_button, "clicked",
		    G_CALLBACK (browser_refresh_devices), NULL);

  task_list_store =
    GTK_LIST_STORE (gtk_builder_get_object (builder, "task_list_store"));
  task_tree_view =
    GTK_WIDGET (gtk_builder_get_object (builder, "task_tree_view"));

  cancel_task_button =
    GTK_WIDGET (gtk_builder_get_object (builder, "cancel_task_button"));
  remove_tasks_button =
    GTK_WIDGET (gtk_builder_get_object (builder, "remove_tasks_button"));
  clear_tasks_button =
    GTK_WIDGET (gtk_builder_get_object (builder, "clear_tasks_button"));
  g_signal_connect (cancel_task_button, "clicked",
		    G_CALLBACK (elektroid_cancel_all_tasks), NULL);
  g_signal_connect (remove_tasks_button, "clicked",
		    G_CALLBACK (elektroid_remove_queued_tasks), NULL);
  g_signal_connect (clear_tasks_button, "clicked",
		    G_CALLBACK (elektroid_clear_finished_tasks), NULL);

  gtk_statusbar_push (status_bar, 0, _("Not connected"));
  elektroid_loop_clicked (loop_button, NULL);
  autoplay = gtk_switch_get_active (GTK_SWITCH (autoplay_switch));

  fs_list_store =
    GTK_LIST_STORE (gtk_builder_get_object (builder, "fs_list_store"));
  fs_combo = GTK_COMBO_BOX (gtk_builder_get_object (builder, "fs_combo"));
  g_signal_connect (fs_combo, "changed", G_CALLBACK (elektroid_set_fs), NULL);

  gtk_widget_set_sensitive (remote_box, FALSE);
  gtk_widget_set_sensitive (rx_sysex_button, FALSE);
  gtk_widget_set_sensitive (tx_sysex_button, FALSE);
  gtk_widget_set_sensitive (os_upgrade_button, FALSE);

  strcpy (local_browser.dir, local_dir);
  elektroid_load_devices (TRUE);	//This triggers a local browser reload due to the extensions and icons selected for the fs

  gethostname (hostname, LABEL_MAX);
  gtk_label_set_text (GTK_LABEL (hostname_label), hostname);

  debug_print (1, "Creating notifier thread...\n");
  notifier_init (&notifier, &local_browser);
  notifier_set_dir (&notifier, local_dir);
  notifier_thread = g_thread_new ("notifier_thread", notifier_run, &notifier);
  free (local_dir);

  gtk_widget_show (main_window);
  gtk_main ();

  free (remote_browser.dir);
  free (local_browser.dir);

  if (connector_check (&connector))
    {
      connector_destroy (&connector);
    }

  audio_destroy (&audio);
  return EXIT_SUCCESS;
}

static gboolean
elektroid_end (gpointer data)
{
  elektroid_quit ();
  return FALSE;
}

static void
elektroid_print_help (gchar * executable_path)
{
  gchar *exec_name;
  const struct option *option;

  fprintf (stderr, "%s\n", PACKAGE_STRING);
  exec_name = basename (executable_path);
  fprintf (stderr, "Usage: %s [options]\n", exec_name);
  fprintf (stderr, "Options:\n");
  option = ELEKTROID_OPTIONS;
  while (option->name)
    {
      fprintf (stderr, "  --%s, -%c", option->name, option->val);
      if (option->has_arg)
	{
	  fprintf (stderr, " value");
	}
      fprintf (stderr, "\n");
      option++;
    }
}

int
main (int argc, char *argv[])
{
  gint opt;
  gchar *local_dir = NULL;
  gint vflg = 0, dflg = 0, errflg = 0;
  int long_index = 0;

  g_unix_signal_add (SIGHUP, elektroid_end, NULL);
  g_unix_signal_add (SIGINT, elektroid_end, NULL);
  g_unix_signal_add (SIGTERM, elektroid_end, NULL);

  setlocale (LC_ALL, "");
  bindtextdomain (PACKAGE, LOCALEDIR);
  textdomain (PACKAGE);

  while ((opt =
	  getopt_long (argc, argv, "l:vh", ELEKTROID_OPTIONS,
		       &long_index)) != -1)
    {
      switch (opt)
	{
	case 'l':
	  local_dir = optarg;
	  dflg++;
	  break;
	case 'v':
	  vflg++;
	  break;
	case 'h':
	  elektroid_print_help (argv[0]);
	  exit (EXIT_SUCCESS);
	case '?':
	  errflg++;
	}
    }

  if (dflg > 1)
    {
      errflg++;
    }

  if (vflg)
    {
      debug_level = vflg;
    }

  if (errflg > 0)
    {
      elektroid_print_help (argv[0]);
      exit (EXIT_FAILURE);
    }

  local_dir = get_local_startup_path (local_dir);
  return elektroid_run (argc, argv, local_dir);
}
