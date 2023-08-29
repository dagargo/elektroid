/*
 *   browser.c
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

#include <glib/gi18n.h>
#include "browser.h"
#include "editor.h"
#include "local.h"
#include "backend.h"

#define DND_TIMEOUT 800

extern struct browser remote_browser;
extern struct editor editor;

struct browser_add_dentry_item_data
{
  struct browser *browser;
  struct item item;
  const gchar *icon;
};

static void
browser_widget_set_sensitive (gpointer widget, gpointer data)
{
  gtk_widget_set_sensitive (GTK_WIDGET (widget), TRUE);
}

static void
browser_widget_set_insensitive (gpointer widget, gpointer data)
{
  gtk_widget_set_sensitive (GTK_WIDGET (widget), FALSE);
}

gint
browser_sort_by_name (GtkTreeModel * model,
		      GtkTreeIter * a, GtkTreeIter * b, gpointer data)
{
  struct item itema;
  struct item itemb;
  gint ret = 0;

  browser_set_item (model, a, &itema);
  browser_set_item (model, b, &itemb);

  if (itema.type == itemb.type)
    {
#if defined(__MINGW32__) | defined(__MINGW64__)
      ret = strcmp (itema.name, itemb.name);
#else
      ret = g_utf8_collate (itema.name, itemb.name);
#endif
    }
  else
    {
      ret = itema.type > itemb.type;
    }

  return ret;
}

gint
browser_sort_by_id (GtkTreeModel * model, GtkTreeIter * a, GtkTreeIter * b,
		    gpointer data)
{
  struct item itema;
  struct item itemb;
  gint ret = 0;

  browser_set_item (model, a, &itema);
  browser_set_item (model, b, &itemb);

  if (itema.type == itemb.type)
    {
      ret = itema.id > itemb.id;
    }
  else
    {
      ret = itema.type > itemb.type;
    }

  return ret;
}

void
browser_set_item (GtkTreeModel * model, GtkTreeIter * iter, struct item *item)
{
  gchar *name;
  gtk_tree_model_get (model, iter, BROWSER_LIST_STORE_TYPE_FIELD, &item->type,
		      BROWSER_LIST_STORE_NAME_FIELD, &name,
		      BROWSER_LIST_STORE_SIZE_FIELD, &item->size,
		      BROWSER_LIST_STORE_ID_FIELD, &item->id, -1);
  snprintf (item->name, LABEL_MAX, "%s", name);
  g_free (name);
}

void
browser_set_selected_row_iter (struct browser *browser, GtkTreeIter * iter)
{
  GtkTreeSelection *selection =
    gtk_tree_view_get_selection (GTK_TREE_VIEW (browser->view));
  GtkTreeModel *model =
    GTK_TREE_MODEL (gtk_tree_view_get_model (browser->view));
  GList *paths = gtk_tree_selection_get_selected_rows (selection, &model);

  gtk_tree_model_get_iter (model, iter, g_list_nth_data (paths, 0));
  g_list_free_full (paths, (GDestroyNotify) gtk_tree_path_free);
}

static void
browser_clear (struct browser *browser)
{
  GtkListStore *list_store =
    GTK_LIST_STORE (gtk_tree_view_get_model (browser->view));

  gtk_entry_set_text (browser->dir_entry, browser->dir ? browser->dir : "");
  gtk_list_store_clear (list_store);
  browser->check_selection (NULL);
}

void
browser_selection_changed (GtkTreeSelection * selection, gpointer data)
{
  struct browser *browser = data;
  g_idle_add (browser->check_selection, NULL);
}

void
browser_refresh (GtkWidget * object, gpointer data)
{
  struct browser *browser = data;
  g_idle_add (browser_load_dir, browser);
}

void
browser_go_up (GtkWidget * object, gpointer data)
{
  struct browser *browser = data;

  g_mutex_lock (&browser->mutex);
  if (!browser->loading)
    {
      if (strcmp (browser->dir, "/"))
	{
	  gchar *new_path = g_path_get_dirname (browser->dir);
	  strcpy (browser->dir, new_path);
	  g_free (new_path);
	}
    }
  g_mutex_unlock (&browser->mutex);

  g_idle_add (browser_load_dir, browser);
}

void
browser_item_activated (GtkTreeView * view, GtkTreePath * path,
			GtkTreeViewColumn * column, gpointer data)
{
  GtkTreeIter iter;
  struct item item;
  struct browser *browser = data;
  GtkTreeModel *model = GTK_TREE_MODEL (gtk_tree_view_get_model
					(browser->view));

  gtk_tree_model_get_iter (model, &iter, path);
  browser_set_item (model, &iter, &item);

  if (item.type == ELEKTROID_DIR)
    {
      enum path_type type = path_type_from_backend (browser->backend);
      gchar *new_dir = path_chain (type, browser->dir, item.name);
      g_free (browser->dir);
      browser->dir = new_dir;
      browser_load_dir (browser);
    }
}

gint
browser_get_selected_items_count (struct browser *browser)
{
  GtkTreeSelection *selection =
    gtk_tree_view_get_selection (GTK_TREE_VIEW (browser->view));
  return gtk_tree_selection_count_selected_rows (selection);
}

void
browser_clear_selection (struct browser *browser)
{
  GtkTreeSelection *selection =
    gtk_tree_view_get_selection (GTK_TREE_VIEW (browser->view));
  gtk_tree_selection_unselect_all (selection);
}

gchar *
browser_get_item_path (struct browser *browser, struct item *item)
{
  gchar *filename = get_filename (browser->fs_ops->options, item);
  enum path_type type = path_type_from_backend (browser->backend);
  gchar *path = path_chain (type, browser->dir, filename);
  debug_print (1, "Using %s path for item %s (id %d)...\n", path, item->name,
	       item->id);
  g_free (filename);
  return path;
}

static gint
browser_add_dentry_item (gpointer data)
{
  gchar *hsize;
  gchar label[LABEL_MAX];
  struct browser_add_dentry_item_data *add_data = data;
  struct browser *browser = add_data->browser;
  struct item *item = &add_data->item;
  GtkTreeIter iter, note_iter;
  GValue v = G_VALUE_INIT;
  GtkListStore *list_store =
    GTK_LIST_STORE (gtk_tree_view_get_model (browser->view));

  hsize = get_human_size (item->size, TRUE);

  gtk_list_store_insert_with_values (list_store, &iter, -1,
				     BROWSER_LIST_STORE_ICON_FIELD,
				     item->type ==
				     ELEKTROID_DIR ? DIR_ICON :
				     add_data->icon,
				     BROWSER_LIST_STORE_NAME_FIELD,
				     item->name,
				     BROWSER_LIST_STORE_SIZE_FIELD,
				     item->size,
				     BROWSER_LIST_STORE_SIZE_STR_FIELD,
				     hsize,
				     BROWSER_LIST_STORE_TYPE_FIELD,
				     item->type,
				     BROWSER_LIST_STORE_ID_FIELD,
				     item->id, -1);
  g_free (hsize);

  if (browser->fs_ops->options & FS_OPTION_SLOT_STORAGE)
    {
      if (browser->fs_ops->get_slot)
	{
	  gchar *s = browser->fs_ops->get_slot (item, browser->backend);
	  g_value_init (&v, G_TYPE_STRING);
	  g_value_set_string (&v, s);
	  gtk_list_store_set_value (list_store, &iter,
				    BROWSER_LIST_STORE_SLOT_FIELD, &v);
	  g_free (s);
	  g_value_unset (&v);
	}
    }

  if (item->type == ELEKTROID_FILE &&
      browser->fs_ops->options & FS_OPTION_SAMPLE_ATTRS &&
      item->sample_info.frames)
    {
      snprintf (label, LABEL_MAX, "%u", item->sample_info.frames);
      g_value_init (&v, G_TYPE_STRING);
      g_value_set_string (&v, label);
      gtk_list_store_set_value (list_store, &iter,
				BROWSER_LIST_STORE_SAMPLE_FRAMES_FIELD, &v);
      g_value_unset (&v);

      snprintf (label, LABEL_MAX, "%.2f kHz",
		item->sample_info.rate / 1000.0);
      g_value_init (&v, G_TYPE_STRING);
      g_value_set_string (&v, label);
      gtk_list_store_set_value (list_store, &iter,
				BROWSER_LIST_STORE_SAMPLE_RATE_FIELD, &v);
      g_value_unset (&v);

      gdouble time =
	item->sample_info.frames / (gdouble) item->sample_info.rate;
      if (time >= 60)
	{
	  snprintf (label, LABEL_MAX, "%.2f %s", time / 60.0, _("min."));
	}
      else
	{
	  snprintf (label, LABEL_MAX, "%.2f s", time);
	}
      g_value_init (&v, G_TYPE_STRING);
      g_value_set_string (&v, label);
      gtk_list_store_set_value (list_store, &iter,
				BROWSER_LIST_STORE_SAMPLE_TIME_FIELD, &v);
      g_value_unset (&v);

      snprintf (label, LABEL_MAX, "%u", item->sample_info.bits);
      g_value_init (&v, G_TYPE_STRING);
      g_value_set_string (&v, label);
      gtk_list_store_set_value (list_store, &iter,
				BROWSER_LIST_STORE_SAMPLE_BITS_FIELD, &v);
      g_value_unset (&v);

      snprintf (label, LABEL_MAX, "%u", item->sample_info.channels);
      g_value_init (&v, G_TYPE_STRING);
      g_value_set_string (&v, label);
      gtk_list_store_set_value (list_store, &iter,
				BROWSER_LIST_STORE_SAMPLE_CHANNELS_FIELD, &v);
      g_value_unset (&v);

      gtk_tree_model_get_iter_first (GTK_TREE_MODEL (editor.notes_list_store),
				     &note_iter);
      if (item->sample_info.midi_note <= 127)
	{
	  for (gint i = 0; i < item->sample_info.midi_note; i++)
	    {
	      gtk_tree_model_iter_next (GTK_TREE_MODEL
					(editor.notes_list_store),
					&note_iter);
	    }
	  gtk_tree_model_get_value (GTK_TREE_MODEL (editor.notes_list_store),
				    &note_iter, 0, &v);
	}
      else
	{
	  g_value_init (&v, G_TYPE_STRING);
	  g_value_set_string (&v, "-");
	}
      gtk_list_store_set_value (list_store, &iter,
				BROWSER_LIST_STORE_SAMPLE_MIDI_NOTE_FIELD,
				&v);
      g_value_unset (&v);
    }

  g_free (add_data);

  return G_SOURCE_REMOVE;
}

static gboolean
browser_load_dir_runner_hide_spinner (gpointer data)
{
  struct browser *browser = data;
  gtk_spinner_stop (GTK_SPINNER (browser->spinner));
  gtk_stack_set_visible_child_name (GTK_STACK (browser->stack), "list");
  return FALSE;
}

static gboolean
browser_load_dir_runner_show_spinner_and_lock_browser (gpointer data)
{
  struct browser *browser = data;
  g_slist_foreach (browser->sensitive_widgets, browser_widget_set_insensitive,
		   NULL);
  gtk_stack_set_visible_child_name (GTK_STACK (browser->stack), "spinner");
  gtk_spinner_start (GTK_SPINNER (browser->spinner));
  return FALSE;
}

static gboolean
browser_load_dir_runner_update_ui (gpointer data)
{
  struct browser *browser = data;
  gboolean active = !browser->backend
    || browser->backend->type == BE_TYPE_SYSTEM;

  notifier_set_active (browser->notifier, active);

  g_thread_join (browser->thread);
  browser->thread = NULL;

  if (browser->check_callback)
    {
      browser->check_callback ();
    }
  gtk_tree_view_columns_autosize (browser->view);

  g_mutex_lock (&browser->mutex);
  browser->loading = FALSE;
  g_mutex_unlock (&browser->mutex);

  gtk_widget_grab_focus (GTK_WIDGET (browser->view));

  //Unlock browser
  g_slist_foreach (browser->sensitive_widgets, browser_widget_set_sensitive,
		   NULL);

  return FALSE;
}

static gpointer
browser_load_dir_runner (gpointer data)
{
  gint err;
  struct browser *browser = data;
  struct item_iterator iter;
  const gchar **extensions = NULL;
  const gchar *icon = browser->fs_ops->gui_icon;

  if (browser->fs_ops == &FS_LOCAL_GENERIC_OPERATIONS &&
      remote_browser.fs_ops->get_ext)
    {
      extensions = g_malloc (sizeof (gchar *) * 2);
      extensions[0] = remote_browser.fs_ops->get_ext (remote_browser.backend,
						      remote_browser.fs_ops);
      extensions[1] = NULL;
      icon = remote_browser.fs_ops->gui_icon;
    }

  g_idle_add (browser_load_dir_runner_show_spinner_and_lock_browser, browser);
  err = browser->fs_ops->readdir (browser->backend, &iter, browser->dir,
				  extensions);
  g_idle_add (browser_load_dir_runner_hide_spinner, browser);
  if (err)
    {
      error_print ("Error while opening '%s' dir\n", browser->dir);
      goto end;
    }

  while (!next_item_iterator (&iter))
    {
      struct browser_add_dentry_item_data *data =
	g_malloc (sizeof (struct browser_add_dentry_item_data));
      data->browser = browser;
      memcpy (&data->item, &iter.item, sizeof (struct item));
      data->icon = icon;
      g_idle_add (browser_add_dentry_item, data);
    }
  free_item_iterator (&iter);

end:
  g_idle_add (browser_load_dir_runner_update_ui, browser);
  g_free (extensions);
  return NULL;
}

gboolean
browser_load_dir (gpointer data)
{
  struct browser *browser = data;

  g_mutex_lock (&browser->mutex);
  if (browser->loading)
    {
      debug_print (1, "Browser already loading. Skipping load...\n");
      g_mutex_unlock (&browser->mutex);
      return FALSE;
    }
  else
    {
      browser->loading = TRUE;
    }
  g_mutex_unlock (&browser->mutex);

  browser_clear (browser);

  if (!browser->fs_ops || !browser->fs_ops->readdir)
    {
      return FALSE;
    }

  browser->thread = g_thread_new ("browser_thread", browser_load_dir_runner,
				  browser);
  return FALSE;
}

void
browser_update_fs_options (struct browser *browser)
{
  gtk_widget_set_visible (browser->add_dir_button,
			  browser->fs_ops && browser->fs_ops->mkdir != NULL);
  gtk_widget_set_sensitive (browser->refresh_button,
			    browser->fs_ops
			    && browser->fs_ops->readdir != NULL);
  gtk_widget_set_sensitive (browser->up_button, browser->fs_ops
			    && browser->fs_ops->readdir != NULL);
}

void
browser_init (struct browser *browser)
{
  browser->notifier = g_malloc (sizeof (struct notifier));
  notifier_init (browser->notifier, browser);
}

void
browser_destroy (struct browser *browser)
{
  notifier_destroy (browser->notifier);
  g_free (browser->notifier);
  if (browser->thread)
    {
      g_thread_join (browser->thread);
    }
  g_slist_free (browser->sensitive_widgets);
}

void
browser_set_options (struct browser *browser)
{
  GtkTreeSortable *sortable =
    GTK_TREE_SORTABLE (gtk_tree_view_get_model (browser->view));

  if (browser->fs_ops->options & FS_OPTION_SORT_BY_ID)
    {
      gtk_tree_sortable_set_sort_func (sortable,
				       BROWSER_LIST_STORE_ID_FIELD,
				       browser_sort_by_id, NULL, NULL);
      gtk_tree_sortable_set_sort_column_id (sortable,
					    BROWSER_LIST_STORE_ID_FIELD,
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

void
browser_reset (struct browser *browser)
{
  browser->fs_ops = NULL;
  g_free (browser->dir);
  browser->dir = NULL;
  browser_clear (browser);
}

void
browser_clear_dnd_function (struct browser *browser)
{
  if (browser->dnd_timeout_function_id)
    {
      g_source_remove (browser->dnd_timeout_function_id);
      browser->dnd_timeout_function_id = 0;
    }
}

void
browser_set_dnd_function (struct browser *browser, GSourceFunc function)
{
  browser_clear_dnd_function (browser);
  browser->dnd_timeout_function_id = g_timeout_add (DND_TIMEOUT, function,
						    browser);
}
