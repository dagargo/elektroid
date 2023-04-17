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

#include "browser.h"
#include "backend.h"

struct browser_add_dentry_item_data
{
  struct browser *browser;
  struct item item;
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
	  free (new_path);
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
  struct browser_add_dentry_item_data *add_data = data;
  struct browser *browser = add_data->browser;
  struct item *item = &add_data->item;
  gchar *hsize, *slot;
  GtkListStore *list_store =
    GTK_LIST_STORE (gtk_tree_view_get_model (browser->view));

  hsize = get_human_size (item->size, TRUE);
  slot = (browser->fs_ops->options & FS_OPTION_SLOT_STORAGE)
    && browser->fs_ops->get_slot ? browser->fs_ops->get_slot (item,
							      browser->backend)
    : NULL;

  gtk_list_store_insert_with_values (list_store, NULL, -1,
				     BROWSER_LIST_STORE_ICON_FIELD,
				     item->type ==
				     ELEKTROID_DIR ? DIR_ICON :
				     browser->file_icon,
				     BROWSER_LIST_STORE_NAME_FIELD,
				     item->name,
				     BROWSER_LIST_STORE_SIZE_FIELD,
				     item->size,
				     BROWSER_LIST_STORE_SIZE_STR_FIELD,
				     hsize,
				     BROWSER_LIST_STORE_TYPE_FIELD,
				     item->type,
				     BROWSER_LIST_STORE_ID_FIELD,
				     item->id,
				     BROWSER_LIST_STORE_SLOT_FIELD,
				     slot ? slot : "", -1);

  g_free (hsize);
  g_free (slot);
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

  g_idle_add (browser_load_dir_runner_show_spinner_and_lock_browser, browser);
  err = browser->fs_ops->readdir (browser->backend, &iter, browser->dir);
  g_idle_add (browser_load_dir_runner_hide_spinner, browser);
  if (err)
    {
      error_print ("Error while opening '%s' dir\n", browser->dir);
      goto end;
    }

  while (!next_item_iterator (&iter))
    {
      if (iter_matches_extensions (&iter, browser->extensions))
	{
	  struct browser_add_dentry_item_data *data =
	    g_malloc (sizeof (struct browser_add_dentry_item_data));
	  data->browser = browser;
	  memcpy (&data->item, &iter.item, sizeof (struct item));
	  g_idle_add (browser_add_dentry_item, data);
	}
    }
  free_item_iterator (&iter);

end:
  g_idle_add (browser_load_dir_runner_update_ui, browser);
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

static void
browser_clear_file_extensions (struct browser *browser)
{
  if (browser->extensions == NULL)
    {
      return;
    }

  gchar **ext = browser->extensions;
  while (*ext)
    {
      g_free (*ext);
      ext++;
    }
  g_free (browser->extensions);

  browser->extensions = NULL;
}

static gboolean
browser_compare_extensions (struct browser *browser, const gchar ** ext_src)
{
  const gchar **src = ext_src;
  gchar **dst = browser->extensions;

  if (!dst && !src)
    {
      return TRUE;
    }

  if (!dst || !src)
    {
      return FALSE;
    }

  while (1)
    {
      if (*src == NULL && *dst == NULL)
	{
	  return TRUE;
	}

      if (strcmp (*src, *dst))
	{
	  return FALSE;
	}

      src++;
      dst++;
    }
}

/**
 * Sets the file extensions and reloads the browser if the extensions were updated.
 * If ext is NULL, it reloads the browser.
 */

gboolean
browser_set_file_extensions (struct browser *browser, const gchar ** ext_src)
{
  const gchar **ext;
  gchar **dst;
  int ext_count;

  if (browser_compare_extensions (browser, ext_src))
    {
      if (!ext_src)
	{
	  browser_load_dir (browser);
	}
      return FALSE;
    }

  browser_clear_file_extensions (browser);

  if (!ext_src)
    {
      goto end;
    }

  ext = ext_src;
  ext_count = 0;
  while (*ext)
    {
      ext_count++;
      ext++;
    }
  ext_count++;			//NULL included

  browser->extensions = malloc (sizeof (gchar *) * ext_count);
  dst = browser->extensions;
  ext = ext_src;
  while (*ext)
    {
      *dst = strdup (*ext);
      ext++;
      dst++;
    }
  *dst = NULL;

end:
  browser_load_dir (browser);
  return TRUE;
}

//See browser_set_file_extensions.

gboolean
browser_set_file_extension (struct browser *browser, gchar * ext)
{
  gboolean updated;
  const gchar **exts;
  if (ext)
    {
      exts = malloc (sizeof (gchar *) * 2);
      exts[0] = ext;
      exts[1] = NULL;
    }
  else
    {
      exts = NULL;
    }
  updated = browser_set_file_extensions (browser, exts);
  if (exts)
    {
      g_free (exts);
    }
  return updated;
}

void
browser_reset (struct browser *browser)
{
  browser->fs_ops = NULL;
  g_free (browser->dir);
  browser->dir = NULL;
  browser_clear (browser);
  browser_clear_file_extensions (browser);
}
