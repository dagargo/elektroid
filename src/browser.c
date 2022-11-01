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
      ret = g_utf8_collate (itema.name, itemb.name);
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

  ret = itema.id > itemb.id;

  return ret;
}

void
browser_set_item (GtkTreeModel * model, GtkTreeIter * iter, struct item *item)
{
  gchar *name;
  gtk_tree_model_get (model, iter, BROWSER_LIST_STORE_TYPE_FIELD, &item->type,
		      BROWSER_LIST_STORE_NAME_FIELD, &name,
		      BROWSER_LIST_STORE_SIZE_FIELD, &item->size,
		      BROWSER_LIST_STORE_INDEX_FIELD, &item->id, -1);
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

void
browser_reset (gpointer data)
{
  struct browser *browser = data;
  GtkListStore *list_store =
    GTK_LIST_STORE (gtk_tree_view_get_model (browser->view));

  gtk_entry_set_text (browser->dir_entry, browser->dir);
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
  if (!browser->active)
    {
      if (strcmp (browser->dir, "/"))
	{
	  gchar *dup = strdup (browser->dir);
	  gchar *new_path = dirname (dup);
	  strcpy (browser->dir, new_path);
	  free (dup);
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
      if (strcmp (browser->dir, "/"))
	{
	  strcat (browser->dir, "/");
	}
      strcat (browser->dir, item.name);
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
  gchar *path = chain_path (browser->dir, item->name);
  debug_print (1, "Using %s path for item %s (%d)...\n", path, item->name,
	       item->id);
  return path;
}

gchar *
browser_get_item_id_path (struct browser *browser, struct item *item)
{
  gchar *id = browser->fs_ops->get_id (item);
  gchar *path = chain_path (browser->dir, id);
  debug_print (1, "Using %s path for item %s (%d)...\n", path, item->name,
	       item->id);
  g_free (id);
  return path;
}

static void
browser_add_dentry_item (struct browser *browser, struct item_iterator *iter)
{
  gchar *hsize, *slot;
  GtkListStore *list_store =
    GTK_LIST_STORE (gtk_tree_view_get_model (browser->view));

  hsize = iter->item.size ? get_human_size (iter->item.size, TRUE) : "";
  slot = (browser->fs_ops->options & FS_OPTION_SLOT_STORAGE)
    && browser->fs_ops->get_slot ? browser->fs_ops->get_slot (&iter->item,
							      browser->backend)
    : "";

  gtk_list_store_insert_with_values (list_store, NULL, -1,
				     BROWSER_LIST_STORE_ICON_FIELD,
				     iter->item.type ==
				     ELEKTROID_DIR ? DIR_ICON :
				     browser->file_icon,
				     BROWSER_LIST_STORE_NAME_FIELD,
				     iter->item.name,
				     BROWSER_LIST_STORE_SIZE_FIELD,
				     iter->item.size,
				     BROWSER_LIST_STORE_SIZE_STR_FIELD,
				     hsize,
				     BROWSER_LIST_STORE_TYPE_FIELD,
				     iter->item.type,
				     BROWSER_LIST_STORE_INDEX_FIELD,
				     iter->item.id,
				     BROWSER_LIST_STORE_SLOT_FIELD, slot, -1);
  if (strlen (hsize))
    {
      g_free (hsize);
    }

  if (strlen (slot))
    {
      g_free (slot);
    }
}

static gboolean
browser_load_dir_runner_hide_spinner (gpointer data)
{
  struct browser *browser = data;
  g_slist_foreach (browser->sensitive_widgets, browser_widget_set_sensitive,
		   NULL);
  gtk_spinner_stop (GTK_SPINNER (browser->spinner));
  gtk_stack_set_visible_child_name (GTK_STACK (browser->stack), "list");
  return FALSE;
}

static gboolean
browser_load_dir_runner_show_spinner (gpointer data)
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

  if (browser->iter)
    {
      while (!next_item_iterator (browser->iter))
	{
	  if (iter_matches_extensions (browser->iter, browser->extensions))
	    {
	      browser_add_dentry_item (browser, browser->iter);
	    }
	}
      free_item_iterator (browser->iter);
      g_free (browser->iter);
      browser->iter = NULL;
    }

  g_thread_join (browser->thread);
  browser->thread = NULL;

  if (browser->check_callback)
    {
      browser->check_callback ();
    }
  gtk_tree_view_columns_autosize (browser->view);

  g_mutex_lock (&browser->mutex);
  browser->active = FALSE;
  g_mutex_unlock (&browser->mutex);

  return FALSE;
}

static gpointer
browser_load_dir_runner (gpointer data)
{
  gint err;
  struct browser *browser = data;

  g_idle_add (browser_load_dir_runner_show_spinner, browser);
  browser->iter = g_malloc (sizeof (struct item_iterator));
  err = browser->fs_ops->readdir (browser->backend, browser->iter,
				  browser->dir);
  g_idle_add (browser_load_dir_runner_hide_spinner, browser);
  if (err)
    {
      error_print ("Error while opening '%s' dir\n", browser->dir);
      g_free (browser->iter);
      browser->iter = NULL;
    }
  g_idle_add (browser_load_dir_runner_update_ui, browser);
  return NULL;
}

gboolean
browser_load_dir (gpointer data)
{
  struct browser *browser = data;

  g_mutex_lock (&browser->mutex);
  if (browser->active)
    {
      debug_print (1, "Browser already loading. Skipping load...\n");
      g_mutex_unlock (&browser->mutex);
      return FALSE;
    }
  else
    {
      browser->active = TRUE;
    }
  g_mutex_unlock (&browser->mutex);

  browser_reset (browser);

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
