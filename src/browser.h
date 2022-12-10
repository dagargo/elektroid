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
#include "utils.h"
#include "notifier.h"

#ifndef BROWSER_H
#define BROWSER_H

#define SIZE_LABEL_LEN 16

#define DIR_ICON "folder-visiting-symbolic"

enum browser_list_field
{
  BROWSER_LIST_STORE_ICON_FIELD = 0,
  BROWSER_LIST_STORE_NAME_FIELD,	//This is the value returned by the funciton se in the get_item_key member in struct fs_operations. It's the filename.
  BROWSER_LIST_STORE_SIZE_FIELD,
  BROWSER_LIST_STORE_SIZE_STR_FIELD,
  BROWSER_LIST_STORE_TYPE_FIELD,
  BROWSER_LIST_STORE_ID_FIELD,
  BROWSER_LIST_STORE_SLOT_FIELD	//This is an optional map of the id (number) to some string like "A1", "001" or "[A:001]" to mimic the device way of numbering the items.
};

struct browser
{
  const gchar *name;
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
  const gchar *file_icon;
  gchar **extensions;
  const struct fs_operations *fs_ops;
  struct backend *backend;
    gboolean (*check_callback) ();
  struct notifier *notifier;
  //Background loading members
  GSList *sensitive_widgets;
  GtkWidget *stack;
  GtkWidget *spinner;
  GThread *thread;
  GMutex mutex;
  gboolean loading;
  struct item_iterator *iter;
};

void browser_set_item (GtkTreeModel *, GtkTreeIter *, struct item *);

gint browser_get_selected_items_count (struct browser *);

void browser_set_selected_row_iter (struct browser *, GtkTreeIter *);

void browser_selection_changed (GtkTreeSelection *, gpointer);

void browser_refresh (GtkWidget *, gpointer);

void browser_go_up (GtkWidget *, gpointer);

void browser_item_activated (GtkTreeView *, GtkTreePath *,
			     GtkTreeViewColumn *, gpointer);

gchar *browser_get_item_path (struct browser *, struct item *);

gchar *browser_get_item_id_path (struct browser *, struct item *);

gboolean browser_load_dir (gpointer);

void browser_update_fs_options (struct browser *);

void browser_init (struct browser *);

void browser_destroy (struct browser *);

void browser_set_options (struct browser *);

gboolean browser_set_file_extensions (struct browser *, const gchar **);

gboolean browser_set_file_extension (struct browser *, gchar *);

void browser_reset (struct browser *);

#endif
