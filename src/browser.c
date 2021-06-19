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
#include "utils.h"

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

void
browser_get_item_info (GtkTreeModel * model, GtkTreeIter * iter,
		       gchar ** type, gchar ** name, gint * size)
{
  if (type)
    {
      gtk_tree_model_get (model, iter, BROWSER_LIST_STORE_ICON_TYPE_FIELD,
			  type, -1);
    }
  if (name)
    {
      gtk_tree_model_get (model, iter, BROWSER_LIST_STORE_NAME_FIELD, name,
			  -1);
    }
  if (size)
    {
      gtk_tree_model_get (model, iter, BROWSER_LIST_STORE_SIZE_FIELD, size,
			  -1);
    }
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
  g_idle_add (browser->load_dir, NULL);
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
    }

  g_idle_add (browser->load_dir, NULL);
}

void
browser_item_activated (GtkTreeView * view, GtkTreePath * path,
			GtkTreeViewColumn * column, gpointer data)
{
  gchar *icon;
  gchar *name;
  GtkTreeIter iter;
  struct browser *browser = data;
  GtkTreeModel *model = GTK_TREE_MODEL (gtk_tree_view_get_model
					(browser->view));

  gtk_tree_model_get_iter (model, &iter, path);
  browser_get_item_info (model, &iter, &icon, &name, NULL);

  if (get_type_from_inventory_icon (icon) == ELEKTROID_DIR)
    {
      if (strcmp (browser->dir, "/") != 0)
	{
	  strcat (browser->dir, "/");
	}
      strcat (browser->dir, name);
      browser->load_dir (NULL);
    }

  g_free (icon);
  g_free (name);
}

gint
browser_get_selected_items_count (struct browser *browser)
{
  GtkTreeSelection *selection =
    gtk_tree_view_get_selection (GTK_TREE_VIEW (browser->view));
  return gtk_tree_selection_count_selected_rows (selection);
}
