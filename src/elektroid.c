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
#include "audio.h"
#include "sample.h"
#include "utils.h"

#define DIR_TYPE "gtk-directory"
#define REG_TYPE "gtk-file"
#define MAX_DRAW_X 10000
#define SIZE_LABEL_LEN 16

struct elektroid_browser
{
  gboolean (*load_dir) (gpointer);
  gboolean (*file_selected) (gpointer);
  gboolean (*file_unselected) (gpointer);
  gint (*mkdir) (gchar *);
  gint (*rename_file) (gchar *, gchar *);
  GtkTreeView *view;
  GtkWidget *up_button;
  GtkWidget *new_dir_button;
  GtkWidget *refresh_button;
  GtkWidget *copy_button;
  GtkEntry *dir_entry;
  char *dir;
};

struct elektroid_progress
{
  GtkDialog *dialog;
  GtkLabel *label;
  GtkProgressBar *progress;
  gdouble percent;
};

//TODO: add signal handler

static struct elektroid_browser remote_browser;
static struct elektroid_browser local_browser;

static struct elektroid_progress elektroid_progress;

static struct audio audio;
static struct connector connector;

static GThread *audio_thread = NULL;
static GThread *load_thread = NULL;
static GThread *progress_thread = NULL;

static int load_thread_running;

static GtkWidget *main_window;
static GtkAboutDialog *about_dialog;
static GtkDialog *name_dialog;
static GtkEntry *name_dialog_entry;
static GtkWidget *name_dialog_accept_button;
static GtkWidget *about_button;
static GtkWidget *remote_box;
static GtkWidget *waveform_draw_area;
static GtkWidget *play_button;
static GtkWidget *upload_button;
static GtkWidget *download_button;
static GtkStatusbar *status_bar;
static GtkListStore *devices_list_store;
static GtkComboBox *devices_combo;

static void
elektroid_load_devices (int auto_select)
{
  int i;
  GtkTreeIter iter;
  GArray *devices = connector_get_elektron_devices ();
  struct connector_device device;

  debug_print ("Loading devices...\n");

  gtk_list_store_clear (devices_list_store);

  for (i = 0; i < devices->len; i++)
    {
      device = g_array_index (devices, ConnectorDevice, i);
      gtk_list_store_append (devices_list_store, &iter);
      gtk_list_store_set (devices_list_store, &iter,
			  0, device.card, 1, device.name, -1);
    }

  g_array_free (devices, TRUE);

  if (auto_select && i == 1)
    {
      debug_print ("Selecting device 0...\n");
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
      snprintf (status, LABEL_MAX, "Connected to %s", connector.device_name);
      gtk_statusbar_push (status_bar, 0, status);
      free (status);
    }
  else
    {
      gtk_statusbar_push (status_bar, 0, "Not connected");
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
      gtk_tree_model_get (model, &iter, 0, type, -1);
    }
  if (name)
    {
      gtk_tree_model_get (model, &iter, 1, name, -1);
    }
  if (size)
    {
      gtk_tree_model_get (model, &iter, 2, size, -1);
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

static gpointer
elektroid_play (gpointer data)
{
  audio_play (&audio);
  return NULL;
}

static void
elektroid_audio_stop ()
{
  audio_stop (&audio);
  if (audio_thread)
    {
      g_thread_join (audio_thread);
      g_thread_unref (audio_thread);
    }
  audio_thread = NULL;
}

static void
elektroid_audio_start ()
{
  elektroid_audio_stop ();
  if (audio_check (&audio))
    {
      audio_thread = g_thread_new ("elektroid_play", elektroid_play, NULL);
    }
}

static gboolean
elektroid_close_and_exit (GtkWidget * widget, GdkEvent * event,
			  gpointer user_data)
{
  elektroid_audio_stop ();

  if (progress_thread)
    {
      g_thread_join (progress_thread);
      g_thread_unref (progress_thread);
    }

  load_thread_running = 0;
  if (load_thread)
    {
      g_thread_join (load_thread);
      g_thread_unref (load_thread);
    }

  gtk_main_quit ();
  return FALSE;
}

static gboolean
elektroid_update_ui_after_load (gpointer data)
{
  if (audio.sample->len > 0)
    {
      if (audio_check (&audio))
	{
	  gtk_widget_set_sensitive (play_button, TRUE);
	}
      if (connector_check (&connector))
	{
	  gtk_widget_set_sensitive (upload_button, TRUE);
	}
    }
  return FALSE;
}

static gboolean
elektroid_update_progress_value (gpointer data)
{
  gtk_progress_bar_set_fraction (elektroid_progress.progress,
				 elektroid_progress.percent);

  return FALSE;
}

static void
elektroid_update_progress (gdouble percent)
{
  elektroid_progress.percent = percent;
  g_idle_add (elektroid_update_progress_value, NULL);
}

static gboolean
elektroid_progress_dialog_end ()
{
  gtk_dialog_response (elektroid_progress.dialog, GTK_RESPONSE_NONE);

  return FALSE;
}

static void
elektroid_update_progress_redraw (gdouble percent)
{
  gtk_widget_queue_draw (waveform_draw_area);
}

static gpointer
elektroid_load_sample (gpointer data)
{
  elektroid_audio_stop ();
  sample_load (audio.sample, &audio.load_mutex, &audio.frames, data,
	       NULL, elektroid_update_progress_redraw);
  gtk_widget_queue_draw (waveform_draw_area);
  g_idle_add (elektroid_update_ui_after_load, NULL);
  elektroid_audio_start ();
  free (data);

  return NULL;
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
  elektroid_audio_stop ();
  g_array_set_size (audio.sample, 0);
  gtk_widget_queue_draw (waveform_draw_area);
  gtk_widget_set_sensitive (upload_button, FALSE);
  gtk_widget_set_sensitive (play_button, FALSE);

  return FALSE;
}

static const char *
elektroid_get_ext (const char *name)
{
  int namelen = strlen (name) - 1;
  int i = namelen;
  const char *ext = &name[namelen];

  while (*(ext - 1) != '.' && i > 0)
    {
      ext--;
      i--;
    }

  if (i == 0 && name[0] != '.')
    {
      return NULL;
    }
  else
    {
      return ext;
    }
}

static gboolean
elektroid_local_file_selected (gpointer data)
{
  char *label;
  char *path = elektroid_get_browser_selected_path (&local_browser);
  if (!path)
    {
      return FALSE;
    }

  load_thread_running = 0;
  if (load_thread)
    {
      g_thread_join (load_thread);
      g_thread_unref (load_thread);
    }

  gtk_widget_set_sensitive (play_button, FALSE);
  gtk_widget_set_sensitive (upload_button, FALSE);

  gtk_window_set_title (GTK_WINDOW (elektroid_progress.dialog),
			"Loading file");

  label = malloc (LABEL_MAX + PATH_MAX);
  snprintf (label, LABEL_MAX + PATH_MAX, "Loading %s...", path);
  gtk_label_set_text (elektroid_progress.label, label);
  free (label);

  load_thread =
    g_thread_new ("elektroid_load_sample", elektroid_load_sample, path);

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
  short *data = (short *) audio.sample->data;

  if (audio.sample->len <= 0)
    {
      return FALSE;
    }

  context = gtk_widget_get_style_context (widget);
  width = gtk_widget_get_allocated_width (widget);
  height = gtk_widget_get_allocated_height (widget);
  mid_y = height / 2.0;
  gtk_render_background (context, cr, 0, 0, width, height);
  gtk_style_context_get_color (context, gtk_style_context_get_state (context),
			       &color);
  gdk_cairo_set_source_rgba (cr, &color);

  g_mutex_lock (&audio.load_mutex);
  x_ratio = audio.frames / (double) MAX_DRAW_X;
  for (i = 0; i < MAX_DRAW_X; i++)
    {
      x_data = i * x_ratio;
      if (x_data <= audio.sample->len)
	{
	  x_widget = i * ((double) width) / MAX_DRAW_X;
	  cairo_move_to (cr, x_widget, mid_y);
	  cairo_line_to (cr, x_widget,
			 mid_y - mid_y * (data[x_data] / (float) SHRT_MIN));
	  cairo_stroke (cr);
	}
    }
  g_mutex_unlock (&audio.load_mutex);

  return FALSE;
}

static void
elektroid_play_clicked (GtkWidget * object, gpointer user_data)
{
  elektroid_audio_start ();
}

static void
elektroid_add_dentry_item (struct elektroid_browser *ebrowser,
			   const gchar type, const gchar * name, ssize_t size)
{
  char human_size[SIZE_LABEL_LEN];
  gchar *type_icon;
  GtkListStore *list_store =
    GTK_LIST_STORE (gtk_tree_view_get_model (ebrowser->view));

  if (type == 'D')
    {
      type_icon = DIR_TYPE;
    }
  else
    {
      type_icon = REG_TYPE;
    }

  if (size > 0)
    {
      snprintf (human_size, SIZE_LABEL_LEN, "%.2fMB",
		size / (1024.0 * 1024.0));
    }
  else
    {
      human_size[0] = 0;
    }

  gtk_list_store_insert_with_values (list_store, NULL, -1, 0, type_icon, 1,
				     name, 2, size, 3, human_size, -1);
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
  const char *ext = elektroid_get_ext (name);

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
elektroid_remote_mkdir (gchar * name)
{
  return connector_create_dir (&connector, name);
}

static gint
elektroid_local_mkdir (gchar * name)
{
  return mkdir (name, 0755);
}

static void
elektroid_new_dir (GtkWidget * object, gpointer user_data)
{
  char *pathname;
  int result;
  gint err;
  GtkWidget *dialog;
  struct elektroid_browser *ebrowser = (struct elektroid_browser *) user_data;

  gtk_entry_set_text (name_dialog_entry, "");
  gtk_widget_grab_focus (GTK_WIDGET (name_dialog_entry));
  gtk_widget_set_sensitive (name_dialog_accept_button, FALSE);

  gtk_window_set_title (GTK_WINDOW (name_dialog), "New directory");

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
elektroid_remote_rename (gchar * old, gchar * new)
{
  return connector_rename (&connector, old, new);
}

static gint
elektroid_local_rename (gchar * old, gchar * new)
{
  return rename (old, new);
}

static void
elektroid_rename_file (GtkCellRendererText * renderer,
		       gchar * path, gchar * new_name, gpointer user_data)
{
  gint res;
  char *old_name;
  char *old_path;
  char *new_path;
  GtkWidget *dialog;
  struct elektroid_browser *ebrowser = (struct elektroid_browser *) user_data;

  elektroid_get_browser_selected_info (ebrowser, NULL, &old_name, NULL);
  old_path = chain_path (ebrowser->dir, old_name);

  new_path = chain_path (ebrowser->dir, new_name);

  res = ebrowser->rename_file (old_path, new_path);

  if (res < 0)
    {
      dialog =
	gtk_message_dialog_new (GTK_WINDOW (main_window),
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
      g_idle_add (ebrowser->load_dir, NULL);
    }

  free (old_path);
  free (new_path);
}

static void
elektroid_cancel_progress (GtkWidget * object, gpointer user_data)
{
  gtk_dialog_response (elektroid_progress.dialog, GTK_RESPONSE_CANCEL);
}

static gpointer
elektroid_upload_process (gpointer user_data)
{
  char *basec;
  char *bname;
  char *remote_path;
  char *path = (char *) user_data;
  ssize_t frames;

  debug_print ("Local path: %s\n", path);

  basec = strdup (path);
  bname = basename (basec);
  remove_ext (bname);
  remote_path = chain_path (remote_browser.dir, bname);
  free (basec);

  debug_print ("Remote path: %s\n", remote_path);


  frames = connector_upload (&connector, audio.sample, remote_path,
			     &load_thread_running, elektroid_update_progress);
  debug_print ("%ld frames sent\n", frames);

  if (frames < 0)
    {
      fprintf (stderr, __FILE__ ": Error while uploading.\n");
    }

  elektroid_check_connector ();

  free (remote_path);

  if (frames == audio.sample->len && load_thread_running)
    {
      g_idle_add (remote_browser.load_dir, NULL);
    }

  g_idle_add (elektroid_progress_dialog_end, NULL);

  return NULL;
}

static void
elektroid_upload (GtkWidget * object, gpointer user_data)
{
  gint result;
  char *label;
  char *path = elektroid_get_browser_selected_path (&local_browser);

  if (!path)
    {
      return;
    }

  gtk_window_set_title (GTK_WINDOW (elektroid_progress.dialog),
			"Uploading file");

  label = malloc (LABEL_MAX + PATH_MAX);
  snprintf (label, LABEL_MAX + PATH_MAX, "Uploading %s...", path);
  gtk_label_set_text (elektroid_progress.label, label);
  free (label);

  load_thread_running = 1;

  progress_thread =
    g_thread_new ("elektroid_upload_process", elektroid_upload_process, path);

  result = gtk_dialog_run (elektroid_progress.dialog);

  if (result == GTK_RESPONSE_CANCEL || result == GTK_RESPONSE_DELETE_EVENT)
    {
      load_thread_running = 0;
    }

  g_thread_join (progress_thread);
  g_thread_unref (progress_thread);
  progress_thread = NULL;

  free (path);

  gtk_widget_hide (GTK_WIDGET (elektroid_progress.dialog));
}

static gpointer
elektroid_download_process (gpointer user_data)
{
  GArray *data;
  size_t frames;
  char *output_file_path;
  char *basec;
  char *bname;
  char *new_filename;
  char *path = (char *) user_data;

  data =
    connector_download (&connector, path, &load_thread_running,
			elektroid_update_progress);
  elektroid_check_connector ();

  if (data != NULL)
    {
      if (load_thread_running)
	{
	  basec = strdup (path);
	  bname = basename (basec);

	  new_filename = malloc (PATH_MAX);
	  snprintf (new_filename, PATH_MAX, "%s.wav", bname);
	  free (basec);

	  output_file_path = chain_path (local_browser.dir, new_filename);
	  free (new_filename);

	  debug_print ("Writing to file '%s'...\n", output_file_path);
	  frames = sample_save (data, output_file_path);
	  debug_print ("%lu frames written\n", frames);
	  free (output_file_path);
	}
      g_array_free (data, TRUE);
      g_idle_add (local_browser.load_dir, NULL);
    }

  g_idle_add (elektroid_progress_dialog_end, NULL);

  return NULL;
}

static void
elektroid_download (GtkWidget * object, gpointer user_data)
{
  gint result;
  char *label;
  char *path = elektroid_get_browser_selected_path (&remote_browser);

  if (!path)
    {
      return;
    }

  gtk_window_set_title (GTK_WINDOW (elektroid_progress.dialog),
			"Downloading file");

  label = malloc (LABEL_MAX);
  snprintf (label, LABEL_MAX, "Downloading %s.wav...", path);
  gtk_label_set_text (elektroid_progress.label, label);
  free (label);

  load_thread_running = 1;

  progress_thread =
    g_thread_new ("elektroid_download_process", elektroid_download_process,
		  path);

  result = gtk_dialog_run (elektroid_progress.dialog);

  if (result == GTK_RESPONSE_CANCEL || result == GTK_RESPONSE_DELETE_EVENT)
    {
      load_thread_running = 0;
    }

  g_thread_join (progress_thread);
  g_thread_unref (progress_thread);
  progress_thread = NULL;

  free (path);

  gtk_widget_hide (GTK_WIDGET (elektroid_progress.dialog));
}

static void
elektroid_row_selected (GtkTreeSelection * treeselection, gpointer user_data)
{
  gchar *icon;
  struct elektroid_browser *ebrowser = (struct elektroid_browser *) user_data;

  elektroid_get_browser_selected_info (ebrowser, &icon, NULL, NULL);

  if (icon)
    {
      if (strcmp (icon, REG_TYPE) == 0)
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
  struct elektroid_browser *ebrowser = (struct elektroid_browser *) user_data;

  elektroid_get_browser_selected_info (ebrowser, &icon, &name, NULL);
  if (icon)
    {
      if (strcmp (icon, DIR_TYPE) == 0)
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
  struct elektroid_browser *ebrowser = (struct elektroid_browser *) user_data;

  g_idle_add (ebrowser->load_dir, NULL);
}

static void
elektroid_go_up (GtkWidget * object, gpointer user_data)
{
  char *dup;
  char *new_path;
  struct elektroid_browser *ebrowser = (struct elektroid_browser *) user_data;

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

static int
elektroid (int argc, char *argv[])
{
  GtkBuilder *builder;
  GtkTreeSortable *sortable;
  GtkWidget *progress_dialog_cancel_button;
  GtkWidget *name_dialog_cancel_button;
  GtkWidget *refresh_devices_button;
  GtkCellRendererText *local_name_cell_renderer_text;
  GtkCellRendererText *remote_name_cell_renderer_text;
  wordexp_t exp_result;
  int err = 0;
  char *glade_file = malloc (PATH_MAX);

  audio_init (&audio);

  if (snprintf (glade_file, PATH_MAX, "%s/%s/res/gui.glade", DATADIR, PACKAGE)
      >= PATH_MAX)
    {
      fprintf (stderr, __FILE__ ": Path too long.\n");
      err = -1;
      goto free_audio;
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

  remote_box = GTK_WIDGET (gtk_builder_get_object (builder, "remote_box"));
  waveform_draw_area =
    GTK_WIDGET (gtk_builder_get_object (builder, "waveform_draw_area"));
  play_button = GTK_WIDGET (gtk_builder_get_object (builder, "play_button"));
  upload_button =
    GTK_WIDGET (gtk_builder_get_object (builder, "upload_button"));
  download_button =
    GTK_WIDGET (gtk_builder_get_object (builder, "download_button"));
  status_bar = GTK_STATUSBAR (gtk_builder_get_object (builder, "status_bar"));

  progress_dialog_cancel_button =
    GTK_WIDGET (gtk_builder_get_object
		(builder, "progress_dialog_cancel_button"));

  local_name_cell_renderer_text =
    GTK_CELL_RENDERER_TEXT (gtk_builder_get_object
			    (builder, "local_name_cell_renderer_text"));
  remote_name_cell_renderer_text =
    GTK_CELL_RENDERER_TEXT (gtk_builder_get_object
			    (builder, "remote_name_cell_renderer_text"));

  g_signal_connect (main_window, "delete-event",
		    G_CALLBACK (elektroid_close_and_exit), NULL);

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
  g_signal_connect (download_button, "clicked",
		    G_CALLBACK (elektroid_download), NULL);
  g_signal_connect (upload_button, "clicked", G_CALLBACK (elektroid_upload),
		    NULL);

  g_signal_connect (progress_dialog_cancel_button, "clicked",
		    G_CALLBACK (elektroid_cancel_progress), NULL);

  g_signal_connect (local_name_cell_renderer_text, "edited",
		    G_CALLBACK (elektroid_rename_file), &local_browser);
  g_signal_connect (remote_name_cell_renderer_text, "edited",
		    G_CALLBACK (elektroid_rename_file), &remote_browser);

  elektroid_progress = (struct elektroid_progress)
  {
    .dialog =
      GTK_DIALOG (gtk_builder_get_object (builder, "progress_dialog")),
    .progress =
      GTK_PROGRESS_BAR (gtk_builder_get_object
			(builder, "progress_dialog_progress")),
    .label =
      GTK_LABEL (gtk_builder_get_object (builder, "progress_dialog_label")),
  };

  remote_browser = (struct elektroid_browser)
  {
    .view =
      GTK_TREE_VIEW (gtk_builder_get_object (builder, "remote_tree_view")),
    .up_button =
      GTK_WIDGET (gtk_builder_get_object (builder, "remote_up_button")),
    .new_dir_button =
      GTK_WIDGET (gtk_builder_get_object (builder, "remote_new_dir_button")),
    .refresh_button =
      GTK_WIDGET (gtk_builder_get_object (builder, "remote_refresh_button")),
    .copy_button = download_button,
    .dir_entry =
      GTK_ENTRY (gtk_builder_get_object (builder, "remote_dir_entry")),
    .dir = malloc (PATH_MAX),
    .load_dir = elektroid_load_remote_dir,
    .file_selected = elektroid_remote_file_selected,
    .file_unselected = elektroid_remote_file_unselected,
    .rename_file = elektroid_remote_rename,
    .mkdir = elektroid_remote_mkdir
  };
  g_signal_connect (gtk_tree_view_get_selection (remote_browser.view),
		    "changed", G_CALLBACK (elektroid_row_selected),
		    &remote_browser);
  g_signal_connect (remote_browser.view, "row-activated",
		    G_CALLBACK (elektroid_row_activated), &remote_browser);
  g_signal_connect (remote_browser.up_button, "clicked",
		    G_CALLBACK (elektroid_go_up), &remote_browser);
  g_signal_connect (remote_browser.new_dir_button, "clicked",
		    G_CALLBACK (elektroid_new_dir), &remote_browser);
  g_signal_connect (remote_browser.refresh_button, "clicked",
		    G_CALLBACK (elektroid_refresh), &remote_browser);
  local_browser = (struct elektroid_browser)
  {
    .view =
      GTK_TREE_VIEW (gtk_builder_get_object (builder, "local_tree_view")),
    .up_button =
      GTK_WIDGET (gtk_builder_get_object (builder, "local_up_button")),
    .new_dir_button =
      GTK_WIDGET (gtk_builder_get_object (builder, "local_new_dir_button")),
    .refresh_button =
      GTK_WIDGET (gtk_builder_get_object (builder, "local_refresh_button")),
    .copy_button = upload_button,
    .dir_entry =
      GTK_ENTRY (gtk_builder_get_object (builder, "local_dir_entry")),
    .dir = malloc (PATH_MAX),
    .load_dir = elektroid_load_local_dir,
    .file_selected = elektroid_local_file_selected,
    .file_unselected = elektroid_local_file_unselected,
    .rename_file = elektroid_local_rename,
    .mkdir = elektroid_local_mkdir
  };

  g_signal_connect (gtk_tree_view_get_selection (local_browser.view),
		    "changed", G_CALLBACK (elektroid_row_selected),
		    &local_browser);
  g_signal_connect (local_browser.view, "row-activated",
		    G_CALLBACK (elektroid_row_activated), &local_browser);
  g_signal_connect (local_browser.up_button, "clicked",
		    G_CALLBACK (elektroid_go_up), &local_browser);
  g_signal_connect (local_browser.new_dir_button, "clicked",
		    G_CALLBACK (elektroid_new_dir), &local_browser);
  g_signal_connect (local_browser.refresh_button, "clicked",
		    G_CALLBACK (elektroid_refresh), &local_browser);

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

  gtk_statusbar_push (status_bar, 0, "Not connected");

  elektroid_load_devices (1);

  gtk_widget_show (main_window);
  gtk_main ();

  free (remote_browser.dir);
  free (local_browser.dir);

  if (connector_check (&connector))
    {
      connector_destroy (&connector);
    }

free_audio:
  audio_destroy (&audio);
  return err;
}

int
main (int argc, char *argv[])
{
  char c;
  int vflg = 0, errflg = 0;

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

  return elektroid (argc, argv);
}
