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
#include <getopt.h>
#include "backend.h"
#include "connector.h"
#include "browser.h"
#include "audio.h"
#include "sample.h"
#include "utils.h"
#include "notifier.h"
#include "local.h"
#include "preferences.h"

#define SHOW_AUDIO_PLAYER(ops) ((ops->options & FS_OPTION_MONO_AUDIO_PLAYER) || (ops->options & FS_OPTION_STEREO_AUDIO_PLAYER))

#define MAX_DRAW_X 10000

#define DND_TIMEOUT 1000

#define TEXT_URI_LIST_STD "text/uri-list"
#define TEXT_URI_LIST_ELEKTROID "text/uri-list-elektroid"

#define MSG_WARN_SAME_SRC_DST "Same source and destination path. Skipping...\n"

enum device_list_store_columns
{
  DEVICES_LIST_STORE_ID_FIELD,
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
  TASK_LIST_STORE_REMOTE_FS_ID_FIELD,
  TASK_LIST_STORE_REMOTE_FS_ICON_FIELD,
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

struct elektroid_add_upload_task_data
{
  struct item_iterator iter;
  gint32 index;
};

static gpointer elektroid_upload_task (gpointer);
static gpointer elektroid_download_task (gpointer);
static void elektroid_update_progress (struct job_control *);
static void elektroid_cancel_all_tasks (GtkWidget *, gpointer);

static const struct option ELEKTROID_OPTIONS[] = {
  {"local-directory", 1, NULL, 'l'},
  {"verbose", 0, NULL, 'v'},
  {"help", 0, NULL, 'h'},
  {NULL, 0, NULL, 0}
};

static const GtkTargetEntry TARGET_ENTRIES_LOCAL_DST[] = {
  {TEXT_URI_LIST_ELEKTROID, GTK_TARGET_SAME_APP | GTK_TARGET_OTHER_WIDGET,
   TARGET_STRING},
  {TEXT_URI_LIST_STD, GTK_TARGET_SAME_APP | GTK_TARGET_SAME_WIDGET,
   TARGET_STRING},
  {TEXT_URI_LIST_STD, GTK_TARGET_OTHER_APP, TARGET_STRING}
};

static const GtkTargetEntry TARGET_ENTRIES_LOCAL_SRC[] = {
  {TEXT_URI_LIST_STD, GTK_TARGET_SAME_APP | GTK_TARGET_OTHER_WIDGET,
   TARGET_STRING},
  {TEXT_URI_LIST_STD, GTK_TARGET_SAME_APP | GTK_TARGET_SAME_WIDGET,
   TARGET_STRING},
  {TEXT_URI_LIST_STD, GTK_TARGET_OTHER_APP, TARGET_STRING}
};

static const GtkTargetEntry TARGET_ENTRIES_REMOTE_DST[] = {
  {TEXT_URI_LIST_STD, GTK_TARGET_SAME_APP | GTK_TARGET_OTHER_WIDGET,
   TARGET_STRING},
  {TEXT_URI_LIST_ELEKTROID, GTK_TARGET_SAME_APP | GTK_TARGET_SAME_WIDGET,
   TARGET_STRING},
  {TEXT_URI_LIST_STD, GTK_TARGET_OTHER_APP, TARGET_STRING}
};

static const GtkTargetEntry TARGET_ENTRIES_REMOTE_DST_SLOT[] = {
  {TEXT_URI_LIST_STD, GTK_TARGET_SAME_APP | GTK_TARGET_OTHER_WIDGET,
   TARGET_STRING},
  {TEXT_URI_LIST_STD, GTK_TARGET_OTHER_APP, TARGET_STRING}
};

static const GtkTargetEntry TARGET_ENTRIES_REMOTE_SRC[] = {
  {TEXT_URI_LIST_ELEKTROID, GTK_TARGET_SAME_APP | GTK_TARGET_OTHER_WIDGET,
   TARGET_STRING},
  {TEXT_URI_LIST_ELEKTROID, GTK_TARGET_SAME_APP | GTK_TARGET_SAME_WIDGET,
   TARGET_STRING}
};

static const GtkTargetEntry TARGET_ENTRIES_UP_BUTTON_DST[] = {
  {TEXT_URI_LIST_STD, GTK_TARGET_SAME_APP, TARGET_STRING},
  {TEXT_URI_LIST_STD, GTK_TARGET_OTHER_APP, TARGET_STRING},
  {TEXT_URI_LIST_ELEKTROID, GTK_TARGET_SAME_APP, TARGET_STRING}
};

static struct browser remote_browser;
static struct browser local_browser;

static struct audio audio;
static struct backend backend;
static struct preferences preferences;

static GThread *load_thread = NULL;
static GThread *task_thread = NULL;
static GThread *sysex_thread = NULL;
static struct elektroid_transfer transfer;
static struct sysex_transfer sysex_transfer;

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
static GtkWidget *local_rename_menuitem;
static GtkWidget *local_delete_menuitem;
static GtkWidget *download_menuitem;
static GtkWidget *remote_rename_menuitem;
static GtkWidget *remote_delete_menuitem;
static GtkWidget *play_button;
static GtkWidget *stop_button;
static GtkWidget *loop_button;
static GtkWidget *autoplay_switch;
static GtkWidget *volume_button;
static GtkListStore *task_list_store;
static GtkWidget *task_tree_view;
static GtkWidget *cancel_task_button;
static GtkWidget *remove_tasks_button;
static GtkWidget *clear_tasks_button;
static GtkListStore *fs_list_store;
static GtkComboBox *fs_combo;
static GtkTreeViewColumn *remote_tree_view_index_column;
static GtkWidget *sample_info_box;
static GtkWidget *sample_length;
static GtkWidget *sample_channels;
static GtkWidget *sample_samplerate;
static GtkWidget *sample_bitdepth;

inline static const gchar *
elektroid_get_fs_name (guint fs)
{
  return backend_get_fs_operations (&backend, fs, NULL)->gui_name;
}

static void
elektroid_clear_file_extensions (gchar *** extensions)
{
  if (*extensions == NULL)
    {
      return;
    }

  gchar **ext = *extensions;
  while (*ext)
    {
      g_free (*ext);
      ext++;
    }
  g_free (*extensions);

  *extensions = NULL;
}

static void
elektroid_set_sample_file_extensions (gchar *** extensions)
{
  const gchar **exts = sample_get_sample_extensions ();
  const gchar **ext = exts;
  int ext_count = 0;
  while (*ext)
    {
      ext_count++;
      ext++;
    }
  ext_count++;			//NULL included

  *extensions = malloc (sizeof (gchar *) * ext_count);
  ext = exts;
  int i = 0;
  while (*ext)
    {
      (*extensions)[i] = strdup (*ext);
      ext++;
      i++;
    }
  (*extensions)[i] = NULL;
}

static void
elektroid_set_local_file_extensions (gchar *** extensions, gint sel_fs)
{
  const struct fs_operations *ops =
    backend_get_fs_operations (&backend, sel_fs, NULL);

  elektroid_clear_file_extensions (extensions);

  if (!ops || SHOW_AUDIO_PLAYER (ops))
    {
      elektroid_set_sample_file_extensions (extensions);
    }
  else
    {
      *extensions = malloc (sizeof (gchar *) * 2);
      (*extensions)[0] = ops->get_ext (&backend.device_desc, ops);
      (*extensions)[1] = NULL;
    }
}

static void
elektroid_set_remote_file_extensions (gchar *** extensions, gint sel_fs)
{
  const struct fs_operations *ops =
    backend_get_fs_operations (&backend, sel_fs, NULL);

  elektroid_clear_file_extensions (extensions);

  if (ops && backend.type == BE_TYPE_SYSTEM)
    {
      elektroid_set_sample_file_extensions (extensions);
    }
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
				   GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE, "%s",
				   msg);
  gtk_dialog_run (GTK_DIALOG (dialog));
  gtk_widget_destroy (dialog);
  g_free (msg);
  va_end (args);
}

static void
elektroid_load_devices (gboolean auto_select)
{
  gint i;
  gint device_index;
  gchar hostname[LABEL_MAX];
  GArray *devices = backend_get_system_devices ();
  struct backend_system_device device;

  debug_print (1, "Loading devices...\n");

  gtk_list_store_clear (fs_list_store);
  gtk_list_store_clear (devices_list_store);

  gethostname (hostname, LABEL_MAX);
  gtk_list_store_insert_with_values (devices_list_store, NULL, -1,
				     DEVICES_LIST_STORE_ID_FIELD,
				     BE_SYSTEM_ID,
				     DEVICES_LIST_STORE_NAME_FIELD,
				     hostname, -1);

  for (i = 0; i < devices->len; i++)
    {
      device = g_array_index (devices, struct backend_system_device, i);
      gtk_list_store_insert_with_values (devices_list_store, NULL, -1,
					 DEVICES_LIST_STORE_ID_FIELD,
					 device.id,
					 DEVICES_LIST_STORE_NAME_FIELD,
					 device.name, -1);
    }

  g_array_free (devices, TRUE);

  device_index = auto_select && i == 1 ? 0 : -1;
  debug_print (1, "Selecting device %d...\n", device_index);
  gtk_combo_box_set_active (devices_combo, device_index);

  if (device_index == -1)
    {
      local_browser.file_icon = BE_FILE_ICON_WAVE;
      elektroid_set_local_file_extensions (&local_browser.extensions, 0);

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
  struct backend_storage_stats statfs;
  GString *statfss;

  gtk_statusbar_pop (status_bar, 0);

  if (backend_check (&backend))
    {
      statfss = g_string_new (NULL);
      if (backend.get_storage_stats)
	{
	  for (gint i = 0, storage = 1; i < MAX_BACKEND_STORAGE;
	       i++, storage <<= 1)
	    {
	      if (backend.device_desc.storage & storage)
		{
		  if (!backend.get_storage_stats (&backend, storage, &statfs))
		    {
		      g_string_append_printf (statfss, " %s %.2f%%",
					      statfs.name,
					      backend_get_storage_stats_percent
					      (&statfs));
		    }
		}
	    }
	}

      statfss_str = g_string_free (statfss, FALSE);
      status = g_malloc (LABEL_MAX);
      if (strlen (backend.device_name))
	{
	  snprintf (status, LABEL_MAX, _("Connected to %s%s"),
		    backend.device_name, statfss_str);
	}
      else
	{
	  status[0] = 0;
	}
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
elektroid_check_backend ()
{
  GtkListStore *list_store;
  GtkTreeIter iter;
  gboolean remote_sensitive;
  gboolean connected = backend_check (&backend);
  gboolean queued = elektroid_get_next_queued_task (&iter, NULL, NULL, NULL,
						    NULL);

  if (!remote_browser.fs_ops
      || remote_browser.fs_ops->options & FS_OPTION_SINGLE_OP)
    {
      remote_sensitive = connected && !queued;
    }
  else
    {
      remote_sensitive = connected;
    }
  gtk_widget_set_sensitive (remote_box, remote_sensitive);
  gtk_widget_set_sensitive (upload_menuitem, remote_sensitive);
  gtk_widget_set_sensitive (rx_sysex_button, connected && !queued);
  gtk_widget_set_sensitive (tx_sysex_button, connected && !queued);
  gtk_widget_set_sensitive (os_upgrade_button, connected && !queued
			    && backend.upgrade_os);

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
elektroid_check_backend_bg (gpointer data)
{
  elektroid_check_backend ();
  return FALSE;
}

static void
elektroid_cancel_all_tasks_and_wait ()
{
  elektroid_cancel_all_tasks (NULL, NULL);
  //In this case, the active waiting can not be avoided as the user has cancelled the operation.
  while (transfer.status == RUNNING)
    {
      usleep (50000);
    }
}

static void
browser_refresh_devices (GtkWidget * object, gpointer data)
{
  if (backend_check (&backend))
    {
      elektroid_cancel_all_tasks_and_wait ();
      backend_destroy (&backend);
      elektroid_check_backend ();
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
  g_mutex_lock (&sysex_transfer.mutex);
  sysex_transfer.active = FALSE;
  g_mutex_unlock (&sysex_transfer.mutex);
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

  g_mutex_lock (&sysex_transfer.mutex);
  active = sysex_transfer.active;
  g_mutex_unlock (&sysex_transfer.mutex);

  return active;
}

static gpointer
elektroid_rx_sysex_thread (gpointer data)
{
  gint *res = malloc (sizeof (gint));
  gchar *text;

  sysex_transfer.status = WAITING;
  sysex_transfer.active = TRUE;
  sysex_transfer.timeout = DUMP_TIMEOUT;
  sysex_transfer.batch = TRUE;

  g_timeout_add (100, elektroid_update_sysex_progress, NULL);

  backend_rx_drain (&backend);
  //This doesn't need to be synchronized because the GUI doesn't allow concurrent access when receiving SysEx in batch mode.
  *res = backend_rx_sysex (&backend, &sysex_transfer);
  if (!*res)
    {
      text = debug_get_hex_msg (sysex_transfer.raw);
      debug_print (1, "SysEx message received (%d): %s\n",
		   sysex_transfer.raw->len, text);
      free (text);
    }

  gtk_dialog_response (GTK_DIALOG (progress_dialog), GTK_RESPONSE_ACCEPT);

  return res;
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
  gint dres;
  gchar *filename;
  gchar *filename_w_ext;
  const gchar *ext;
  gint *res;
  GtkFileChooserAction action = GTK_FILE_CHOOSER_ACTION_SAVE;

  g_idle_add (elektroid_start_rx_thread, NULL);

  gtk_window_set_title (GTK_WINDOW (progress_dialog), _("Receive SysEx"));
  dres = gtk_dialog_run (GTK_DIALOG (progress_dialog));
  sysex_transfer.active = FALSE;
  gtk_widget_hide (GTK_WIDGET (progress_dialog));

  res = elektroid_join_sysex_thread ();

  if (!res)			//Signal captured while running the dialog.
    {
      g_byte_array_free (sysex_transfer.raw, TRUE);
      return;
    }

  if (dres != GTK_RESPONSE_ACCEPT)
    {
      if (!*res)
	{
	  g_byte_array_free (sysex_transfer.raw, TRUE);
	}
      g_free (res);
      return;
    }

  if (*res)
    {
      elektroid_check_backend ();
      g_free (res);
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
      *res = save_file (filename, sysex_transfer.raw, NULL);
      if (*res)
	{
	  show_error_msg (_("Error while saving “%s”: %s."),
			  filename, g_strerror (*res));
	}
      g_byte_array_free (sysex_transfer.raw, TRUE);
      g_free (res);
      g_free (filename);
    }

  gtk_widget_destroy (dialog);
}

static gint
elektroid_send_sysex_file (const gchar * filename, t_sysex_transfer f)
{
  gint err = load_file (filename, sysex_transfer.raw, NULL);
  if (!err)
    {
      err = f (&backend, &sysex_transfer);
    }
  if (err && err != -ECANCELED)
    {
      show_error_msg (_("Error while loading “%s”: %s."),
		      filename, g_strerror (err));
    }
  return err;
}

static gpointer
elektroid_tx_sysex_files_thread (gpointer data)
{
  GSList *filenames = data;
  gint *err = malloc (sizeof (gint));
  sysex_transfer.raw = g_byte_array_new ();
  sysex_transfer.timeout = SYSEX_TIMEOUT_MS;

  g_timeout_add (100, elektroid_update_sysex_progress, NULL);

  *err = 0;
  while (*err != -ECANCELED && filenames)
    {
      g_byte_array_set_size (sysex_transfer.raw, 0);
      *err = elektroid_send_sysex_file (filenames->data, backend_tx_sysex);
      filenames = filenames->next;
      usleep (REST_TIME_US);
    }
  gtk_dialog_response (GTK_DIALOG (progress_dialog), GTK_RESPONSE_CANCEL);

  free_msg (sysex_transfer.raw);
  return err;
}

static gpointer
elektroid_tx_upgrade_os_thread (gpointer data)
{
  GSList *filenames = data;
  gint *err = malloc (sizeof (gint));
  sysex_transfer.raw = g_byte_array_new ();
  sysex_transfer.timeout = SYSEX_TIMEOUT_MS;

  g_timeout_add (100, elektroid_update_sysex_progress, NULL);

  *err = elektroid_send_sysex_file (filenames->data, backend.upgrade_os);
  gtk_dialog_response (GTK_DIALOG (progress_dialog), GTK_RESPONSE_CANCEL);

  free_msg (sysex_transfer.raw);
  return err;
}

static void
elektroid_tx_sysex_common (GThreadFunc func, gboolean multiple)
{
  GtkWidget *dialog;
  GtkFileChooser *chooser;
  GtkFileFilter *filter;
  gint res, *err;
  GSList *filenames;

  dialog = gtk_file_chooser_dialog_new (_("Open SysEx"),
					GTK_WINDOW (main_window),
					GTK_FILE_CHOOSER_ACTION_OPEN,
					_("_Cancel"),
					GTK_RESPONSE_CANCEL,
					_("_Open"),
					GTK_RESPONSE_ACCEPT, NULL);
  chooser = GTK_FILE_CHOOSER (dialog);
  filter = gtk_file_filter_new ();
  gtk_file_filter_set_name (filter, _("SysEx Files"));
  gtk_file_filter_add_pattern (filter, "*.syx");
  gtk_file_chooser_add_filter (chooser, filter);
  gtk_file_chooser_set_current_folder (chooser, preferences.local_dir);
  gtk_file_chooser_set_select_multiple (chooser, multiple);

  res = gtk_dialog_run (GTK_DIALOG (dialog));
  if (res == GTK_RESPONSE_ACCEPT)
    {
      gtk_widget_hide (GTK_WIDGET (dialog));
      filenames = gtk_file_chooser_get_filenames (chooser);
      sysex_thread = g_thread_new ("sysex_thread", func, filenames);
      gtk_window_set_title (GTK_WINDOW (progress_dialog), _("Send SysEx"));
      gtk_dialog_run (GTK_DIALOG (progress_dialog));

      //If the progress_dialog is closed, we end the transfer.
      g_mutex_lock (&sysex_transfer.mutex);
      sysex_transfer.active = FALSE;
      g_mutex_unlock (&sysex_transfer.mutex);

      gtk_widget_hide (GTK_WIDGET (progress_dialog));
      err = elektroid_join_sysex_thread ();

      g_slist_free_full (g_steal_pointer (&filenames), g_free);

      if (!err)			//Signal captured while running the dialog.
	{
	  goto cleanup;
	}

      if (*err < 0)
	{
	  elektroid_check_backend ();
	}

      g_free (err);
    }

cleanup:
  gtk_widget_destroy (dialog);
}

static void
elektroid_tx_sysex (GtkWidget * object, gpointer data)
{
  elektroid_tx_sysex_common (elektroid_tx_sysex_files_thread, TRUE);
}

static void
elektroid_upgrade_os (GtkWidget * object, gpointer data)
{
  elektroid_tx_sysex_common (elektroid_tx_upgrade_os_thread, FALSE);
  backend_destroy (&backend);
  elektroid_check_backend ();
}

static void
elektroid_show_about (GtkWidget * object, gpointer data)
{
  gtk_dialog_run (GTK_DIALOG (about_dialog));
  gtk_widget_hide (GTK_WIDGET (about_dialog));
}

static void
elektroid_set_sample_on_load (gboolean sensitive)
{
  gchar label[LABEL_MAX];
  struct sample_info *sample_info = audio.control.data;

  gtk_widget_set_sensitive (local_play_menuitem, sensitive);
  gtk_widget_set_sensitive (play_button, sensitive);
  gtk_widget_set_sensitive (stop_button, sensitive);

  if (sample_info->frames)
    {
      double time = sample_info->frames / (double) sample_info->samplerate;

      gtk_widget_set_visible (sample_info_box, TRUE);

      if (time >= 60)
	{
	  snprintf (label, LABEL_MAX, "%d; %.0f %s", sample_info->frames,
		    time / 60, _("min."));
	}
      else if (time >= 10)
	{
	  snprintf (label, LABEL_MAX, "%d; %.0f s", sample_info->frames,
		    time);
	}
      else if (time >= 1)
	{
	  snprintf (label, LABEL_MAX, "%d; %.1f s", sample_info->frames,
		    time);
	}
      else
	{
	  snprintf (label, LABEL_MAX, "%d; %.2f s", sample_info->frames,
		    time);
	}
      gtk_label_set_text (GTK_LABEL (sample_length), label);

      snprintf (label, LABEL_MAX, "%.2f kHz",
		sample_info->samplerate / 1000.f);
      gtk_label_set_text (GTK_LABEL (sample_samplerate), label);

      snprintf (label, LABEL_MAX, "%d", sample_info->channels);
      gtk_label_set_text (GTK_LABEL (sample_channels), label);

      if (sample_info->bitdepth)
	{
	  snprintf (label, LABEL_MAX, "%d", sample_info->bitdepth);
	  gtk_label_set_text (GTK_LABEL (sample_bitdepth), label);
	}
    }
  else
    {
      gtk_widget_set_visible (sample_info_box, FALSE);
    }
}

static gboolean
elektroid_update_ui_on_load (gpointer data)
{
  gboolean ready_to_play;

  g_mutex_lock (&audio.control.mutex);
  ready_to_play = audio.frames >= LOAD_BUFFER_LEN || (!audio.control.active
						      && audio.frames > 0);
  g_mutex_unlock (&audio.control.mutex);

  if (ready_to_play)
    {
      if (audio_check (&audio))
	{
	  elektroid_set_sample_on_load (TRUE);
	}
      if (preferences.autoplay)
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
  struct item item;

  gtk_tree_model_get_iter (model, &iter, tree_path);
  browser_set_item (model, &iter, &item);
  path = browser_get_item_path (browser, &item);
  id_path = browser_get_item_id_path (browser, &item);

  debug_print (1, "Deleting %s...\n", id_path);
  err = browser->fs_ops->delete (browser->backend, id_path);
  if (err)
    {
      show_error_msg (_("Error while deleting “%s”: %s."),
		      path, g_strerror (err));
    }
  else
    {
      gtk_list_store_remove (GTK_LIST_STORE (model), &iter);
    }

  g_free (path);
  g_free (id_path);
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
  struct item item;
  struct browser *browser = data;
  GtkTreeModel *model =
    GTK_TREE_MODEL (gtk_tree_view_get_model (browser->view));

  browser_set_selected_row_iter (browser, &iter);
  browser_set_item (model, &iter, &item);
  old_path = browser_get_item_id_path (browser, &item);

  gtk_entry_set_text (name_dialog_entry, item.name);
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

	  err = browser->fs_ops->move (&backend, old_path, new_path);

	  if (err)
	    {
	      show_error_msg (_("Error while renaming to “%s”: %s."),
			      new_path, g_strerror (err));
	    }
	  else
	    {
	      browser_load_dir (browser);
	    }

	  free (new_path);
	}
    }

  g_free (old_path);
  gtk_widget_hide (GTK_WIDGET (name_dialog));
}

static gboolean
elektroid_drag_begin (GtkWidget * widget, GdkDragContext * context,
		      gpointer data)
{
  GtkTreeIter iter;
  GtkTreeSelection *selection;
  GtkTreeModel *model;
  GList *tree_path_list;
  GList *list;
  gchar *uri;
  gchar *path;
  struct item item;
  struct browser *browser = data;

  selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (widget));
  model = GTK_TREE_MODEL (gtk_tree_view_get_model (GTK_TREE_VIEW (widget)));
  tree_path_list = gtk_tree_selection_get_selected_rows (selection, &model);

  browser->dnd_data = g_string_new ("");
  for (list = tree_path_list; list != NULL; list = g_list_next (list))
    {
      gtk_tree_model_get_iter (model, &iter, list->data);
      browser_set_item (model, &iter, &item);
      path = browser_get_item_id_path (browser, &item);
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
elektroid_redraw_sample (struct job_control *control)
{
  g_idle_add (elektroid_queue_draw_waveform, NULL);
}

static gpointer
elektroid_load_sample (gpointer data)
{
  struct sample_info *sample_info = audio.control.data;

  g_timeout_add (100, elektroid_update_ui_on_load, NULL);

  g_mutex_lock (&audio.control.mutex);
  audio.control.active = TRUE;
  g_mutex_unlock (&audio.control.mutex);

  sample_info->samplerate = AUDIO_SAMPLE_RATE;
  sample_info->channels =
    !remote_browser.fs_ops
    || (remote_browser.
	fs_ops->options & FS_OPTION_STEREO_AUDIO_PLAYER) ? 2 : 1;
  if (sample_load_with_frames
      (audio.path, audio.sample, &audio.control, &audio.frames) >= 0)
    {
      debug_print (1,
		   "Samples: %d (%d B); channels: %d; loop start at %d; loop end at %d; sample rate: %d; bit depth: %d\n",
		   audio.frames, audio.sample->len, sample_info->channels,
		   sample_info->loopstart, sample_info->loopend,
		   sample_info->samplerate, sample_info->bitdepth);
    }

  g_mutex_lock (&audio.control.mutex);
  audio.control.active = FALSE;
  g_mutex_unlock (&audio.control.mutex);

  return NULL;
}

static void
elektroid_start_load_thread ()
{
  debug_print (1, "Creating load thread...\n");
  load_thread = g_thread_new ("load_sample", elektroid_load_sample, NULL);
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

static void
elektroid_set_css_local_class (GtkWidget * widget, gboolean local)
{
  GtkStyleContext *context = gtk_widget_get_style_context (widget);
  if (widget == autoplay_switch)
    {
      gtk_style_context_remove_class (context,
				      local ? "remote_switch" :
				      "local_switch");
      gtk_style_context_add_class (context,
				   local ? "local_switch" : "remote_switch");
    }
  else
    {
      gtk_style_context_remove_class (context, local ? "remote" : "local");
      gtk_style_context_add_class (context, local ? "local" : "remote");
    }
}

static gboolean
elektroid_check_and_load_sample (struct browser *browser, gint count)
{
  struct item item;
  GtkTreeIter iter;
  gchar *sample_path;
  GtkTreeModel *model;
  gboolean loaded = FALSE;
  gboolean audio_fs = !remote_browser.fs_ops
    || SHOW_AUDIO_PLAYER (remote_browser.fs_ops);

  //If remote type is also the system, we clear the selection of the other browser.
  if (backend.type == BE_TYPE_SYSTEM)
    {
      struct browser *disabled_browser =
	browser == &local_browser ? &remote_browser : &local_browser;
      GtkTreeSelection *selection =
	gtk_tree_view_get_selection (GTK_TREE_VIEW (disabled_browser->view));
      g_signal_handler_block (gtk_tree_view_get_selection
			      (disabled_browser->view),
			      disabled_browser->selection_changed_handler_id);
      gtk_tree_selection_unselect_all (selection);
      g_signal_handler_unblock (gtk_tree_view_get_selection
				(disabled_browser->view),
				disabled_browser->selection_changed_handler_id);
    }

  if (count == 1)
    {
      browser_set_selected_row_iter (browser, &iter);
      model = GTK_TREE_MODEL (gtk_tree_view_get_model (browser->view));
      browser_set_item (model, &iter, &item);
      if (item.type == ELEKTROID_DIR)
	{
	  audio_stop (&audio, TRUE);
	  elektroid_stop_load_thread ();
	  audio_reset_sample (&audio);
	  gtk_widget_queue_draw (waveform_draw_area);
	}
      else
	{
	  sample_path = chain_path (browser->dir, item.name);
	  if (strcmp (audio.path, sample_path))
	    {
	      if (audio_fs)
		{
		  loaded = TRUE;
		  audio_stop (&audio, TRUE);
		  elektroid_stop_load_thread ();
		  audio_reset_sample (&audio);
		  strcpy (audio.path, sample_path);
		  elektroid_start_load_thread ();

		  gboolean local = browser == &local_browser;
		  elektroid_set_css_local_class (autoplay_switch, local);
		  elektroid_set_css_local_class (play_button, local);
		  elektroid_set_css_local_class (stop_button, local);
		  elektroid_set_css_local_class (loop_button, local);
		  elektroid_set_css_local_class (volume_button, local);
		  elektroid_set_css_local_class (waveform_draw_area, local);
		}
	    }
	  g_free (sample_path);
	}
    }
  else
    {
      audio_stop (&audio, TRUE);
      elektroid_stop_load_thread ();
      audio_reset_sample (&audio);
      gtk_widget_queue_draw (waveform_draw_area);
    }

  return loaded;
}

static gboolean
elektroid_remote_check_selection (gpointer data)
{
  gint count = browser_get_selected_items_count (&remote_browser);
  gboolean dl_impl = remote_browser.fs_ops
    && remote_browser.fs_ops->download ? TRUE : FALSE;
  gboolean move_impl = remote_browser.fs_ops
    && remote_browser.fs_ops->move ? TRUE : FALSE;
  gboolean del_impl = remote_browser.fs_ops
    && remote_browser.fs_ops->delete ? TRUE : FALSE;

  if (backend.type == BE_TYPE_SYSTEM)
    {
      elektroid_check_and_load_sample (&remote_browser, count);
    }

  gtk_widget_set_sensitive (download_menuitem, count > 0 && dl_impl);
  gtk_widget_set_sensitive (remote_rename_menuitem, count == 1 && move_impl);
  gtk_widget_set_sensitive (remote_delete_menuitem, count > 0 && del_impl);

  return FALSE;
}

static gboolean
elektroid_local_check_selection (gpointer data)
{
  gint count = browser_get_selected_items_count (&local_browser);
  gboolean loaded = elektroid_check_and_load_sample (&local_browser, count);

  elektroid_set_sample_on_load (loaded);
  gtk_widget_set_sensitive (local_open_menuitem, loaded);
  gtk_widget_set_sensitive (local_show_menuitem, count <= 1);
  gtk_widget_set_sensitive (local_rename_menuitem, count == 1);
  gtk_widget_set_sensitive (local_delete_menuitem, count > 0);
  gtk_widget_set_sensitive (upload_menuitem, count > 0
			    && remote_browser.fs_ops
			    && remote_browser.fs_ops->upload
			    && !(remote_browser.
				 fs_ops->options & FS_OPTION_SINGLE_OP));

  return FALSE;
}

static gboolean
elektroid_draw_waveform (GtkWidget * widget, cairo_t * cr, gpointer data)
{
  guint width, height;
  GdkRGBA color;
  GtkStyleContext *context;
  gint x_widget, x_sample;
  double x_ratio, mid_y1, mid_y2, value;
  short *sample;
  double y_scale = 1.0 / (double) SHRT_MIN;
  double x_scale = 1.0 / (double) MAX_DRAW_X;
  struct sample_info *sample_info = audio.control.data;
  gboolean stereo = ((!remote_browser.fs_ops
		      || (remote_browser.
			  fs_ops->options & FS_OPTION_STEREO_AUDIO_PLAYER))
		     && (sample_info->channels == 2));

  g_mutex_lock (&audio.control.mutex);

  context = gtk_widget_get_style_context (widget);
  width = gtk_widget_get_allocated_width (widget) - 2;
  height = gtk_widget_get_allocated_height (widget) - 2;
  y_scale *= height;
  if (stereo)
    {
      mid_y1 = height * 0.25;
      mid_y2 = height * 0.75;
      y_scale *= 0.25;
    }
  else
    {
      mid_y1 = height * 0.50;
      y_scale *= 0.5;
    }
  gtk_render_background (context, cr, 0, 0, width, height);
  gtk_style_context_get_color (context, gtk_style_context_get_state (context),
			       &color);
  gdk_cairo_set_source_rgba (cr, &color);

  sample = (short *) audio.sample->data;
  x_ratio = audio.frames / (double) MAX_DRAW_X;
  for (gint i = 0; i < MAX_DRAW_X; i++)
    {
      x_sample = i * x_ratio * (stereo ? 2 : 1);
      x_widget = i * width * x_scale;
      if (x_sample < audio.sample->len >> 1)
	{
	  value = mid_y1 - sample[x_sample] * y_scale;
	  cairo_move_to (cr, x_widget, mid_y1);
	  cairo_line_to (cr, x_widget, value);
	  cairo_stroke (cr);
	  if (stereo)
	    {
	      value = mid_y2 - sample[x_sample + 1] * y_scale;
	      cairo_move_to (cr, x_widget, mid_y2);
	      cairo_line_to (cr, x_widget, value);
	      cairo_stroke (cr);
	    }
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
  struct item item;
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
      browser_set_item (model, &iter, &item);
      path = chain_path (local_browser.dir, item.name);
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
  struct item item;

  browser_set_selected_row_iter (&local_browser, &iter);
  model = GTK_TREE_MODEL (gtk_tree_view_get_model (local_browser.view));
  browser_set_item (model, &iter, &item);
  path = chain_path (local_browser.dir, item.name);

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
  preferences.autoplay = state;
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

	  err = browser->fs_ops->mkdir (&backend, pathname);

	  if (err)
	    {
	      show_error_msg (_("Error while creating dir “%s”: %s."),
			      pathname, g_strerror (err));
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
      if (remote_browser.fs_ops->options & FS_OPTION_SINGLE_OP)
	{
	  gtk_widget_set_sensitive (remote_box, FALSE);
	  gtk_widget_set_sensitive (upload_menuitem, FALSE);
	}
      gtk_widget_set_sensitive (rx_sysex_button, FALSE);
      gtk_widget_set_sensitive (tx_sysex_button, FALSE);
      gtk_widget_set_sensitive (os_upgrade_button, FALSE);

      gtk_list_store_set (task_list_store, &iter,
			  TASK_LIST_STORE_STATUS_FIELD, RUNNING,
			  TASK_LIST_STORE_STATUS_HUMAN_FIELD, status_human,
			  -1);
      path =
	gtk_tree_model_get_path (GTK_TREE_MODEL (task_list_store), &iter);
      gtk_tree_view_set_cursor (GTK_TREE_VIEW (task_tree_view), path, NULL,
				FALSE);
      gtk_tree_path_free (path);
      transfer.status = RUNNING;
      transfer.control.active = TRUE;
      transfer.control.callback = elektroid_update_progress;
      transfer.src = src;
      transfer.dst = dst;
      transfer.fs_ops = backend_get_fs_operations (&backend, fs, NULL);
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
      gtk_widget_set_sensitive (remote_box, TRUE);
      gtk_widget_set_sensitive (upload_menuitem, TRUE);
      gtk_widget_set_sensitive (rx_sysex_button, TRUE);
      gtk_widget_set_sensitive (tx_sysex_button, TRUE);
      gtk_widget_set_sensitive (os_upgrade_button,
				backend.upgrade_os != NULL);
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

  res = transfer.fs_ops->upload (remote_browser.backend, transfer.dst, array,
				 &transfer.control);
  g_free (transfer.control.data);
  transfer.control.data = NULL;
  g_idle_add (elektroid_check_backend_bg, NULL);

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
  const gchar *icon =
    backend_get_fs_operations (&backend, remote_fs_id, NULL)->gui_icon;

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
				     remote_fs_id,
				     TASK_LIST_STORE_REMOTE_FS_ICON_FIELD,
				     icon, -1);

  gtk_widget_set_sensitive (remove_tasks_button, TRUE);
}

static void
elektroid_add_upload_task_path (const gchar * rel_path,
				const gchar * src_dir,
				const gchar * dst_dir,
				struct item_iterator *remote_dir_iter,
				gint32 * next_idx)
{
  gint32 children_next_idx;
  gchar *path;
  gchar *dst_abs_dir;
  gchar *upload_path;
  struct item_iterator iter;
  struct item_iterator children_remote_item_iterator;
  gchar *dst_abs_path = chain_path (dst_dir, rel_path);
  gchar *src_abs_path = chain_path (src_dir, rel_path);

  if (local_browser.
      fs_ops->readdir (local_browser.backend, &iter, src_abs_path))
    {
      dst_abs_dir = dirname (dst_abs_path);
      upload_path =
	remote_browser.fs_ops->get_upload_path (&backend,
						remote_dir_iter,
						remote_browser.fs_ops,
						dst_abs_dir, src_abs_path,
						next_idx);
      elektroid_add_task (UPLOAD, src_abs_path, upload_path,
			  remote_browser.fs_ops->fs);
      goto cleanup_not_dir;
    }

  if (remote_browser.fs_ops->mkdir)
    {
      if (remote_browser.fs_ops->mkdir (remote_browser.backend, dst_abs_path))
	{
	  error_print ("Error while creating remote %s dir\n", dst_abs_path);
	  goto cleanup;
	}

      if (!strchr (rel_path, '/'))
	{
	  browser_load_dir (&remote_browser);
	}
    }

  if (!remote_browser.
      fs_ops->readdir (remote_browser.backend, &children_remote_item_iterator,
		       dst_abs_path))
    {
      while (!next_item_iterator (&iter))
	{
	  path = chain_path (rel_path, iter.item.name);
	  elektroid_add_upload_task_path (path, src_dir, dst_dir,
					  &children_remote_item_iterator,
					  &children_next_idx);
	  free (path);
	}

      free_item_iterator (&children_remote_item_iterator);
    }


cleanup:
  free_item_iterator (&iter);
cleanup_not_dir:
  g_free (dst_abs_path);
  g_free (src_abs_path);
}

static void
elektroid_add_upload_task (GtkTreeModel * model,
			   GtkTreePath * path,
			   GtkTreeIter * iter, gpointer userdata)
{
  struct item item;
  struct elektroid_add_upload_task_data *data = userdata;
  browser_set_item (model, iter, &item);
  elektroid_add_upload_task_path (item.name, local_browser.dir,
				  remote_browser.dir, &data->iter,
				  &data->index);
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
  remote_browser.fs_ops->readdir (remote_browser.backend, &data.iter,
				  remote_browser.dir);
  gtk_tree_selection_selected_foreach (selection, elektroid_add_upload_task,
				       &data);
  free_item_iterator (&data.iter);

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

  array = g_byte_array_new ();

  debug_print (1, "Remote path: %s\n", transfer.src);
  debug_print (1, "Local path: %s\n", transfer.dst);

  res = transfer.fs_ops->download (remote_browser.backend,
				   transfer.src, array, &transfer.control);
  g_idle_add (elektroid_check_backend_bg, NULL);

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
      g_free (transfer.control.data);
      transfer.control.data = NULL;
    }
  g_mutex_unlock (&transfer.control.mutex);

  g_idle_add (elektroid_complete_running_task, NULL);
  g_idle_add (elektroid_run_next_task, NULL);

  return NULL;
}

static void
elektroid_add_download_task_path (const gchar * rel_path,
				  const gchar * src_dir,
				  const gchar * dst_dir,
				  struct item_iterator *remote_dir_iter)
{
  struct item_iterator iter, iter_copy;
  gchar *path, *id, *download_path, *dst_abs_dirc, *dst_abs_dir;
  gchar *src_abs_path = chain_path (src_dir, rel_path);
  gchar *dst_abs_path = chain_path (dst_dir, rel_path);

  if (remote_browser.fs_ops->readdir (remote_browser.backend, &iter,
				      src_abs_path))
    {
      dst_abs_dirc = strdup (dst_abs_path);
      dst_abs_dir = dirname (dst_abs_dirc);
      download_path =
	remote_browser.fs_ops->get_download_path (&backend,
						  remote_dir_iter,
						  remote_browser.fs_ops,
						  dst_abs_dir, src_abs_path);
      elektroid_add_task (DOWNLOAD, src_abs_path, download_path,
			  remote_browser.fs_ops->fs);
      g_free (dst_abs_dirc);
      g_free (download_path);
      goto cleanup_not_dir;
    }

  if (local_browser.fs_ops->mkdir (local_browser.backend, dst_abs_path))
    {
      error_print ("Error while creating local %s dir\n", dst_abs_path);
      goto cleanup;
    }

  copy_item_iterator (&iter_copy, &iter, TRUE);
  while (!next_item_iterator (&iter))
    {
      id = remote_browser.fs_ops->getid (&iter.item);
      path = chain_path (rel_path, id);
      elektroid_add_download_task_path (path, src_dir, dst_dir, &iter_copy);
      g_free (path);
      g_free (id);
    }
  free_item_iterator (&iter_copy);

cleanup:
  free_item_iterator (&iter);
cleanup_not_dir:
  free (dst_abs_path);
  free (src_abs_path);
}

static void
elektroid_add_download_task (GtkTreeModel * model,
			     GtkTreePath * path,
			     GtkTreeIter * iter, gpointer data)
{
  struct item item;
  char *id;

  browser_set_item (model, iter, &item);
  id = remote_browser.fs_ops->getid (&item);
  elektroid_add_download_task_path (id, remote_browser.dir, local_browser.dir,
				    data);
  g_free (id);
}

static void
elektroid_add_download_tasks (GtkWidget * object, gpointer data)
{
  gboolean queued;
  GtkTreeIter iter;
  struct item_iterator item_iterator;
  GtkTreeSelection *selection =
    gtk_tree_view_get_selection (GTK_TREE_VIEW (remote_browser.view));

  if (!gtk_tree_selection_count_selected_rows (selection))
    {
      return;
    }

  queued = elektroid_get_next_queued_task (&iter, NULL, NULL, NULL, NULL);

  remote_browser.fs_ops->readdir (remote_browser.backend, &item_iterator,
				  remote_browser.dir);
  gtk_tree_selection_selected_foreach (selection, elektroid_add_download_task,
				       &item_iterator);
  free_item_iterator (&item_iterator);

  if (!queued)
    {
      elektroid_run_next_task (NULL);
    }
}

static gboolean
elektroid_set_progress_value (gpointer data)
{
  GtkTreeIter iter;
  gdouble progress;

  if (elektroid_get_running_task (&iter))
    {
      g_mutex_lock (&transfer.control.mutex);
      progress = transfer.control.progress;
      g_mutex_unlock (&transfer.control.mutex);

      gtk_list_store_set (task_list_store, &iter,
			  TASK_LIST_STORE_PROGRESS_FIELD,
			  100.0 * progress, -1);
    }

  free (data);

  return FALSE;
}

static void
elektroid_update_progress (struct job_control *control)
{
  g_idle_add (elektroid_set_progress_value, NULL);
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
	      struct backend *backend = remote_browser.backend;
	      backend_enable_cache (backend);
	      elektroid_add_download_tasks (NULL, NULL);
	      backend_disable_cache (backend);
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
elektroid_set_browser_options (struct browser *browser)
{
  GtkTreeSortable *sortable =
    GTK_TREE_SORTABLE (gtk_tree_view_get_model (browser->view));

  if (browser->fs_ops->options & FS_OPTION_SORT_BY_ID)
    {
      gtk_tree_sortable_set_sort_func (sortable,
				       BROWSER_LIST_STORE_INDEX_FIELD,
				       browser_sort_by_id, NULL, NULL);
      gtk_tree_sortable_set_sort_column_id (sortable,
					    BROWSER_LIST_STORE_INDEX_FIELD,
					    GTK_TREE_SORTABLE_DEFAULT_SORT_COLUMN_ID);
    }
  else if (browser->fs_ops->options & FS_OPTION_SORT_BY_NAME)
    {
      gtk_tree_sortable_set_sort_func (sortable,
				       BROWSER_LIST_STORE_NAME_FIELD,
				       browser_sort_by_name, NULL, NULL);
      gtk_tree_sortable_set_sort_column_id (sortable,
					    BROWSER_LIST_STORE_NAME_FIELD,
					    GTK_TREE_SORTABLE_DEFAULT_SORT_COLUMN_ID);
    }
  else
    {
      gtk_tree_sortable_set_sort_column_id (sortable,
					    GTK_TREE_SORTABLE_UNSORTED_SORT_COLUMN_ID,
					    GTK_TREE_SORTABLE_DEFAULT_SORT_COLUMN_ID);
    }
}

static void
elektroid_set_fs (GtkWidget * object, gpointer data)
{
  GtkTreeIter iter;
  GValue fsv = G_VALUE_INIT;
  gint fs;

  *remote_browser.dir = 0;
  if (!gtk_combo_box_get_active_iter (fs_combo, &iter))
    {
      remote_browser.fs_ops = NULL;
      browser_load_dir (&remote_browser);
      elektroid_set_local_file_extensions (&local_browser.extensions, 0);
      browser_update_fs_options (&remote_browser);
      browser_load_dir (&local_browser);
      return;
    }

  gtk_tree_model_get_value (GTK_TREE_MODEL (fs_list_store),
			    &iter, FS_LIST_STORE_ID_FIELD, &fsv);
  fs = g_value_get_uint (&fsv);

  remote_browser.fs_ops = backend_get_fs_operations (&backend, fs, NULL);
  remote_browser.file_icon = remote_browser.fs_ops->gui_icon;
  if (backend.type == BE_TYPE_SYSTEM)
    {
      g_free (remote_browser.dir);
      remote_browser.dir = get_expanded_dir ("~");
    }
  else
    {
      strcpy (remote_browser.dir, "/");
    }
  browser_load_dir (&remote_browser);
  browser_update_fs_options (&remote_browser);

  local_browser.file_icon = remote_browser.file_icon;
  elektroid_set_local_file_extensions (&local_browser.extensions, fs);
  elektroid_set_remote_file_extensions (&remote_browser.extensions, fs);
  browser_load_dir (&local_browser);

  gtk_widget_set_visible (remote_rename_menuitem,
			  remote_browser.fs_ops->rename != NULL);
  gtk_widget_set_visible (remote_delete_menuitem,
			  remote_browser.fs_ops->delete != NULL);
  gtk_widget_set_visible (local_audio_box,
			  SHOW_AUDIO_PLAYER (remote_browser.fs_ops));
  gtk_tree_view_column_set_visible (remote_tree_view_index_column,
				    remote_browser.fs_ops->options &
				    FS_OPTION_SHOW_INDEX_COLUMN);

  if (remote_browser.fs_ops->options & FS_OPTION_SLOT_STORAGE)
    {
      gtk_drag_dest_set ((GtkWidget *) remote_browser.view,
			 GTK_DEST_DEFAULT_ALL, TARGET_ENTRIES_REMOTE_DST_SLOT,
			 G_N_ELEMENTS (TARGET_ENTRIES_REMOTE_DST_SLOT),
			 GDK_ACTION_COPY);
    }
  else
    {
      gtk_drag_dest_set ((GtkWidget *) remote_browser.view,
			 GTK_DEST_DEFAULT_ALL, TARGET_ENTRIES_REMOTE_DST,
			 G_N_ELEMENTS (TARGET_ENTRIES_REMOTE_DST),
			 GDK_ACTION_COPY);
    }

  if (SHOW_AUDIO_PLAYER (remote_browser.fs_ops))
    {
      audio_stop (&audio, TRUE);
    }

  elektroid_set_browser_options (&remote_browser);
}

static void
elektroid_fill_fs_combo ()
{
  const struct fs_operations *ops;

  gtk_list_store_clear (fs_list_store);

  if (!backend.device_desc.filesystems)
    {
      elektroid_set_fs (NULL, NULL);
      return;
    }

  for (gint fs = 1, i = 0; i < MAX_BACKEND_FSS; fs = fs << 1, i++)
    {
      if (backend.device_desc.filesystems & fs)
	{
	  ops = backend_get_fs_operations (&backend, fs, NULL);
	  if (ops->gui_name)
	    {
	      gtk_list_store_insert_with_values (fs_list_store, NULL, -1,
						 FS_LIST_STORE_ID_FIELD,
						 fs,
						 FS_LIST_STORE_ICON_FIELD,
						 ops->gui_icon,
						 FS_LIST_STORE_NAME_FIELD,
						 elektroid_get_fs_name (fs),
						 -1);
	    }
	}
    }

  debug_print (1, "Selecting first filesystem...\n");
  gtk_combo_box_set_active (fs_combo, 0);
}

static void
elektroid_set_device (GtkWidget * object, gpointer data)
{
  GtkTreeIter iter;
  gchar *id;

  elektroid_cancel_all_tasks_and_wait ();

  if (gtk_combo_box_get_active_iter (devices_combo, &iter) == TRUE)
    {
      if (backend_check (&backend))
	{
	  backend_destroy (&backend);
	}

      gtk_tree_model_get (GTK_TREE_MODEL (devices_list_store), &iter,
			  DEVICES_LIST_STORE_ID_FIELD, &id, -1);

      if (connector_init (&backend, id, NULL) < 0)
	{
	  error_print ("Error while connecting\n");
	}

      if (elektroid_check_backend ())
	{
	  elektroid_fill_fs_combo ();
	}

      g_free (id);
    }
}

static void
elektroid_dnd_received_local (const gchar * dir, const gchar * name,
			      const gchar * filename)
{
  gchar *dst_path;
  gint res;

  if (strcmp (dir, local_browser.dir))
    {
      dst_path = chain_path (local_browser.dir, name);
      res =
	local_browser.fs_ops->move (local_browser.backend, filename,
				    dst_path);
      if (res)
	{
	  show_error_msg (_
			  ("Error while moving from “%s” to “%s”: %s."),
			  filename, dst_path, g_strerror (res));
	}
      g_free (dst_path);
    }
  else
    {
      debug_print (1, MSG_WARN_SAME_SRC_DST);
    }
}

static void
elektroid_dnd_received_remote (const gchar * dir, const gchar * name,
			       const gchar * filename,
			       struct item_iterator *remote_item_iterator,
			       gint32 * next_idx)
{
  gchar *dst_path;
  gint res;

  if (strcmp (dir, remote_browser.dir))
    {
      dst_path =
	remote_browser.fs_ops->get_upload_path (&backend,
						remote_item_iterator,
						remote_browser.fs_ops,
						remote_browser.dir, name,
						next_idx);
      res =
	remote_browser.fs_ops->move (remote_browser.backend, filename,
				     dst_path);
      if (res)
	{
	  show_error_msg (_
			  ("Error while moving from “%s” to “%s”: %s."),
			  filename, dst_path, g_strerror (res));
	}
      g_free (dst_path);
      browser_load_dir (&remote_browser);
    }
  else
    {
      debug_print (1, MSG_WARN_SAME_SRC_DST);
    }
}

static void
elektroid_add_upload_task_slot (const gchar * name,
				const gchar * src_file_path, gint slot)
{
  GtkTreeIter iter;
  GtkTreeModel *model;
  struct item item;
  gchar *dst_file_path, *name_wo_ext;

  model =
    GTK_TREE_MODEL (gtk_tree_view_get_model
		    (GTK_TREE_VIEW (remote_browser.view)));

  if (gtk_tree_model_get_iter (model, &iter, remote_browser.dnd_motion_path))
    {
      for (gint i = 0; i < slot; i++)
	{
	  if (!gtk_tree_model_iter_next (model, &iter))
	    {
	      return;
	    }
	}

      browser_set_item (model, &iter, &item);

      name_wo_ext = strdup (name);
      remove_ext (name_wo_ext);
      dst_file_path = g_malloc (PATH_MAX);
      snprintf (dst_file_path, PATH_MAX, "%s%s%s%s%s", remote_browser.dir,
		strcmp (remote_browser.dir, "/") ? "/" : "", item.name,
		SAMPLE_ID_NAME_SEPARATOR, name_wo_ext);
      g_free (name_wo_ext);

      elektroid_add_task (UPLOAD, src_file_path, dst_file_path,
			  remote_browser.fs_ops->fs);
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
  GtkTreeIter iter;
  gboolean queued_before, queued_after, load_remote;
  GdkAtom type;
  gchar *type_name;
  gint32 next_idx = 1;
  struct item_iterator remote_item_iterator;

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
  queued_before =
    elektroid_get_next_queued_task (&iter, NULL, NULL, NULL, NULL);

  if (widget == GTK_WIDGET (local_browser.view))
    {
      backend_enable_cache (&backend);
    }

  load_remote = widget == GTK_WIDGET (remote_browser.view) ||
    strcmp (type_name, TEXT_URI_LIST_ELEKTROID) == 0;

  if (load_remote)
    {
      remote_browser.fs_ops->readdir (remote_browser.backend,
				      &remote_item_iterator,
				      remote_browser.dir);
    }

  for (gint i = 0; uris[i] != NULL; i++)
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
	      elektroid_dnd_received_local (dir, name, filename);
	    }
	  else if (strcmp (type_name, TEXT_URI_LIST_ELEKTROID) == 0)
	    {
	      elektroid_add_download_task_path (name, dir, local_browser.dir,
						&remote_item_iterator);
	    }
	}
      else if (widget == GTK_WIDGET (remote_browser.view))
	{
	  if (strcmp (type_name, TEXT_URI_LIST_ELEKTROID) == 0)
	    {
	      elektroid_dnd_received_remote (dir, name, filename,
					     &remote_item_iterator,
					     &next_idx);
	    }
	  else if (strcmp (type_name, TEXT_URI_LIST_STD) == 0)
	    {
	      if (remote_browser.fs_ops->options & FS_OPTION_SLOT_STORAGE)
		{
		  elektroid_add_upload_task_slot (name, filename, i);
		}
	      else
		{
		  elektroid_add_upload_task_path (name, dir,
						  remote_browser.dir,
						  &remote_item_iterator,
						  &next_idx);
		}
	    }
	}

      g_free (path_basename);
      g_free (path_dirname);
      g_free (filename);
    }

  if (load_remote)
    {
      free_item_iterator (&remote_item_iterator);
    }

  if (widget == GTK_WIDGET (local_browser.view))
    {
      backend_disable_cache (&backend);
    }

  queued_after =
    elektroid_get_next_queued_task (&iter, NULL, NULL, NULL, NULL);
  if (!queued_before && queued_after)
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
  gboolean slot;
  GtkTreeSelection *selection;
  struct item item;
  struct browser *browser = user_data;

  slot = widget == GTK_WIDGET (remote_browser.view)
    && remote_browser.fs_ops->options & FS_OPTION_SLOT_STORAGE;

  gtk_tree_view_convert_widget_to_bin_window_coords
    (GTK_TREE_VIEW (widget), wx, wy, &tx, &ty);

  if (gtk_tree_view_get_path_at_pos
      (GTK_TREE_VIEW (widget), tx, ty, &path, NULL, NULL, NULL))
    {
      spath = gtk_tree_path_to_string (path);
      debug_print (2, "Drag motion path: %s\n", spath);
      g_free (spath);

      if (slot)
	{
	  gtk_tree_view_set_drag_dest_row (remote_browser.view, path,
					   GTK_TREE_VIEW_DROP_INTO_OR_BEFORE);
	}
      else
	{
	  selection =
	    gtk_tree_view_get_selection (GTK_TREE_VIEW (browser->view));
	  if (gtk_tree_selection_path_is_selected (selection, path))
	    {
	      if (browser->dnd_timeout_function_id)
		{
		  g_source_remove (browser->dnd_timeout_function_id);
		  browser->dnd_timeout_function_id = 0;
		}
	      return TRUE;
	    }
	}

      model =
	GTK_TREE_MODEL (gtk_tree_view_get_model (GTK_TREE_VIEW (widget)));
      gtk_tree_model_get_iter (model, &iter, path);
      browser_set_item (model, &iter, &item);

      if (item.type == ELEKTROID_DIR && (!browser->dnd_motion_path
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
elektroid_run (int argc, char *argv[])
{
  GtkBuilder *builder;
  GtkCssProvider *css_provider;
  GtkWidget *name_dialog_cancel_button;
  GtkWidget *refresh_devices_button;
  GtkWidget *hostname_label;
  gchar hostname[LABEL_MAX];

  gtk_init (&argc, &argv);
  builder = gtk_builder_new ();
  gtk_builder_add_from_file (builder, DATADIR "/gui.glade", NULL);

  css_provider = gtk_css_provider_new ();
  gtk_css_provider_load_from_path (css_provider, DATADIR "/gui.css", NULL);
  gtk_style_context_add_provider_for_screen (gdk_screen_get_default (),
					     GTK_STYLE_PROVIDER
					     (css_provider),
					     GTK_STYLE_PROVIDER_PRIORITY_USER);

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
  remote_rename_menuitem =
    GTK_WIDGET (gtk_builder_get_object (builder, "remote_rename_menuitem"));
  remote_delete_menuitem =
    GTK_WIDGET (gtk_builder_get_object (builder, "remote_delete_menuitem"));
  g_signal_connect (download_menuitem, "activate",
		    G_CALLBACK (elektroid_add_download_tasks), NULL);
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
    .add_dir_button =
      GTK_WIDGET (gtk_builder_get_object (builder, "remote_add_dir_button")),
    .refresh_button =
      GTK_WIDGET (gtk_builder_get_object (builder, "remote_refresh_button")),
    .dir_entry =
      GTK_ENTRY (gtk_builder_get_object (builder, "remote_dir_entry")),
    .menu = GTK_MENU (gtk_builder_get_object (builder, "remote_menu")),
    .dir = g_malloc0 (PATH_MAX),
    .check_selection = elektroid_remote_check_selection,
    .file_icon = NULL,
    .fs_ops = NULL,
    .backend = &backend,
    .notify_dir_change = NULL,
    .check_callback = elektroid_check_backend
  };
  remote_tree_view_index_column =
    GTK_TREE_VIEW_COLUMN (gtk_builder_get_object
			  (builder, "remote_tree_view_index_column"));

  remote_browser.selection_changed_handler_id =
    g_signal_connect (gtk_tree_view_get_selection (remote_browser.view),
		      "changed", G_CALLBACK (browser_selection_changed),
		      &remote_browser);
  g_signal_connect (remote_browser.view, "row-activated",
		    G_CALLBACK (browser_item_activated), &remote_browser);
  g_signal_connect (remote_browser.up_button, "clicked",
		    G_CALLBACK (browser_go_up), &remote_browser);
  g_signal_connect (remote_browser.add_dir_button, "clicked",
		    G_CALLBACK (elektroid_add_dir), &remote_browser);
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
		       TARGET_ENTRIES_REMOTE_SRC,
		       G_N_ELEMENTS (TARGET_ENTRIES_REMOTE_SRC),
		       GDK_ACTION_COPY);
  gtk_drag_dest_set ((GtkWidget *) remote_browser.up_button,
		     GTK_DEST_DEFAULT_MOTION | GTK_DEST_DEFAULT_HIGHLIGHT,
		     TARGET_ENTRIES_UP_BUTTON_DST,
		     G_N_ELEMENTS (TARGET_ENTRIES_UP_BUTTON_DST),
		     GDK_ACTION_COPY);

  local_browser = (struct browser)
  {
    .view =
      GTK_TREE_VIEW (gtk_builder_get_object (builder, "local_tree_view")),
    .up_button =
      GTK_WIDGET (gtk_builder_get_object (builder, "local_up_button")),
    .add_dir_button =
      GTK_WIDGET (gtk_builder_get_object (builder, "local_add_dir_button")),
    .refresh_button =
      GTK_WIDGET (gtk_builder_get_object (builder, "local_refresh_button")),
    .dir_entry =
      GTK_ENTRY (gtk_builder_get_object (builder, "local_dir_entry")),
    .menu = GTK_MENU (gtk_builder_get_object (builder, "local_menu")),
    .dir = preferences.local_dir,
    .check_selection = elektroid_local_check_selection,
    .file_icon = BE_FILE_ICON_WAVE,
    .extensions = NULL,
    .fs_ops = &FS_LOCAL_OPERATIONS,
    .backend = NULL,
    .notify_dir_change = elektroid_notify_local_dir_change,
    .check_callback = NULL
  };

  local_browser.selection_changed_handler_id =
    g_signal_connect (gtk_tree_view_get_selection (local_browser.view),
		      "changed", G_CALLBACK (browser_selection_changed),
		      &local_browser);
  g_signal_connect (local_browser.view, "row-activated",
		    G_CALLBACK (browser_item_activated), &local_browser);
  g_signal_connect (local_browser.up_button, "clicked",
		    G_CALLBACK (browser_go_up), &local_browser);
  g_signal_connect (local_browser.add_dir_button, "clicked",
		    G_CALLBACK (elektroid_add_dir), &local_browser);
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

  elektroid_set_browser_options (&local_browser);

  gtk_drag_source_set ((GtkWidget *) local_browser.view, GDK_BUTTON1_MASK,
		       TARGET_ENTRIES_LOCAL_SRC,
		       G_N_ELEMENTS (TARGET_ENTRIES_LOCAL_SRC),
		       GDK_ACTION_MOVE);
  gtk_drag_dest_set ((GtkWidget *) local_browser.view, GTK_DEST_DEFAULT_ALL,
		     TARGET_ENTRIES_LOCAL_DST,
		     G_N_ELEMENTS (TARGET_ENTRIES_LOCAL_DST),
		     GDK_ACTION_COPY);
  gtk_drag_dest_set ((GtkWidget *) local_browser.up_button,
		     GTK_DEST_DEFAULT_MOTION | GTK_DEST_DEFAULT_HIGHLIGHT,
		     TARGET_ENTRIES_UP_BUTTON_DST,
		     G_N_ELEMENTS (TARGET_ENTRIES_UP_BUTTON_DST),
		     GDK_ACTION_COPY);

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
  gtk_switch_set_active (GTK_SWITCH (autoplay_switch), preferences.autoplay);

  fs_list_store =
    GTK_LIST_STORE (gtk_builder_get_object (builder, "fs_list_store"));
  fs_combo = GTK_COMBO_BOX (gtk_builder_get_object (builder, "fs_combo"));
  g_signal_connect (fs_combo, "changed", G_CALLBACK (elektroid_set_fs), NULL);

  sample_info_box =
    GTK_WIDGET (gtk_builder_get_object (builder, "sample_info_box"));
  sample_length =
    GTK_WIDGET (gtk_builder_get_object (builder, "sample_length"));
  sample_channels =
    GTK_WIDGET (gtk_builder_get_object (builder, "sample_channels"));
  sample_samplerate =
    GTK_WIDGET (gtk_builder_get_object (builder, "sample_samplerate"));
  sample_bitdepth =
    GTK_WIDGET (gtk_builder_get_object (builder, "sample_bitdepth"));

  gtk_widget_set_sensitive (remote_box, FALSE);
  gtk_widget_set_sensitive (rx_sysex_button, FALSE);
  gtk_widget_set_sensitive (tx_sysex_button, FALSE);
  gtk_widget_set_sensitive (os_upgrade_button, FALSE);

  elektroid_load_devices (TRUE);	//This triggers a local browser reload due to the extensions and icons selected for the fs

  gethostname (hostname, LABEL_MAX);
  gtk_label_set_text (GTK_LABEL (hostname_label), hostname);

  debug_print (1, "Creating notifier thread...\n");
  notifier_init (&notifier, &local_browser);
  notifier_set_dir (&notifier, local_browser.dir);
  notifier_thread = g_thread_new ("notifier_thread", notifier_run, &notifier);

  gtk_widget_show (main_window);
  gtk_main ();

  free (remote_browser.dir);

  if (backend_check (&backend))
    {
      backend_destroy (&backend);
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
  gint opt, ret;
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

  preferences_load (&preferences);
  if (local_dir)
    {
      g_free (preferences.local_dir);
      preferences.local_dir = get_local_startup_path (local_dir);
    }

  ret = elektroid_run (argc, argv);

  preferences_save (&preferences);
  preferences_free (&preferences);

  return ret;
}
