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
#include "browser.h"
#include "editor.h"
#include "local.h"
#include "name_window.h"
#include "notifier.h"
#include "preferences.h"
#include "preferences_window.h"
#include "progress_window.h"
#include "regconn.h"
#include "regma.h"
#include "regpref.h"
#include "sample.h"
#include "tasks.h"

#define BACKEND_PLAYING "\u23f5"
#define BACKEND_STOPPED "\u23f9"

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

struct elektroid_set_device_data
{
  struct backend_device backend_device;
  struct controllable controllable;
  gint err;
};

static gpointer elektroid_upload_task_runner (gpointer);
static gpointer elektroid_download_task_runner (gpointer);
static void elektroid_update_progress (struct job_control *);

void autosampler_destroy ();
void autosampler_init (GtkBuilder * builder);
void microbrute_destroy ();
void microbrute_init ();

static gchar *local_dir;
static guint batch_id;

extern struct maction_context maction_context;

#define BACKEND (remote_browser.backend)

GtkWindow *main_window;
static GtkAboutDialog *about_dialog;
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
elektroid_show_error_msg_response (GtkDialog *dialog, gint response_id,
				   gpointer user_data)
{
  gtk_widget_destroy (GTK_WIDGET (dialog));
}

void
elektroid_show_error_msg (const char *format, ...)
{
  gchar *msg;
  va_list args;
  GtkWidget *dialog;

  va_start (args, format);
  g_vasprintf (&msg, format, args);
  dialog = gtk_message_dialog_new (main_window, GTK_DIALOG_MODAL,
				   GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE,
				   "%s", msg);
  gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_ACCEPT);
  g_signal_connect (dialog, "response",
		    G_CALLBACK (elektroid_show_error_msg_response), NULL);
  gtk_widget_set_visible (dialog, TRUE);

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

  if (editor_get_browser () == &remote_browser)
    {
      editor_reset (NULL);
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
		audio_name (), audio_version (), audio.rate / 1000.f,
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
  const gchar *v = BACKEND->type == BE_TYPE_MIDI ? BACKEND_PLAYING :
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

  if (backend_check (BACKEND))
    {
      statfss = g_string_new (NULL);
      if (BACKEND->get_storage_stats)
	{
	  for (guint i = 1; i < G_MAXUINT8; i <<= 1)
	    {
	      gint v = BACKEND->get_storage_stats (BACKEND, i, &statfs,
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

      if (strlen (BACKEND->name))
	{
	  snprintf (status, LABEL_MAX, "%s", BACKEND->name);
	  if (*BACKEND->version)
	    {
	      strncat (status, " ", LABEL_MAX - sizeof (status) - 2);
	      strncat (status, BACKEND->version,
		       LABEL_MAX - sizeof (status) -
		       strlen (BACKEND->version) - 1);
	    }
	  if (*BACKEND->description)
	    {
	      strncat (status, " (", LABEL_MAX - sizeof (status) - 3);
	      strncat (status, BACKEND->description,
		       LABEL_MAX - sizeof (status) -
		       strlen (BACKEND->description) - 1);
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

static gboolean
elektroid_check_backend_int (gboolean startup)
{
  gboolean connected = backend_check (BACKEND);

  gtk_widget_set_sensitive (remote_box, connected);

  if (!connected)
    {
      browser_reset (&remote_browser);
      elektroid_load_devices (startup);
    }

  elektroid_update_backend_status ();

  return connected;
}

gboolean
elektroid_check_backend ()
{
  return elektroid_check_backend_int (FALSE);
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
elektroid_refresh_devices_int (gboolean startup)
{
  if (backend_check (BACKEND))
    {
      elektroid_cancel_all_tasks_and_wait ();
      backend_destroy (BACKEND);
      maction_menu_clear (&maction_context);
      browser_reset (&remote_browser);
    }
  elektroid_check_backend_int (startup);	//This triggers the actual devices refresh if there is no backend
}

void
elektroid_refresh_devices ()
{
  elektroid_refresh_devices_int (FALSE);
}

static void
elektroid_refresh_devices_clicked (GtkWidget *widget, gpointer data)
{
  elektroid_refresh_devices_int (FALSE);
}

static void
elektroid_show_remote (gboolean active)
{
  elektroid_refresh_devices_int (TRUE);
  gtk_widget_set_visible (local_name_entry, active);
  gtk_widget_set_visible (remote_side, active);
  gtk_widget_set_margin_end (local_side, active ? 6 : 0);
  gtk_widget_set_visible (tasks_box, active);
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
elektroid_open_preferences (GtkWidget *object, gpointer data)
{
  preferences_window_open ();
}

static void
elektroid_show_about (GtkWidget *object, gpointer data)
{
  gtk_dialog_run (GTK_DIALOG (about_dialog));
  gtk_widget_hide (GTK_WIDGET (about_dialog));
}

static gint
elektroid_delete_file (struct browser *browser, gchar *dir, struct item *item,
		       gboolean has_progress_window)
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
	  elektroid_delete_file (browser, path, &iter.item,
				 has_progress_window);

	  if (has_progress_window && !progress_window_is_active ())
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

void
elektroid_delete_items_runner (gpointer data)
{
  GList *list, *tree_path_list, *ref_list;
  GtkTreeSelection *selection;
  GtkTreeModel *model;
  struct browser_delete_items_data *delete_data = data;
  struct browser *browser = delete_data->browser;

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

      if (elektroid_delete_file
	  (browser, browser->dir, &item, delete_data->has_progress_window))
	{
	  error_print ("Error while deleting file");
	}

      if (delete_data->has_progress_window && !progress_window_is_active ())
	{
	  break;
	}

      list = g_list_next (list);
    }
  g_list_free_full (ref_list, (GDestroyNotify) gtk_tree_row_reference_free);
  g_mutex_unlock (&browser->mutex);

  g_free (delete_data);

  g_idle_add (browser_load_dir_if_needed, browser);
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
  gboolean found = tasks_get_next_queued (&iter, &type, &src, &dst,
					  &fs, &batch_id, &mode);
  const gchar *status_human = tasks_get_human_status (TASK_STATUS_RUNNING);

  transfer_active =
    controllable_is_active (&tasks.transfer.control.controllable);

  if (!transfer_active && found)
    {
      const struct fs_operations *ops =
	backend_get_fs_operations_by_id (BACKEND, fs);
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
      tasks.transfer.control.controllable.active = TRUE;
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

      tasks_update_current_progress (NULL);

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

  tasks_check_buttons ();

  return FALSE;
}

static void
elektroid_show_task_overwrite_dialog_response (GtkDialog *dialog,
					       gint response_id,
					       gpointer user_data)
{
  GtkWidget *checkbutton = user_data;
  gboolean apply_to_all =
    gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (checkbutton));
  switch (response_id)
    {
    case GTK_RESPONSE_CANCEL:
      //Cancel current task.
      tasks.transfer.status = TASK_STATUS_CANCELED;
      //Cancel all tasks belonging to the same batch.
      tasks_visit_pending (tasks_visitor_set_batch_canceled);
      break;
    case GTK_RESPONSE_REJECT:
      //Cancel current task.
      tasks.transfer.status = TASK_STATUS_CANCELED;
      if (apply_to_all)
	{
	  //Mark pending tasks as SKIP.
	  tasks_visit_pending (tasks_batch_visitor_set_skip);
	}
      break;
    case GTK_RESPONSE_ACCEPT:
      if (apply_to_all)
	{
	  //Mark pending tasks as REPLACE.
	  tasks_visit_pending (tasks_batch_visitor_set_replace);
	}
      break;
    }

  gtk_widget_destroy (GTK_WIDGET (dialog));

  g_mutex_lock (&tasks.transfer.control.controllable.mutex);
  g_cond_signal (&tasks.transfer.control.cond);
  g_mutex_unlock (&tasks.transfer.control.controllable.mutex);
}

static gboolean
elektroid_show_task_overwrite_dialog (gpointer data)
{
  GtkWidget *dialog;
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

  gtk_widget_set_visible (dialog, TRUE);
  g_signal_connect (dialog, "response",
		    G_CALLBACK
		    (elektroid_show_task_overwrite_dialog_response),
		    checkbutton);

  return FALSE;
}

//Close the preparing tasks progress dialog if it is open.
static gboolean
elektroid_close_progress_window_dialog (gpointer data)
{
  progress_window_cancel ();
  return FALSE;
}

static void
elektroid_check_file_and_wait (gchar *path, struct browser *browser)
{
  const struct fs_operations *fs_ops = browser->fs_ops;
  if (fs_ops->file_exists && fs_ops->file_exists (browser->backend, path))
    {
      switch (tasks.transfer.mode)
	{
	case TASK_MODE_ASK:
	  g_idle_add (elektroid_close_progress_window_dialog, NULL);
	  g_idle_add (elektroid_show_task_overwrite_dialog, path);
	  g_cond_wait (&tasks.transfer.control.cond,
		       &tasks.transfer.control.controllable.mutex);
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
  gboolean active;
  struct idata idata;
  gchar *dst_dir, *upload_path;

  debug_print (1, "Local path: %s", tasks.transfer.src);
  debug_print (1, "Remote path: %s", tasks.transfer.dst);

  if (remote_browser.fs_ops->mkdir
      && remote_browser.fs_ops->mkdir (BACKEND, tasks.transfer.dst))
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

  upload_path = remote_browser.fs_ops->get_upload_path (BACKEND,
							remote_browser.fs_ops,
							tasks.transfer.dst,
							tasks.transfer.src,
							&idata);
  g_mutex_lock (&tasks.transfer.control.controllable.mutex);
  elektroid_check_file_and_wait (upload_path, &remote_browser);
  g_mutex_unlock (&tasks.transfer.control.controllable.mutex);
  if (tasks.transfer.status == TASK_STATUS_CANCELED)
    {
      goto cleanup;
    }

  res = tasks.transfer.fs_ops->upload (BACKEND,
				       upload_path, &idata,
				       &tasks.transfer.control);

  active = controllable_is_active (&tasks.transfer.control.controllable);
  if (res && active)
    {
      error_print ("Error while uploading");
      tasks.transfer.status = TASK_STATUS_COMPLETED_ERROR;
    }
  else
    {
      tasks.transfer.status = active ?
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
				const gchar *src_dir, const gchar *dst_dir,
				gboolean has_progress_window)
{
  struct item_iterator iter;
  gchar *path, *src_abs_path, *rel_path_trans;
  enum path_type type = backend_get_path_type (BACKEND);

  if (has_progress_window && !progress_window_is_active ())
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
      tasks_add (TASK_TYPE_UPLOAD, src_abs_path, dst_abs_dir,
		 remote_browser.fs_ops->id, BACKEND);
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
      elektroid_add_upload_task_path (path, src_dir, dst_dir,
				      has_progress_window);
      g_free (path);
    }

cleanup_iter:
  item_iterator_free (&iter);
cleanup:
  g_free (src_abs_path);
}

static void
elektroid_add_upload_tasks_runner (gpointer data)
{
  GtkTreeIter iter;
  GList *selected_rows;
  gboolean queued_before, queued_after;
  gboolean *has_progress_window = data;
  GtkTreeModel *model = gtk_tree_view_get_model (local_browser.view);
  GtkTreeSelection *sel = gtk_tree_view_get_selection (local_browser.view);

  queued_before = tasks_get_next_queued (&iter, NULL, NULL, NULL,
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
				      remote_browser.dir,
				      *has_progress_window);

      if (*has_progress_window && !progress_window_is_active ())
	{
	  break;
	}

      selected_rows = g_list_next (selected_rows);
    }
  g_list_free_full (selected_rows, (GDestroyNotify) gtk_tree_path_free);

  queued_after = tasks_get_next_queued (&iter, NULL, NULL, NULL,
					NULL, NULL, NULL);
  if (!queued_before && queued_after)
    {
      g_idle_add (elektroid_run_next, NULL);
    }

  g_free (has_progress_window);
}

void
elektroid_add_upload_tasks (GtkWidget *object, gpointer data)
{
  GtkTreeSelection *sel = gtk_tree_view_get_selection (local_browser.view);
  gboolean *has_progress_window = g_malloc (sizeof (gboolean));

  if (!gtk_tree_selection_count_selected_rows (sel))
    {
      return;
    }

  if (browser_no_progress_needed (&local_browser))
    {
      *has_progress_window = FALSE;
      elektroid_add_upload_tasks_runner (has_progress_window);
    }
  else
    {
      *has_progress_window = TRUE;
      progress_window_open (elektroid_add_upload_tasks_runner, NULL, NULL,
			    has_progress_window, PROGRESS_TYPE_PULSE,
			    _("Preparing Tasks"), _("Waiting..."), TRUE);
    }
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

  res = tasks.transfer.fs_ops->download (BACKEND,
					 tasks.transfer.src, &idata,
					 &tasks.transfer.control);

  g_mutex_lock (&tasks.transfer.control.controllable.mutex);
  if (res)
    {
      if (tasks.transfer.control.controllable.active)
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

  dst_path = remote_browser.fs_ops->get_download_path (BACKEND,
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
  g_mutex_unlock (&tasks.transfer.control.controllable.mutex);

  g_idle_add (tasks_complete_current, &tasks);
  g_idle_add (elektroid_run_next, NULL);

end_no_dir:
  return NULL;
}

static void
elektroid_add_download_task_path (const gchar *rel_path,
				  const gchar *src_dir, const gchar *dst_dir,
				  gboolean has_progress_window)
{
  struct item_iterator iter;
  gchar *path, *filename, *src_abs_path, *rel_path_trans;
  enum path_type type = backend_get_path_type (BACKEND);

  if (has_progress_window && !progress_window_is_active ())
    {
      return;
    }

  rel_path_trans = path_translate (type, rel_path);
  src_abs_path = path_chain (type, src_dir, rel_path_trans);
  g_free (rel_path_trans);

  //Check if the item is a dir. If error, it's not.
  if (remote_browser.fs_ops->readdir (BACKEND, &iter, src_abs_path, NULL))
    {
      rel_path_trans = path_translate (PATH_SYSTEM, rel_path);
      gchar *dst_abs_path = path_chain (PATH_SYSTEM, dst_dir, rel_path_trans);
      g_free (rel_path_trans);

      gchar *dst_abs_dir = g_path_get_dirname (dst_abs_path);
      tasks_add (TASK_TYPE_DOWNLOAD, src_abs_path, dst_abs_dir,
		 remote_browser.fs_ops->id, BACKEND);
      g_free (dst_abs_dir);
      g_free (dst_abs_path);
      goto cleanup;
    }

  while (!item_iterator_next (&iter))
    {
      filename = item_get_filename (&iter.item,
				    remote_browser.fs_ops->options);
      path = path_chain (PATH_INTERNAL, rel_path, filename);
      elektroid_add_download_task_path (path, src_dir, dst_dir,
					has_progress_window);
      debug_print (1, "name: %s", filename);
      g_free (path);
      g_free (filename);
      debug_print (1, "next");
    }

  item_iterator_free (&iter);
cleanup:
  g_free (src_abs_path);
}

static void
elektroid_add_download_tasks_runner (gpointer data)
{
  GtkTreeIter iter;
  GList *selected_rows;
  gboolean queued_before, queued_after;
  gboolean *has_progress_window = data;
  GtkTreeModel *model = gtk_tree_view_get_model (remote_browser.view);
  GtkTreeSelection *sel = gtk_tree_view_get_selection (remote_browser.view);

  queued_before = tasks_get_next_queued (&iter, NULL, NULL, NULL,
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
					local_browser.dir,
					*has_progress_window);
      g_free (filename);

      if (*has_progress_window && !progress_window_is_active ())
	{
	  break;
	}

      selected_rows = g_list_next (selected_rows);
    }
  g_list_free_full (selected_rows, (GDestroyNotify) gtk_tree_path_free);

  queued_after = tasks_get_next_queued (&iter, NULL, NULL, NULL,
					NULL, NULL, NULL);
  if (!queued_before && queued_after)
    {
      g_idle_add (elektroid_run_next, NULL);
    }

  g_free (has_progress_window);
}

void
elektroid_add_download_tasks (GtkWidget *object, gpointer data)
{
  GtkTreeSelection *sel = gtk_tree_view_get_selection (remote_browser.view);
  gboolean *has_progress_window = g_malloc (sizeof (gboolean));

  if (!gtk_tree_selection_count_selected_rows (sel))
    {
      return;
    }

  if (browser_no_progress_needed (&remote_browser))
    {
      *has_progress_window = FALSE;
      elektroid_add_download_tasks_runner (has_progress_window);
    }
  else
    {
      *has_progress_window = TRUE;
      progress_window_open (elektroid_add_download_tasks_runner, NULL, NULL,
			    has_progress_window, PROGRESS_TYPE_PULSE,
			    _("Preparing Tasks"), _("Waiting..."), TRUE);
    }
}

static void
elektroid_update_progress (struct job_control *control)
{
  g_idle_add (tasks_update_current_progress, NULL);
}

static void
elektroid_set_fs (GtkWidget *object, gpointer data)
{
  GtkTreeIter iter;
  GValue fsv = G_VALUE_INIT;
  gint fs;
  const struct fs_operations *fs_ops;

  if (!gtk_combo_box_get_active_iter (GTK_COMBO_BOX (fs_combo), &iter))
    {
      browser_remote_set_fs_operations (NULL);
      return;
    }

  gtk_tree_model_get_value (GTK_TREE_MODEL (fs_list_store),
			    &iter, FS_LIST_STORE_ID_FIELD, &fsv);
  fs = g_value_get_uint (&fsv);
  g_value_unset (&fsv);

  fs_ops = backend_get_fs_operations_by_id (BACKEND, fs);
  browser_remote_set_fs_operations (fs_ops);
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

  e = BACKEND->fs_ops;
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

static void
elektroid_set_device_consumer (gpointer data)
{
  struct elektroid_set_device_data *set_device_data = data;

  if (set_device_data->err)
    {
      if (set_device_data->err != -ECANCELED)
	{
	  error_print ("Error while connecting: %s",
		       g_strerror (-set_device_data->err));
	  elektroid_show_error_msg (_("Device “%s” not recognized: %s"),
				    set_device_data->backend_device.name,
				    g_strerror (-set_device_data->err));

	  gtk_combo_box_set_active (GTK_COMBO_BOX (devices_combo), -1);
	  elektroid_check_backend_int (FALSE);
	}

      gtk_combo_box_set_active (GTK_COMBO_BOX (devices_combo), -1);
      elektroid_check_backend_int (FALSE);
    }
  else
    {
      elektroid_fill_fs_combo_bg (NULL);
      maction_menu_setup (&maction_context);
    }

  controllable_clear (&set_device_data->controllable);
  g_free (set_device_data);
}

static void
elektroid_set_device_runner (gpointer data)
{
  struct elektroid_set_device_data *set_device_data = data;
  set_device_data->err = backend_init_connector (BACKEND,
						 &set_device_data->backend_device,
						 NULL,
						 &set_device_data->controllable);
  elektroid_update_midi_status ();
}

static void
elektroid_set_device_cancel (gpointer data)
{
  struct elektroid_set_device_data *set_device_data = data;
  controllable_set_active (&set_device_data->controllable, FALSE);
}

static void
elektroid_set_device (GtkWidget *object, gpointer data)
{
  GtkTreeIter iter;
  gchar *id, *name;
  struct elektroid_set_device_data *set_device_data =
    g_malloc (sizeof (struct elektroid_set_device_data));

  elektroid_cancel_all_tasks_and_wait ();

  if (!gtk_combo_box_get_active_iter (GTK_COMBO_BOX (devices_combo), &iter))
    {
      return;
    }

  if (backend_check (BACKEND))
    {
      backend_destroy (BACKEND);
    }

  gtk_tree_model_get (GTK_TREE_MODEL (devices_list_store), &iter,
		      DEVICES_LIST_STORE_TYPE_FIELD,
		      &set_device_data->backend_device.type,
		      DEVICES_LIST_STORE_ID_FIELD, &id,
		      DEVICES_LIST_STORE_NAME_FIELD, &name, -1);

  strcpy (set_device_data->backend_device.id, id);
  strcpy (set_device_data->backend_device.name, name);
  g_free (id);
  g_free (name);

  maction_menu_clear (&maction_context);

  if (set_device_data->backend_device.type == BE_TYPE_SYSTEM)
    {
      backend_init_connector (BACKEND, &set_device_data->backend_device, NULL,
			      NULL);
      elektroid_update_midi_status ();
      elektroid_fill_fs_combo_bg (NULL);
      maction_menu_setup (&maction_context);
      g_free (set_device_data);
    }
  else
    {
      controllable_init (&set_device_data->controllable);
      progress_window_open (elektroid_set_device_runner,
			    elektroid_set_device_consumer,
			    elektroid_set_device_cancel, set_device_data,
			    PROGRESS_TYPE_PULSE, _("Connecting to Device"),
			    _("Connecting..."), TRUE);

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

      tasks_add (TASK_TYPE_UPLOAD, src_file_path, dst_file_path,
		 remote_browser.fs_ops->id, BACKEND);
    }
}

void
elektroid_browser_drag_data_received_runner (gpointer data)
{
  GtkTreeIter iter;
  gboolean queued_before, queued_after;
  struct browser_drag_data_received_data *dnd_data = data;
  GtkWidget *widget = dnd_data->widget;

  queued_before = tasks_get_next_queued (&iter, NULL, NULL, NULL,
					 NULL, NULL, NULL);

  for (gint i = 0; dnd_data->uris[i] != NULL; i++)
    {
      if (dnd_data->has_progress_window && !progress_window_is_active ())
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
	      elektroid_add_download_task_path (name, dir, local_browser.dir,
						dnd_data->has_progress_window);
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
						  remote_browser.dir,
						  dnd_data->has_progress_window);
		}
	    }
	}

      g_free (name);
      g_free (dir);
      g_free (src_path);
    }

end:
  queued_after = tasks_get_next_queued (&iter, NULL, NULL, NULL,
					NULL, NULL, NULL);
  if (!queued_before && queued_after)
    {
      g_idle_add (elektroid_run_next, NULL);
    }

  g_free (dnd_data->type_name);
  g_strfreev (dnd_data->uris);
  g_free (dnd_data);

  batch_id++;
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
  tasks_stop_thread ();

  progress_window_destroy ();
  microbrute_destroy ();
  autosampler_destroy ();
  preferences_window_destroy ();
  name_window_destroy ();

  browser_destroy_all ();
  editor_destroy ();

  if (backend_check (BACKEND))
    {
      backend_destroy (BACKEND);
    }

  gtk_widget_destroy (GTK_WIDGET (main_window));
}

static gboolean
elektroid_delete_main_window (GtkWidget *widget, GdkEvent *event,
			      gpointer data)
{
  elektroid_exit ();
  return FALSE;
}

static void
elektroid_startup (GApplication *gapp, gpointer *user_data)
{
  GtkCssProvider *css_provider;
  GtkWidget *refresh_devices_button;
  GtkBuilder *builder;
  gchar *thanks;

  if (local_dir)
    {
      //Check the directory passed by `-l`
      gchar *abs_local_dir = g_canonicalize_filename (local_dir, NULL);
      g_free (local_dir);
      preferences_set_string (PREF_KEY_LOCAL_DIR,
			      get_system_startup_path (abs_local_dir));
      g_free (abs_local_dir);
    }

  builder = gtk_builder_new ();
  gtk_builder_add_from_file (builder, DATADIR "/elektroid.ui", NULL);

  css_provider = gtk_css_provider_new ();
  gtk_css_provider_load_from_path (css_provider, DATADIR "/elektroid.css",
				   NULL);
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
		    G_CALLBACK (elektroid_delete_main_window), NULL);

  g_signal_connect (show_remote_button, "clicked",
		    G_CALLBACK (elektroid_show_remote_clicked), NULL);

  g_signal_connect (preferences_button, "clicked",
		    G_CALLBACK (elektroid_open_preferences), NULL);

  g_signal_connect (about_button, "clicked",
		    G_CALLBACK (elektroid_show_about), NULL);

  devices_list_store =
    GTK_LIST_STORE (gtk_builder_get_object (builder, "devices_list_store"));
  devices_combo =
    GTK_WIDGET (gtk_builder_get_object (builder, "devices_combo"));
  refresh_devices_button =
    GTK_WIDGET (gtk_builder_get_object (builder, "refresh_devices_button"));
  g_signal_connect (devices_combo, "changed",
		    G_CALLBACK (elektroid_set_device), NULL);
  g_signal_connect (refresh_devices_button, "clicked",
		    G_CALLBACK (elektroid_refresh_devices_clicked), NULL);

  gtk_label_set_text (backend_status_label, _("Not connected"));

  fs_list_store =
    GTK_LIST_STORE (gtk_builder_get_object (builder, "fs_list_store"));
  fs_combo = GTK_WIDGET (gtk_builder_get_object (builder, "fs_combo"));
  g_signal_connect (fs_combo, "changed", G_CALLBACK (elektroid_set_fs), NULL);

  browser_init_all (builder);
  name_window_init (builder);
  preferences_window_init (builder);
  editor_init (builder);
  elektroid_update_midi_status ();
  tasks_init (builder);
  progress_window_init (builder);

  microbrute_init ();
  autosampler_init (builder);

  g_object_set (G_OBJECT (show_remote_button), "active",
		preferences_get_boolean (PREF_KEY_SHOW_REMOTE), NULL);

  gtk_widget_set_sensitive (remote_box, FALSE);

  elektroid_show_remote (preferences_get_boolean (PREF_KEY_SHOW_REMOTE));	//This triggers both browsers initializations.

  GtkEntryBuffer *buf = gtk_entry_get_buffer (GTK_ENTRY (local_name_entry));
  gtk_entry_buffer_set_text (buf, g_get_host_name (), -1);

  elektroid_set_window_size ();

  g_object_unref (builder);
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
elektroid_activate (GApplication *gapp, gpointer *user_data)
{
  gtk_application_add_window (GTK_APPLICATION (gapp),
			      GTK_WINDOW (main_window));
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

  preferences_save ();
  preferences_free ();

  regconn_unregister ();
  regma_unregister ();
  regpref_unregister ();

  return err;
}
