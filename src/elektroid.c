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
#include <gtk/gtk.h>
#include <unistd.h>
#include "connector.h"
#include <sys/stat.h>
#include <sys/types.h>
#include <glib-unix.h>
#include <glib/gi18n.h>
#include "browser.h"
#include "audio.h"
#include "sample.h"
#include "utils.h"

#define MAX_DRAW_X 10000
#define SIZE_LABEL_LEN 16

#define DEVICES_LIST_STORE_CARD_FIELD 0
#define DEVICES_LIST_STORE_NAME_FIELD 1

#define TASK_LIST_STORE_STATUS_FIELD 0
#define TASK_LIST_STORE_TYPE_FIELD 1
#define TASK_LIST_STORE_SRC_FIELD 2
#define TASK_LIST_STORE_DST_FIELD 3
#define TASK_LIST_STORE_PROGRESS_FIELD 4
#define TASK_LIST_STORE_STATUS_HUMAN_FIELD 5
#define TASK_LIST_STORE_TYPE_HUMAN_FIELD 6

static gpointer elektroid_upload_task (gpointer);
static gpointer elektroid_download_task (gpointer);
static void elektroid_update_progress (gdouble);

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

struct elektroid_active_task
{
  gint running;
  gchar *src;			//Contains a path to a file
  gchar *dst;			//Contains a path to a dir
  enum elektroid_task_status status;	//Contains the final status
  gdouble progress;
};

static struct browser remote_browser;
static struct browser local_browser;
static struct browser *popup_browser;

static struct audio audio;
static struct connector connector;
static gboolean autoplay;

static GThread *load_thread = NULL;
static GThread *task_thread = NULL;
static gint load_thread_running;
static GMutex load_mutex;
static struct elektroid_active_task active_task;

static GtkWidget *main_window;
static GtkAboutDialog *about_dialog;
static GtkDialog *name_dialog;
static GtkEntry *name_dialog_entry;
static GtkWidget *name_dialog_accept_button;
static GtkWidget *about_button;
static GtkWidget *remote_box;
static GtkWidget *waveform_draw_area;
static GtkWidget *upload_button;
static GtkWidget *download_button;
static GtkStatusbar *status_bar;
static GtkListStore *devices_list_store;
static GtkComboBox *devices_combo;
static GtkWidget *item_popmenu;
static GtkWidget *rename_button;
static GtkWidget *delete_button;
static GtkWidget *play_button;
static GtkWidget *stop_button;
static GtkWidget *volume_button;
static GtkListStore *task_list_store;
static GtkWidget *task_tree_view;
static GtkWidget *cancel_task_button;
static GtkWidget *remove_tasks_button;
static GtkWidget *clear_tasks_button;

static void
elektroid_load_devices (int auto_select)
{
  int i;
  GArray *devices = connector_get_elektron_devices ();
  struct connector_device device;

  debug_print (1, "Loading devices...\n");

  gtk_list_store_clear (devices_list_store);

  for (i = 0; i < devices->len; i++)
    {
      device = g_array_index (devices, struct connector_device, i);
      gtk_list_store_insert_with_values (devices_list_store, NULL, -1,
					 DEVICES_LIST_STORE_CARD_FIELD,
					 device.card,
					 DEVICES_LIST_STORE_NAME_FIELD,
					 device.name, -1);
    }

  g_array_free (devices, TRUE);

  if (auto_select && i == 1)
    {
      debug_print (1, "Selecting device 0...\n");
      gtk_combo_box_set_active (devices_combo, 0);
    }
  else
    {
      gtk_combo_box_set_active (devices_combo, -1);
    }
}

static void
elektroid_update_statusbar ()
{
  char *status;

  gtk_statusbar_pop (status_bar, 0);

  if (connector_check (&connector))
    {
      status = malloc (LABEL_MAX);
      snprintf (status, LABEL_MAX, _("Connected to %s"),
		connector.device_name);
      gtk_statusbar_push (status_bar, 0, status);
      free (status);
    }
  else
    {
      gtk_statusbar_push (status_bar, 0, _("Not connected"));
    }
}

static int
elektroid_check_connector ()
{
  GtkListStore *list_store;
  int status = connector_check (&connector);

  if (status)
    {
      gtk_widget_set_sensitive (remote_box, TRUE);
    }
  else
    {
      list_store =
	GTK_LIST_STORE (gtk_tree_view_get_model (remote_browser.view));
      gtk_entry_set_text (remote_browser.dir_entry, "");
      gtk_list_store_clear (list_store);
      gtk_widget_set_sensitive (remote_box, FALSE);
      gtk_widget_set_sensitive (download_button, FALSE);
      gtk_widget_set_sensitive (upload_button, FALSE);

      elektroid_load_devices (0);
    }

  elektroid_update_statusbar ();

  return status;
}

static void
browser_refresh_devices (GtkWidget * object, gpointer data)
{
  if (connector_check (&connector))
    {
      connector_destroy (&connector);
      elektroid_check_connector ();
    }
  elektroid_load_devices (0);
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
  gtk_widget_set_sensitive (play_button, sensitive);
  gtk_widget_set_sensitive (stop_button, sensitive);
}

static gboolean
elektroid_update_ui_after_load (gpointer data)
{
  if (audio.sample->len > 0)
    {
      if (audio_check (&audio))
	{
	  elektroid_controls_set_sensitive (TRUE);
	}
      if (autoplay)
	{
	  audio_play (&audio);
	}
    }
  return FALSE;
}

static void
elektroid_delete_file (GtkTreeModel * model, GtkTreePath * tree_path)
{
  GtkTreeIter iter;
  gchar *name;
  gchar *icon;
  gchar *path;
  gchar type;
  gint err;
  GtkWidget *dialog;

  gtk_tree_model_get_iter (model, &iter, tree_path);
  browser_get_item_info (model, &iter, &icon, &name, NULL);
  type = get_type_from_inventory_icon (icon);

  path = chain_path (popup_browser->dir, name);
  debug_print (1, "Deleting %s...\n", path);

  err = popup_browser->delete (path, type);
  if (err < 0)
    {
      dialog =
	gtk_message_dialog_new (GTK_WINDOW (main_window),
				GTK_DIALOG_DESTROY_WITH_PARENT |
				GTK_DIALOG_MODAL,
				GTK_MESSAGE_ERROR,
				GTK_BUTTONS_CLOSE,
				_("Error while deleting %s: %s"),
				path, g_strerror (errno));
      gtk_dialog_run (GTK_DIALOG (dialog));
      gtk_widget_destroy (dialog);
    }
  else
    {
      gtk_list_store_remove (GTK_LIST_STORE (model), &iter);
    }

  free (path);
  free (icon);
  free (name);
}

static void
elektroid_delete_files (GtkWidget * object, gpointer data)
{
  GtkTreeRowReference *reference;
  GList *item;
  GtkTreePath *tree_path;
  GtkTreeSelection *selection;
  GtkTreeModel *model;
  GtkWidget *dialog;
  GList *tree_path_list;
  GList *ref_list;
  gint confirmation;

  dialog =
    gtk_message_dialog_new (GTK_WINDOW (main_window),
			    GTK_DIALOG_DESTROY_WITH_PARENT | GTK_DIALOG_MODAL,
			    GTK_MESSAGE_ERROR, GTK_BUTTONS_NONE,
			    _("Are you sure you want to delete the selected items?"));
  gtk_dialog_add_buttons (GTK_DIALOG (dialog), _("Cancel"),
			  GTK_RESPONSE_CANCEL, _("Delete"),
			  GTK_RESPONSE_ACCEPT, NULL);
  gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_ACCEPT);
  confirmation = gtk_dialog_run (GTK_DIALOG (dialog));
  gtk_widget_destroy (dialog);
  if (confirmation != GTK_RESPONSE_ACCEPT)
    {
      return;
    }

  selection =
    gtk_tree_view_get_selection (GTK_TREE_VIEW (popup_browser->view));
  model = GTK_TREE_MODEL (gtk_tree_view_get_model (popup_browser->view));
  tree_path_list = gtk_tree_selection_get_selected_rows (selection, &model);
  ref_list = NULL;

  for (item = tree_path_list; item != NULL; item = g_list_next (item))
    {
      reference = gtk_tree_row_reference_new (model, item->data);
      ref_list = g_list_append (ref_list, reference);
    }
  g_list_free_full (tree_path_list, (GDestroyNotify) gtk_tree_path_free);

  for (item = ref_list; item != NULL; item = g_list_next (item))
    {
      tree_path = gtk_tree_row_reference_get_path (item->data);
      elektroid_delete_file (model, tree_path);
    }
  g_list_free_full (ref_list, (GDestroyNotify) gtk_tree_row_reference_free);

  popup_browser->load_dir (NULL);
}

static void
elektroid_rename_item (GtkWidget * object, gpointer data)
{
  char *old_name;
  char *old_path;
  char *new_path;
  int result;
  gint err;
  GtkWidget *dialog;
  GtkTreeIter iter;
  GtkTreeModel *model = GTK_TREE_MODEL
    (gtk_tree_view_get_model (popup_browser->view));

  browser_set_selected_row_iter (popup_browser, &iter);
  browser_get_item_info (model, &iter, NULL, &old_name, NULL);

  old_path = chain_path (popup_browser->dir, old_name);

  gtk_entry_set_text (name_dialog_entry, old_name);
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
	    chain_path (popup_browser->dir,
			gtk_entry_get_text (name_dialog_entry));

	  err = popup_browser->rename (old_path, new_path);

	  if (err < 0)
	    {
	      dialog =
		gtk_message_dialog_new (GTK_WINDOW (name_dialog),
					GTK_DIALOG_DESTROY_WITH_PARENT |
					GTK_DIALOG_MODAL,
					GTK_MESSAGE_ERROR,
					GTK_BUTTONS_CLOSE,
					_("Error while renaming to %s: %s"),
					new_path, g_strerror (errno));
	      gtk_dialog_run (GTK_DIALOG (dialog));
	      gtk_widget_destroy (dialog);
	    }
	  else
	    {
	      popup_browser->load_dir (NULL);
	    }

	  free (new_path);
	}
    }

  free (old_name);
  free (old_path);
  gtk_widget_hide (GTK_WIDGET (name_dialog));
}

static gboolean
elektroid_show_item_popup (GtkWidget * treeview, GdkEventButton * event,
			   gpointer data)
{
  GdkRectangle rect;
  GtkTreePath *path;
  gint count;
  gboolean selected;
  GtkTreeSelection *selection;
  struct browser *browser = data;

  popup_browser = browser;

  if (event->type == GDK_BUTTON_PRESS && event->button == 3)
    {
      selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (browser->view));
      gtk_tree_view_get_path_at_pos (browser->view,
				     event->x, event->y, &path, NULL, NULL,
				     NULL);

      selected = gtk_tree_selection_path_is_selected (selection, path);
      if (!selected)
	{
	  if ((event->state & GDK_SHIFT_MASK) != GDK_SHIFT_MASK
	      && (event->state & GDK_CONTROL_MASK) != GDK_CONTROL_MASK)
	    {
	      gtk_tree_selection_unselect_all (selection);
	    }
	  gtk_tree_selection_select_path (selection, path);
	}

      count = browser_get_selected_items_count (browser);
      gtk_widget_set_sensitive (rename_button, count == 1 ? TRUE : FALSE);

      gtk_tree_view_get_background_area (browser->view, path, NULL, &rect);
      gtk_tree_path_free (path);

      rect.x = event->x;
      rect.y = rect.y + rect.height;
      gtk_popover_set_pointing_to (GTK_POPOVER (item_popmenu), &rect);
      gtk_popover_set_relative_to (GTK_POPOVER (item_popmenu), treeview);
      gtk_popover_popup (GTK_POPOVER (item_popmenu));

      return TRUE;
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
  sample_load (audio.sample, &load_mutex, &audio.frames, path,
	       &load_thread_running, elektroid_redraw_sample);
  free (path);
  g_idle_add (elektroid_update_ui_after_load, NULL);
  return NULL;
}

static void
elektroid_start_load_thread (gchar * path)
{
  debug_print (1, "Creating load thread...\n");
  load_thread_running = 1;
  load_thread = g_thread_new ("load_sample", elektroid_load_sample, path);
}

static void
elektroid_stop_load_thread ()
{
  debug_print (1, "Stopping load thread...\n");
  load_thread_running = 0;
  if (load_thread)
    {
      g_thread_join (load_thread);
      g_thread_unref (load_thread);
    }
  load_thread = NULL;
}

static void
elektroid_join_task_thread ()
{
  debug_print (2, "Joining task thread...\n");
  if (task_thread)
    {
      g_thread_join (task_thread);
      g_thread_unref (task_thread);
    }
  task_thread = NULL;
}

static void
elektroid_stop_task_thread ()
{
  debug_print (1, "Stopping task thread...\n");
  active_task.running = 0;
  elektroid_join_task_thread ();
}

static gboolean
elektroid_remote_check_selection (gpointer data)
{
  gint count = browser_get_selected_items_count (&remote_browser);
  gtk_widget_set_sensitive (download_button, count > 0);
  return FALSE;
}

static gboolean
elektroid_local_check_selection (gpointer data)
{
  GtkTreeIter iter;
  gchar *sample_path;
  gchar *name;
  gchar *icon;
  gchar type;
  GtkTreeModel *model;
  gint count = browser_get_selected_items_count (&local_browser);

  audio_stop (&audio, TRUE);
  elektroid_stop_load_thread ();
  audio_reset_sample (&audio);
  gtk_widget_queue_draw (waveform_draw_area);
  elektroid_controls_set_sensitive (FALSE);
  gtk_widget_set_sensitive (upload_button, count > 0
			    && connector_check (&connector));

  if (count == 1)
    {
      browser_set_selected_row_iter (&local_browser, &iter);
      model = GTK_TREE_MODEL (gtk_tree_view_get_model (local_browser.view));
      browser_get_item_info (model, &iter, &icon, &name, NULL);
      type = get_type_from_inventory_icon (icon);
      if (type == ELEKTROID_FILE)
	{
	  sample_path = chain_path (local_browser.dir, name);
	  elektroid_start_load_thread (sample_path);
	}
      free (icon);
      free (name);
    }

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

  g_mutex_lock (&load_mutex);

  if (audio.sample->len <= 0)
    {
      goto cleanup;
    }

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
      if (x_sample < audio.sample->len)
	{
	  x_widget = i * ((double) width) / MAX_DRAW_X;
	  value = mid_y - mid_y * (sample[x_sample] / (float) SHRT_MIN);
	  cairo_move_to (cr, x_widget, mid_y);
	  cairo_line_to (cr, x_widget, value);
	  cairo_stroke (cr);
	}
    }

cleanup:
  g_mutex_unlock (&load_mutex);
  return FALSE;
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
elektroid_add_dentry_item (GtkListStore * list_store,
			   const gchar type, const gchar * name, ssize_t size)
{
  const gchar *type_icon;
  char human_size[SIZE_LABEL_LEN];

  type_icon = get_inventory_icon_from_type (type);

  if (size > 0)
    {
      snprintf (human_size, SIZE_LABEL_LEN, "%.2f MiB",
		size / (1024.0 * 1024.0));
    }
  else
    {
      human_size[0] = 0;
    }

  gtk_list_store_insert_with_values (list_store, NULL, -1,
				     BROWSER_LIST_STORE_ICON_TYPE_FIELD,
				     type_icon, BROWSER_LIST_STORE_NAME_FIELD,
				     name, BROWSER_LIST_STORE_SIZE_FIELD,
				     size,
				     BROWSER_LIST_STORE_HUMAN_SIZE_FIELD,
				     human_size, -1);
}

static gboolean
elektroid_load_remote_dir (gpointer data)
{
  struct connector_dir_iterator *d_iter;
  GtkListStore *list_store =
    GTK_LIST_STORE (gtk_tree_view_get_model (remote_browser.view));

  browser_reset (&remote_browser);

  d_iter = connector_read_dir (&connector, remote_browser.dir);
  elektroid_check_connector ();
  if (d_iter == NULL)
    {
      fprintf (stderr, __FILE__ ": Error while opening remote %s dir.\n",
	       local_browser.dir);
      goto end;
    }

  while (!connector_get_next_dentry (d_iter))
    {
      elektroid_add_dentry_item (list_store, d_iter->type,
				 d_iter->dentry, d_iter->size);
    }
  connector_free_dir_iterator (d_iter);

end:
  gtk_tree_view_columns_autosize (remote_browser.view);
  return FALSE;
}

static gint
elektroid_valid_file (const char *name)
{
  const char *ext = get_ext (name);

  return (ext != NULL && (!strcasecmp (ext, "wav") || !strcasecmp (ext, "ogg")
			  || !strcasecmp (ext, "aiff")
			  || !strcasecmp (ext, "flac")));
}

static gboolean
elektroid_load_local_dir (gpointer data)
{
  DIR *dir;
  struct dirent *dirent;
  char type;
  struct stat st;
  ssize_t size;
  char *path;
  GtkListStore *list_store =
    GTK_LIST_STORE (gtk_tree_view_get_model (local_browser.view));

  browser_reset (&local_browser);

  if (!(dir = opendir (local_browser.dir)))
    {
      fprintf (stderr, __FILE__ ": Error while opening local %s dir.\n",
	       local_browser.dir);
      goto end;
    }

  while ((dirent = readdir (dir)) != NULL)
    {
      if (dirent->d_name[0] == '.')
	{
	  continue;
	}

      if (dirent->d_type == DT_DIR
	  || (dirent->d_type == DT_REG
	      && elektroid_valid_file (dirent->d_name)))
	{
	  if (dirent->d_type == DT_DIR)
	    {
	      type = ELEKTROID_DIR;
	      size = -1;
	    }
	  else
	    {
	      type = ELEKTROID_FILE;
	      path = chain_path (local_browser.dir, dirent->d_name);
	      if (stat (path, &st) == 0)
		{
		  size = st.st_size;
		}
	      else
		{
		  size = -1;
		}
	      free (path);
	    }
	  elektroid_add_dentry_item (list_store, type, dirent->d_name, size);
	}
    }
  closedir (dir);

end:
  gtk_tree_view_columns_autosize (local_browser.view);
  return FALSE;
}

static gint
elektroid_remote_mkdir (const gchar * name)
{
  return connector_create_dir (&connector, name);
}

static gint
elektroid_local_mkdir (const gchar * name)
{
  return mkdir (name, 0755);
}

static void
elektroid_add_dir (GtkWidget * object, gpointer data)
{
  char *pathname;
  int result;
  gint err;
  GtkWidget *dialog;
  struct browser *browser = data;

  gtk_entry_set_text (name_dialog_entry, "");
  gtk_widget_grab_focus (GTK_WIDGET (name_dialog_entry));
  gtk_widget_set_sensitive (name_dialog_accept_button, FALSE);

  gtk_window_set_title (GTK_WINDOW (name_dialog), _("Add directory"));

  result = GTK_RESPONSE_ACCEPT;

  err = -1;
  while (err < 0 && result == GTK_RESPONSE_ACCEPT)
    {
      result = gtk_dialog_run (GTK_DIALOG (name_dialog));

      if (result == GTK_RESPONSE_ACCEPT)
	{
	  pathname =
	    chain_path (browser->dir, gtk_entry_get_text (name_dialog_entry));

	  err = browser->mkdir (pathname);

	  if (err < 0)
	    {
	      dialog =
		gtk_message_dialog_new (GTK_WINDOW (name_dialog),
					GTK_DIALOG_DESTROY_WITH_PARENT |
					GTK_DIALOG_MODAL,
					GTK_MESSAGE_ERROR,
					GTK_BUTTONS_CLOSE,
					_("Error while creating dir %s: %s"),
					pathname, g_strerror (errno));
	      gtk_dialog_run (GTK_DIALOG (dialog));
	      gtk_widget_destroy (dialog);
	    }
	  else
	    {
	      browser->load_dir (NULL);
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

static gint
elektroid_remote_rename (const gchar * old, const gchar * new)
{
  return connector_rename (&connector, old, new);
}

static gint
elektroid_local_rename (const gchar * old, const gchar * new)
{
  return rename (old, new);
}

static gint
elektroid_remote_delete (const gchar * path, const char type)
{
  if (type == ELEKTROID_FILE)
    {
      return connector_delete_file (&connector, path);
    }
  else
    {
      return connector_delete_dir (&connector, path);
    }
}

static gint
elektroid_local_delete (const gchar * path, const char type)
{
  if (type == ELEKTROID_FILE)
    {
      return unlink (path);
    }
  else
    {
      return rmdir (path);
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
elektroid_cancel_running_task (GtkWidget * object, gpointer data)
{
  active_task.running = 0;
}

static gboolean
elektroid_select_queued_task (enum elektroid_task_status status)
{
  return (status == QUEUED);
}

static gboolean
elektroid_select_finished_task (enum elektroid_task_status status)
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

      if (elektroid_select_queued_task (status))
	{
	  queued = TRUE;
	}

      if (elektroid_select_finished_task (status))
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
  elektroid_remove_tasks_on_cond (elektroid_select_queued_task);
}

static void
elektroid_clear_finished_tasks (GtkWidget * object, gpointer data)
{
  elektroid_remove_tasks_on_cond (elektroid_select_finished_task);
}


static gboolean
elektroid_complete_running_task (gpointer data)
{
  GtkTreeIter iter;
  const gchar *status = elektroid_get_human_task_status (active_task.status);

  if (elektroid_get_running_task (&iter))
    {
      gtk_list_store_set (task_list_store, &iter,
			  TASK_LIST_STORE_STATUS_FIELD, active_task.status,
			  TASK_LIST_STORE_STATUS_HUMAN_FIELD, status, -1);
      active_task.running = 0;
      free (active_task.src);
      free (active_task.dst);
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
  enum elektroid_task_status status;
  enum elektroid_task_type type;
  gchar *src;
  gchar *dst;
  GtkTreeIter iter;
  GtkTreePath *path;
  gboolean found = FALSE;
  gboolean valid =
    gtk_tree_model_get_iter_first (GTK_TREE_MODEL (task_list_store), &iter);

  while (valid)
    {
      gtk_tree_model_get (GTK_TREE_MODEL (task_list_store), &iter,
			  TASK_LIST_STORE_STATUS_FIELD, &status,
			  TASK_LIST_STORE_TYPE_FIELD, &type,
			  TASK_LIST_STORE_SRC_FIELD, &src,
			  TASK_LIST_STORE_DST_FIELD, &dst, -1);

      if (status == RUNNING)
	{
	  debug_print (1, "Task running. Skipping...\n");
	  break;
	}
      else if (status == QUEUED)
	{
	  found = TRUE;
	  break;
	}
      valid =
	gtk_tree_model_iter_next (GTK_TREE_MODEL (task_list_store), &iter);
    }

  if (found)
    {
      gtk_list_store_set (task_list_store, &iter,
			  TASK_LIST_STORE_STATUS_FIELD, RUNNING, -1);
      path =
	gtk_tree_model_get_path (GTK_TREE_MODEL (task_list_store), &iter);
      gtk_tree_view_set_cursor (GTK_TREE_VIEW (task_tree_view), path, NULL,
				FALSE);
      gtk_tree_path_free (path);
      active_task.running = 1;
      active_task.src = src;
      active_task.dst = dst;
      active_task.progress = 0.0;
      debug_print (1, "Running task type %d from %s to %s...\n", type,
		   active_task.src, active_task.dst);
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
      gtk_widget_set_sensitive (cancel_task_button, FALSE);
    }

  return FALSE;
}

static gpointer
elektroid_upload_task (gpointer data)
{
  char *basec;
  char *bname;
  char *remote_path;
  ssize_t frames;
  GArray *sample;

  debug_print (1, "Local path: %s\n", active_task.src);

  basec = strdup (active_task.src);
  bname = basename (basec);
  remove_ext (bname);
  remote_path = chain_path (active_task.dst, bname);
  free (basec);

  debug_print (1, "Remote path: %s\n", remote_path);

  sample = g_array_new (FALSE, FALSE, sizeof (gshort));

  sample_load (sample, NULL, NULL, active_task.src, &active_task.running,
	       NULL);

  frames = connector_upload (&connector, sample, remote_path,
			     &active_task.running, elektroid_update_progress);
  free (remote_path);
  elektroid_check_connector ();

  if (frames < 0)
    {
      fprintf (stderr, __FILE__ ": Error while uploading.\n");
      active_task.status = COMPLETED_ERROR;
    }
  else
    {
      if (active_task.running)
	{
	  active_task.status = COMPLETED_OK;
	}
      else
	{
	  active_task.status = CANCELED;
	}
    }

  g_array_free (sample, TRUE);

  if (strcmp (active_task.dst, remote_browser.dir) == 0)
    {
      g_idle_add (remote_browser.load_dir, NULL);
    }
  g_idle_add (elektroid_complete_running_task, NULL);
  g_idle_add (elektroid_run_next_task, NULL);
  g_idle_add (elektroid_check_task_buttons, NULL);

  return NULL;
}

static void
elektroid_add_task (enum elektroid_task_type type, const char *src,
		    const char *dst)
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
				     type_human, -1);

  gtk_widget_set_sensitive (remove_tasks_button, TRUE);
  gtk_widget_set_sensitive (clear_tasks_button, TRUE);
}

static void
elektroid_add_upload_task_dir (gchar * rel_dir)
{
  gchar *path;
  struct dirent *dirent;
  gchar *remote_abs_dir;
  gchar *local_abs_dir = chain_path (local_browser.dir, rel_dir);
  DIR *dir = opendir (local_abs_dir);

  if (!dir)
    {
      fprintf (stderr, __FILE__ ": Error while opening local %s dir.\n",
	       local_abs_dir);
      goto cleanup_not_dir;
    }

  remote_abs_dir = chain_path (remote_browser.dir, rel_dir);
  if (elektroid_remote_mkdir (remote_abs_dir))
    {
      fprintf (stderr, __FILE__ ": Error while creating remote %s dir.\n",
	       remote_abs_dir);
      goto cleanup;
    }

  if (!strchr (rel_dir, '/'))
    {				//first call
      remote_browser.load_dir (NULL);
    }

  while ((dirent = readdir (dir)) != NULL)
    {
      if (dirent->d_name[0] == '.')
	{
	  continue;
	}

      if (dirent->d_type == DT_DIR
	  || (dirent->d_type == DT_REG
	      && elektroid_valid_file (dirent->d_name)))
	{
	  if (dirent->d_type == DT_DIR)
	    {
	      path = chain_path (rel_dir, dirent->d_name);
	      elektroid_add_upload_task_dir (path);
	      free (path);
	    }
	  else
	    {
	      path = chain_path (local_abs_dir, dirent->d_name);
	      elektroid_add_task (UPLOAD, path, remote_abs_dir);
	      free (path);
	    }
	}
    }

cleanup:
  free (remote_abs_dir);
  closedir (dir);
cleanup_not_dir:
  free (local_abs_dir);
}

static void
elektroid_add_upload_task (GtkTreeModel * model,
			   GtkTreePath * path,
			   GtkTreeIter * iter, gpointer userdata)
{
  gchar *dst;
  gchar *name;
  gchar *icon;
  gchar type;
  gchar *src;

  browser_get_item_info (model, iter, &icon, &name, NULL);
  type = get_type_from_inventory_icon (icon);

  if (type == ELEKTROID_DIR)
    {
      elektroid_add_upload_task_dir (name);
    }
  else
    {
      src = chain_path (local_browser.dir, name);
      dst = strdup (remote_browser.dir);
      elektroid_add_task (UPLOAD, src, dst);
      free (dst);
      free (src);
    }
  free (name);
  free (icon);
}

static void
elektroid_add_upload_tasks (GtkWidget * object, gpointer data)
{
  GtkTreeSelection *selection =
    gtk_tree_view_get_selection (GTK_TREE_VIEW (local_browser.view));

  gtk_tree_selection_selected_foreach (selection,
				       elektroid_add_upload_task, NULL);

  elektroid_run_next_task (NULL);
}

static gpointer
elektroid_download_task (gpointer data)
{
  GArray *sample;
  size_t frames;
  gchar *local_path;
  gchar *basec;
  gchar *bname;
  gchar *new_filename;

  debug_print (1, "Remote path: %s\n", active_task.src);

  basec = strdup (active_task.src);
  bname = basename (basec);
  new_filename = malloc (PATH_MAX);
  snprintf (new_filename, PATH_MAX, "%s.wav", bname);
  free (basec);
  local_path = chain_path (active_task.dst, new_filename);
  free (new_filename);

  debug_print (1, "Local path: %s\n", local_path);

  sample =
    connector_download (&connector, active_task.src, &active_task.running,
			elektroid_update_progress);
  elektroid_check_connector ();

  if (sample == NULL)
    {
      fprintf (stderr, __FILE__ ": Error while downloading.\n");
      active_task.status = COMPLETED_ERROR;
    }
  else
    {
      if (active_task.running)
	{
	  debug_print (1, "Writing to file '%s'...\n", local_path);
	  frames = sample_save (sample, local_path);
	  debug_print (1, "%zu frames written\n", frames);
	  free (local_path);

	  active_task.status = COMPLETED_OK;
	}
      else
	{
	  active_task.status = CANCELED;
	}
      g_array_free (sample, TRUE);
      if (strcmp (active_task.dst, local_browser.dir) == 0)
	{
	  g_idle_add (local_browser.load_dir, NULL);
	}
    }

  g_idle_add (elektroid_complete_running_task, NULL);
  g_idle_add (elektroid_run_next_task, NULL);
  g_idle_add (elektroid_check_task_buttons, NULL);

  return NULL;
}

static void
elektroid_add_download_task_dir (gchar * rel_dir)
{
  gchar *path;
  gchar *local_abs_dir;
  struct connector_dir_iterator *d_iter;
  gchar *remote_abs_dir = chain_path (remote_browser.dir, rel_dir);

  d_iter = connector_read_dir (&connector, remote_abs_dir);
  elektroid_check_connector ();
  if (d_iter == NULL)
    {
      fprintf (stderr, __FILE__ ": Error while opening remote %s dir.\n",
	       remote_abs_dir);
      goto cleanup_not_dir;
    }

  local_abs_dir = chain_path (local_browser.dir, rel_dir);
  if (elektroid_local_mkdir (local_abs_dir))
    {
      fprintf (stderr, __FILE__ ": Error while creating local %s dir.\n",
	       local_abs_dir);
      goto cleanup;
    }

  if (!strchr (rel_dir, '/'))
    {				//first call
      local_browser.load_dir (NULL);
    }

  while (!connector_get_next_dentry (d_iter))
    {
      if (d_iter->type == ELEKTROID_DIR)
	{
	  path = chain_path (rel_dir, d_iter->dentry);
	  elektroid_add_download_task_dir (path);
	  free (path);
	}
      else
	{
	  path = chain_path (remote_abs_dir, d_iter->dentry);
	  elektroid_add_task (DOWNLOAD, path, local_abs_dir);
	  free (path);
	}
    }

cleanup:
  free (local_abs_dir);
  connector_free_dir_iterator (d_iter);
cleanup_not_dir:
  free (remote_abs_dir);
}

static void
elektroid_add_download_task (GtkTreeModel * model,
			     GtkTreePath * path,
			     GtkTreeIter * iter, gpointer data)
{
  gchar *dst;
  gchar *name;
  gchar *icon;
  gchar type;
  gchar *src;

  browser_get_item_info (model, iter, &icon, &name, NULL);
  type = get_type_from_inventory_icon (icon);

  if (type == ELEKTROID_DIR)
    {
      elektroid_add_download_task_dir (name);
    }
  else
    {
      src = chain_path (remote_browser.dir, name);
      dst = strdup (local_browser.dir);
      elektroid_add_task (DOWNLOAD, src, dst);
      free (dst);
      free (src);
    }
  free (name);
  free (icon);
}

static void
elektroid_add_download_tasks (GtkWidget * object, gpointer data)
{
  GtkTreeSelection *selection =
    gtk_tree_view_get_selection (GTK_TREE_VIEW (remote_browser.view));

  gtk_tree_selection_selected_foreach (selection,
				       elektroid_add_download_task, NULL);

  elektroid_run_next_task (NULL);
}

static gboolean
elektroid_set_progress_value (gpointer data)
{
  GtkTreeIter iter;

  if (elektroid_get_running_task (&iter))
    {
      gtk_list_store_set (task_list_store, &iter,
			  TASK_LIST_STORE_PROGRESS_FIELD,
			  100.0 * active_task.progress, -1);
    }

  return FALSE;
}

static void
elektroid_update_progress (gdouble progress)
{
  active_task.progress = progress;
  g_idle_add (elektroid_set_progress_value, NULL);
}

static void
elektroid_set_device (GtkWidget * object, gpointer data)
{
  GtkTreeIter iter;
  GValue cardv = G_VALUE_INIT;
  guint card;

  if (gtk_combo_box_get_active_iter (devices_combo, &iter) == TRUE)
    {
      gtk_tree_model_get_value (GTK_TREE_MODEL (devices_list_store),
				&iter, 0, &cardv);

      card = g_value_get_uint (&cardv);

      if (connector_init (&connector, card) < 0)
	{
	  fprintf (stderr, __FILE__ ": Error while connecing.\n");
	}

      if (elektroid_check_connector ())
	{
	  strcpy (remote_browser.dir, "/");
	  remote_browser.load_dir (NULL);
	}
    }
}

static void
elektroid_quit ()
{
  elektroid_stop_task_thread ();
  elektroid_stop_load_thread ();
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
      fprintf (stderr, __FILE__ ": Path too long.\n");
      return -1;
    }

  if (snprintf
      (css_file, PATH_MAX, "%s/%s/res/gui.css", DATADIR, PACKAGE) >= PATH_MAX)
    {
      fprintf (stderr, __FILE__ ": Path too long.\n");
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

  about_button =
    GTK_WIDGET (gtk_builder_get_object (builder, "about_button"));

  hostname_label =
    GTK_WIDGET (gtk_builder_get_object (builder, "hostname_label"));

  remote_box = GTK_WIDGET (gtk_builder_get_object (builder, "remote_box"));
  waveform_draw_area =
    GTK_WIDGET (gtk_builder_get_object (builder, "waveform_draw_area"));
  play_button = GTK_WIDGET (gtk_builder_get_object (builder, "play_button"));
  stop_button = GTK_WIDGET (gtk_builder_get_object (builder, "stop_button"));
  loop_button = GTK_WIDGET (gtk_builder_get_object (builder, "loop_button"));
  autoplay_switch =
    GTK_WIDGET (gtk_builder_get_object (builder, "autoplay_switch"));
  volume_button =
    GTK_WIDGET (gtk_builder_get_object (builder, "volume_button"));
  upload_button =
    GTK_WIDGET (gtk_builder_get_object (builder, "upload_button"));
  download_button =
    GTK_WIDGET (gtk_builder_get_object (builder, "download_button"));
  status_bar = GTK_STATUSBAR (gtk_builder_get_object (builder, "status_bar"));

  item_popmenu =
    GTK_WIDGET (gtk_builder_get_object (builder, "item_popmenu"));
  rename_button =
    GTK_WIDGET (gtk_builder_get_object (builder, "rename_button"));
  delete_button =
    GTK_WIDGET (gtk_builder_get_object (builder, "delete_button"));

  g_signal_connect (main_window, "delete-event",
		    G_CALLBACK (elektroid_delete_window), NULL);

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
  g_signal_connect (download_button, "clicked",
		    G_CALLBACK (elektroid_add_download_tasks), NULL);
  g_signal_connect (upload_button, "clicked",
		    G_CALLBACK (elektroid_add_upload_tasks), NULL);

  g_signal_connect (rename_button, "clicked",
		    G_CALLBACK (elektroid_rename_item), NULL);
  g_signal_connect (delete_button, "clicked",
		    G_CALLBACK (elektroid_delete_files), NULL);

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
    .copy_button = download_button,
    .dir_entry =
      GTK_ENTRY (gtk_builder_get_object (builder, "remote_dir_entry")),
    .dir = malloc (PATH_MAX),
    .load_dir = elektroid_load_remote_dir,
    .check_selection = elektroid_remote_check_selection,
    .rename = elektroid_remote_rename,
    .delete = elektroid_remote_delete,
    .mkdir = elektroid_remote_mkdir
  };

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
		    G_CALLBACK (elektroid_show_item_popup), &remote_browser);

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
    .copy_button = upload_button,
    .dir_entry =
      GTK_ENTRY (gtk_builder_get_object (builder, "local_dir_entry")),
    .dir = malloc (PATH_MAX),
    .load_dir = elektroid_load_local_dir,
    .check_selection = elektroid_local_check_selection,
    .rename = elektroid_local_rename,
    .delete = elektroid_local_delete,
    .mkdir = elektroid_local_mkdir
  };

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
		    G_CALLBACK (elektroid_show_item_popup), &local_browser);

  sortable = GTK_TREE_SORTABLE (gtk_tree_view_get_model (local_browser.view));
  gtk_tree_sortable_set_sort_func (sortable, BROWSER_LIST_STORE_NAME_FIELD,
				   browser_sort, NULL, NULL);
  gtk_tree_sortable_set_sort_column_id (sortable,
					BROWSER_LIST_STORE_NAME_FIELD,
					GTK_TREE_SORTABLE_DEFAULT_SORT_COLUMN_ID);

  sortable =
    GTK_TREE_SORTABLE (gtk_tree_view_get_model (remote_browser.view));
  gtk_tree_sortable_set_sort_func (sortable, BROWSER_LIST_STORE_NAME_FIELD,
				   browser_sort, NULL, NULL);
  gtk_tree_sortable_set_sort_column_id (sortable,
					BROWSER_LIST_STORE_NAME_FIELD,
					GTK_TREE_SORTABLE_DEFAULT_SORT_COLUMN_ID);

  audio_init (&audio, elektroid_set_volume_callback);

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
		    G_CALLBACK (elektroid_cancel_running_task), NULL);
  g_signal_connect (remove_tasks_button, "clicked",
		    G_CALLBACK (elektroid_remove_queued_tasks), NULL);
  g_signal_connect (clear_tasks_button, "clicked",
		    G_CALLBACK (elektroid_clear_finished_tasks), NULL);

  gtk_statusbar_push (status_bar, 0, _("Not connected"));
  elektroid_loop_clicked (loop_button, NULL);
  autoplay = gtk_switch_get_active (GTK_SWITCH (autoplay_switch));

  elektroid_load_devices (1);

  gethostname (hostname, LABEL_MAX);
  gtk_label_set_text (GTK_LABEL (hostname_label), hostname);

  strcpy (local_browser.dir, local_dir);
  free (local_dir);
  local_browser.load_dir (NULL);

  gtk_widget_show (main_window);
  gtk_main ();

  free (remote_browser.dir);
  free (local_browser.dir);

  if (connector_check (&connector))
    {
      connector_destroy (&connector);
    }

  audio_destroy (&audio);
  return 0;
}

static gboolean
elektroid_end (gpointer data)
{
  elektroid_quit ();
  return FALSE;
}

int
main (int argc, char *argv[])
{
  gchar c;
  gchar *exec_name;
  gchar *local_dir = NULL;
  int vflg = 0, dflg = 0, errflg = 0;

  g_unix_signal_add (SIGHUP, elektroid_end, NULL);
  g_unix_signal_add (SIGINT, elektroid_end, NULL);
  g_unix_signal_add (SIGTERM, elektroid_end, NULL);

  setlocale (LC_ALL, "");
  bindtextdomain (PACKAGE, LOCALEDIR);
  textdomain (PACKAGE);

  while ((c = getopt (argc, argv, "l:v")) != -1)
    {
      switch (c)
	{
	case 'l':
	  local_dir = optarg;
	  dflg++;
	  break;
	case 'v':
	  vflg++;
	  break;
	case '?':
	  fprintf (stderr, "Unrecognized option: -%c\n", optopt);
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
      fprintf (stderr, "%s\n", PACKAGE_STRING);
      exec_name = basename (argv[0]);
      fprintf (stderr, "Usage: %s [-l local-dir] [-v]\n", exec_name);
      exit (EXIT_FAILURE);
    }

  local_dir = get_local_startup_path (local_dir);
  return elektroid_run (argc, argv, local_dir);
}
