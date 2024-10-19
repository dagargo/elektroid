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
#include "utils.h"
#include "notifier.h"
#include "preferences.h"
#include "connector.h"

#ifndef BROWSER_H
#define BROWSER_H

#define SIZE_LABEL_LEN 16

//Common columns
#define BROWSER_LIST_STORE_ICON_FIELD 0
#define BROWSER_LIST_STORE_NAME_FIELD 1	//This is the value returned by the funciton se in the get_item_key member in struct fs_operations. It's the filename.
#define BROWSER_LIST_STORE_SIZE_FIELD 2
#define BROWSER_LIST_STORE_SIZE_STR_FIELD 3
#define BROWSER_LIST_STORE_TYPE_FIELD 4
#define BROWSER_LIST_STORE_ID_FIELD 5
#define BROWSER_LIST_STORE_INFO_FIELD 6
#define BROWSER_LIST_STORE_SAMPLE_FRAMES_FIELD 7
#define BROWSER_LIST_STORE_SAMPLE_RATE_FIELD 8
#define BROWSER_LIST_STORE_SAMPLE_TIME_FIELD 9
#define BROWSER_LIST_STORE_SAMPLE_FORMAT_FIELD 10
#define BROWSER_LIST_STORE_SAMPLE_CHANNELS_FIELD 11
#define BROWSER_LIST_STORE_SAMPLE_MIDI_NOTE_FIELD 12
//Remote columns
#define BROWSER_LIST_STORE_SLOT_FIELD 13	//This is an optional map of the id (number) to some string like "A1", "001" or "[A:001]" to mimic the device way of numbering the items.

#define BROWSER_IS_SYSTEM(b) (!(b)->backend || (b)->backend->type == BE_TYPE_SYSTEM)

struct browser
{
  const gchar *name;
  GtkTreeView *view;
  GtkWidget *buttons_stack;
  GtkWidget *up_button;
  GtkWidget *add_dir_button;
  GtkWidget *refresh_button;
  GtkWidget *search_button;
  GtkWidget *search_entry;
  GtkEntry *dir_entry;
  gchar *dir;
  gboolean dnd;
  GtkTreePath *dnd_motion_path;
  gint dnd_timeout_function_id;
  GString *dnd_data;
  const struct fs_operations *fs_ops;
  struct backend *backend;
    gboolean (*check_callback) ();
  void (*set_columns_visibility) ();
  void (*set_popover_buttons_visibility) ();
  struct notifier *notifier;
  //Background loading members
  GSList *sensitive_widgets;
  GtkWidget *list_stack;
  GtkWidget *spinner;
  GThread *thread;
  GMutex mutex;
  gboolean loading;
  gboolean dirty;
  gboolean search_mode;
  const gchar *filter;
  gint64 last_selected_index;	//This needs space for gint and -1
  //Menu
  GtkWidget *popover;
  GtkWidget *popover_transfer_button;
  GtkWidget *popover_play_separator;
  GtkWidget *popover_play_button;
  GtkWidget *popover_options_separator;
  GtkWidget *popover_open_button;
  GtkWidget *popover_show_button;
  GtkWidget *popover_actions_separator;
  GtkWidget *popover_rename_button;
  GtkWidget *popover_delete_button;
  GtkTreeViewColumn *tree_view_name_column;
  GtkTreeViewColumn *tree_view_info_column;
  GtkTreeViewColumn *tree_view_sample_frames_column;
  GtkTreeViewColumn *tree_view_sample_rate_column;
  GtkTreeViewColumn *tree_view_sample_duration_column;
  GtkTreeViewColumn *tree_view_sample_channels_column;
  GtkTreeViewColumn *tree_view_sample_bits_column;
  GtkTreeViewColumn *tree_view_sample_midi_note_column;
  //Only present in the remote browser
  GtkTreeViewColumn *tree_view_id_column;
  GtkTreeViewColumn *tree_view_slot_column;
  GtkTreeViewColumn *tree_view_size_column;
};

void browser_set_item (GtkTreeModel *, GtkTreeIter *, struct item *);

gint browser_get_selected_items_count (struct browser *);

void browser_clear_selection (struct browser *);

gint browser_set_selected_row_iter (struct browser *, GtkTreeIter *);

void browser_selection_changed (GtkTreeSelection *, gpointer);

void browser_refresh (GtkWidget *, gpointer);

void browser_go_up (GtkWidget *, gpointer);

void browser_item_activated (GtkTreeView *, GtkTreePath *,
			     GtkTreeViewColumn *, gpointer);

gchar *browser_get_item_path (struct browser *, struct item *);

gboolean browser_load_dir (gpointer);

gboolean browser_load_dir_if_needed (gpointer);

void browser_update_fs_options (struct browser *);

void browser_local_init (struct browser *, GtkBuilder *, gchar *);

void browser_remote_init (struct browser *, GtkBuilder *, struct backend *);

void browser_destroy (struct browser *);

void browser_reset (struct browser *);

void browser_clear_dnd_function (struct browser *);

void browser_set_dnd_function (struct browser *, GSourceFunc);

void browser_open_search (GtkWidget *, gpointer);

void browser_close_search (GtkSearchEntry *, gpointer);

void browser_search_changed (GtkSearchEntry *, gpointer);

#endif
