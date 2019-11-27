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
#include <libgen.h>
#include <wordexp.h>
#include "connector.h"
#include <sys/stat.h>
#include <sys/types.h>
#include <glib-unix.h>
#include <glib/gi18n.h>
#include "audio.h"
#include "sample.h"
#include "utils.h"

#define MAX_DRAW_X 10000
#define SIZE_LABEL_LEN 16

#define DEVICES_LIST_STORE_CARD_FIELD 0
#define DEVICES_LIST_STORE_NAME_FIELD 1

#define BROWSER_LIST_STORE_ICON_TYPE_FIELD 0
#define BROWSER_LIST_STORE_NAME_FIELD 1
#define BROWSER_LIST_STORE_SIZE_FIELD 2
#define BROWSER_LIST_STORE_HUMAN_SIZE_FIELD 3

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

struct elektroid_browser
{
  gboolean (*load_dir) (gpointer);
  gboolean (*file_selected) (gpointer);
  gboolean (*file_unselected) (gpointer);
  gint (*mkdir) (const gchar *);
  gint (*rename) (const gchar *, const gchar *);
  gint (*delete) (const gchar *, const gchar);
  GtkTreeView *view;
  GtkWidget *up_button;
  GtkWidget *add_dir_button;
  GtkWidget *refresh_button;
  GtkWidget *copy_button;
  GtkEntry *dir_entry;
  gchar *dir;
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

struct elektroid_active_task
{
  gint running;
  gchar *src;			//Contains a path to a file
  gchar *dst;			//Contains a path to a dir
  enum elektroid_task_status status;	//Contains the final status
};

static struct elektroid_browser remote_browser;
static struct elektroid_browser local_browser;
static struct elektroid_browser *popup_elektroid_browser;

static struct audio audio;
static struct connector connector;
static gboolean autoplay;

static GThread *load_thread = NULL;
static GThread *progress_thread = NULL;
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

      elektroid_load_devices (0);
    }

  elektroid_update_statusbar ();

  return status;
}

static void
elektroid_refresh_devices (GtkWidget * object, gpointer user_data)
{
  if (connector_check (&connector))
    {
      connector_destroy (&connector);
      elektroid_check_connector ();
    }
  elektroid_load_devices (0);
}

static void
elektroid_show_about (GtkWidget * object, gpointer user_data)
{
  gtk_dialog_run (GTK_DIALOG (about_dialog));
  gtk_widget_hide (GTK_WIDGET (about_dialog));
}

static gint
electra_browser_sort (GtkTreeModel * model,
		      GtkTreeIter * a, GtkTreeIter * b, gpointer userdata)
{
  gint ret = 0;
  gchar *type1, *type2;
  gchar *name1, *name2;

  gtk_tree_model_get (model, a, 0, &type1, -1);
  gtk_tree_model_get (model, b, 0, &type2, -1);

  if (type1 == NULL || type2 == NULL)
    {
      if (type1 == NULL && type2 == NULL)
	{
	  ret = 0;
	}
      else
	{
	  if (type1 == NULL)
	    {
	      ret = -1;
	      g_free (type2);
	    }
	  else
	    {
	      ret = 1;
	      g_free (type1);
	    }
	}
    }
  else
    {
      ret = g_utf8_collate (type1, type2);
      g_free (type1);
      g_free (type2);
      if (ret == 0)
	{
	  gtk_tree_model_get (model, a, 1, &name1, -1);
	  gtk_tree_model_get (model, b, 1, &name2, -1);
	  if (name1 == NULL || name2 == NULL)
	    {
	      if (name1 == NULL && name2 == NULL)
		{
		  ret = 0;
		}
	      else
		{
		  if (name1 == NULL)
		    {
		      ret = -1;
		      g_free (name2);
		    }
		  else
		    {
		      ret = 1;
		      g_free (name1);
		    }
		}
	    }
	  else
	    {
	      ret = g_utf8_collate (name1, name2);
	      g_free (name1);
	      g_free (name2);
	    }
	}
    }

  return ret;
}

static void
elektroid_get_browser_selected_info (struct elektroid_browser *ebrowser,
				     char **type, char **name, gint * size)
{
  GtkTreeIter iter;
  GtkTreeSelection *selection = gtk_tree_view_get_selection (ebrowser->view);
  GtkTreeModel *model = gtk_tree_view_get_model (ebrowser->view);
  GList *paths = gtk_tree_selection_get_selected_rows (selection, &model);

  if (!paths
      || !gtk_tree_model_get_iter (model, &iter, g_list_nth_data (paths, 0)))
    {
      g_list_free_full (paths, (GDestroyNotify) gtk_tree_path_free);
      if (type)
	{
	  *type = NULL;
	}
      if (name)
	{
	  *name = NULL;
	}
      if (size)
	{
	  size = NULL;
	}
      return;
    }

  g_list_free_full (paths, (GDestroyNotify) gtk_tree_path_free);

  if (type)
    {
      gtk_tree_model_get (model, &iter, BROWSER_LIST_STORE_ICON_TYPE_FIELD,
			  type, -1);
    }
  if (name)
    {
      gtk_tree_model_get (model, &iter, BROWSER_LIST_STORE_NAME_FIELD, name,
			  -1);
    }
  if (size)
    {
      gtk_tree_model_get (model, &iter, BROWSER_LIST_STORE_SIZE_FIELD, size,
			  -1);
    }
}

static char *
elektroid_get_browser_selected_path (struct elektroid_browser *ebrowser)
{
  char *name;
  char *path;

  elektroid_get_browser_selected_info (ebrowser, NULL, &name, NULL);
  if (name)
    {
      path = chain_path (ebrowser->dir, name);
      g_free (name);
      return path;
    }
  else
    {
      return NULL;
    }
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
      if (connector_check (&connector))
	{
	  gtk_widget_set_sensitive (upload_button, TRUE);
	}
      if (autoplay)
	{
	  audio_play (&audio);
	}
    }
  return FALSE;
}

static void
elektroid_delete_item (GtkWidget * object, gpointer user_data)
{
  char *path;
  char *name;
  char *icon;
  gint err;
  GtkWidget *dialog;
  char type;

  elektroid_get_browser_selected_info (popup_elektroid_browser, &icon, &name,
				       NULL);

  type = get_type_from_inventory_icon (icon);

  path = chain_path (popup_elektroid_browser->dir, name);
  err = popup_elektroid_browser->delete (path, type);

  if (err < 0)
    {
      dialog =
	gtk_message_dialog_new (GTK_WINDOW (main_window),
				GTK_DIALOG_DESTROY_WITH_PARENT |
				GTK_DIALOG_MODAL,
				GTK_MESSAGE_ERROR,
				GTK_BUTTONS_CLOSE,
				"Error while deleting “%s”: %s",
				path, g_strerror (errno));
      gtk_dialog_run (GTK_DIALOG (dialog));
      gtk_widget_destroy (dialog);
    }
  else
    {
      g_idle_add (popup_elektroid_browser->load_dir, NULL);
    }

  g_free (icon);
  g_free (path);
}

static void
elektroid_rename_item (GtkWidget * object, gpointer user_data)
{
  char *old_name;
  char *old_path;
  char *new_path;
  int result;
  gint err;
  GtkWidget *dialog;

  elektroid_get_browser_selected_info (popup_elektroid_browser, NULL,
				       &old_name, NULL);
  old_path = chain_path (popup_elektroid_browser->dir, old_name);

  gtk_entry_set_text (name_dialog_entry, old_name);
  gtk_widget_grab_focus (GTK_WIDGET (name_dialog_entry));
  gtk_widget_set_sensitive (name_dialog_accept_button, FALSE);

  gtk_window_set_title (GTK_WINDOW (name_dialog), "Rename");

  result = GTK_RESPONSE_ACCEPT;

  err = -1;
  while (err < 0 && result == GTK_RESPONSE_ACCEPT)
    {
      result = gtk_dialog_run (GTK_DIALOG (name_dialog));

      if (result == GTK_RESPONSE_ACCEPT)
	{
	  new_path =
	    chain_path (popup_elektroid_browser->dir,
			gtk_entry_get_text (name_dialog_entry));

	  err = popup_elektroid_browser->rename (old_path, new_path);

	  if (err < 0)
	    {
	      dialog =
		gtk_message_dialog_new (GTK_WINDOW (name_dialog),
					GTK_DIALOG_DESTROY_WITH_PARENT |
					GTK_DIALOG_MODAL,
					GTK_MESSAGE_ERROR,
					GTK_BUTTONS_CLOSE,
					"Error while renaming to “%s”: %s",
					new_path, g_strerror (errno));
	      gtk_dialog_run (GTK_DIALOG (dialog));
	      gtk_widget_destroy (dialog);
	    }
	  else
	    {
	      g_idle_add (popup_elektroid_browser->load_dir, NULL);
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
			   gpointer user_data)
{
  GdkRectangle rect;
  GtkTreePath *path;
  struct elektroid_browser *ebrowser = user_data;
  popup_elektroid_browser = ebrowser;

  if (event->type == GDK_BUTTON_PRESS && event->button == 3)
    {
      gtk_tree_view_get_path_at_pos (ebrowser->view,
				     event->x,
				     event->y, &path, NULL, NULL, NULL);
      gtk_tree_view_get_background_area (ebrowser->view, path, NULL, &rect);
      gtk_tree_path_free (path);
      rect.x = event->x;
      rect.y = rect.y + rect.height;
      gtk_popover_set_pointing_to (GTK_POPOVER (item_popmenu), &rect);
      gtk_popover_set_relative_to (GTK_POPOVER (item_popmenu), treeview);
      gtk_popover_popup (GTK_POPOVER (item_popmenu));
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
elektroid_update_progress_redraw (gdouble percent)
{
  g_idle_add (elektroid_queue_draw_waveform, NULL);
}

static gpointer
elektroid_load_sample (gpointer data)
{
  sample_load (audio.sample, &load_mutex, &audio.frames, data,
	       &load_thread_running, elektroid_update_progress_redraw);
  free (data);
  g_idle_add (elektroid_update_ui_after_load, NULL);
  return NULL;
}

static void
elektroid_start_load_thread (char *path)
{
  debug_print (1, "Creating load thread...\n");
  load_thread_running = 1;
  load_thread =
    g_thread_new ("elektroid_load_sample", elektroid_load_sample, path);
}

static void
elektroid_join_progress_thread ()
{
  debug_print (2, "Joining progress thread...\n");
  if (progress_thread)
    {
      g_thread_join (progress_thread);
      g_thread_unref (progress_thread);
    }
  progress_thread = NULL;
}

static void
elektroid_stop_progress_thread ()
{
  debug_print (1, "Stopping progress thread...\n");
  active_task.running = 0;
  elektroid_join_progress_thread ();
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

static gboolean
elektroid_remote_file_unselected (gpointer data)
{
  gtk_widget_set_sensitive (download_button, FALSE);
  return FALSE;
}

static gboolean
elektroid_remote_file_selected (gpointer data)
{
  gtk_widget_set_sensitive (download_button, TRUE);
  return FALSE;
}

static gboolean
elektroid_local_file_unselected (gpointer data)
{
  audio_stop (&audio);
  elektroid_stop_load_thread ();
  audio_reset_sample (&audio);
  gtk_widget_queue_draw (waveform_draw_area);
  gtk_widget_set_sensitive (upload_button, FALSE);
  elektroid_controls_set_sensitive (FALSE);
  return FALSE;
}

static gboolean
elektroid_local_file_selected (gpointer data)
{
  char *path = elektroid_get_browser_selected_path (&local_browser);
  if (!path)
    {
      return FALSE;
    }

  elektroid_local_file_unselected (NULL);
  elektroid_start_load_thread (path);
  return FALSE;
}

static gboolean
elektroid_draw_waveform (GtkWidget * widget, cairo_t * cr, gpointer user_data)
{
  guint width, height;
  GdkRGBA color;
  GtkStyleContext *context;
  int i, x_widget, x_data;
  double x_ratio, mid_y;
  short *data;

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

  data = (short *) audio.sample->data;
  x_ratio = audio.frames / (double) MAX_DRAW_X;
  for (i = 0; i < MAX_DRAW_X; i++)
    {
      x_data = i * x_ratio;
      if (x_data < audio.sample->len)
	{
	  x_widget = i * ((double) width) / MAX_DRAW_X;
	  cairo_move_to (cr, x_widget, mid_y);
	  cairo_line_to (cr, x_widget,
			 mid_y - mid_y * (data[x_data] / (float) SHRT_MIN));
	  cairo_stroke (cr);
	}
    }

cleanup:
  g_mutex_unlock (&load_mutex);
  return FALSE;
}

static void
elektroid_play_clicked (GtkWidget * object, gpointer user_data)
{
  audio_play (&audio);
}

static void
elektroid_stop_clicked (GtkWidget * object, gpointer user_data)
{
  audio_stop (&audio);
}

static void
elektroid_loop_clicked (GtkWidget * object, gpointer user_data)
{
  audio.loop = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (object));
}

static gboolean
elektroid_autoplay_clicked (GtkWidget * object, gboolean state,
			    gpointer user_data)
{
  autoplay = state;
  return FALSE;
}

static void
elektroid_set_volume (GtkScaleButton * button,
		      gdouble value, gpointer user_data)
{
  audio_set_volume (&audio, value);
}

static void
elektroid_set_volume_callback (gdouble value)
{
  gtk_scale_button_set_value (GTK_SCALE_BUTTON (volume_button), value);
}

static void
elektroid_add_dentry_item (struct elektroid_browser *ebrowser,
			   const gchar type, const gchar * name, ssize_t size)
{
  const gchar *type_icon;
  char human_size[SIZE_LABEL_LEN];
  GtkListStore *list_store =
    GTK_LIST_STORE (gtk_tree_view_get_model (ebrowser->view));

  type_icon = get_inventory_icon_from_type (type);

  if (size > 0)
    {
      snprintf (human_size, SIZE_LABEL_LEN, "%.2fMB",
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

  gtk_entry_set_text (remote_browser.dir_entry, remote_browser.dir);

  gtk_widget_set_sensitive (download_button, FALSE);

  gtk_list_store_clear (list_store);
  gtk_tree_view_columns_autosize (remote_browser.view);

  d_iter = connector_read_dir (&connector, remote_browser.dir);
  elektroid_check_connector ();
  if (d_iter == NULL)
    {
      return FALSE;
    }

  while (!connector_get_next_dentry (d_iter))
    {
      elektroid_add_dentry_item (&remote_browser, d_iter->type,
				 d_iter->dentry, d_iter->size);
    }
  connector_free_dir_iterator (d_iter);

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

  elektroid_local_file_unselected (NULL);

  gtk_entry_set_text (local_browser.dir_entry, local_browser.dir);

  gtk_list_store_clear (list_store);
  gtk_tree_view_columns_autosize (local_browser.view);

  if ((dir = opendir (local_browser.dir)) != NULL)
    {
      path = malloc (PATH_MAX);
      while ((dirent = readdir (dir)) != NULL)
	{
	  if (dirent->d_name[0] != '.')
	    {
	      if (dirent->d_type == DT_DIR
		  || (dirent->d_type == DT_REG
		      && elektroid_valid_file (dirent->d_name)))
		{
		  if (dirent->d_type == DT_DIR)
		    {
		      type = 'D';
		      size = -1;
		    }
		  else
		    {
		      type = 'F';
		      snprintf (path, PATH_MAX, "%s/%s", local_browser.dir,
				dirent->d_name);
		      if (stat (path, &st) == 0)
			{
			  size = st.st_size;
			}
		      else
			{
			  size = -1;
			}
		    }
		  elektroid_add_dentry_item (&local_browser, type,
					     dirent->d_name, size);
		}
	    }
	}
      closedir (dir);
      free (path);
    }

  else
    {
      fprintf (stderr, __FILE__ ": Error while opening dir.\n");
    }

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
elektroid_add_dir (GtkWidget * object, gpointer user_data)
{
  char *pathname;
  int result;
  gint err;
  GtkWidget *dialog;
  struct elektroid_browser *ebrowser = user_data;

  gtk_entry_set_text (name_dialog_entry, "");
  gtk_widget_grab_focus (GTK_WIDGET (name_dialog_entry));
  gtk_widget_set_sensitive (name_dialog_accept_button, FALSE);

  gtk_window_set_title (GTK_WINDOW (name_dialog), "Add directory");

  result = GTK_RESPONSE_ACCEPT;

  err = -1;
  while (err < 0 && result == GTK_RESPONSE_ACCEPT)
    {
      result = gtk_dialog_run (GTK_DIALOG (name_dialog));

      if (result == GTK_RESPONSE_ACCEPT)
	{
	  pathname =
	    chain_path (ebrowser->dir,
			gtk_entry_get_text (name_dialog_entry));

	  err = ebrowser->mkdir (pathname);

	  if (err < 0)
	    {
	      dialog =
		gtk_message_dialog_new (GTK_WINDOW (name_dialog),
					GTK_DIALOG_DESTROY_WITH_PARENT |
					GTK_DIALOG_MODAL,
					GTK_MESSAGE_ERROR,
					GTK_BUTTONS_CLOSE,
					"Error while creating dir “%s”: %s",
					pathname, g_strerror (errno));
	      gtk_dialog_run (GTK_DIALOG (dialog));
	      gtk_widget_destroy (dialog);
	    }
	  else
	    {
	      g_idle_add (ebrowser->load_dir, NULL);
	    }

	  free (pathname);
	}
    }

  gtk_widget_hide (GTK_WIDGET (name_dialog));
}

static void
elektroid_accept_name (GtkWidget * object, gpointer user_data)
{
  gtk_dialog_response (name_dialog, GTK_RESPONSE_ACCEPT);
}

static void
elektroid_cancel_name (GtkWidget * object, gpointer user_data)
{
  gtk_dialog_response (name_dialog, GTK_RESPONSE_CANCEL);
}

static void
elektroid_name_dialog_entry_changed (GtkWidget * object, gpointer user_data)
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
  if (type == 'F')
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
  if (type == 'F')
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
  gboolean found;
  GtkTreeIter iter;
  gboolean valid =
    gtk_tree_model_get_iter_first (GTK_TREE_MODEL (task_list_store), &iter);

  found = FALSE;
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
      active_task.running = 1;
      active_task.src = src;
      active_task.dst = dst;
      debug_print (1, "Launching task type %d from %s to %s...\n", type,
		   active_task.src, active_task.dst);
      if (type == UPLOAD)
	{
	  progress_thread =
	    g_thread_new ("elektroid_upload_task", elektroid_upload_task,
			  NULL);
	}
      else if (type == DOWNLOAD)
	{
	  progress_thread =
	    g_thread_new ("elektroid_download_task",
			  elektroid_download_task, NULL);
	}
    }

  return FALSE;
}

static gpointer
elektroid_upload_task (gpointer user_data)
{
  char *basec;
  char *bname;
  char *remote_path;
  ssize_t frames;

  debug_print (1, "Local path: %s\n", active_task.src);

  basec = strdup (active_task.src);
  bname = basename (basec);
  remove_ext (bname);
  remote_path = chain_path (active_task.dst, bname);
  free (basec);

  debug_print (1, "Remote path: %s\n", remote_path);

  frames = connector_upload (&connector, audio.sample, remote_path,
			     &active_task.running, elektroid_update_progress);
  free (remote_path);

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

  elektroid_check_connector ();

  g_idle_add (remote_browser.load_dir, NULL);

  g_idle_add (elektroid_complete_running_task, NULL);
  g_idle_add (elektroid_run_next_task, NULL);

  return NULL;
}

static void
elektroid_add_upload_task (GtkWidget * object, gpointer user_data)
{
  gchar *src = elektroid_get_browser_selected_path (&local_browser);
  gchar *dst = strdup (remote_browser.dir);
  const gchar *status = elektroid_get_human_task_status (active_task.status);

  gtk_list_store_insert_with_values (task_list_store, NULL, -1,
				     TASK_LIST_STORE_STATUS_FIELD, QUEUED,
				     TASK_LIST_STORE_TYPE_FIELD, UPLOAD,
				     TASK_LIST_STORE_SRC_FIELD, src,
				     TASK_LIST_STORE_DST_FIELD, dst,
				     TASK_LIST_STORE_PROGRESS_FIELD, 0.0,
				     TASK_LIST_STORE_STATUS_HUMAN_FIELD,
				     status,
				     TASK_LIST_STORE_TYPE_HUMAN_FIELD,
				     _("Upload"), -1);

  g_idle_add (elektroid_run_next_task, NULL);
}

static gpointer
elektroid_download_task (gpointer user_data)
{
  GArray *data;
  size_t frames;
  gchar *output_file_path;
  gchar *basec;
  gchar *bname;
  gchar *new_filename;

  data =
    connector_download (&connector, active_task.src, &active_task.running,
			elektroid_update_progress);
  elektroid_check_connector ();

  if (data == NULL)
    {
      fprintf (stderr, __FILE__ ": Error while downloading.\n");
      active_task.status = COMPLETED_ERROR;
    }
  else
    {
      if (active_task.running)
	{
	  basec = strdup (active_task.src);
	  bname = basename (basec);

	  new_filename = malloc (PATH_MAX);
	  snprintf (new_filename, PATH_MAX, "%s.wav", bname);
	  free (basec);

	  output_file_path = chain_path (active_task.dst, new_filename);
	  free (new_filename);

	  debug_print (1, "Writing to file '%s'...\n", output_file_path);
	  frames = sample_save (data, output_file_path);
	  debug_print (1, "%zu frames written\n", frames);
	  free (output_file_path);

	  active_task.status = COMPLETED_OK;
	}
      else
	{
	  active_task.status = CANCELED;
	}
      g_array_free (data, TRUE);
      if (strcmp (active_task.dst, local_browser.dir) == 0)
	{
	  g_idle_add (local_browser.load_dir, NULL);
	}
    }

  g_idle_add (elektroid_complete_running_task, NULL);
  g_idle_add (elektroid_run_next_task, NULL);

  return NULL;
}

static void
elektroid_add_download_task (GtkWidget * object, gpointer user_data)
{
  gchar *src = elektroid_get_browser_selected_path (&remote_browser);
  gchar *dst = strdup (local_browser.dir);
  const gchar *status = elektroid_get_human_task_status (active_task.status);

  gtk_list_store_insert_with_values (task_list_store, NULL, -1,
				     TASK_LIST_STORE_STATUS_FIELD, QUEUED,
				     TASK_LIST_STORE_TYPE_FIELD, DOWNLOAD,
				     TASK_LIST_STORE_SRC_FIELD, src,
				     TASK_LIST_STORE_DST_FIELD, dst,
				     TASK_LIST_STORE_PROGRESS_FIELD, 0.0,
				     TASK_LIST_STORE_STATUS_HUMAN_FIELD,
				     status,
				     TASK_LIST_STORE_TYPE_HUMAN_FIELD,
				     _("Download"), -1);

  g_idle_add (elektroid_run_next_task, NULL);
}

static void
elektroid_update_progress (gdouble progress)
{
  GtkTreeIter iter;

  if (elektroid_get_running_task (&iter))
    {
      gtk_list_store_set (task_list_store, &iter,
			  TASK_LIST_STORE_PROGRESS_FIELD, progress * 100, -1);
    }
}

static void
elektroid_tree_view_sel_changed (GtkTreeSelection * treeselection,
				 gpointer user_data)
{
  gchar *icon;
  struct elektroid_browser *ebrowser = user_data;

  elektroid_get_browser_selected_info (ebrowser, &icon, NULL, NULL);

  if (icon)
    {
      if (get_type_from_inventory_icon (icon) == 'F')
	{
	  g_idle_add (ebrowser->file_selected, NULL);
	}
      else
	{
	  g_idle_add (ebrowser->file_unselected, NULL);
	}
      g_free (icon);
    }
}

static void
elektroid_row_activated (GtkTreeView * view,
			 GtkTreePath * path,
			 GtkTreeViewColumn * column, gpointer user_data)
{
  gchar *icon;
  gchar *name;
  struct elektroid_browser *ebrowser = user_data;

  elektroid_get_browser_selected_info (ebrowser, &icon, &name, NULL);
  if (icon)
    {
      if (get_type_from_inventory_icon (icon) == 'D')
	{
	  if (strcmp (ebrowser->dir, "/") != 0)
	    {
	      strcat (ebrowser->dir, "/");
	    }
	  strcat (ebrowser->dir, name);
	  g_idle_add (ebrowser->load_dir, NULL);
	}
      g_free (icon);
      g_free (name);
    }
}

static void
elektroid_refresh (GtkWidget * object, gpointer user_data)
{
  struct elektroid_browser *ebrowser = user_data;

  g_idle_add (ebrowser->load_dir, NULL);
}

static void
elektroid_go_up (GtkWidget * object, gpointer user_data)
{
  char *dup;
  char *new_path;
  struct elektroid_browser *ebrowser = user_data;

  if (strcmp (ebrowser->dir, "/") != 0)
    {
      dup = strdup (ebrowser->dir);
      new_path = dirname (dup);
      strcpy (ebrowser->dir, new_path);
      free (dup);
    }

  gtk_widget_set_sensitive (ebrowser->copy_button, FALSE);

  g_idle_add (ebrowser->load_dir, NULL);
}

static void
elektroid_set_device (GtkWidget * object, gpointer user_data)
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
	  g_idle_add (remote_browser.load_dir, NULL);
	}
    }
}

static void
elektroid_quit ()
{
  elektroid_stop_progress_thread ();
  elektroid_stop_load_thread ();
  debug_print (1, "Quitting GTK+...\n");
  gtk_main_quit ();
}

static gboolean
elektroid_delete_window (GtkWidget * widget, GdkEvent * event,
			 gpointer user_data)
{
  elektroid_quit ();
  return FALSE;
}

static int
elektroid_run (int argc, char *argv[])
{
  GtkBuilder *builder;
  GtkTreeSortable *sortable;
  GtkWidget *name_dialog_cancel_button;
  GtkWidget *refresh_devices_button;
  GtkWidget *hostname_label;
  GtkWidget *loop_button;
  GtkWidget *autoplay_switch;
  wordexp_t exp_result;
  char *glade_file = malloc (PATH_MAX);
  char hostname[LABEL_MAX];

  if (snprintf
      (glade_file, PATH_MAX, "%s/%s/res/gui.glade", DATADIR,
       PACKAGE) >= PATH_MAX)
    {
      fprintf (stderr, __FILE__ ": Path too long.\n");
      return -1;
    }

  gtk_init (&argc, &argv);
  builder = gtk_builder_new ();
  gtk_builder_add_from_file (builder, glade_file, NULL);
  free (glade_file);

  main_window = GTK_WIDGET (gtk_builder_get_object (builder, "main_window"));

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
		    G_CALLBACK (elektroid_add_download_task), NULL);
  g_signal_connect (upload_button, "clicked",
		    G_CALLBACK (elektroid_add_upload_task), NULL);

  g_signal_connect (rename_button, "clicked",
		    G_CALLBACK (elektroid_rename_item), NULL);
  g_signal_connect (delete_button, "clicked",
		    G_CALLBACK (elektroid_delete_item), NULL);

  remote_browser = (struct elektroid_browser)
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
    .file_selected = elektroid_remote_file_selected,
    .file_unselected = elektroid_remote_file_unselected,
    .rename = elektroid_remote_rename,
    .delete = elektroid_remote_delete,
    .mkdir = elektroid_remote_mkdir
  };
  g_signal_connect (gtk_tree_view_get_selection (remote_browser.view),
		    "changed", G_CALLBACK (elektroid_tree_view_sel_changed),
		    &remote_browser);
  g_signal_connect (remote_browser.view, "row-activated",
		    G_CALLBACK (elektroid_row_activated), &remote_browser);
  g_signal_connect (remote_browser.up_button, "clicked",
		    G_CALLBACK (elektroid_go_up), &remote_browser);
  g_signal_connect (remote_browser.add_dir_button, "clicked",
		    G_CALLBACK (elektroid_add_dir), &remote_browser);
  g_signal_connect (remote_browser.refresh_button, "clicked",
		    G_CALLBACK (elektroid_refresh), &remote_browser);
  g_signal_connect (remote_browser.view, "button-press-event",
		    G_CALLBACK (elektroid_show_item_popup), &remote_browser);

  local_browser = (struct elektroid_browser)
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
    .file_selected = elektroid_local_file_selected,
    .file_unselected = elektroid_local_file_unselected,
    .rename = elektroid_local_rename,
    .delete = elektroid_local_delete,
    .mkdir = elektroid_local_mkdir
  };

  g_signal_connect (gtk_tree_view_get_selection (local_browser.view),
		    "changed", G_CALLBACK (elektroid_tree_view_sel_changed),
		    &local_browser);
  g_signal_connect (local_browser.view, "row-activated",
		    G_CALLBACK (elektroid_row_activated), &local_browser);
  g_signal_connect (local_browser.up_button, "clicked",
		    G_CALLBACK (elektroid_go_up), &local_browser);
  g_signal_connect (local_browser.add_dir_button, "clicked",
		    G_CALLBACK (elektroid_add_dir), &local_browser);
  g_signal_connect (local_browser.refresh_button, "clicked",
		    G_CALLBACK (elektroid_refresh), &local_browser);
  g_signal_connect (local_browser.view, "button-press-event",
		    G_CALLBACK (elektroid_show_item_popup), &local_browser);

  sortable = GTK_TREE_SORTABLE (gtk_tree_view_get_model (local_browser.view));
  gtk_tree_sortable_set_sort_func (sortable, 1, electra_browser_sort, NULL,
				   NULL);
  gtk_tree_sortable_set_sort_column_id (sortable, 1,
					GTK_TREE_SORTABLE_DEFAULT_SORT_COLUMN_ID);

  sortable =
    GTK_TREE_SORTABLE (gtk_tree_view_get_model (remote_browser.view));
  gtk_tree_sortable_set_sort_func (sortable, 1, electra_browser_sort, NULL,
				   NULL);
  gtk_tree_sortable_set_sort_column_id (sortable, 1,
					GTK_TREE_SORTABLE_DEFAULT_SORT_COLUMN_ID);

  audio_init (&audio, elektroid_set_volume_callback);

  wordexp ("~", &exp_result, 0);
  strcpy (local_browser.dir, exp_result.we_wordv[0]);
  wordfree (&exp_result);
  local_browser.load_dir (NULL);

  devices_list_store =
    GTK_LIST_STORE (gtk_builder_get_object (builder, "devices_list_store"));
  devices_combo =
    GTK_COMBO_BOX (gtk_builder_get_object (builder, "devices_combo"));
  refresh_devices_button =
    GTK_WIDGET (gtk_builder_get_object (builder, "refresh_devices_button"));
  g_signal_connect (devices_combo, "changed",
		    G_CALLBACK (elektroid_set_device), NULL);
  g_signal_connect (refresh_devices_button, "clicked",
		    G_CALLBACK (elektroid_refresh_devices), NULL);

  task_list_store =
    GTK_LIST_STORE (gtk_builder_get_object (builder, "task_list_store"));

  gtk_statusbar_push (status_bar, 0, _("Not connected"));
  elektroid_loop_clicked (loop_button, NULL);
  autoplay = gtk_switch_get_active (GTK_SWITCH (autoplay_switch));

  elektroid_load_devices (1);

  gethostname (hostname, LABEL_MAX);
  gtk_label_set_text (GTK_LABEL (hostname_label), hostname);

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
elektroid_end (gpointer user_data)
{
  elektroid_quit ();
  return FALSE;
}

int
main (int argc, char *argv[])
{
  char c;
  int vflg = 0, errflg = 0;

  g_unix_signal_add (SIGHUP, elektroid_end, NULL);
  g_unix_signal_add (SIGINT, elektroid_end, NULL);
  g_unix_signal_add (SIGTERM, elektroid_end, NULL);

  setlocale (LC_ALL, "");
  bindtextdomain (PACKAGE, LOCALEDIR);
  textdomain (PACKAGE);

  while ((c = getopt (argc, argv, "v")) != -1)
    {
      switch (c)
	{
	case 'v':
	  vflg++;
	  break;
	case '?':
	  fprintf (stderr, "Unrecognized option: -%c\n", optopt);
	  errflg++;
	}
    }

  if (vflg)
    {
      debug_level = vflg;
    }

  if (errflg > 0)
    {
      fprintf (stderr, "%s\n", PACKAGE_STRING);
      char *exec_name = basename (argv[0]);
      fprintf (stderr, "Usage: %s [-v]\n", exec_name);
      exit (EXIT_FAILURE);
    }

  return elektroid_run (argc, argv);
}
