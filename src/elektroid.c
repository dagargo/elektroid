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

#include <limits.h>
#include <locale.h>
#include <gtk/gtk.h>
#if defined(__linux__)
#include <glib-unix.h>
#endif
#include <glib/gi18n.h>
#include <glib/gprintf.h>
#include <getopt.h>
#include "regconn.h"
#include "regma.h"
#include "regpref.h"
#include "browser.h"
#include "editor.h"
#include "tasks.h"
#include "sample.h"
#include "local.h"
#include "preferences.h"
#include "progress.h"

#define PATH_TYPE_FROM_DND_TYPE(dnd) (strcmp (dnd, TEXT_URI_LIST_ELEKTROID) ? PATH_SYSTEM : backend_get_path_type (&backend))

#define TEXT_URI_LIST_STD "text/uri-list"
#define TEXT_URI_LIST_ELEKTROID "text/uri-list-elektroid"

#define MSG_WARN_SAME_SRC_DST "Same source and destination path. Skipping..."

#define TREEVIEW_SCROLL_LINES 2
#define TREEVIEW_EDGE_SIZE 20

#define PROGRESS_DELETE_THRESHOLD 25

#define BACKEND_PLAYING "\u23f5"
#define BACKEND_STOPPED "\u23f9"

#define SYSEX_FILTER "*." BE_SYSEX_EXT

enum device_list_store_columns
{
  DEVICES_LIST_STORE_TYPE_FIELD,
  DEVICES_LIST_STORE_ID_FIELD,
  DEVICES_LIST_STORE_NAME_FIELD
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

struct elektroid_dnd_data
{
  GtkWidget *widget;
  gchar **uris;
  gchar *type_name;
};

static gpointer elektroid_upload_task_runner (gpointer);
static gpointer elektroid_download_task_runner (gpointer);
static void elektroid_update_progress (struct job_control *);

static const GtkTargetEntry TARGET_ENTRIES_LOCAL_DST[] = {
  {TEXT_URI_LIST_STD, 0, TARGET_STRING},
  {TEXT_URI_LIST_ELEKTROID, GTK_TARGET_SAME_APP | GTK_TARGET_OTHER_WIDGET,
   TARGET_STRING}
};

static const GtkTargetEntry TARGET_ENTRIES_LOCAL_SRC[] = {
  {TEXT_URI_LIST_STD, 0, TARGET_STRING}
};

static const GtkTargetEntry TARGET_ENTRIES_REMOTE_SYSTEM_DST[] = {
  {TEXT_URI_LIST_STD, 0, TARGET_STRING},
  {TEXT_URI_LIST_ELEKTROID, GTK_TARGET_SAME_APP, TARGET_STRING}
};

static const GtkTargetEntry TARGET_ENTRIES_REMOTE_SYSTEM_SRC[] = {
  {TEXT_URI_LIST_ELEKTROID, 0, TARGET_STRING}
};

static const GtkTargetEntry TARGET_ENTRIES_REMOTE_MIDI_DST[] = {
  {TEXT_URI_LIST_STD, 0, TARGET_STRING},
  {TEXT_URI_LIST_ELEKTROID, GTK_TARGET_SAME_APP, TARGET_STRING}
};

static const GtkTargetEntry TARGET_ENTRIES_REMOTE_MIDI_DST_SLOT[] = {
  {TEXT_URI_LIST_STD, 0, TARGET_STRING}
};

static const GtkTargetEntry TARGET_ENTRIES_REMOTE_MIDI_SRC[] = {
  {TEXT_URI_LIST_ELEKTROID, GTK_TARGET_SAME_APP, TARGET_STRING}
};

static const GtkTargetEntry TARGET_ENTRIES_UP_BUTTON_DST[] = {
  {TEXT_URI_LIST_STD, 0, TARGET_STRING},
  {TEXT_URI_LIST_ELEKTROID, GTK_TARGET_SAME_APP, TARGET_STRING}
};

static const gchar *hostname;
static gchar *local_dir;

struct editor editor;
struct tasks tasks;
extern struct browser local_browser;
extern struct browser remote_browser;
extern struct maction_context maction_context;

static struct backend backend;

static guint batch_id;

GtkWidget *dialog;

static GtkWindow *main_window;
static GtkBuilder *builder;
static GtkAboutDialog *about_dialog;
static GtkDialog *name_dialog;
static GtkEntry *name_dialog_entry;
static GtkWidget *name_dialog_accept_button;
static GtkDialog *preferences_dialog;
static GtkWidget *audio_use_float_switch;
static GtkWidget *play_sample_while_loading_switch;
static GtkWidget *audio_buffer_length_combo;
static GtkWidget *stop_device_when_connecting_switch;
static GtkWidget *elektron_load_sound_tags_switch;
static GtkPopover *main_popover;
static GtkWidget *show_remote_button;
static GtkWidget *preferences_button;
static GtkWidget *about_button;
static GtkWidget *local_name_entry;
static GtkWidget *local_box;
static GtkWidget *remote_devices_box;
static GtkWidget *remote_box;
static GtkWidget *local_side;
static GtkWidget *remote_side;
static GtkWidget *tasks_box;
static GtkLabel *backend_status_label;
static GtkLabel *host_audio_status_label;
static GtkLabel *host_midi_status_label;
static GtkListStore *devices_list_store;
static GtkWidget *devices_combo;
static GtkListStore *fs_list_store;
static GtkWidget *fs_combo;

static void
show_error_msg (const char *format, ...)
{
  gchar *msg;
  va_list args;

  va_start (args, format);
  g_vasprintf (&msg, format, args);
  dialog = gtk_message_dialog_new (main_window, GTK_DIALOG_MODAL,
				   GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE,
				   "%s", msg);
  gtk_dialog_run (GTK_DIALOG (dialog));
  gtk_widget_destroy (dialog);
  dialog = NULL;
  g_free (msg);
  va_end (args);
}

static void
elektroid_load_devices (gboolean auto_select)
{
  gint i;
  gint device_index;
  GArray *devices = backend_get_devices ();
  struct backend_device device;

  debug_print (1, "Loading devices...");

  if (editor.browser == &remote_browser)
    {
      editor_reset (&editor, NULL);
    }

  gtk_list_store_clear (fs_list_store);
  gtk_list_store_clear (devices_list_store);

  for (i = 0; i < devices->len; i++)
    {
      device = g_array_index (devices, struct backend_device, i);
      gtk_list_store_insert_with_values (devices_list_store, NULL, -1,
					 DEVICES_LIST_STORE_TYPE_FIELD,
					 device.type,
					 DEVICES_LIST_STORE_ID_FIELD,
					 device.id,
					 DEVICES_LIST_STORE_NAME_FIELD,
					 device.name, -1);
    }

  g_array_free (devices, TRUE);

  device_index = auto_select && i == 1 ? 0 : -1;
  debug_print (1, "Selecting device %d...", device_index);
  gtk_combo_box_set_active (GTK_COMBO_BOX (devices_combo), device_index);
  if (device_index == -1)
    {
      browser_update_fs_options (&local_browser);
      browser_load_dir (&local_browser);
    }
}

void
elektroid_update_audio_status (gboolean status)
{
  gchar msg[LABEL_MAX];
  if (status)
    {
      snprintf (msg, LABEL_MAX, "%s: %s %s, %.5g kHz %s %s", _("Audio"),
		audio_name (), audio_version (),
		editor.audio.rate / 1000.f,
		preferences_get_boolean (PREF_KEY_AUDIO_USE_FLOAT) ?
		SF_FORMAT_FLOAT_STR : SF_FORMAT_PCM_16_STR, BACKEND_PLAYING);
    }
  else
    {
      snprintf (msg, LABEL_MAX, "%s: %s %s %s", _("Audio"), audio_name (),
		audio_version (), BACKEND_STOPPED);
    }
  gtk_label_set_text (host_audio_status_label, msg);
}

static void
elektroid_update_midi_status ()
{
  gchar msg[LABEL_MAX];
  const gchar *v = backend.type == BE_TYPE_MIDI ? BACKEND_PLAYING :
    BACKEND_STOPPED;
  snprintf (msg, LABEL_MAX, "MIDI: %s %s", backend_name (), v);
  gtk_label_set_text (host_midi_status_label, msg);
}

static void
elektroid_update_backend_status ()
{
  gchar *status;
  gchar *statfss_str;
  struct backend_storage_stats statfs;
  GString *statfss;

  if (backend_check (&backend))
    {
      statfss = g_string_new (NULL);
      if (backend.get_storage_stats)
	{
	  for (guint i = 1; i < G_MAXUINT8; i <<= 1)
	    {
	      gint v = backend.get_storage_stats (&backend, i, &statfs,
						  remote_browser.dir);
	      if (v >= 0)
		{
		  g_string_append_printf (statfss, " %s %.2f%%", statfs.name,
					  backend_get_storage_stats_percent
					  (&statfs));
		}

	      if (!v)
		{
		  break;
		}
	    }
	}

      statfss_str = g_string_free (statfss, FALSE);
      status = g_malloc (LABEL_MAX);

      if (strlen (backend.name))
	{
	  snprintf (status, LABEL_MAX, "%s", backend.name);
	  if (*backend.version)
	    {
	      strncat (status, " ", LABEL_MAX - sizeof (status) - 2);
	      strncat (status, backend.version,
		       LABEL_MAX - sizeof (status) -
		       strlen (backend.version) - 1);
	    }
	  if (*backend.description)
	    {
	      strncat (status, " (", LABEL_MAX - sizeof (status) - 3);
	      strncat (status, backend.description,
		       LABEL_MAX - sizeof (status) -
		       strlen (backend.description) - 1);
	      strncat (status, ")", LABEL_MAX - sizeof (status) - 2);
	    }
	  if (statfss_str)
	    {
	      strncat (status, statfss_str,
		       sizeof (status) - strlen (statfss_str) - 1);
	    }
	}
      else
	{
	  status[0] = 0;
	}
      gtk_label_set_text (backend_status_label, status);
      g_free (status);
      g_free (statfss_str);
    }
  else
    {
      gtk_label_set_text (backend_status_label, _("Not connected"));
    }
}

gboolean
elektroid_check_backend (gboolean startup)
{
  gboolean connected = backend_check (&backend);

  gtk_widget_set_sensitive (remote_box, connected);

  if (!connected)
    {
      browser_reset (&remote_browser);
      elektroid_load_devices (startup);
    }

  elektroid_update_backend_status ();

  return connected;
}

static void
elektroid_cancel_all_tasks_and_wait ()
{
  tasks_cancel_all (NULL, &tasks);
  //In this case, the active waiting can not be avoided as the user has canceled the operation.
  while (tasks.transfer.status == TASK_STATUS_RUNNING)
    {
      usleep (50000);
    }
}

static void
elektroid_set_preferences_remote_dir ()
{
  if (backend.type == BE_TYPE_SYSTEM)
    {
      if (remote_browser.dir)
	{
	  preferences_set_string (PREF_KEY_REMOTE_DIR,
				  strdup (remote_browser.dir));
	}
    }
}

void
elektroid_refresh_devices (gboolean startup)
{
  elektroid_set_preferences_remote_dir ();

  if (backend_check (&backend))
    {
      elektroid_cancel_all_tasks_and_wait ();
      backend_destroy (&backend);
      maction_menu_clear (&maction_context);
      browser_reset (&remote_browser);
    }
  elektroid_check_backend (startup);	//This triggers the actual devices refresh if there is no backend
}

static void
elektroid_refresh_devices_int (GtkWidget *widget, gpointer data)
{
  elektroid_refresh_devices (FALSE);
}

static gpointer
elektroid_rx_sysex_runner (gpointer data)
{
  gint *res = g_malloc (sizeof (gint));
  gchar *text;

  progress.sysex_transfer.status = WAITING;
  progress.sysex_transfer.active = TRUE;
  progress.sysex_transfer.timeout = BE_SYSEX_TIMEOUT_MS;
  progress.sysex_transfer.batch = TRUE;

  //This doesn't need to be synchronized because the GUI doesn't allow concurrent access when receiving SysEx in batch mode.
  backend_rx_drain (&backend);

  if (progress.sysex_transfer.active)
    {
      *res = backend_rx_sysex (&backend, &progress.sysex_transfer);
      if (!*res)
	{
	  text = debug_get_hex_msg (progress.sysex_transfer.raw);
	  debug_print (1, "SysEx message received (%d): %s",
		       progress.sysex_transfer.raw->len, text);
	  g_free (text);
	}
    }
  else
    {
      *res = -ECANCELED;
    }

  progress_response (GTK_RESPONSE_ACCEPT);

  return res;
}

void
elektroid_rx_sysex ()
{
  GtkFileChooser *chooser;
  GtkFileFilter *filter;
  gint dres;
  gchar *filename;
  gchar *filename_w_ext;
  const gchar *ext;
  gint *res;
  GtkFileChooserAction action = GTK_FILE_CHOOSER_ACTION_SAVE;

  res =
    progress_run (elektroid_rx_sysex_runner, PROGRESS_TYPE_SYSEX_TRANSFER,
		  NULL, _("Receiving SysEx"), "", TRUE, &dres);
  if (!res)			//Signal captured while running the dialog.
    {
      g_byte_array_free (progress.sysex_transfer.raw, TRUE);
      return;
    }

  if (dres != GTK_RESPONSE_ACCEPT)
    {
      if (!*res)
	{
	  g_byte_array_free (progress.sysex_transfer.raw, TRUE);
	}
      g_free (res);
      return;
    }

  if (*res)
    {
      elektroid_check_backend (FALSE);
      g_free (res);
      return;
    }

  dialog = gtk_file_chooser_dialog_new (_("Save SysEx"), main_window, action,
					_("_Cancel"), GTK_RESPONSE_CANCEL,
					_("_Save"), GTK_RESPONSE_ACCEPT,
					NULL);
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

  while (gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_ACCEPT)
    {
      filename = gtk_file_chooser_get_filename (chooser);
      ext = filename_get_ext (filename);

      if (strcmp (ext, BE_SYSEX_EXT) != 0)
	{
	  filename_w_ext = g_strconcat (filename, SYSEX_FILTER, NULL);
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
      struct idata idata;
      idata.content = progress.sysex_transfer.raw;
      *res = file_save (filename, &idata, NULL);
      if (*res)
	{
	  show_error_msg (_("Error while saving “%s”: %s."),
			  filename, g_strerror (-*res));
	}
      g_byte_array_free (progress.sysex_transfer.raw, TRUE);
      g_free (res);
      g_free (filename);
    }

  gtk_widget_destroy (dialog);
  dialog = NULL;
}

static gint
elektroid_send_sysex_file (const gchar *filename, t_sysex_transfer f)
{
  struct idata idata;
  gint err = file_load (filename, &idata, NULL);
  if (!err)
    {
      progress.sysex_transfer.raw = idata.content;
      err = f (&backend, &progress.sysex_transfer);
      idata_free (&idata);
    }
  if (err && err != -ECANCELED)
    {
      show_error_msg (_("Error while loading “%s”: %s."),
		      filename, g_strerror (-err));
    }
  return err;
}

gpointer
elektroid_tx_sysex_files_runner (gpointer data)
{
  GSList *filenames = data;
  gint *err = g_malloc (sizeof (gint));

  progress.sysex_transfer.active = TRUE;
  progress.sysex_transfer.status = SENDING;

  *err = 0;
  while (*err != -ECANCELED && filenames)
    {
      *err = elektroid_send_sysex_file (filenames->data,
					backend_tx_sysex_no_status);
      filenames = filenames->next;
      //The device may have sent some messages in response so we skip all these.
      backend_rx_drain (&backend);
      usleep (BE_REST_TIME_US);
    }
  progress_response (GTK_RESPONSE_CANCEL);	//Any response is OK.

  return err;
}

gpointer
elektroid_tx_upgrade_os_runner (gpointer data)
{
  GSList *filenames = data;
  gint *err = g_malloc (sizeof (gint));

  progress.sysex_transfer.active = TRUE;
  progress.sysex_transfer.status = SENDING;
  progress.sysex_transfer.timeout = BE_SYSEX_TIMEOUT_MS;

  *err = elektroid_send_sysex_file (filenames->data, backend.upgrade_os);
  progress_response (GTK_RESPONSE_CANCEL);	//Any response is OK.

  return err;
}

void
elektroid_tx_sysex_common (GThreadFunc func, gboolean multiple)
{
  GtkFileChooser *chooser;
  GtkFileFilter *filter;
  gint res, *err;
  GSList *filenames;

  dialog = gtk_file_chooser_dialog_new (_("Open SysEx"), main_window,
					GTK_FILE_CHOOSER_ACTION_OPEN,
					_("_Cancel"), GTK_RESPONSE_CANCEL,
					_("_Open"), GTK_RESPONSE_ACCEPT,
					NULL);
  chooser = GTK_FILE_CHOOSER (dialog);
  filter = gtk_file_filter_new ();
  gtk_file_filter_set_name (filter, _("SysEx Files"));
  gtk_file_filter_add_pattern (filter, SYSEX_FILTER);
  gtk_file_chooser_add_filter (chooser, filter);
  gtk_file_chooser_set_current_folder (chooser, g_get_home_dir ());
  gtk_file_chooser_set_select_multiple (chooser, multiple);

  res = gtk_dialog_run (GTK_DIALOG (dialog));
  if (res == GTK_RESPONSE_ACCEPT)
    {
      gtk_widget_hide (GTK_WIDGET (dialog));
      filenames = gtk_file_chooser_get_filenames (chooser);
      err = progress_run (func, PROGRESS_TYPE_SYSEX_TRANSFER, filenames,
			  _("Sending SysEx"), "", TRUE, NULL);
      g_slist_free_full (g_steal_pointer (&filenames), g_free);

      if (!err)			//Signal captured while running the dialog.
	{
	  goto cleanup;
	}

      if (*err < 0)
	{
	  elektroid_check_backend (FALSE);
	}

      g_free (err);
    }

cleanup:
  gtk_widget_destroy (dialog);
  dialog = NULL;
}

static void
elektroid_show_remote (gboolean active)
{
  elektroid_refresh_devices (TRUE);
  gtk_widget_set_visible (local_name_entry, active);
  gtk_widget_set_visible (remote_side, active);
  gtk_widget_set_margin_end (local_side, active ? 6 : 0);
  gtk_widget_set_visible (tasks_box, active);
  gtk_widget_set_visible (editor.mix_switch_box, active);
}

static void
elektroid_show_remote_clicked (GtkWidget *object, gpointer data)
{
  gboolean active;

  g_object_get (G_OBJECT (show_remote_button), "active", &active, NULL);
  active = !active;
  preferences_set_boolean (PREF_KEY_SHOW_REMOTE, active);
  g_object_set (G_OBJECT (show_remote_button), "active", active, NULL);

  gtk_widget_hide (GTK_WIDGET (main_popover));

  elektroid_show_remote (active);
}

static void
elektroid_show_preferences (GtkWidget *object, gpointer data)
{
  gint res, i, j, prev, post;
  gboolean b;
  GtkTreeIter iter;
  GValue x = G_VALUE_INIT;
  GtkTreeModel *model =
    gtk_combo_box_get_model (GTK_COMBO_BOX (audio_buffer_length_combo));

  b = preferences_get_boolean (PREF_KEY_AUDIO_USE_FLOAT);
  gtk_switch_set_active (GTK_SWITCH (audio_use_float_switch), b);
  b = preferences_get_boolean (PREF_KEY_PLAY_WHILE_LOADING);
  gtk_switch_set_active (GTK_SWITCH (play_sample_while_loading_switch), b);
  b = preferences_get_boolean (PREF_KEY_STOP_DEVICE_WHEN_CONNECTING);
  gtk_switch_set_active (GTK_SWITCH (stop_device_when_connecting_switch), b);
  b = preferences_get_boolean (PREF_KEY_ELEKTRON_LOAD_SOUND_TAGS);
  gtk_switch_set_active (GTK_SWITCH (elektron_load_sound_tags_switch), b);

  i = preferences_get_int (PREF_KEY_AUDIO_BUFFER_LEN);

  gtk_tree_model_get_iter_first (model, &iter);

  j = 0;
  do
    {
      gtk_tree_model_get_value (model, &iter, 0, &x);
      if (g_value_get_int (&x) == i)
	{
	  gtk_combo_box_set_active (GTK_COMBO_BOX (audio_buffer_length_combo),
				    j);
	}
      j++;
      g_value_unset (&x);
    }
  while (gtk_tree_model_iter_next (model, &iter));

  prev = (preferences_get_boolean (PREF_KEY_AUDIO_USE_FLOAT) ? 0x10000 : 0) +
    i;

  res = gtk_dialog_run (GTK_DIALOG (preferences_dialog));
  gtk_widget_hide (GTK_WIDGET (preferences_dialog));

  if (res != GTK_RESPONSE_ACCEPT)
    {
      return;
    }

  b = gtk_switch_get_active (GTK_SWITCH (audio_use_float_switch));
  preferences_set_boolean (PREF_KEY_AUDIO_USE_FLOAT, b);
  b = gtk_switch_get_active (GTK_SWITCH (play_sample_while_loading_switch));
  preferences_set_boolean (PREF_KEY_PLAY_WHILE_LOADING, b);
  b = gtk_switch_get_active (GTK_SWITCH (stop_device_when_connecting_switch));
  preferences_set_boolean (PREF_KEY_STOP_DEVICE_WHEN_CONNECTING, b);
  b = gtk_switch_get_active (GTK_SWITCH (elektron_load_sound_tags_switch));
  preferences_set_boolean (PREF_KEY_ELEKTRON_LOAD_SOUND_TAGS, b);

  i = gtk_combo_box_get_active (GTK_COMBO_BOX (audio_buffer_length_combo));

  gtk_tree_model_get_iter_first (model, &iter);
  for (j = 0; j < i; j++)
    {
      gtk_tree_model_iter_next (model, &iter);
    }
  gtk_tree_model_get_value (model, &iter, 0, &x);

  i = g_value_get_int (&x);

  preferences_set_int (PREF_KEY_AUDIO_BUFFER_LEN, i);
  g_value_unset (&x);

  post = (preferences_get_boolean (PREF_KEY_AUDIO_USE_FLOAT) ? 0x10000 : 0) +
    i;

  if (prev != post)
    {
      editor_reset_audio (&editor);
    }
}

static void
elektroid_show_about (GtkWidget *object, gpointer data)
{
  gtk_dialog_run (GTK_DIALOG (about_dialog));
  gtk_widget_hide (GTK_WIDGET (about_dialog));
}

static gint
elektroid_delete_file (struct browser *browser, gchar *dir, struct item *item)
{
  gint err = 0;
  gchar *path;
  enum path_type type = backend_get_path_type (browser->backend);

  path = path_chain (type, dir, item->name);

  debug_print (1, "Deleting %s...", path);

  if (item->type == ITEM_TYPE_FILE)
    {
      gchar *filename = item_get_filename (item, browser->fs_ops->options);
      gchar *id_path = path_chain (type, dir, filename);
      g_free (filename);
      err = browser->fs_ops->delete (browser->backend, id_path);
      if (err)
	{
	  error_print ("Error while deleting “%s”: %s.", path,
		       g_strerror (-err));
	}
      g_free (id_path);
    }
  else if (item->type == ITEM_TYPE_DIR)
    {
      struct item_iterator iter;
      if (browser->fs_ops->readdir (browser->backend, &iter, path, NULL))
	{
	  err = -ENOTDIR;
	  goto end;
	}

      while (!item_iterator_next (&iter))
	{
	  elektroid_delete_file (browser, path, &iter.item);

	  if (!progress_is_active ())
	    {
	      item_iterator_free (&iter);
	      err = -ECANCELED;
	      goto end;
	    }
	}

      browser->fs_ops->delete (browser->backend, path);
      item_iterator_free (&iter);
    }

end:
  g_free (path);
  return err;
}

static gpointer
elektroid_delete_files_runner (gpointer data)
{
  GList *list, *tree_path_list, *ref_list;
  GtkTreeSelection *selection;
  GtkTreeModel *model;
  struct browser *browser = data;

  progress.sysex_transfer.active = TRUE;

  selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (browser->view));
  model = GTK_TREE_MODEL (gtk_tree_view_get_model (browser->view));
  tree_path_list = gtk_tree_selection_get_selected_rows (selection, &model);
  ref_list = NULL;

  //A GtkTreeModel object can NOT be modified while iterating over the selection.
  list = tree_path_list;
  while (list)
    {
      GtkTreeRowReference *ref = gtk_tree_row_reference_new (model,
							     list->data);
      ref_list = g_list_append (ref_list, ref);

      list = g_list_next (list);
    }
  g_list_free_full (tree_path_list, (GDestroyNotify) gtk_tree_path_free);

  g_mutex_lock (&browser->mutex);
  list = ref_list;
  while (list)
    {
      GtkTreeIter iter;
      struct item item;
      GtkTreePath *tree_path = gtk_tree_row_reference_get_path (list->data);

      gtk_tree_model_get_iter (model, &iter, tree_path);
      browser_set_item (model, &iter, &item);

      if (elektroid_delete_file (browser, browser->dir, &item))
	{
	  error_print ("Error while deleting file");
	}

      if (!progress_is_active ())
	{
	  break;
	}

      list = g_list_next (list);
    }
  g_list_free_full (ref_list, (GDestroyNotify) gtk_tree_row_reference_free);
  g_mutex_unlock (&browser->mutex);

  progress_response (GTK_RESPONSE_ACCEPT);
  return NULL;
}

static void
elektroid_delete_files (GtkWidget *object, gpointer data)
{
  gint res;
  struct browser *browser = data;

  dialog = gtk_message_dialog_new (main_window, GTK_DIALOG_MODAL,
				   GTK_MESSAGE_ERROR, GTK_BUTTONS_NONE,
				   _
				   ("Are you sure you want to delete the selected items?"));
  gtk_dialog_add_buttons (GTK_DIALOG (dialog), _("_Cancel"),
			  GTK_RESPONSE_CANCEL, _("_Delete"),
			  GTK_RESPONSE_ACCEPT, NULL);
  gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_ACCEPT);
  res = gtk_dialog_run (GTK_DIALOG (dialog));
  gtk_widget_destroy (dialog);
  dialog = NULL;
  if (res != GTK_RESPONSE_ACCEPT)
    {
      return;
    }

  if (BROWSER_IS_SYSTEM (browser) &&
      browser_get_selected_items_count (browser) <= PROGRESS_DELETE_THRESHOLD)
    {
      elektroid_delete_files_runner (browser);
    }
  else
    {
      progress_run (elektroid_delete_files_runner, PROGRESS_TYPE_PULSE,
		    browser, _("Deleting Files"), _("Deleting..."), TRUE,
		    NULL);
    }

  browser_load_dir_if_needed (data);
}

static void
elektroid_rename_item (GtkWidget *object, gpointer data)
{
  gchar *old_path, *new_path;
  const gchar *ext;
  gint result, err, sel_len, ext_len;
  GtkTreeIter iter;
  struct item item;
  struct browser *browser = data;
  GtkTreeModel *model =
    GTK_TREE_MODEL (gtk_tree_view_get_model (browser->view));

  browser_set_selected_row_iter (browser, &iter);
  browser_set_item (model, &iter, &item);
  old_path = browser_get_item_path (browser, &item);

  sel_len = strlen (item.name);
  ext = filename_get_ext (item.name);
  ext_len = strlen (ext);
  if (ext_len)
    {
      sel_len -= ext_len + 1;
    }

  gtk_entry_set_max_length (name_dialog_entry, browser->fs_ops->max_name_len);
  gtk_entry_set_text (name_dialog_entry, item.name);
  gtk_widget_grab_focus (GTK_WIDGET (name_dialog_entry));
  gtk_editable_select_region (GTK_EDITABLE (name_dialog_entry), 0, sel_len);
  gtk_widget_set_sensitive (name_dialog_accept_button, FALSE);

  gtk_window_set_title (GTK_WINDOW (name_dialog), _("Rename"));

  result = GTK_RESPONSE_ACCEPT;

  err = -1;
  while (err < 0 && result == GTK_RESPONSE_ACCEPT)
    {
      result = gtk_dialog_run (GTK_DIALOG (name_dialog));

      if (result == GTK_RESPONSE_ACCEPT)
	{
	  if (browser->fs_ops->options & FS_OPTION_SLOT_STORAGE)
	    {
	      new_path = strdup (gtk_entry_get_text (name_dialog_entry));
	    }
	  else
	    {
	      enum path_type type = backend_get_path_type (browser->backend);
	      new_path = path_chain (type, browser->dir,
				     gtk_entry_get_text (name_dialog_entry));
	    }
	  err = browser->fs_ops->rename (&backend, old_path, new_path);
	  if (err)
	    {
	      show_error_msg (_("Error while renaming to “%s”: %s."),
			      new_path, g_strerror (-err));
	    }
	  else
	    {
	      browser_load_dir_if_needed (browser);
	    }
	  g_free (new_path);
	}
    }

  g_free (old_path);
  gtk_widget_hide (GTK_WIDGET (name_dialog));
}

static gboolean
elektroid_drag_begin (GtkWidget *widget, GdkDragContext *context,
		      gpointer data)
{
  GtkTreeIter iter;
  GList *tree_path_list;
  GList *list;
  gchar *uri, *path;
  struct item item;
  struct browser *browser = data;
  enum path_type type = backend_get_path_type (browser->backend);
  GtkTreeView *view = GTK_TREE_VIEW (widget);
  GtkTreeModel *model = gtk_tree_view_get_model (view);
  GtkTreeSelection *selection = gtk_tree_view_get_selection (view);

  tree_path_list = gtk_tree_selection_get_selected_rows (selection, &model);

  browser->dnd_data = g_string_new ("");
  for (list = tree_path_list; list != NULL; list = g_list_next (list))
    {
      gtk_tree_model_get_iter (model, &iter, list->data);
      browser_set_item (model, &iter, &item);
      path = browser_get_item_path (browser, &item);
      uri = path_filename_to_uri (type, path);
      g_free (path);
      g_string_append (browser->dnd_data, uri);
      g_free (uri);
      g_string_append (browser->dnd_data, "\n");
    }
  g_list_free_full (tree_path_list, (GDestroyNotify) gtk_tree_path_free);
  browser->dnd = TRUE;

  debug_print (1, "Drag begin data:\n%s", browser->dnd_data->str);

  return FALSE;
}

static gboolean
elektroid_selection_function_true (GtkTreeSelection *selection,
				   GtkTreeModel *model,
				   GtkTreePath *path,
				   gboolean path_currently_selected,
				   gpointer data)
{
  return TRUE;
}

static gboolean
elektroid_selection_function_false (GtkTreeSelection *selection,
				    GtkTreeModel *model,
				    GtkTreePath *path,
				    gboolean path_currently_selected,
				    gpointer data)
{
  return FALSE;
}

static gboolean
elektroid_drag_end (GtkWidget *widget, GdkDragContext *context, gpointer data)
{
  GtkTreeSelection *selection;
  struct browser *browser = data;

  debug_print (1, "Drag end");

  g_string_free (browser->dnd_data, TRUE);
  browser->dnd = FALSE;

  //Performing a DND that was ending in the same browser and directory has no
  //effect. But the selection function was disabled on the button press and
  //never enabled again as there is no button release signal.
  selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (browser->view));
  gtk_tree_selection_set_select_function (selection,
					  elektroid_selection_function_true,
					  NULL, NULL);

  return FALSE;
}

static gboolean
elektroid_button_press (GtkWidget *treeview, GdkEventButton *event,
			gpointer data)
{
  GtkTreePath *path;
  GtkTreeSelection *selection;
  struct browser *browser = data;
  gboolean val = FALSE;

  selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (browser->view));

  gtk_tree_selection_set_select_function (selection,
					  elektroid_selection_function_true,
					  NULL, NULL);

  if (event->state & (GDK_SHIFT_MASK | GDK_CONTROL_MASK))
    {
      return FALSE;
    }

  if (event->button == GDK_BUTTON_PRIMARY
      || event->button == GDK_BUTTON_SECONDARY)
    {
      gtk_tree_view_get_path_at_pos (browser->view, event->x, event->y, &path,
				     NULL, NULL, NULL);

      if (path)
	{

	  if (gtk_tree_selection_path_is_selected (selection, path))
	    {
	      if (event->button == GDK_BUTTON_PRIMARY)
		{
		  gtk_tree_selection_set_select_function (selection,
							  elektroid_selection_function_false,
							  NULL, NULL);
		}
	      else if (event->button == GDK_BUTTON_SECONDARY)
		{
		  val = TRUE;
		}
	    }
	  else
	    {
	      gtk_tree_selection_unselect_all (selection);
	      gtk_tree_selection_select_path (selection, path);
	    }

	  gtk_tree_path_free (path);
	}
      else
	{
	  gtk_tree_selection_unselect_all (selection);
	}

      if (event->button == GDK_BUTTON_SECONDARY)
	{
	  gtk_menu_popup_at_pointer (browser->menu, (GdkEvent *) event);
	}
    }

  return val;
}

static gboolean
elektroid_button_release (GtkWidget *treeview, GdkEventButton *event,
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
      gtk_tree_view_get_path_at_pos (browser->view, event->x, event->y,
				     &path, NULL, NULL, NULL);

      if (path)
	{
	  selection = gtk_tree_view_get_selection (browser->view);

	  if (gtk_tree_selection_path_is_selected (selection, path))
	    {
	      gtk_tree_selection_set_select_function (selection,
						      elektroid_selection_function_true,
						      NULL, NULL);
	      if (browser_get_selected_items_count (browser) != 1)
		{
		  gtk_tree_selection_unselect_all (selection);
		  gtk_tree_selection_select_path (selection, path);
		}
	    }
	  gtk_tree_path_free (path);
	}
    }

  return FALSE;
}

static void
elektroid_show_clicked (GtkWidget *object, gpointer data)
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
  struct browser *browser = data;
  gint count = browser_get_selected_items_count (browser);
  enum path_type type = backend_get_path_type (browser->backend);

  if (count == 0)
    {
      path = path_chain (type, browser->dir, NULL);
    }
  else if (count == 1)
    {
      browser_set_selected_row_iter (browser, &iter);
      model = GTK_TREE_MODEL (gtk_tree_view_get_model (browser->view));
      browser_set_item (model, &iter, &item);
      path = path_chain (type, browser->dir, item.name);
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
					 NULL, "org.freedesktop.FileManager1",
					 "/org/freedesktop/FileManager1",
					 "org.freedesktop.FileManager1", NULL,
					 NULL);
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
elektroid_open_clicked (GtkWidget *object, gpointer data)
{
  GtkTreeIter iter;
  GtkTreeModel *model;
  gchar *path;
  gchar *uri;
  GFile *file;
  struct item item;
  struct browser *browser = data;
  enum path_type type = backend_get_path_type (browser->backend);

  browser_set_selected_row_iter (browser, &iter);
  model = GTK_TREE_MODEL (gtk_tree_view_get_model (browser->view));
  browser_set_item (model, &iter, &item);
  path = path_chain (type, browser->dir, item.name);

  file = g_file_new_for_path (path);
  g_free (path);
  uri = g_file_get_uri (file);
  g_object_unref (file);

  g_app_info_launch_default_for_uri_async (uri, NULL, NULL, NULL, NULL);
  g_free (uri);
}

gint
elektroid_run_dialog_and_destroy (GtkWidget *custom_dialog)
{
  dialog = custom_dialog;
  gtk_window_set_transient_for (GTK_WINDOW (dialog), main_window);
  gint result = gtk_dialog_run (GTK_DIALOG (dialog));
  gtk_widget_destroy (dialog);
  dialog = NULL;
  return result;
}

gchar *
elektroid_ask_name_get_path (const gchar *title, const gchar *value,
			     struct browser *browser, gint start_pos,
			     gint end_pos)
{
  char *pathname = NULL;
  int result;
  gint err;
  enum path_type type = backend_get_path_type (browser->backend);

  gtk_entry_set_text (name_dialog_entry, value);
  gtk_entry_set_max_length (name_dialog_entry, browser->fs_ops->max_name_len);
  gtk_widget_grab_focus (GTK_WIDGET (name_dialog_entry));
  gtk_editable_select_region (GTK_EDITABLE (name_dialog_entry), start_pos,
			      end_pos);
  gtk_widget_set_sensitive (name_dialog_accept_button, strlen (value) > 0);

  gtk_window_set_title (GTK_WINDOW (name_dialog), title);

  result = GTK_RESPONSE_ACCEPT;

  err = -1;
  while (err < 0 && result == GTK_RESPONSE_ACCEPT)
    {
      result = gtk_dialog_run (GTK_DIALOG (name_dialog));

      if (result == GTK_RESPONSE_ACCEPT)
	{
	  pathname = path_chain (type, browser->dir,
				 gtk_entry_get_text (name_dialog_entry));
	  break;
	}
    }

  gtk_widget_hide (GTK_WIDGET (name_dialog));

  return pathname;
}

static void
elektroid_add_dir (GtkWidget *object, gpointer data)
{
  char *pathname;
  struct browser *browser = data;

  pathname = elektroid_ask_name_get_path (_("Add Directory"), "", browser, 0,
					  0);
  if (pathname)
    {
      gint err = browser->fs_ops->mkdir (&backend, pathname);

      if (err)
	{
	  show_error_msg (_("Error while creating dir “%s”: %s."),
			  pathname, g_strerror (-err));
	}
      else
	{
	  browser_load_dir_if_needed (browser);
	}

      g_free (pathname);
    }
}

static void
elektroid_name_dialog_entry_changed (GtkWidget *object, gpointer data)
{
  size_t len = strlen (gtk_entry_get_text (name_dialog_entry));
  gtk_widget_set_sensitive (name_dialog_accept_button, len > 0);
}

static gboolean
elektroid_run_next (gpointer data)
{
  GtkTreeIter iter;
  enum task_type type;
  gchar *src;
  gchar *dst;
  gint fs;
  guint batch_id, mode;
  GtkTreePath *path;
  gboolean transfer_active;
  gboolean found = tasks_get_next_queued (&tasks, &iter, &type, &src, &dst,
					  &fs, &batch_id, &mode);
  const gchar *status_human = tasks_get_human_status (TASK_STATUS_RUNNING);

  transfer_active = job_control_get_active_lock (&tasks.transfer.control);

  if (!transfer_active && found)
    {
      const struct fs_operations *ops =
	backend_get_fs_operations_by_id (&backend, fs);
      if (ops->options & FS_OPTION_SINGLE_OP)
	{
	  gtk_widget_set_sensitive (remote_box, FALSE);
	  gtk_widget_set_sensitive (fs_combo, FALSE);
	}
      gtk_widget_set_sensitive (maction_context.box, FALSE);

      gtk_list_store_set (tasks.list_store, &iter,
			  TASK_LIST_STORE_STATUS_FIELD, TASK_STATUS_RUNNING,
			  TASK_LIST_STORE_STATUS_HUMAN_FIELD, status_human,
			  -1);
      path = gtk_tree_model_get_path (GTK_TREE_MODEL (tasks.list_store),
				      &iter);
      gtk_tree_view_set_cursor (GTK_TREE_VIEW (tasks.tree_view), path, NULL,
				FALSE);
      gtk_tree_path_free (path);
      tasks.transfer.status = TASK_STATUS_RUNNING;
      tasks.transfer.control.active = TRUE;
      tasks.transfer.control.callback = elektroid_update_progress;
      tasks.transfer.control.parts = 0;
      tasks.transfer.control.part = 0;
      tasks.transfer.control.progress = 0.0;
      tasks.transfer.src = src;
      tasks.transfer.dst = dst;
      tasks.transfer.fs_ops = ops;
      tasks.transfer.batch_id = batch_id;
      tasks.transfer.mode = mode;
      debug_print (1, "Running task type %d from %s to %s (filesystem %s)...",
		   type, tasks.transfer.src, tasks.transfer.dst,
		   tasks.transfer.fs_ops->name);

      tasks_update_current_progress (&tasks);

      if (type == TASK_TYPE_UPLOAD)
	{
	  tasks.thread = g_thread_new ("upload_task",
				       elektroid_upload_task_runner, NULL);
	  remote_browser.dirty = TRUE;
	}
      else if (type == TASK_TYPE_DOWNLOAD)
	{
	  tasks.thread = g_thread_new ("download_task",
				       elektroid_download_task_runner, NULL);
	}

      gtk_widget_set_sensitive (tasks.cancel_task_button, TRUE);
    }
  else
    {
      if (remote_browser.fs_ops &&
	  remote_browser.fs_ops->options & FS_OPTION_SINGLE_OP)
	{
	  gtk_widget_set_sensitive (remote_box, TRUE);
	  gtk_widget_set_sensitive (fs_combo, TRUE);

	  if (remote_browser.dirty)
	    {
	      remote_browser.dirty = FALSE;
	      g_idle_add (browser_load_dir_if_needed, &remote_browser);
	    }
	}
      gtk_widget_set_sensitive (maction_context.box, TRUE);
    }

  tasks_check_buttons (&tasks);

  return FALSE;
}

static gboolean
elektroid_show_task_overwrite_dialog (gpointer data)
{
  gint res;
  gboolean apply_to_all;
  GtkWidget *container, *checkbutton;

  dialog = gtk_message_dialog_new (main_window, GTK_DIALOG_MODAL |
				   GTK_DIALOG_USE_HEADER_BAR,
				   GTK_MESSAGE_WARNING, GTK_BUTTONS_NONE,
				   _("Replace file “%s”?"),
				   (gchar *) data);
  gtk_dialog_add_buttons (GTK_DIALOG (dialog), _("_Cancel"),
			  GTK_RESPONSE_CANCEL, _("_Skip"),
			  GTK_RESPONSE_REJECT, _("_Replace"),
			  GTK_RESPONSE_ACCEPT, NULL);
  gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_ACCEPT);
  container = gtk_dialog_get_content_area (GTK_DIALOG (dialog));
  checkbutton =
    gtk_check_button_new_with_label (_("Apply this action to all files"));
  gtk_widget_set_hexpand (checkbutton, TRUE);
  gtk_widget_set_halign (checkbutton, GTK_ALIGN_CENTER);
  gtk_widget_show (checkbutton);
  gtk_container_add (GTK_CONTAINER (container), checkbutton);

  res = gtk_dialog_run (GTK_DIALOG (dialog));
  apply_to_all =
    gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (checkbutton));
  switch (res)
    {
    case GTK_RESPONSE_CANCEL:
      //Cancel current task.
      tasks.transfer.status = TASK_STATUS_CANCELED;
      //Cancel all tasks belonging to the same batch.
      tasks_visit_pending (&tasks, tasks_visitor_set_batch_canceled);
      break;
    case GTK_RESPONSE_REJECT:
      //Cancel current task.
      tasks.transfer.status = TASK_STATUS_CANCELED;
      if (apply_to_all)
	{
	  //Mark pending tasks as SKIP.
	  tasks_visit_pending (&tasks, tasks_batch_visitor_set_skip);
	}
      break;
    case GTK_RESPONSE_ACCEPT:
      //Mark pending tasks as REPLACE.
      if (apply_to_all)
	{
	  tasks_visit_pending (&tasks, tasks_batch_visitor_set_replace);
	}
      break;
    }

  gtk_widget_destroy (dialog);
  dialog = NULL;

  g_mutex_lock (&tasks.transfer.control.mutex);
  g_cond_signal (&tasks.transfer.control.cond);
  g_mutex_unlock (&tasks.transfer.control.mutex);

  return FALSE;
}

//Close the preparing tasks progress dialog if it is open.
static gboolean
elektroid_close_progress_dialog (gpointer data)
{
  progress_response (GTK_RESPONSE_CANCEL);
  return FALSE;
}

static void
elektroid_check_file_and_wait (gchar *path, struct browser *browser)
{
  struct backend *backend = browser->backend;
  const struct fs_operations *fs_ops = browser->fs_ops;
  if (fs_ops->file_exists && fs_ops->file_exists (backend, path))
    {
      switch (tasks.transfer.mode)
	{
	case TASK_MODE_ASK:
	  g_idle_add (elektroid_close_progress_dialog, NULL);
	  g_idle_add (elektroid_show_task_overwrite_dialog, path);
	  g_cond_wait (&tasks.transfer.control.cond,
		       &tasks.transfer.control.mutex);
	  break;
	case TASK_MODE_SKIP:
	  tasks.transfer.status = TASK_STATUS_CANCELED;
	  break;
	}
    }
}

static gpointer
elektroid_upload_task_runner (gpointer data)
{
  gint res;
  struct idata idata;
  gchar *dst_dir, *upload_path;

  debug_print (1, "Local path: %s", tasks.transfer.src);
  debug_print (1, "Remote path: %s", tasks.transfer.dst);

  if (remote_browser.fs_ops->mkdir
      && remote_browser.fs_ops->mkdir (remote_browser.backend,
				       tasks.transfer.dst))
    {
      error_print ("Error while creating remote %s dir", tasks.transfer.dst);
      tasks.transfer.status = TASK_STATUS_COMPLETED_ERROR;
      return NULL;
    }

  res = tasks.transfer.fs_ops->load (tasks.transfer.src, &idata,
				     &tasks.transfer.control);
  if (res)
    {
      error_print ("Error while loading file");
      tasks.transfer.status = TASK_STATUS_COMPLETED_ERROR;
      goto end;
    }

  debug_print (1, "Writing from file %s (filesystem %s)...",
	       tasks.transfer.src, tasks.transfer.fs_ops->name);

  upload_path = remote_browser.fs_ops->get_upload_path (&backend,
							remote_browser.fs_ops,
							tasks.transfer.dst,
							tasks.transfer.src,
							&idata);
  g_mutex_lock (&tasks.transfer.control.mutex);
  elektroid_check_file_and_wait (upload_path, &remote_browser);
  g_mutex_unlock (&tasks.transfer.control.mutex);
  if (tasks.transfer.status == TASK_STATUS_CANCELED)
    {
      goto cleanup;
    }

  res = tasks.transfer.fs_ops->upload (remote_browser.backend,
				       upload_path, &idata,
				       &tasks.transfer.control);

  if (res && tasks.transfer.control.active)
    {
      error_print ("Error while uploading");
      tasks.transfer.status = TASK_STATUS_COMPLETED_ERROR;
    }
  else
    {
      tasks.transfer.status =
	job_control_get_active_lock (&tasks.transfer.control) ?
	TASK_STATUS_COMPLETED_OK : TASK_STATUS_CANCELED;
    }

  dst_dir = g_path_get_dirname (upload_path);
  if (!res && tasks.transfer.fs_ops == remote_browser.fs_ops &&
      !strncmp (dst_dir, remote_browser.dir,
		strlen (remote_browser.dir))
      && !(tasks.transfer.fs_ops->options & FS_OPTION_SINGLE_OP))
    {
      g_idle_add (browser_load_dir_if_needed, &remote_browser);
    }

  g_free (upload_path);
  g_free (dst_dir);

cleanup:
  idata_free (&idata);
end:
  g_idle_add (tasks_complete_current, &tasks);
  g_idle_add (elektroid_run_next, NULL);
  return NULL;
}

static void
elektroid_add_upload_task_path (const gchar *rel_path,
				const gchar *src_dir, const gchar *dst_dir)
{
  struct item_iterator iter;
  gchar *path, *src_abs_path, *rel_path_trans;
  enum path_type type = backend_get_path_type (&backend);

  if (!progress_is_active ())
    {
      return;
    }

  rel_path_trans = path_translate (PATH_SYSTEM, rel_path);
  src_abs_path = path_chain (PATH_SYSTEM, src_dir, rel_path_trans);
  g_free (rel_path_trans);

  //Check if the item is a dir. If error, it's not.
  if (local_browser.fs_ops->readdir (NULL, &iter, src_abs_path, NULL))
    {
      rel_path_trans = path_translate (type, rel_path);
      gchar *dst_abs_path = path_chain (type, dst_dir, rel_path_trans);
      g_free (rel_path_trans);

      gchar *dst_abs_dir = g_path_get_dirname (dst_abs_path);
      tasks_add (&tasks, TASK_TYPE_UPLOAD, src_abs_path, dst_abs_dir,
		 remote_browser.fs_ops->id, &backend);
      g_free (dst_abs_path);
      goto cleanup;
    }

  if (!remote_browser.fs_ops->mkdir)
    {				//No recursive case.
      goto cleanup_iter;
    }

  while (!item_iterator_next (&iter))
    {
      path = path_chain (PATH_INTERNAL, rel_path, iter.item.name);
      elektroid_add_upload_task_path (path, src_dir, dst_dir);
      g_free (path);
    }

cleanup_iter:
  item_iterator_free (&iter);
cleanup:
  g_free (src_abs_path);
}

static gpointer
elektroid_add_upload_tasks_runner (gpointer userdata)
{
  GtkTreeIter iter;
  GList *selected_rows;
  gboolean queued_before, queued_after;
  GtkTreeModel *model = gtk_tree_view_get_model (local_browser.view);
  GtkTreeSelection *sel = gtk_tree_view_get_selection (local_browser.view);

  progress.sysex_transfer.active = TRUE;

  queued_before = tasks_get_next_queued (&tasks, &iter, NULL, NULL, NULL,
					 NULL, NULL, NULL);

  selected_rows = gtk_tree_selection_get_selected_rows (sel, NULL);
  while (selected_rows)
    {
      struct item item;
      GtkTreeIter path_iter;
      GtkTreePath *path = selected_rows->data;

      gtk_tree_model_get_iter (model, &path_iter, path);
      browser_set_item (model, &path_iter, &item);
      elektroid_add_upload_task_path (item.name, local_browser.dir,
				      remote_browser.dir);

      if (!progress_is_active ())
	{
	  break;
	}

      selected_rows = g_list_next (selected_rows);
    }
  g_list_free_full (selected_rows, (GDestroyNotify) gtk_tree_path_free);

  queued_after = tasks_get_next_queued (&tasks, &iter, NULL, NULL, NULL,
					NULL, NULL, NULL);
  if (!queued_before && queued_after)
    {
      g_idle_add (elektroid_run_next, NULL);
    }

  progress_response (GTK_RESPONSE_ACCEPT);
  return NULL;
}

static void
elektroid_add_upload_tasks (GtkWidget *object, gpointer data)
{
  GtkTreeSelection *sel = gtk_tree_view_get_selection (local_browser.view);

  if (!gtk_tree_selection_count_selected_rows (sel))
    {
      return;
    }

  progress_run (elektroid_add_upload_tasks_runner, PROGRESS_TYPE_PULSE, NULL,
		_("Preparing Tasks"), _("Waiting..."), TRUE, NULL);
}

static gpointer
elektroid_download_task_runner (gpointer userdata)
{
  gint res;
  struct idata idata;
  gchar *dst_path;

  debug_print (1, "Remote path: %s", tasks.transfer.src);
  debug_print (1, "Local dir: %s", tasks.transfer.dst);

  if (local_browser.fs_ops->mkdir (local_browser.backend, tasks.transfer.dst))
    {
      error_print ("Error while creating local %s dir", tasks.transfer.dst);
      tasks.transfer.status = TASK_STATUS_COMPLETED_ERROR;
      goto end_no_dir;
    }

  res = tasks.transfer.fs_ops->download (remote_browser.backend,
					 tasks.transfer.src, &idata,
					 &tasks.transfer.control);

  g_mutex_lock (&tasks.transfer.control.mutex);
  if (res)
    {
      if (tasks.transfer.control.active)
	{
	  error_print ("Error while downloading");
	  tasks.transfer.status = TASK_STATUS_COMPLETED_ERROR;
	}
      else
	{
	  tasks.transfer.status = TASK_STATUS_CANCELED;
	}
      goto end_with_download_error;
    }

  dst_path = remote_browser.fs_ops->get_download_path (&backend,
						       remote_browser.fs_ops,
						       tasks.transfer.dst,
						       tasks.transfer.src,
						       &idata);
  elektroid_check_file_and_wait (dst_path, &local_browser);

  if (tasks.transfer.status != TASK_STATUS_CANCELED)
    {
      debug_print (1, "Writing %d bytes to file %s (filesystem %s)...",
		   idata.content->len, dst_path, tasks.transfer.fs_ops->name);
      res = tasks.transfer.fs_ops->save (dst_path, &idata,
					 &tasks.transfer.control);
      if (!res)
	{
	  tasks.transfer.status = TASK_STATUS_COMPLETED_OK;
	  g_idle_add (browser_load_dir_if_needed, &local_browser);
	}
    }
  g_free (dst_path);

  idata_free (&idata);

end_with_download_error:
  g_mutex_unlock (&tasks.transfer.control.mutex);

  g_idle_add (tasks_complete_current, &tasks);
  g_idle_add (elektroid_run_next, NULL);

end_no_dir:
  return NULL;
}

static void
elektroid_add_download_task_path (const gchar *rel_path,
				  const gchar *src_dir, const gchar *dst_dir)
{
  struct item_iterator iter;
  gchar *path, *filename, *src_abs_path, *rel_path_trans;
  enum path_type type = backend_get_path_type (&backend);

  if (!progress_is_active ())
    {
      return;
    }

  rel_path_trans = path_translate (type, rel_path);
  src_abs_path = path_chain (type, src_dir, rel_path_trans);
  g_free (rel_path_trans);

  //Check if the item is a dir. If error, it's not.
  if (remote_browser.
      fs_ops->readdir (remote_browser.backend, &iter, src_abs_path, NULL))
    {
      rel_path_trans = path_translate (PATH_SYSTEM, rel_path);
      gchar *dst_abs_path = path_chain (PATH_SYSTEM, dst_dir, rel_path_trans);
      g_free (rel_path_trans);

      gchar *dst_abs_dir = g_path_get_dirname (dst_abs_path);
      tasks_add (&tasks, TASK_TYPE_DOWNLOAD, src_abs_path, dst_abs_dir,
		 remote_browser.fs_ops->id, &backend);
      g_free (dst_abs_dir);
      g_free (dst_abs_path);
      goto cleanup;
    }

  while (!item_iterator_next (&iter))
    {
      filename = item_get_filename (&iter.item,
				    remote_browser.fs_ops->options);
      path = path_chain (PATH_INTERNAL, rel_path, filename);
      elektroid_add_download_task_path (path, src_dir, dst_dir);
      debug_print (1, "name: %s", filename);
      g_free (path);
      g_free (filename);
      debug_print (1, "next");
    }

  item_iterator_free (&iter);
cleanup:
  g_free (src_abs_path);
}

static gpointer
elektroid_add_download_tasks_runner (gpointer data)
{
  GtkTreeIter iter;
  GList *selected_rows;
  gboolean queued_before, queued_after;
  GtkTreeModel *model = gtk_tree_view_get_model (remote_browser.view);
  GtkTreeSelection *sel = gtk_tree_view_get_selection (remote_browser.view);

  progress.sysex_transfer.active = TRUE;

  queued_before = tasks_get_next_queued (&tasks, &iter, NULL, NULL, NULL,
					 NULL, NULL, NULL);

  selected_rows = gtk_tree_selection_get_selected_rows (sel, NULL);
  while (selected_rows)
    {
      gchar *filename;
      struct item item;
      GtkTreeIter path_iter;
      GtkTreePath *path = selected_rows->data;

      gtk_tree_model_get_iter (model, &path_iter, path);
      browser_set_item (model, &path_iter, &item);
      filename = item_get_filename (&item, remote_browser.fs_ops->options);
      elektroid_add_download_task_path (filename, remote_browser.dir,
					local_browser.dir);
      g_free (filename);

      if (!progress_is_active ())
	{
	  break;
	}

      selected_rows = g_list_next (selected_rows);
    }
  g_list_free_full (selected_rows, (GDestroyNotify) gtk_tree_path_free);

  queued_after = tasks_get_next_queued (&tasks, &iter, NULL, NULL, NULL,
					NULL, NULL, NULL);
  if (!queued_before && queued_after)
    {
      g_idle_add (elektroid_run_next, NULL);
    }

  progress_response (GTK_RESPONSE_ACCEPT);
  return NULL;
}

static void
elektroid_add_download_tasks (GtkWidget *object, gpointer data)
{
  GtkTreeSelection *sel = gtk_tree_view_get_selection (remote_browser.view);

  if (!gtk_tree_selection_count_selected_rows (sel))
    {
      return;
    }

  progress_run (elektroid_add_download_tasks_runner, PROGRESS_TYPE_PULSE,
		NULL, _("Preparing Tasks"), _("Waiting..."), TRUE, NULL);
}

static void
elektroid_update_progress (struct job_control *control)
{
  g_idle_add (tasks_update_current_progress, &tasks);
}

static gboolean
elektroid_common_key_press (GtkWidget *widget, GdkEventKey *event,
			    gpointer data)
{
  gint count;
  GtkAllocation allocation;
  GdkWindow *gdk_window;
  struct browser *browser = data;
  struct sample_info *sample_info = editor.audio.sample.info;

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
  else if (event->keyval == GDK_KEY_space && sample_info->frames)
    {
      editor_play_clicked (NULL, &editor);
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
elektroid_remote_key_press (GtkWidget *widget, GdkEventKey *event,
			    gpointer data)
{
  if (event->type != GDK_KEY_PRESS)
    {
      return FALSE;
    }

  if (!(event->state & GDK_CONTROL_MASK) || event->keyval != GDK_KEY_Left)
    {
      return elektroid_common_key_press (widget, event, data);
    }

  if (!remote_browser.fs_ops->download)
    {
      return FALSE;
    }

  elektroid_add_download_tasks (NULL, NULL);
  return TRUE;
}

static gboolean
elektroid_local_key_press (GtkWidget *widget, GdkEventKey *event,
			   gpointer data)
{
  if (event->type != GDK_KEY_PRESS)
    {
      return FALSE;
    }

  if (!(event->state & GDK_CONTROL_MASK) || event->keyval != GDK_KEY_Right)
    {
      return elektroid_common_key_press (widget, event, data);
    }

  if (remote_browser.fs_ops->options & FS_OPTION_SLOT_STORAGE)
    {
      //Slot mode needs a slot destination.
      return FALSE;
    }

  if (!remote_browser.fs_ops->upload)
    {
      return FALSE;
    }

  elektroid_add_upload_tasks (NULL, NULL);
  return TRUE;
}

static void
elektroid_set_fs (GtkWidget *object, gpointer data)
{
  GtkTreeIter iter;
  GValue fsv = G_VALUE_INIT;
  gint fs;
  gboolean editor_visible;

  if (!gtk_combo_box_get_active_iter (GTK_COMBO_BOX (fs_combo), &iter))
    {
      local_browser.fs_ops = &FS_LOCAL_SAMPLE_OPERATIONS;
      browser_update_fs_options (&local_browser);
      browser_load_dir (&local_browser);

      browser_reset (&remote_browser);
      browser_update_fs_options (&remote_browser);

      gtk_widget_set_visible (editor.box, TRUE);
      editor_set_audio_mono_mix (&editor);

      return;
    }

  gtk_tree_model_get_value (GTK_TREE_MODEL (fs_list_store),
			    &iter, FS_LIST_STORE_ID_FIELD, &fsv);
  fs = g_value_get_uint (&fsv);
  g_value_unset (&fsv);

  remote_browser.fs_ops = backend_get_fs_operations_by_id (&backend, fs);
  editor_visible = remote_browser.fs_ops->options & FS_OPTION_SAMPLE_EDITOR ?
    TRUE : FALSE;

  if (editor_visible)
    {
      local_browser.fs_ops = &FS_LOCAL_SAMPLE_OPERATIONS;
    }
  else
    {
      local_browser.fs_ops = &FS_LOCAL_GENERIC_OPERATIONS;
      editor_reset (&editor, NULL);
    }

  editor_set_audio_mono_mix (&editor);

  if (backend.type == BE_TYPE_SYSTEM)
    {
      if (!remote_browser.dir)
	{
	  gchar *dir = strdup (preferences_get_string (PREF_KEY_REMOTE_DIR));
	  remote_browser.dir = dir;
	}
    }
  else
    {
      if (remote_browser.dir)
	{
	  g_free (remote_browser.dir);
	}
      remote_browser.dir = strdup ("/");
    }

  gtk_widget_set_visible (editor.box, editor_visible);

  gtk_drag_source_unset ((GtkWidget *) remote_browser.view);
  gtk_drag_dest_unset ((GtkWidget *) remote_browser.view);

  if (remote_browser.fs_ops->upload)
    {
      if (backend.type == BE_TYPE_SYSTEM)
	{
	  gtk_drag_dest_set ((GtkWidget *) remote_browser.view,
			     GTK_DEST_DEFAULT_ALL,
			     TARGET_ENTRIES_REMOTE_SYSTEM_DST,
			     G_N_ELEMENTS
			     (TARGET_ENTRIES_REMOTE_SYSTEM_DST),
			     GDK_ACTION_COPY | GDK_ACTION_MOVE);
	}
      else
	{
	  if (remote_browser.fs_ops->options & FS_OPTION_SLOT_STORAGE)
	    {
	      gtk_drag_dest_set ((GtkWidget *) remote_browser.view,
				 GTK_DEST_DEFAULT_ALL,
				 TARGET_ENTRIES_REMOTE_MIDI_DST_SLOT,
				 G_N_ELEMENTS
				 (TARGET_ENTRIES_REMOTE_MIDI_DST_SLOT),
				 GDK_ACTION_COPY);
	    }
	  else
	    {
	      gtk_drag_dest_set ((GtkWidget *) remote_browser.view,
				 GTK_DEST_DEFAULT_ALL,
				 TARGET_ENTRIES_REMOTE_MIDI_DST,
				 G_N_ELEMENTS
				 (TARGET_ENTRIES_REMOTE_MIDI_DST),
				 GDK_ACTION_COPY);
	    }
	}
    }

  if (remote_browser.fs_ops->download)
    {
      if (backend.type == BE_TYPE_SYSTEM)
	{
	  gtk_drag_source_set ((GtkWidget *) remote_browser.view,
			       GDK_BUTTON1_MASK,
			       TARGET_ENTRIES_REMOTE_SYSTEM_SRC,
			       G_N_ELEMENTS
			       (TARGET_ENTRIES_REMOTE_SYSTEM_SRC),
			       GDK_ACTION_COPY | GDK_ACTION_MOVE);
	}
      else
	{
	  if (remote_browser.fs_ops->options & FS_OPTION_SLOT_STORAGE)
	    {
	      gtk_drag_source_set ((GtkWidget *) remote_browser.view,
				   GDK_BUTTON1_MASK,
				   TARGET_ENTRIES_REMOTE_MIDI_SRC,
				   G_N_ELEMENTS
				   (TARGET_ENTRIES_REMOTE_MIDI_SRC),
				   GDK_ACTION_COPY);
	    }
	  else
	    {
	      gtk_drag_source_set ((GtkWidget *) remote_browser.view,
				   GDK_BUTTON1_MASK,
				   TARGET_ENTRIES_REMOTE_MIDI_SRC,
				   G_N_ELEMENTS
				   (TARGET_ENTRIES_REMOTE_MIDI_SRC),
				   GDK_ACTION_COPY);
	    }
	}
    }

  browser_close_search (NULL, &local_browser);	//This triggers a refresh
  browser_update_fs_options (&local_browser);

  browser_close_search (NULL, &remote_browser);	//This triggers a refresh
  browser_update_fs_options (&remote_browser);
}

static gboolean
elektroid_fill_fs_combo_bg (gpointer data)
{
  const struct fs_operations *fs_ops;
  GSList *e;
  gboolean any = FALSE;

  g_signal_handlers_block_by_func (fs_combo, G_CALLBACK (elektroid_set_fs),
				   NULL);

  gtk_list_store_clear (fs_list_store);

  e = backend.fs_ops;
  while (e)
    {
      fs_ops = e->data;
      if (fs_ops->gui_name)
	{
	  any = TRUE;
	  gtk_list_store_insert_with_values (fs_list_store, NULL, -1,
					     FS_LIST_STORE_ID_FIELD,
					     fs_ops->id,
					     FS_LIST_STORE_ICON_FIELD,
					     fs_ops->gui_icon,
					     FS_LIST_STORE_NAME_FIELD,
					     fs_ops->gui_name, -1);
	}
      e = e->next;
    }

  g_signal_handlers_unblock_by_func (fs_combo, G_CALLBACK (elektroid_set_fs),
				     NULL);

  if (any)
    {
      debug_print (1, "Selecting first filesystem...");
      gtk_combo_box_set_active (GTK_COMBO_BOX (fs_combo), 0);
    }

  return FALSE;
}

static gpointer
elektroid_set_device_runner (gpointer data)
{
  struct backend_device *be_sys_device = data;

  progress.sysex_transfer.active = TRUE;

  progress.sysex_transfer.err = backend_init_connector (&backend,
							be_sys_device, NULL,
							&progress.sysex_transfer);
  elektroid_update_midi_status ();
  progress_response (backend_check (&backend) ? GTK_RESPONSE_ACCEPT
		     : GTK_RESPONSE_CANCEL);
  return NULL;
}

static void
elektroid_set_device (GtkWidget *object, gpointer data)
{
  GtkTreeIter iter;
  gchar *id, *name;
  gint dres, err;
  struct backend_device be_sys_device;

  elektroid_cancel_all_tasks_and_wait ();

  if (!gtk_combo_box_get_active_iter (GTK_COMBO_BOX (devices_combo), &iter))
    {
      return;
    }

  elektroid_set_preferences_remote_dir ();

  if (backend_check (&backend))
    {
      backend_destroy (&backend);
    }

  gtk_tree_model_get (GTK_TREE_MODEL (devices_list_store), &iter,
		      DEVICES_LIST_STORE_TYPE_FIELD, &be_sys_device.type,
		      DEVICES_LIST_STORE_ID_FIELD, &id,
		      DEVICES_LIST_STORE_NAME_FIELD, &name, -1);

  strcpy (be_sys_device.id, id);
  strcpy (be_sys_device.name, name);
  g_free (id);
  g_free (name);

  maction_menu_clear (&maction_context);

  if (be_sys_device.type == BE_TYPE_SYSTEM)
    {
      backend_init_connector (&backend, &be_sys_device, NULL, NULL);
      elektroid_update_midi_status ();
      err = 0;
    }
  else
    {
      progress_run (elektroid_set_device_runner, PROGRESS_TYPE_PULSE,
		    &be_sys_device, _("Connecting to Device"),
		    _("Connecting..."), TRUE, &dres);

      if (progress.sysex_transfer.err &&
	  progress.sysex_transfer.err != -ECANCELED)
	{
	  error_print ("Error while connecting: %s",
		       g_strerror (-progress.sysex_transfer.err));
	  show_error_msg (_("Device “%s” not recognized: %s"),
			  be_sys_device.name,
			  g_strerror (-progress.sysex_transfer.err));
	}

      elektroid_check_backend (FALSE);
      err = dres == GTK_RESPONSE_ACCEPT ? 0 : 1;
    }

  if (err)
    {
      gtk_combo_box_set_active (GTK_COMBO_BOX (devices_combo), -1);
    }
  else
    {
      elektroid_fill_fs_combo_bg (NULL);
      maction_menu_setup (&maction_context);
    }
}

static void
elektroid_dnd_received_browser (const gchar *dir, const gchar *name,
				const gchar *filename,
				struct browser *browser)
{
  gchar *dst_path;
  gint res;
  enum path_type type = backend_get_path_type (browser->backend);

  if (strcmp (dir, browser->dir))
    {
      dst_path = path_chain (type, browser->dir, name);
      res = browser->fs_ops->move (browser->backend, filename, dst_path);
      if (res)
	{
	  error_print ("Error while moving from “%s” to “%s”: %s.",
		       filename, dst_path, g_strerror (-res));
	}
      g_free (dst_path);
      g_idle_add (browser_load_dir_if_needed, browser);
    }
  else
    {
      debug_print (1, MSG_WARN_SAME_SRC_DST);
    }
}

static void
elektroid_add_upload_task_slot (const gchar *name,
				const gchar *src_file_path, gint slot)
{
  GtkTreeIter iter;
  struct item item;
  gchar *dst_file_path, *filename;
  GString *str;
  GtkTreeModel *model = gtk_tree_view_get_model (remote_browser.view);

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

      filename = item_get_filename (&item, remote_browser.fs_ops->options);
      str = g_string_new (NULL);
      g_string_append_printf (str, "%s%s%s", remote_browser.dir,
			      strcmp (remote_browser.dir, "/") ?
			      "/" : "", filename);
      g_free (filename);
      dst_file_path = g_string_free (str, FALSE);

      tasks_add (&tasks, TASK_TYPE_UPLOAD, src_file_path, dst_file_path,
		 remote_browser.fs_ops->id, &backend);
    }
}

static gpointer
elektroid_dnd_received_runner_dialog (gpointer data)
{
  GtkTreeIter iter;
  gboolean queued_before, queued_after;
  struct elektroid_dnd_data *dnd_data = data;
  GtkWidget *widget = dnd_data->widget;

  progress.sysex_transfer.active = TRUE;

  queued_before = tasks_get_next_queued (&tasks, &iter, NULL, NULL, NULL,
					 NULL, NULL, NULL);

  for (gint i = 0; dnd_data->uris[i] != NULL; i++)
    {
      if (!progress_is_active ())
	{
	  goto end;
	}

      enum path_type type = PATH_TYPE_FROM_DND_TYPE (dnd_data->type_name);
      gchar *src_path = path_filename_from_uri (type, dnd_data->uris[i]);
      gchar *name = g_path_get_basename (src_path);
      gchar *dir = g_path_get_dirname (src_path);

      if (widget == GTK_WIDGET (local_browser.view))
	{
	  if (!strcmp (dnd_data->type_name, TEXT_URI_LIST_STD))
	    {
	      elektroid_dnd_received_browser (dir, name, src_path,
					      &local_browser);
	    }
	  else if (!strcmp (dnd_data->type_name, TEXT_URI_LIST_ELEKTROID))
	    {
	      elektroid_add_download_task_path (name, dir, local_browser.dir);
	    }
	}
      else if (widget == GTK_WIDGET (remote_browser.view))
	{
	  if (!strcmp (dnd_data->type_name, TEXT_URI_LIST_ELEKTROID))
	    {
	      elektroid_dnd_received_browser (dir, name, src_path,
					      &remote_browser);
	    }
	  else if (!strcmp (dnd_data->type_name, TEXT_URI_LIST_STD))
	    {
	      if (remote_browser.fs_ops->options & FS_OPTION_SLOT_STORAGE)
		{
		  elektroid_add_upload_task_slot (name, src_path, i);
		}
	      else
		{
		  elektroid_add_upload_task_path (name, dir,
						  remote_browser.dir);
		}
	    }
	}

      g_free (name);
      g_free (dir);
      g_free (src_path);
    }

end:
  queued_after = tasks_get_next_queued (&tasks, &iter, NULL, NULL, NULL,
					NULL, NULL, NULL);
  if (!queued_before && queued_after)
    {
      g_idle_add (elektroid_run_next, NULL);
    }

  progress_response (GTK_RESPONSE_ACCEPT);

  g_free (dnd_data->type_name);
  g_strfreev (dnd_data->uris);
  g_free (dnd_data);

  return NULL;
}

static void
elektroid_dnd_received (GtkWidget *widget, GdkDragContext *context,
			gint x, gint y,
			GtkSelectionData *selection_data,
			guint info, guint time, gpointer userdata)
{
  gchar *data;
  GdkAtom type;
  const gchar *title, *text;
  gboolean blocking = TRUE;
  gchar *filename, *src_dir, *dst_dir = NULL;
  struct elektroid_dnd_data *dnd_data;

  if (!gtk_selection_data_get_length (selection_data))
    {
      gtk_drag_finish (context, TRUE, TRUE, time);
      error_print ("DND invalid data");
      return;
    }

  if (widget == GTK_WIDGET (remote_browser.view) &&
      (remote_browser.fs_ops->options & FS_OPTION_SLOT_STORAGE) &&
      remote_browser.dnd_motion_path == NULL)
    {
      gtk_drag_finish (context, TRUE, TRUE, time);
      error_print ("DND destination needs a slot");
      return;
    }

  dnd_data = g_malloc (sizeof (struct elektroid_dnd_data));
  dnd_data->widget = widget;

  type = gtk_selection_data_get_data_type (selection_data);
  dnd_data->type_name = gdk_atom_name (type);

  data = (gchar *) gtk_selection_data_get_data (selection_data);
  debug_print (1, "DND received batch %d data (%s):\n%s", batch_id,
	       dnd_data->type_name, data);

  dnd_data->uris = g_uri_list_extract_uris (data);

  gtk_drag_finish (context, TRUE, TRUE, time);

  enum path_type path_type = PATH_TYPE_FROM_DND_TYPE (dnd_data->type_name);
  filename = path_filename_from_uri (path_type, dnd_data->uris[0]);
  src_dir = g_path_get_dirname (filename);

  //Checking if it's a local move.
  if (widget == GTK_WIDGET (local_browser.view) &&
      !strcmp (dnd_data->type_name, TEXT_URI_LIST_STD))
    {
      dst_dir = local_browser.dir;	//Move
    }

  //Checking if it's a remote move.
  if (widget == GTK_WIDGET (remote_browser.view)
      && !strcmp (dnd_data->type_name, TEXT_URI_LIST_ELEKTROID))
    {
      dst_dir = remote_browser.dir;	//Move
    }

  if (dst_dir)
    {
      // If we are moving a file (source and destination is the same browser) and the
      // basedir of the first URI (every URI will share the same basename), equals
      // the browser directory, there's nothing to do.
      if (!strcmp (src_dir, dst_dir))
	{
	  debug_print (1, MSG_WARN_SAME_SRC_DST);
	  goto end;
	}

      title = _("Moving Files");
      text = _("Moving...");

      if (!strcmp (dnd_data->type_name, TEXT_URI_LIST_STD) ||
	  (!strcmp (dnd_data->type_name, TEXT_URI_LIST_ELEKTROID) &&
	   backend.type == BE_TYPE_SYSTEM))
	{
	  //Moving inside the local browser takes no time.
	  blocking = FALSE;
	}
    }
  else
    {
      title = _("Preparing Tasks");
      text = _("Waiting...");
    }

  if (blocking)
    {
      progress_run (elektroid_dnd_received_runner_dialog, PROGRESS_TYPE_PULSE,
		    dnd_data, title, text, TRUE, NULL);
      batch_id++;
    }
  else
    {
      elektroid_dnd_received_runner_dialog (dnd_data);
    }

end:
  g_free (filename);
  g_free (src_dir);
}

static void
elektroid_dnd_get (GtkWidget *widget,
		   GdkDragContext *context,
		   GtkSelectionData *selection_data,
		   guint info, guint time, gpointer user_data)
{
  struct browser *browser = user_data;
  debug_print (1, "Creating DND data...");
  gtk_selection_data_set (selection_data,
			  gtk_selection_data_get_target
			  (selection_data), 8,
			  (guchar *) browser->dnd_data->str,
			  browser->dnd_data->len);
}

static gboolean
elektroid_drag_list_timeout (gpointer user_data)
{
  struct browser *browser = user_data;
  gchar *spath;

  spath = gtk_tree_path_to_string (browser->dnd_motion_path);
  debug_print (2, "Getting into path: %s...", spath);
  g_free (spath);

  browser_item_activated (browser->view, browser->dnd_motion_path, NULL,
			  browser);

  gtk_tree_path_free (browser->dnd_motion_path);
  browser_clear_dnd_function (browser);
  browser->dnd_motion_path = NULL;
  return FALSE;
}

static gboolean
elektroid_drag_scroll_up_timeout (gpointer user_data)
{
  GtkTreePath *start;
  struct browser *browser = user_data;
  debug_print (2, "Scrolling up...");
  gtk_tree_view_get_visible_range (browser->view, &start, NULL);
  for (guint i = 0; i < TREEVIEW_SCROLL_LINES; i++)
    {
      gtk_tree_path_prev (start);
    }
  gtk_tree_view_scroll_to_cell (browser->view, start, NULL, FALSE, .0, .0);
  gtk_tree_path_free (start);
  browser_set_dnd_function (browser, elektroid_drag_scroll_up_timeout);
  return TRUE;
}

static gboolean
elektroid_drag_scroll_down_timeout (gpointer user_data)
{
  GtkTreePath *end;
  struct browser *browser = user_data;
  debug_print (2, "Scrolling down...");
  gtk_tree_view_get_visible_range (browser->view, NULL, &end);
  for (guint i = 0; i < TREEVIEW_SCROLL_LINES; i++)
    {
      gtk_tree_path_next (end);
    }
  gtk_tree_view_scroll_to_cell (browser->view, end, NULL, FALSE, .0, .0);
  gtk_tree_path_free (end);
  browser_set_dnd_function (browser, elektroid_drag_scroll_down_timeout);
  return TRUE;
}

static gboolean
elektroid_drag_motion_list (GtkWidget *widget,
			    GdkDragContext *context,
			    gint wx, gint wy, guint time, gpointer user_data)
{
  GtkTreePath *path;
  GtkTreeModel *model;
  GtkTreeIter iter;
  gchar *spath;
  gint tx, ty;
  gboolean slot;
  GtkTreeSelection *selection;
  struct item item;
  struct browser *browser = user_data;

  slot = GTK_TREE_VIEW (widget) == remote_browser.view &&
    remote_browser.fs_ops->options & FS_OPTION_SLOT_STORAGE;

  gtk_tree_view_convert_widget_to_bin_window_coords (browser->view, wx, wy,
						     &tx, &ty);

  if (gtk_tree_view_get_path_at_pos (browser->view, tx, ty, &path, NULL,
				     NULL, NULL))
    {
      GtkAllocation allocation;
      gtk_widget_get_allocation (widget, &allocation);

      spath = gtk_tree_path_to_string (path);
      debug_print (2, "Drag motion path: %s", spath);
      g_free (spath);

      if (slot)
	{
	  gtk_tree_view_set_drag_dest_row (browser->view, path,
					   GTK_TREE_VIEW_DROP_INTO_OR_BEFORE);
	}
      else
	{
	  selection = gtk_tree_view_get_selection (browser->view);
	  if (gtk_tree_selection_path_is_selected (selection, path))
	    {
	      browser_clear_dnd_function (browser);
	      return TRUE;
	    }
	}

      model = gtk_tree_view_get_model (browser->view);
      gtk_tree_model_get_iter (model, &iter, path);
      browser_set_item (model, &iter, &item);

      if (item.type == ITEM_TYPE_DIR && (!browser->dnd_motion_path ||
					 (browser->dnd_motion_path &&
					  gtk_tree_path_compare
					  (browser->dnd_motion_path, path))))
	{
	  browser_set_dnd_function (browser, elektroid_drag_list_timeout);
	}

      if (ty < TREEVIEW_EDGE_SIZE)
	{
	  browser_set_dnd_function (browser,
				    elektroid_drag_scroll_up_timeout);
	}
      else if (wy > allocation.height - TREEVIEW_EDGE_SIZE)
	{
	  browser_set_dnd_function (browser,
				    elektroid_drag_scroll_down_timeout);
	}
    }
  else
    {
      browser_clear_dnd_function (browser);
      if (slot)
	{
	  gtk_tree_view_set_drag_dest_row (browser->view, NULL,
					   GTK_TREE_VIEW_DROP_INTO_OR_BEFORE);
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
elektroid_drag_leave_list (GtkWidget *widget,
			   GdkDragContext *context,
			   guint time, gpointer user_data)
{
  browser_clear_dnd_function (user_data);
}

static gboolean
elektroid_drag_up_timeout (gpointer user_data)
{
  struct browser *browser = user_data;
  browser_go_up (NULL, browser);
  return TRUE;
}

static gboolean
elektroid_drag_motion_up (GtkWidget *widget,
			  GdkDragContext *context,
			  gint wx, gint wy, guint time, gpointer user_data)
{
  struct browser *browser = user_data;
  browser_set_dnd_function (browser, elektroid_drag_up_timeout);
  return TRUE;
}

static void
elektroid_drag_leave_up (GtkWidget *widget,
			 GdkDragContext *context,
			 guint time, gpointer user_data)
{
  browser_clear_dnd_function (user_data);
}

static void
elektroid_set_window_size ()
{
  GdkRectangle geometry;
  GdkDisplay *display = gdk_display_get_default ();
  GdkMonitor *monitor = gdk_display_get_monitor (display, 0);
  gdk_monitor_get_geometry (monitor, &geometry);
  if (geometry.height >= 800)
    {
      gtk_window_resize (main_window, 1024, 768);
    }
  else
    {
      gtk_window_maximize (main_window);
    }
}

static void
elektroid_exit ()
{
  gtk_dialog_response (GTK_DIALOG (about_dialog), GTK_RESPONSE_CANCEL);
  progress_response (GTK_RESPONSE_CANCEL);
  if (dialog)
    {
      gtk_dialog_response (GTK_DIALOG (dialog), GTK_RESPONSE_CANCEL);
    }

  progress_stop_thread ();
  tasks_stop_thread (&tasks);
  editor_stop_load_thread (&editor);

  browser_destroy (&local_browser);
  browser_destroy (&remote_browser);

  editor_destroy (&editor);

  if (backend_check (&backend))
    {
      backend_destroy (&backend);
    }

  gtk_widget_destroy (GTK_WIDGET (main_window));
  g_object_unref (builder);
}

static gboolean
elektroid_delete_window (GtkWidget *widget, GdkEvent *event, gpointer data)
{
  elektroid_exit ();
  return FALSE;
}

static void
build_ui ()
{
  GtkCssProvider *css_provider;
  GtkWidget *refresh_devices_button;
  gchar *thanks;

  builder = gtk_builder_new ();
  gtk_builder_add_from_file (builder, DATADIR "/gui.glade", NULL);

  css_provider = gtk_css_provider_new ();
  gtk_css_provider_load_from_path (css_provider, DATADIR "/gui.css", NULL);
  gtk_style_context_add_provider_for_screen (gdk_screen_get_default (),
					     GTK_STYLE_PROVIDER
					     (css_provider),
					     GTK_STYLE_PROVIDER_PRIORITY_USER);

  main_window = GTK_WINDOW (gtk_builder_get_object (builder, "main_window"));

  about_dialog =
    GTK_ABOUT_DIALOG (gtk_builder_get_object (builder, "about_dialog"));
  gtk_about_dialog_set_version (about_dialog, PACKAGE_VERSION);

  if (g_file_get_contents (DATADIR "/THANKS", &thanks, NULL, NULL))
    {
      gchar *last_new_line = strrchr (thanks, '\n');
      if (last_new_line != NULL)
	{
	  *last_new_line = 0;
	}
      gchar **lines = g_strsplit (thanks, "\n", 0);
      gtk_about_dialog_add_credit_section (about_dialog,
					   _("Acknowledgements"),
					   (const gchar **) lines);
      g_free (thanks);
      g_strfreev (lines);
    }

  name_dialog = GTK_DIALOG (gtk_builder_get_object (builder, "name_dialog"));
  name_dialog_accept_button =
    GTK_WIDGET (gtk_builder_get_object
		(builder, "name_dialog_accept_button"));
  name_dialog_entry =
    GTK_ENTRY (gtk_builder_get_object (builder, "name_dialog_entry"));

  preferences_dialog =
    GTK_DIALOG (gtk_builder_get_object (builder, "preferences_dialog"));
  audio_use_float_switch =
    GTK_WIDGET (gtk_builder_get_object (builder, "audio_use_float_switch"));
  play_sample_while_loading_switch =
    GTK_WIDGET (gtk_builder_get_object (builder,
					"play_sample_while_loading_switch"));
  audio_buffer_length_combo =
    GTK_WIDGET (gtk_builder_get_object (builder,
					"audio_buffer_length_combo"));
  stop_device_when_connecting_switch =
    GTK_WIDGET (gtk_builder_get_object (builder,
					"stop_device_when_connecting_switch"));
  elektron_load_sound_tags_switch =
    GTK_WIDGET (gtk_builder_get_object (builder,
					"elektron_load_sound_tags_switch"));

  maction_context.box =
    GTK_WIDGET (gtk_builder_get_object (builder, "menu_actions_box"));

  main_popover =
    GTK_POPOVER (gtk_builder_get_object (builder, "main_popover"));
  gtk_popover_set_constrain_to (main_popover, GTK_POPOVER_CONSTRAINT_NONE);
  show_remote_button =
    GTK_WIDGET (gtk_builder_get_object (builder, "show_remote_button"));
  g_object_set (G_OBJECT (show_remote_button), "role",
		GTK_BUTTON_ROLE_CHECK, NULL);
  preferences_button =
    GTK_WIDGET (gtk_builder_get_object (builder, "preferences_button"));
  about_button =
    GTK_WIDGET (gtk_builder_get_object (builder, "about_button"));

  local_name_entry =
    GTK_WIDGET (gtk_builder_get_object (builder, "local_name_entry"));
  remote_devices_box =
    GTK_WIDGET (gtk_builder_get_object (builder, "remote_devices_box"));
  local_box = GTK_WIDGET (gtk_builder_get_object (builder, "local_box"));
  remote_box = GTK_WIDGET (gtk_builder_get_object (builder, "remote_box"));
  local_side = GTK_WIDGET (gtk_builder_get_object (builder, "local_side"));
  remote_side = GTK_WIDGET (gtk_builder_get_object (builder, "remote_side"));
  tasks_box = GTK_WIDGET (gtk_builder_get_object (builder, "tasks_box"));
  backend_status_label =
    GTK_LABEL (gtk_builder_get_object (builder, "backend_status_label"));
  host_audio_status_label =
    GTK_LABEL (gtk_builder_get_object (builder, "host_audio_status_label"));
  host_midi_status_label =
    GTK_LABEL (gtk_builder_get_object (builder, "host_midi_status_label"));

  g_signal_connect (GTK_WIDGET (main_window), "delete-event",
		    G_CALLBACK (elektroid_delete_window), NULL);

  g_signal_connect (show_remote_button, "clicked",
		    G_CALLBACK (elektroid_show_remote_clicked), NULL);

  g_signal_connect (preferences_button, "clicked",
		    G_CALLBACK (elektroid_show_preferences), NULL);

  g_signal_connect (about_button, "clicked",
		    G_CALLBACK (elektroid_show_about), NULL);

  g_signal_connect (name_dialog_entry, "changed",
		    G_CALLBACK (elektroid_name_dialog_entry_changed), NULL);

  browser_remote_init (&remote_browser, builder, &backend);

  g_signal_connect (remote_browser.transfer_menuitem, "activate",
		    G_CALLBACK (elektroid_add_download_tasks), NULL);
  g_signal_connect (remote_browser.play_menuitem, "activate",
		    G_CALLBACK (editor_play_clicked), &editor);
  g_signal_connect (remote_browser.open_menuitem, "activate",
		    G_CALLBACK (elektroid_open_clicked), &remote_browser);
  g_signal_connect (remote_browser.show_menuitem, "activate",
		    G_CALLBACK (elektroid_show_clicked), &remote_browser);
  g_signal_connect (remote_browser.rename_menuitem, "activate",
		    G_CALLBACK (elektroid_rename_item), &remote_browser);
  g_signal_connect (remote_browser.delete_menuitem, "activate",
		    G_CALLBACK (elektroid_delete_files), &remote_browser);

  browser_local_init (&local_browser, builder,
		      strdup (preferences_get_string (PREF_KEY_LOCAL_DIR)));

  g_signal_connect (local_browser.transfer_menuitem, "activate",
		    G_CALLBACK (elektroid_add_upload_tasks), NULL);
  g_signal_connect (local_browser.play_menuitem, "activate",
		    G_CALLBACK (editor_play_clicked), &editor);
  g_signal_connect (local_browser.open_menuitem, "activate",
		    G_CALLBACK (elektroid_open_clicked), &local_browser);
  g_signal_connect (local_browser.show_menuitem, "activate",
		    G_CALLBACK (elektroid_show_clicked), &local_browser);
  g_signal_connect (local_browser.rename_menuitem, "activate",
		    G_CALLBACK (elektroid_rename_item), &local_browser);
  g_signal_connect (local_browser.delete_menuitem, "activate",
		    G_CALLBACK (elektroid_delete_files), &local_browser);

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
  g_signal_connect (remote_browser.search_button, "clicked",
		    G_CALLBACK (browser_open_search), &remote_browser);
  g_signal_connect (remote_browser.search_entry, "stop-search",
		    G_CALLBACK (browser_close_search), &remote_browser);
  g_signal_connect (remote_browser.search_entry, "search-changed",
		    G_CALLBACK (browser_search_changed), &remote_browser);
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
		    G_CALLBACK (elektroid_dnd_received), NULL);
  g_signal_connect (remote_browser.view, "drag-motion",
		    G_CALLBACK (elektroid_drag_motion_list), &remote_browser);
  g_signal_connect (remote_browser.view, "drag-leave",
		    G_CALLBACK (elektroid_drag_leave_list), &remote_browser);
  g_signal_connect (remote_browser.up_button, "drag-motion",
		    G_CALLBACK (elektroid_drag_motion_up), &remote_browser);
  g_signal_connect (remote_browser.up_button, "drag-leave",
		    G_CALLBACK (elektroid_drag_leave_up), &remote_browser);

  gtk_drag_dest_set ((GtkWidget *) remote_browser.up_button,
		     GTK_DEST_DEFAULT_MOTION | GTK_DEST_DEFAULT_HIGHLIGHT,
		     TARGET_ENTRIES_UP_BUTTON_DST,
		     G_N_ELEMENTS (TARGET_ENTRIES_UP_BUTTON_DST),
		     GDK_ACTION_COPY | GDK_ACTION_MOVE);

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
  g_signal_connect (local_browser.search_button, "clicked",
		    G_CALLBACK (browser_open_search), &local_browser);
  g_signal_connect (local_browser.search_entry, "stop-search",
		    G_CALLBACK (browser_close_search), &local_browser);
  g_signal_connect (local_browser.search_entry, "search-changed",
		    G_CALLBACK (browser_search_changed), &local_browser);
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
		    G_CALLBACK (elektroid_dnd_received), NULL);
  g_signal_connect (local_browser.view, "drag-motion",
		    G_CALLBACK (elektroid_drag_motion_list), &local_browser);
  g_signal_connect (local_browser.view, "drag-leave",
		    G_CALLBACK (elektroid_drag_leave_list), &local_browser);
  g_signal_connect (local_browser.up_button, "drag-motion",
		    G_CALLBACK (elektroid_drag_motion_up), &local_browser);
  g_signal_connect (local_browser.up_button, "drag-leave",
		    G_CALLBACK (elektroid_drag_leave_up), &local_browser);

  gtk_drag_source_set ((GtkWidget *) local_browser.view,
		       GDK_BUTTON1_MASK, TARGET_ENTRIES_LOCAL_SRC,
		       G_N_ELEMENTS (TARGET_ENTRIES_LOCAL_SRC),
		       GDK_ACTION_COPY | GDK_ACTION_MOVE);
  gtk_drag_dest_set ((GtkWidget *) local_browser.view,
		     GTK_DEST_DEFAULT_ALL, TARGET_ENTRIES_LOCAL_DST,
		     G_N_ELEMENTS (TARGET_ENTRIES_LOCAL_DST),
		     GDK_ACTION_COPY | GDK_ACTION_MOVE);
  gtk_drag_dest_set ((GtkWidget *) local_browser.up_button,
		     GTK_DEST_DEFAULT_MOTION | GTK_DEST_DEFAULT_HIGHLIGHT,
		     TARGET_ENTRIES_UP_BUTTON_DST,
		     G_N_ELEMENTS (TARGET_ENTRIES_UP_BUTTON_DST),
		     GDK_ACTION_COPY | GDK_ACTION_MOVE);

  devices_list_store =
    GTK_LIST_STORE (gtk_builder_get_object (builder, "devices_list_store"));
  devices_combo =
    GTK_WIDGET (gtk_builder_get_object (builder, "devices_combo"));
  refresh_devices_button =
    GTK_WIDGET (gtk_builder_get_object (builder, "refresh_devices_button"));
  g_signal_connect (devices_combo, "changed",
		    G_CALLBACK (elektroid_set_device), NULL);
  g_signal_connect (refresh_devices_button, "clicked",
		    G_CALLBACK (elektroid_refresh_devices_int), NULL);

  gtk_label_set_text (backend_status_label, _("Not connected"));

  fs_list_store =
    GTK_LIST_STORE (gtk_builder_get_object (builder, "fs_list_store"));
  fs_combo = GTK_WIDGET (gtk_builder_get_object (builder, "fs_combo"));
  g_signal_connect (fs_combo, "changed", G_CALLBACK (elektroid_set_fs), NULL);

  editor_init (&editor, builder);
  elektroid_update_midi_status ();
  tasks_init (&tasks, builder);
  progress_init (builder);

  g_object_set (G_OBJECT (show_remote_button), "active",
		preferences_get_boolean (PREF_KEY_SHOW_REMOTE), NULL);

  gtk_widget_set_sensitive (remote_box, FALSE);

  maction_context.backend = &backend;
  maction_context.audio = &editor.audio;
  maction_context.builder = builder;
  maction_context.parent = main_window;

  elektroid_show_remote (preferences_get_boolean (PREF_KEY_SHOW_REMOTE));	//This triggers both browsers initializations.

  gtk_entry_set_text (GTK_ENTRY (local_name_entry), hostname);

  elektroid_set_window_size ();
}

#if defined(__linux__)
static gboolean
elektroid_signal_handler (gpointer data)
{
  elektroid_exit ();
  return FALSE;
}
#endif

static void
elektroid_startup (GApplication *gapp, gpointer *user_data)
{
  if (local_dir)
    {
      gchar *abs_local_dir = g_canonicalize_filename (local_dir, NULL);
      g_free (local_dir);
      preferences_set_string (PREF_KEY_LOCAL_DIR,
			      get_system_startup_path (abs_local_dir));
      g_free (abs_local_dir);
    }

  build_ui ();
  gtk_application_add_window (GTK_APPLICATION (gapp),
			      GTK_WINDOW (main_window));
}

static void
elektroid_activate (GApplication *gapp, gpointer *user_data)
{
  gtk_window_present (GTK_WINDOW (main_window));
}

static gboolean
elektroid_increment_debug_level (const gchar *option_name,
				 const gchar *value,
				 gpointer data, GError **error)
{
  debug_level++;
  return TRUE;
}

const GOptionEntry CMD_PARAMS[] = {
  {
   .long_name = "verbosity",
   .short_name = 'v',
   .flags = G_OPTION_FLAG_NO_ARG,
   .arg = G_OPTION_ARG_CALLBACK,
   .arg_data = elektroid_increment_debug_level,
   .description =
   "Increase verbosity. For more verbosity use it more than once.",
   .arg_description = NULL,
   },
  {
   .long_name = "local-directory",
   .short_name = 'l',
   .flags = G_OPTION_FLAG_NONE,
   .arg = G_OPTION_ARG_FILENAME,
   .arg_data = &local_dir,
   .description = "Local directory at startup",
   .arg_description = "DIRECTORY",
   },
  {NULL}
};

int
main (int argc, char *argv[])
{
  gint err;
  GtkApplication *app;

#if defined(__linux__)
  g_unix_signal_add (SIGHUP, elektroid_signal_handler, NULL);
  g_unix_signal_add (SIGINT, elektroid_signal_handler, NULL);
  g_unix_signal_add (SIGTERM, elektroid_signal_handler, NULL);
#endif

  setlocale (LC_ALL, "");
  bindtextdomain (PACKAGE, LOCALEDIR);
  textdomain (PACKAGE);

  hostname = g_get_host_name ();

  regconn_register ();
  regma_register ();
  regpref_register ();

  preferences_load ();

  app = gtk_application_new ("io.github.dagargo.Elektroid",
			     G_APPLICATION_NON_UNIQUE);

  g_signal_connect (app, "startup", G_CALLBACK (elektroid_startup), NULL);
  g_signal_connect (app, "activate", G_CALLBACK (elektroid_activate), NULL);

  g_application_add_main_option_entries (G_APPLICATION (app), CMD_PARAMS);

  err = g_application_run (G_APPLICATION (app), argc, argv);

  g_object_unref (app);

  preferences_set_string (PREF_KEY_LOCAL_DIR, strdup (local_browser.dir));
  elektroid_set_preferences_remote_dir ();

  preferences_save ();
  preferences_free ();

  regconn_unregister ();
  regma_unregister ();
  regpref_unregister ();

  return err;
}
