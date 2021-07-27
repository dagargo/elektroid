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

gint
browser_sort (GtkTreeModel * model,
	      GtkTreeIter * a, GtkTreeIter * b, gpointer data)
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
      ret = -g_utf8_collate (type1, type2);
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

struct item *
browser_get_item (GtkTreeModel * model, GtkTreeIter * iter)
{
  struct item *item = malloc (sizeof (item));

  gtk_tree_model_get (model, iter, BROWSER_LIST_STORE_TYPE_FIELD, &item->type,
		      BROWSER_LIST_STORE_NAME_FIELD, &item->name,
		      BROWSER_LIST_STORE_SIZE_FIELD, &item->size,
		      BROWSER_LIST_STORE_INDEX_FIELD, &item->index, -1);

  return item;
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
  char *dup;
  char *new_path;
  struct browser *browser = data;

  if (strcmp (browser->dir, "/") != 0)
    {
      dup = strdup (browser->dir);
      new_path = dirname (dup);
      strcpy (browser->dir, new_path);
      free (dup);
      if (browser->notify_dir_change)
	{
	  browser->notify_dir_change (browser);
	}
    }

  g_idle_add (browser_load_dir, browser);
}

void
browser_item_activated (GtkTreeView * view, GtkTreePath * path,
			GtkTreeViewColumn * column, gpointer data)
{
  GtkTreeIter iter;
  struct item *item;
  struct browser *browser = data;
  GtkTreeModel *model = GTK_TREE_MODEL (gtk_tree_view_get_model
					(browser->view));

  gtk_tree_model_get_iter (model, &iter, path);
  item = browser_get_item (model, &iter);

  if (item->type == ELEKTROID_DIR)
    {
      if (strcmp (browser->dir, "/") != 0)
	{
	  strcat (browser->dir, "/");
	}
      strcat (browser->dir, item->name);
      browser_load_dir (browser);
      if (browser->notify_dir_change)
	{
	  browser->notify_dir_change (browser);
	}
    }

  browser_free_item (item);
}

gint
browser_get_selected_items_count (struct browser *browser)
{
  GtkTreeSelection *selection =
    gtk_tree_view_get_selection (GTK_TREE_VIEW (browser->view));
  return gtk_tree_selection_count_selected_rows (selection);
}

void
browser_free_item (struct item *item)
{
  g_free (item->name);
  g_free (item);
}

gchar *
browser_get_item_path (struct browser *browser, struct item *item)
{
  gchar *id;
  gchar *path;

  id = browser->fs_operations->getid (item);
  path = chain_path (browser->dir, id);
  g_free (id);
  debug_print (1, "Using %s path for item %s...\n", path, item->name);

  return path;
}

static void
local_add_dentry_item (struct browser *browser, struct item_iterator *iter)
{
  gchar sizes[SIZE_LABEL_LEN];
  GtkListStore *list_store =
    GTK_LIST_STORE (gtk_tree_view_get_model (browser->view));

  if (iter->size > 0)
    {
      snprintf (sizes, SIZE_LABEL_LEN, "%.2f MiB",
		iter->size / (1024.0 * 1024.0));
    }
  else
    {
      sizes[0] = 0;
    }

  gtk_list_store_insert_with_values (list_store, NULL, -1,
				     BROWSER_LIST_STORE_ICON_FIELD,
				     iter->type ==
				     ELEKTROID_DIR ? DIR_ICON :
				     browser->file_icon,
				     BROWSER_LIST_STORE_NAME_FIELD,
				     iter->entry,
				     BROWSER_LIST_STORE_SIZE_FIELD,
				     iter->size,
				     BROWSER_LIST_STORE_SIZE_STR_FIELD, sizes,
				     BROWSER_LIST_STORE_TYPE_FIELD,
				     iter->type,
				     BROWSER_LIST_STORE_INDEX_FIELD, iter->id,
				     -1);
}

static gboolean
browser_file_match_extensions (struct browser *browser,
			       struct item_iterator *iter)
{
  gboolean match;
  const gchar *entry_ext;
  const gchar **ext = browser->extensions;

  if (iter->type == ELEKTROID_DIR)
    {
      return TRUE;
    }

  if (!ext)
    {
      return TRUE;
    }

  entry_ext = get_ext (iter->entry);

  if (!entry_ext)
    {
      return FALSE;
    }

  match = FALSE;
  while (*ext != NULL && !match)
    {
      match = !strcasecmp (entry_ext, *ext);
      ext++;
    }

  return match;
}

gboolean
browser_load_dir (gpointer data)
{
  struct item_iterator *iter;
  struct browser *browser = data;

  browser_reset (browser);

  iter = browser->fs_operations->readdir (browser->dir, browser->data);
  if (!iter)
    {
      error_print ("Error while opening %s dir\n", browser->dir);
      goto end;
    }

  while (!next_item_iterator (iter))
    {
      if (browser_file_match_extensions (browser, iter))
	{
	  local_add_dentry_item (browser, iter);
	}
    }
  free_item_iterator (iter);

end:
  gtk_tree_view_columns_autosize (browser->view);
  return FALSE;
}
