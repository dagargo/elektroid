/*
 *   browser.h
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

#include <gtk/gtk.h>
#include <libgen.h>

#define BROWSER_LIST_STORE_ICON_TYPE_FIELD 0
#define BROWSER_LIST_STORE_NAME_FIELD 1
#define BROWSER_LIST_STORE_SIZE_FIELD 2
#define BROWSER_LIST_STORE_HUMAN_SIZE_FIELD 3

struct browser
{
  gint (*mkdir) (const gchar *);
  gint (*rename) (const gchar *, const gchar *);
  gint (*delete) (const gchar *, const gchar);
  GSourceFunc load_dir;
  GSourceFunc check_selection;
  GtkTreeView *view;
  GtkWidget *up_button;
  GtkWidget *add_dir_button;
  GtkWidget *refresh_button;
  GtkEntry *dir_entry;
  gchar *dir;
  GtkMenu *menu;
  gboolean dnd;
  GtkTreePath *dnd_motion_path;
  gint dnd_timeout_function_id;
  GString *dnd_data;
};

gint browser_sort (GtkTreeModel *, GtkTreeIter *, GtkTreeIter *, gpointer);

void browser_get_item_info (GtkTreeModel *, GtkTreeIter *, gchar **, gchar **,
			    gint *);

gint browser_get_selected_items_count (struct browser *);

void browser_set_selected_row_iter (struct browser *, GtkTreeIter *);

void browser_reset (gpointer);

void browser_selection_changed (GtkTreeSelection *, gpointer);

void browser_refresh (GtkWidget *, gpointer);

void browser_go_up (GtkWidget *, gpointer);

void browser_item_activated (GtkTreeView *, GtkTreePath *,
			     GtkTreeViewColumn *, gpointer);
