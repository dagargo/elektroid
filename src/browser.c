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
#include "sample.h"

#define OTHER_BROWSER(b) (b == &local_browser ? &remote_browser : &local_browser)

#define DND_TIMEOUT 800

struct browser local_browser;
struct browser remote_browser;
extern struct editor editor;

struct browser_add_dentry_item_data
{
  struct browser *browser;
  struct item item;
  const gchar *icon;
  gchar *rel_path;
};

gboolean elektroid_check_backend ();

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
browser_sort_by_name (GtkTreeModel *model,
		      GtkTreeIter *a, GtkTreeIter *b, gpointer data)
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
browser_sort_by_id (GtkTreeModel *model, GtkTreeIter *a, GtkTreeIter *b,
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
browser_set_item (GtkTreeModel *model, GtkTreeIter *iter, struct item *item)
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
browser_set_selected_row_iter (struct browser *browser, GtkTreeIter *iter)
{
  GtkTreeModel *model;
  GtkTreeSelection *selection =
    gtk_tree_view_get_selection (GTK_TREE_VIEW (browser->view));
  GList *paths = gtk_tree_selection_get_selected_rows (selection, &model);

  gtk_tree_model_get_iter (model, iter, g_list_nth_data (paths, 0));
  g_list_free_full (paths, (GDestroyNotify) gtk_tree_path_free);
}

static void
browser_clear (struct browser *browser)
{
  GtkListStore *list_store =
    GTK_LIST_STORE (gtk_tree_view_get_model (browser->view));
  GtkTreeSelection *selection =
    gtk_tree_view_get_selection (GTK_TREE_VIEW (browser->view));

  gtk_entry_set_text (browser->dir_entry, browser->dir ? browser->dir : "");
  g_signal_handlers_block_by_func (selection,
				   G_CALLBACK (browser_selection_changed),
				   browser);
  gtk_list_store_clear (list_store);
  g_signal_handlers_unblock_by_func (selection,
				     G_CALLBACK (browser_selection_changed),
				     browser);
}

void
browser_refresh (GtkWidget *object, gpointer data)
{
  struct browser *browser = data;
  browser_load_dir (browser);
}

void
browser_go_up (GtkWidget *object, gpointer data)
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

  browser_load_dir (browser);
}

void
browser_item_activated (GtkTreeView *view, GtkTreePath *path,
			GtkTreeViewColumn *column, gpointer data)
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
      enum path_type type = backend_get_path_type (browser->backend);
      gchar *new_dir = path_chain (type, browser->dir, item.name);
      g_free (browser->dir);
      browser->dir = new_dir;
      browser_close_search (NULL, browser);	//This triggers a refresh
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
  g_signal_handlers_block_by_func (selection,
				   G_CALLBACK (browser_selection_changed),
				   browser);
  gtk_tree_selection_unselect_all (selection);
  g_signal_handlers_unblock_by_func (selection,
				     G_CALLBACK (browser_selection_changed),
				     browser);
}

gchar *
browser_get_item_path (struct browser *browser, struct item *item)
{
  gchar *filename = get_filename (browser->fs_ops->options, item);
  enum path_type type = backend_get_path_type (browser->backend);
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
  gdouble time;
  gchar *name;
  gchar label[LABEL_MAX];
  GtkTreeIter iter, note_iter;
  struct browser_add_dentry_item_data *add_data = data;
  struct browser *browser = add_data->browser;
  struct item *item = &add_data->item;
  GValue v = G_VALUE_INIT;
  GtkListStore *list_store =
    GTK_LIST_STORE (gtk_tree_view_get_model (browser->view));
  GtkTreeSelection *selection =
    gtk_tree_view_get_selection (GTK_TREE_VIEW (browser->view));


  hsize = get_human_size (item->size, TRUE);

  gtk_list_store_insert_with_values (list_store, &iter, -1,
				     BROWSER_LIST_STORE_ICON_FIELD,
				     item->type ==
				     ELEKTROID_DIR ? DIR_ICON :
				     add_data->icon,
				     BROWSER_LIST_STORE_NAME_FIELD,
				     add_data->rel_path,
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
      browser->fs_ops->options & FS_OPTION_SHOW_SAMPLE_COLUMNS &&
      item->sample_info.frames)
    {
      snprintf (label, LABEL_MAX, "%u", item->sample_info.frames);
      g_value_init (&v, G_TYPE_STRING);
      g_value_set_string (&v, label);
      gtk_list_store_set_value (list_store, &iter,
				BROWSER_LIST_STORE_SAMPLE_FRAMES_FIELD, &v);
      g_value_unset (&v);

      snprintf (label, LABEL_MAX, "%.5g kHz",
		item->sample_info.rate / 1000.0);
      g_value_init (&v, G_TYPE_STRING);
      g_value_set_string (&v, label);
      gtk_list_store_set_value (list_store, &iter,
				BROWSER_LIST_STORE_SAMPLE_RATE_FIELD, &v);
      g_value_unset (&v);

      time = item->sample_info.frames / (gdouble) item->sample_info.rate;
      if (time >= 60)
	{
	  snprintf (label, LABEL_MAX, "%.4g %s", time / 60.0, _("min."));
	}
      else
	{
	  snprintf (label, LABEL_MAX, "%.3g s", time);
	}
      g_value_init (&v, G_TYPE_STRING);
      g_value_set_string (&v, label);
      gtk_list_store_set_value (list_store, &iter,
				BROWSER_LIST_STORE_SAMPLE_TIME_FIELD, &v);
      g_value_unset (&v);

      snprintf (label, LABEL_MAX, "%s, %s",
		sample_get_format (&item->sample_info),
		sample_get_subtype (&item->sample_info));
      g_value_init (&v, G_TYPE_STRING);
      g_value_set_string (&v, label);
      gtk_list_store_set_value (list_store, &iter,
				BROWSER_LIST_STORE_SAMPLE_FORMAT_FIELD, &v);
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

  if (item->type == ELEKTROID_FILE &&
      browser->fs_ops->options & FS_OPTION_SHOW_INFO_COLUMN)
    {
      g_value_init (&v, G_TYPE_STRING);
      g_value_set_string (&v, item->object_info);
      gtk_list_store_set_value (list_store, &iter,
				BROWSER_LIST_STORE_INFO_FIELD, &v);
      g_value_unset (&v);
    }

  if (editor.audio.path && editor.browser == browser)
    {
      name = path_chain (PATH_SYSTEM, browser->dir, add_data->rel_path);
      if (!strcmp (editor.audio.path, name))
	{
	  g_signal_handlers_block_by_func (selection,
					   G_CALLBACK
					   (browser_selection_changed),
					   browser);
	  gtk_tree_selection_select_iter (selection, &iter);
	  g_signal_handlers_unblock_by_func (selection,
					     G_CALLBACK
					     (browser_selection_changed),
					     browser);
	}
      g_free (name);
    }

  g_free (add_data->rel_path);
  g_free (add_data);

  return G_SOURCE_REMOVE;
}

static gboolean
browser_load_dir_runner_hide_spinner (gpointer data)
{
  struct browser *browser = data;
  gtk_spinner_stop (GTK_SPINNER (browser->spinner));
  gtk_stack_set_visible_child_name (GTK_STACK (browser->list_stack), "list");
  return FALSE;
}

static gboolean
browser_load_dir_runner_show_spinner_and_lock_browser (gpointer data)
{
  struct browser *browser = data;
  g_slist_foreach (browser->sensitive_widgets, browser_widget_set_insensitive,
		   NULL);
  gtk_stack_set_visible_child_name (GTK_STACK (browser->list_stack),
				    "spinner");
  gtk_spinner_start (GTK_SPINNER (browser->spinner));
  return FALSE;
}

static void
browser_wait (struct browser *browser)
{
  if (browser->thread)
    {
      g_thread_join (browser->thread);
      browser->thread = NULL;
    }
}

static gboolean
browser_load_dir_runner_update_ui (gpointer data)
{
  struct browser *browser = data;
  GtkTreeSelection *selection =
    gtk_tree_view_get_selection (GTK_TREE_VIEW (browser->view));
  gboolean active = (!browser->backend
		     || browser->backend->type == BE_TYPE_SYSTEM);

  browser_wait (browser);

  if (browser->check_callback)
    {
      browser->check_callback ();
    }
  gtk_tree_view_columns_autosize (browser->view);

  if (!browser->search_mode)
    {
      gtk_widget_grab_focus (GTK_WIDGET (browser->view));
      notifier_set_active (browser->notifier, active);
    }

  //Unlock browser
  g_slist_foreach (browser->sensitive_widgets, browser_widget_set_sensitive,
		   NULL);

  //Wait for every pending call to browser_add_dentry_item scheduled from the thread
  while (gtk_events_pending ())
    {
      gtk_main_iteration ();
    }

  if (browser_get_selected_items_count (browser))
    {
      GList *list = gtk_tree_selection_get_selected_rows (selection, NULL);
      g_signal_handlers_block_by_func (selection,
				       G_CALLBACK
				       (browser_selection_changed), browser);
      gtk_tree_view_set_cursor (browser->view, list->data, NULL, FALSE);
      g_signal_handlers_unblock_by_func (selection,
					 G_CALLBACK
					 (browser_selection_changed),
					 browser);
      g_list_free_full (list, (GDestroyNotify) gtk_tree_path_free);
    }
  else
    {
      GtkTreeModel *model =
	GTK_TREE_MODEL (gtk_tree_view_get_model (browser->view));
      if (gtk_tree_model_iter_n_children (model, NULL))
	{
	  GtkTreePath *first = gtk_tree_path_new_first ();
	  gtk_tree_view_scroll_to_cell (browser->view, first, NULL, FALSE, 0,
					0);
	  gtk_tree_path_free (first);
	}
      //If editor.audio.path is empty is a recording buffer.
      if (editor.browser == browser && editor.audio.path)
	{
	  editor_reset (&editor, NULL);
	}
    }

  g_mutex_lock (&browser->mutex);
  browser->loading = FALSE;
  g_mutex_unlock (&browser->mutex);

  return FALSE;
}

static void
browser_iterate_dir_add (struct browser *browser, struct item_iterator *iter,
			 const gchar *icon, struct item *item,
			 gchar *rel_path)
{
  if (browser->filter)
    {
      if (browser->fs_ops->options & FS_OPTION_SHOW_INFO_COLUMN)
	{
	  if (!g_str_match_string (browser->filter, iter->item.name, TRUE) &&
	      !g_str_match_string (browser->filter, iter->item.object_info,
				   TRUE))
	    {
	      return;
	    }
	}
      else
	{
	  if (!g_str_match_string (browser->filter, iter->item.name, TRUE))
	    {
	      return;
	    }
	}

    }

  struct browser_add_dentry_item_data *data =
    g_malloc (sizeof (struct browser_add_dentry_item_data));
  data->browser = browser;
  memcpy (&data->item, &iter->item, sizeof (struct item));
  data->icon = icon;
  data->rel_path = rel_path;
  g_idle_add (browser_add_dentry_item, data);
}

static void
browser_iterate_dir (struct browser *browser, struct item_iterator *iter,
		     const gchar *icon)
{
  gboolean loading = TRUE;

  while (loading && !next_item_iterator (iter))
    {
      browser_iterate_dir_add (browser, iter, icon, &iter->item,
			       strdup (iter->item.name));

      g_mutex_lock (&browser->mutex);
      loading = browser->loading;
      g_mutex_unlock (&browser->mutex);
    }
  free_item_iterator (iter);
}

static void
browser_iterate_dir_recursive (struct browser *browser, const gchar *rel_dir,
			       struct item_iterator *iter, const gchar *icon,
			       gchar **extensions)
{
  gint err;
  gchar *child_dir, *child_rel_dir;
  struct item_iterator child_iter;
  gboolean loading = TRUE;

  while (loading && !next_item_iterator (iter))
    {
      child_rel_dir = path_chain (PATH_SYSTEM, rel_dir, iter->item.name);

      browser_iterate_dir_add (browser, iter, icon, &iter->item,
			       strdup (child_rel_dir));

      if (iter->item.type == ELEKTROID_DIR)
	{
	  child_dir = path_chain (PATH_SYSTEM, browser->dir, child_rel_dir);
	  err = browser->fs_ops->readdir (browser->backend, &child_iter,
					  child_dir, extensions);
	  if (!err)
	    {
	      browser_iterate_dir_recursive (browser, child_rel_dir,
					     &child_iter, icon, extensions);
	    }
	  g_free (child_dir);
	}
      g_free (child_rel_dir);

      g_mutex_lock (&browser->mutex);
      loading = browser->loading;
      g_mutex_unlock (&browser->mutex);
    }
  free_item_iterator (iter);
}

static gpointer
browser_load_dir_runner (gpointer data)
{
  gint err;
  struct browser *browser = data;
  struct item_iterator iter;
  gchar **exts;
  const gchar *icon = browser->fs_ops->gui_icon;
  gboolean search_mode;

  if (remote_browser.fs_ops)
    {
      if (remote_browser.fs_ops->get_exts)
	{
	  exts = remote_browser.fs_ops->get_exts (remote_browser.backend,
						  remote_browser.fs_ops);
	}
      else
	{
	  exts = new_ext_array (remote_browser.fs_ops->ext);
	}
    }
  else
    {
      //If !remote_browser.fs_ops, only FS_LOCAL_SAMPLE_OPERATIONS is used, which implements get_exts.
      exts = local_browser.fs_ops->get_exts (remote_browser.backend,
					     remote_browser.fs_ops);
    }

  g_idle_add (browser_load_dir_runner_show_spinner_and_lock_browser, browser);
  err = browser->fs_ops->readdir (browser->backend, &iter, browser->dir,
				  exts);
  g_idle_add (browser_load_dir_runner_hide_spinner, browser);
  if (err)
    {
      error_print ("Error while opening '%s' dir\n", browser->dir);
      goto end;
    }

  g_mutex_lock (&browser->mutex);
  search_mode = browser->search_mode;
  g_mutex_unlock (&browser->mutex);
  if (search_mode)
    {
      browser_iterate_dir_recursive (browser, "", &iter, icon, exts);
    }
  else
    {
      browser_iterate_dir (browser, &iter, icon);
    }

end:
  g_idle_add (browser_load_dir_runner_update_ui, browser);
  free_ext_array (exts);
  return NULL;
}

gboolean
browser_load_dir (gpointer data)
{
  struct browser *browser = data;

  g_mutex_lock (&browser->mutex);
  if (browser->loading)
    {
      g_mutex_unlock (&browser->mutex);
      debug_print (1, "Browser already loading. Skipping load...\n");
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

gboolean
browser_load_dir_if_needed (gpointer data)
{
  struct browser *browser = data;
  if ((browser->backend && browser->backend->type == BE_TYPE_MIDI) ||
      !browser->notifier)
    {
      browser_load_dir (browser);
    }
  return FALSE;
}

static void
browser_update_fs_sorting_options (struct browser *browser)
{
  GtkTreeSortable *sortable =
    GTK_TREE_SORTABLE (gtk_tree_view_get_model (browser->view));

  if (!browser->search_mode && browser->fs_ops
      && browser->fs_ops->options & FS_OPTION_SORT_BY_ID)
    {
      gtk_tree_sortable_set_sort_func (sortable,
				       BROWSER_LIST_STORE_ID_FIELD,
				       browser_sort_by_id, NULL, NULL);
      gtk_tree_sortable_set_sort_column_id (sortable,
					    BROWSER_LIST_STORE_ID_FIELD,
					    GTK_TREE_SORTABLE_DEFAULT_SORT_COLUMN_ID);
    }
  else if (browser->search_mode || (browser->fs_ops
				    && browser->
				    fs_ops->options & FS_OPTION_SORT_BY_NAME))
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
browser_update_fs_options (struct browser *browser)
{
  gtk_widget_set_visible (browser->add_dir_button,
			  !browser->fs_ops || browser->fs_ops->mkdir);
  gtk_widget_set_visible (browser->search_button,
			  !browser->fs_ops ||
			  browser->fs_ops->options & FS_OPTION_ALLOW_SEARCH);
  gtk_widget_set_sensitive (browser->refresh_button,
			    browser->fs_ops && browser->fs_ops->readdir);
  gtk_widget_set_sensitive (browser->up_button, browser->fs_ops &&
			    browser->fs_ops->readdir);

  browser_update_fs_sorting_options (browser);
  browser->set_columns_visibility ();
  browser->set_popup_menuitems_visibility ();
}

static void
browser_init (struct browser *browser)
{
  notifier_init (&browser->notifier, browser);
}

void
browser_destroy (struct browser *browser)
{
  notifier_destroy (browser->notifier);
  if (browser->thread)
    {
      browser_wait (browser);
    }
  g_slist_free (browser->sensitive_widgets);
}

static void
browser_reset_search (struct browser *browser)
{
  gtk_stack_set_visible_child_name (GTK_STACK (browser->buttons_stack),
				    "buttons");

  g_mutex_lock (&browser->mutex);
  browser->loading = FALSE;
  browser->search_mode = FALSE;
  g_mutex_unlock (&browser->mutex);
  browser_wait (browser);

  gtk_entry_set_text (GTK_ENTRY (browser->search_entry), "");
  browser->filter = NULL;
}

void
browser_reset (struct browser *browser)
{
  browser->fs_ops = NULL;
  g_free (browser->dir);
  browser->dir = NULL;
  browser_clear (browser);
  browser_reset_search (browser);
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

static void
browser_set_columns_visibility (struct browser *browser)
{
  gboolean sample_columns, info_column;

  if (browser->fs_ops)
    {
      sample_columns = (browser->fs_ops->options &
			FS_OPTION_SHOW_SAMPLE_COLUMNS) != 0;
      info_column = (browser->fs_ops->options &
		     FS_OPTION_SHOW_INFO_COLUMN) != 0;
    }
  else
    {
      sample_columns = FALSE;
      info_column = FALSE;
    }

  gtk_tree_view_column_set_visible
    (browser->tree_view_sample_frames_column, sample_columns);
  gtk_tree_view_column_set_visible
    (browser->tree_view_sample_rate_column, sample_columns);
  gtk_tree_view_column_set_visible
    (browser->tree_view_sample_duration_column, sample_columns);
  gtk_tree_view_column_set_visible
    (browser->tree_view_sample_channels_column, sample_columns);
  gtk_tree_view_column_set_visible
    (browser->tree_view_sample_bits_column, sample_columns);
  gtk_tree_view_column_set_visible
    (browser->tree_view_sample_midi_note_column, sample_columns);
  gtk_tree_view_column_set_visible
    (browser->tree_view_info_column, info_column);
}

static inline void
browser_local_set_columns_visibility ()
{
  browser_set_columns_visibility (&local_browser);
}

static void
browser_remote_set_columns_visibility ()
{
  browser_set_columns_visibility (&remote_browser);

  if (remote_browser.fs_ops)
    {
      gtk_tree_view_column_set_visible (remote_browser.tree_view_id_column,
					remote_browser.fs_ops->options &
					FS_OPTION_SHOW_ID_COLUMN);
      gtk_tree_view_column_set_visible (remote_browser.tree_view_slot_column,
					remote_browser.fs_ops->options &
					FS_OPTION_SHOW_SLOT_COLUMN);
      gtk_tree_view_column_set_visible (remote_browser.tree_view_size_column,
					remote_browser.fs_ops->options &
					FS_OPTION_SHOW_SIZE_COLUMN);
    }
  else
    {
      gtk_tree_view_column_set_visible (remote_browser.tree_view_id_column,
					FALSE);
      gtk_tree_view_column_set_visible (remote_browser.tree_view_slot_column,
					FALSE);
      gtk_tree_view_column_set_visible (remote_browser.tree_view_size_column,
					FALSE);
    }
}

void
browser_open_search (GtkWidget *widget, gpointer data)
{
  struct browser *browser = data;
  gtk_stack_set_visible_child_name (GTK_STACK (browser->buttons_stack),
				    "search");
  g_mutex_lock (&browser->mutex);
  browser->loading = FALSE;
  browser->search_mode = TRUE;
  g_mutex_unlock (&browser->mutex);
  browser_wait (browser);
  browser_clear (browser);
  browser_update_fs_sorting_options (data);
}

void
browser_close_search (GtkSearchEntry *entry, gpointer data)
{
  struct browser *browser = data;
  browser_reset_search (browser);
  browser_update_fs_sorting_options (browser);
  browser_refresh (NULL, browser);
}

void
browser_search_changed (GtkSearchEntry *entry, gpointer data)
{
  struct browser *browser = data;
  const gchar *filter = gtk_entry_get_text (GTK_ENTRY (entry));

  g_mutex_lock (&browser->mutex);
  browser->loading = FALSE;
  g_mutex_unlock (&browser->mutex);
  browser_wait (browser);

  //Wait for every pending call to browser_add_dentry_item scheduled from the thread
  while (gtk_events_pending ())
    {
      gtk_main_iteration ();
    }

  browser_clear (browser);

  usleep (250000);

  if (strlen (filter))
    {
      browser->filter = filter;
      browser_refresh (NULL, browser);
    }
}

static void
browser_clear_other_browser_if_system (struct browser *browser)
{
  if ((browser == &local_browser
       && (remote_browser.backend
	   && remote_browser.backend->type == BE_TYPE_SYSTEM))
      || browser == &remote_browser)
    {
      browser_clear_selection (OTHER_BROWSER (browser));
    }
}

static void
browser_check_and_load_sample (struct browser *browser, gint count)
{
  struct item item;
  GtkTreeIter iter;
  GtkTreeModel *model;
  gboolean sample_editor = !remote_browser.fs_ops
    || (remote_browser.fs_ops->options & FS_OPTION_SAMPLE_EDITOR);

  if (count == 1)
    {
      browser_set_selected_row_iter (browser, &iter);
      model = GTK_TREE_MODEL (gtk_tree_view_get_model (browser->view));
      browser_set_item (model, &iter, &item);

      if (item.type == ELEKTROID_FILE && sample_editor)
	{
	  enum path_type type = backend_get_path_type (browser->backend);
	  gchar *sample_path = path_chain (type, browser->dir, item.name);
	  browser_clear_other_browser_if_system (browser);
	  editor_reset (&editor, browser);
	  g_free (editor.audio.path);
	  editor.audio.path = sample_path;
	  editor_start_load_thread (&editor);

	  return;
	}
    }

  browser_clear_other_browser_if_system (browser);
  editor_reset (&editor, NULL);
}

static gboolean
browser_local_check_selection (gpointer data)
{
  struct browser *browser = data;
  gint count = browser_get_selected_items_count (browser);
  gboolean editor_impl = browser->fs_ops
    && browser->fs_ops->options && FS_OPTION_SAMPLE_EDITOR ? TRUE : FALSE;

  if (editor_impl)
    {
      browser_check_and_load_sample (browser, count);
    }

  return FALSE;
}

static gboolean
browser_remote_check_selection (gpointer data)
{
  struct browser *browser = data;
  gint count = browser_get_selected_items_count (browser);
  gboolean sel_impl = browser->fs_ops
    && browser->fs_ops->select_item ? TRUE : FALSE;

  if (browser->backend->type == BE_TYPE_SYSTEM)
    {
      browser_local_check_selection (browser);
    }

  if (count == 1 && sel_impl)
    {
      GtkTreeIter iter;
      GtkTreeModel *model;
      struct item item;
      browser_set_selected_row_iter (browser, &iter);
      model = GTK_TREE_MODEL (gtk_tree_view_get_model (browser->view));
      browser_set_item (model, &iter, &item);
      remote_browser.fs_ops->select_item (browser->backend, browser->dir,
					  &item);
    }

  return FALSE;
}

void
browser_local_set_popup_visibility ()
{
  gboolean ul_avail = remote_browser.fs_ops &&
    !(remote_browser.fs_ops->options & FS_OPTION_SLOT_STORAGE)
    && remote_browser.fs_ops->upload;
  gboolean edit_avail =
    local_browser.fs_ops->options & FS_OPTION_SAMPLE_EDITOR;

  gtk_widget_set_visible (local_browser.transfer_menuitem, ul_avail);
  gtk_widget_set_visible (local_browser.play_separator, ul_avail);
  gtk_widget_set_visible (local_browser.play_menuitem, edit_avail);
  gtk_widget_set_visible (local_browser.options_separator, edit_avail);
}

static void
browser_local_set_popup_sensitivity (gint count, gboolean file)
{
  gboolean ul_avail = remote_browser.fs_ops &&
    !(remote_browser.fs_ops->options & FS_OPTION_SLOT_STORAGE)
    && remote_browser.fs_ops->upload;
  gboolean editing = editor.browser == &local_browser;

  gtk_widget_set_sensitive (local_browser.transfer_menuitem, count > 0
			    && ul_avail);
  gtk_widget_set_sensitive (local_browser.play_menuitem, file && editing);
  gtk_widget_set_sensitive (local_browser.open_menuitem, file);
  gtk_widget_set_sensitive (local_browser.show_menuitem, count <= 1);
  gtk_widget_set_sensitive (local_browser.rename_menuitem, count == 1);
  gtk_widget_set_sensitive (local_browser.delete_menuitem, count > 0);
}

void
browser_remote_set_popup_visibility ()
{
  gboolean dl_impl = remote_browser.fs_ops
    && remote_browser.fs_ops->download ? TRUE : FALSE;
  gboolean edit_avail = remote_browser.fs_ops
    && remote_browser.fs_ops->options & FS_OPTION_SAMPLE_EDITOR;
  gboolean system = remote_browser.fs_ops
    && remote_browser.backend->type == BE_TYPE_SYSTEM;

  gtk_widget_set_visible (remote_browser.transfer_menuitem, dl_impl);
  gtk_widget_set_visible (remote_browser.play_separator, dl_impl);
  gtk_widget_set_visible (remote_browser.play_menuitem, system && edit_avail);
  gtk_widget_set_visible (remote_browser.options_separator, system
			  && edit_avail);
  gtk_widget_set_visible (remote_browser.open_menuitem, system);
  gtk_widget_set_visible (remote_browser.show_menuitem, system);
  gtk_widget_set_visible (remote_browser.actions_separator, system);
}

static void
browser_remote_set_popup_sensitivity (gint count, gboolean file)
{
  gboolean dl_impl = remote_browser.fs_ops
    && remote_browser.fs_ops->download ? TRUE : FALSE;
  gboolean ren_impl = remote_browser.fs_ops
    && remote_browser.fs_ops->rename ? TRUE : FALSE;
  gboolean del_impl = remote_browser.fs_ops
    && remote_browser.fs_ops->delete ? TRUE : FALSE;
  gboolean editing = editor.browser == &remote_browser;
  gboolean system = remote_browser.fs_ops
    && remote_browser.backend->type == BE_TYPE_SYSTEM;

  gtk_widget_set_sensitive (remote_browser.transfer_menuitem, count > 0
			    && dl_impl);
  gtk_widget_set_sensitive (remote_browser.play_menuitem, file && editing);
  gtk_widget_set_sensitive (remote_browser.open_menuitem, file);
  gtk_widget_set_sensitive (remote_browser.show_menuitem, count <= 1
			    && system);
  gtk_widget_set_sensitive (remote_browser.rename_menuitem, count == 1
			    && ren_impl);
  gtk_widget_set_sensitive (remote_browser.delete_menuitem, count > 0
			    && del_impl);
}

static void
browser_setup_popup_sensitivity (struct browser *browser)
{
  struct item item;
  GtkTreeIter iter;
  gboolean file = FALSE;
  gint count = browser_get_selected_items_count (browser);
  GtkTreeModel *model = GTK_TREE_MODEL (gtk_tree_view_get_model
					(browser->view));

  if (count == 1)
    {
      browser_set_selected_row_iter (browser, &iter);
      browser_set_item (model, &iter, &item);
      file = item.type == ELEKTROID_FILE;
    }

  if (browser == &local_browser)
    {
      browser_local_set_popup_sensitivity (count, file);
    }
  else
    {
      browser_remote_set_popup_sensitivity (count, file);
    }
}

void
browser_selection_changed (GtkTreeSelection *selection, gpointer data)
{
  struct browser *browser = data;
  browser->check_selection (data);
  browser_setup_popup_sensitivity (browser);
}

void
browser_local_init (struct browser *browser, GtkBuilder *builder,
		    gchar *local_dir)
{
  browser->name = "local";
  browser->view =
    GTK_TREE_VIEW (gtk_builder_get_object (builder, "local_tree_view"));
  browser->buttons_stack =
    GTK_WIDGET (gtk_builder_get_object (builder, "local_buttons_stack"));
  browser->up_button =
    GTK_WIDGET (gtk_builder_get_object (builder, "local_up_button"));
  browser->add_dir_button =
    GTK_WIDGET (gtk_builder_get_object (builder, "local_add_dir_button"));
  browser->refresh_button =
    GTK_WIDGET (gtk_builder_get_object (builder, "local_refresh_button"));
  browser->search_button =
    GTK_WIDGET (gtk_builder_get_object (builder, "local_search_button"));
  browser->search_entry =
    GTK_WIDGET (gtk_builder_get_object (builder, "local_search_entry"));
  browser->dir_entry =
    GTK_ENTRY (gtk_builder_get_object (builder, "local_dir_entry"));
  browser->menu = GTK_MENU (gtk_builder_get_object (builder, "local_menu"));
  browser->dir = local_dir;
  browser->check_selection = browser_local_check_selection;
  browser->fs_ops = &FS_LOCAL_SAMPLE_OPERATIONS;
  browser->backend = NULL;
  browser->check_callback = NULL;
  browser->set_popup_menuitems_visibility =
    browser_local_set_popup_visibility;
  browser->set_columns_visibility = browser_local_set_columns_visibility;
  browser->sensitive_widgets = NULL;
  browser->list_stack =
    GTK_WIDGET (gtk_builder_get_object (builder, "local_list_stack"));
  browser->spinner =
    GTK_WIDGET (gtk_builder_get_object (builder, "local_spinner"));
  browser->transfer_menuitem =
    GTK_WIDGET (gtk_builder_get_object (builder, "upload_menuitem"));
  browser->play_separator =
    GTK_WIDGET (gtk_builder_get_object (builder, "local_play_separator"));
  browser->play_menuitem =
    GTK_WIDGET (gtk_builder_get_object (builder, "local_play_menuitem"));
  browser->options_separator =
    GTK_WIDGET (gtk_builder_get_object (builder, "local_options_separator"));
  browser->open_menuitem =
    GTK_WIDGET (gtk_builder_get_object (builder, "local_open_menuitem"));
  browser->show_menuitem =
    GTK_WIDGET (gtk_builder_get_object (builder, "local_show_menuitem"));
  browser->rename_menuitem =
    GTK_WIDGET (gtk_builder_get_object (builder, "local_rename_menuitem"));
  browser->delete_menuitem =
    GTK_WIDGET (gtk_builder_get_object (builder, "local_delete_menuitem"));
  browser->tree_view_name_column =
    GTK_TREE_VIEW_COLUMN (gtk_builder_get_object
			  (builder, "local_tree_view_name_column"));

  browser->sensitive_widgets =
    g_slist_append (browser->sensitive_widgets, browser->view);
  browser->sensitive_widgets =
    g_slist_append (browser->sensitive_widgets, browser->up_button);
  browser->sensitive_widgets =
    g_slist_append (browser->sensitive_widgets, browser->add_dir_button);
  browser->sensitive_widgets =
    g_slist_append (browser->sensitive_widgets, browser->refresh_button);
  browser->sensitive_widgets =
    g_slist_append (browser->sensitive_widgets, browser->search_button);

  browser->tree_view_sample_frames_column =
    GTK_TREE_VIEW_COLUMN (gtk_builder_get_object
			  (builder, "local_tree_view_sample_frames_column"));
  browser->tree_view_sample_rate_column =
    GTK_TREE_VIEW_COLUMN (gtk_builder_get_object
			  (builder, "local_tree_view_sample_rate_column"));
  browser->tree_view_sample_duration_column =
    GTK_TREE_VIEW_COLUMN (gtk_builder_get_object
			  (builder,
			   "local_tree_view_sample_duration_column"));
  browser->tree_view_sample_channels_column =
    GTK_TREE_VIEW_COLUMN (gtk_builder_get_object
			  (builder,
			   "local_tree_view_sample_channels_column"));
  browser->tree_view_sample_bits_column =
    GTK_TREE_VIEW_COLUMN (gtk_builder_get_object
			  (builder, "local_tree_view_sample_bits_column"));
  browser->tree_view_sample_midi_note_column =
    GTK_TREE_VIEW_COLUMN (gtk_builder_get_object
			  (builder,
			   "local_tree_view_sample_midi_note_column"));

  browser->tree_view_info_column =
    GTK_TREE_VIEW_COLUMN (gtk_builder_get_object
			  (builder, "local_tree_view_info_column"));

  browser->tree_view_id_column = NULL;
  browser->tree_view_slot_column = NULL;
  browser->tree_view_size_column = NULL;

  browser_init (browser);
}

void
browser_remote_init (struct browser *browser,
		     GtkBuilder *builder, struct backend *backend)
{
  browser->name = "remote";
  browser->view =
    GTK_TREE_VIEW (gtk_builder_get_object (builder, "remote_tree_view"));
  browser->buttons_stack =
    GTK_WIDGET (gtk_builder_get_object (builder, "remote_buttons_stack"));
  browser->up_button =
    GTK_WIDGET (gtk_builder_get_object (builder, "remote_up_button"));
  browser->add_dir_button =
    GTK_WIDGET (gtk_builder_get_object (builder, "remote_add_dir_button"));
  browser->refresh_button =
    GTK_WIDGET (gtk_builder_get_object (builder, "remote_refresh_button"));
  browser->search_button =
    GTK_WIDGET (gtk_builder_get_object (builder, "remote_search_button"));
  browser->search_entry =
    GTK_WIDGET (gtk_builder_get_object (builder, "remote_search_entry"));
  browser->dir_entry =
    GTK_ENTRY (gtk_builder_get_object (builder, "remote_dir_entry"));
  browser->menu = GTK_MENU (gtk_builder_get_object (builder, "remote_menu"));
  browser->dir = NULL;
  browser->check_selection = browser_remote_check_selection;
  browser->fs_ops = NULL;
  browser->backend = backend;
  browser->check_callback = elektroid_check_backend;
  browser->set_popup_menuitems_visibility =
    browser_remote_set_popup_visibility;
  browser->set_columns_visibility = browser_remote_set_columns_visibility;
  browser->sensitive_widgets = NULL;
  browser->list_stack =
    GTK_WIDGET (gtk_builder_get_object (builder, "remote_list_stack"));
  browser->spinner =
    GTK_WIDGET (gtk_builder_get_object (builder, "remote_spinner"));
  browser->transfer_menuitem =
    GTK_WIDGET (gtk_builder_get_object (builder, "download_menuitem"));
  browser->play_separator =
    GTK_WIDGET (gtk_builder_get_object (builder, "remote_play_separator"));
  browser->play_menuitem =
    GTK_WIDGET (gtk_builder_get_object (builder, "remote_play_menuitem"));
  browser->options_separator =
    GTK_WIDGET (gtk_builder_get_object (builder, "remote_options_separator"));
  browser->open_menuitem =
    GTK_WIDGET (gtk_builder_get_object (builder, "remote_open_menuitem"));
  browser->show_menuitem =
    GTK_WIDGET (gtk_builder_get_object (builder, "remote_show_menuitem"));
  browser->actions_separator =
    GTK_WIDGET (gtk_builder_get_object (builder, "remote_actions_separator"));
  browser->rename_menuitem =
    GTK_WIDGET (gtk_builder_get_object (builder, "remote_rename_menuitem"));
  browser->delete_menuitem =
    GTK_WIDGET (gtk_builder_get_object (builder, "remote_delete_menuitem"));
  browser->tree_view_name_column =
    GTK_TREE_VIEW_COLUMN (gtk_builder_get_object
			  (builder, "remote_tree_view_name_column"));

  browser->sensitive_widgets =
    g_slist_append (browser->sensitive_widgets,
		    GTK_WIDGET (gtk_builder_get_object
				(builder, "devices_combo")));
  browser->sensitive_widgets =
    g_slist_append (browser->sensitive_widgets,
		    GTK_WIDGET (gtk_builder_get_object
				(builder, "refresh_devices_button")));
  browser->sensitive_widgets =
    g_slist_append (browser->sensitive_widgets,
		    GTK_WIDGET (gtk_builder_get_object
				(builder, "fs_combo")));
  browser->sensitive_widgets =
    g_slist_append (browser->sensitive_widgets, browser->view);
  browser->sensitive_widgets =
    g_slist_append (browser->sensitive_widgets, browser->up_button);
  browser->sensitive_widgets =
    g_slist_append (browser->sensitive_widgets, browser->add_dir_button);
  browser->sensitive_widgets =
    g_slist_append (browser->sensitive_widgets, browser->refresh_button);
  browser->sensitive_widgets =
    g_slist_append (browser->sensitive_widgets, browser->search_button);

  browser->tree_view_id_column =
    GTK_TREE_VIEW_COLUMN (gtk_builder_get_object
			  (builder, "remote_tree_view_id_column"));
  browser->tree_view_slot_column =
    GTK_TREE_VIEW_COLUMN (gtk_builder_get_object
			  (builder, "remote_tree_view_slot_column"));
  browser->tree_view_size_column =
    GTK_TREE_VIEW_COLUMN (gtk_builder_get_object
			  (builder, "remote_tree_view_size_column"));

  browser->tree_view_sample_frames_column =
    GTK_TREE_VIEW_COLUMN (gtk_builder_get_object
			  (builder, "remote_tree_view_sample_frames_column"));
  browser->tree_view_sample_rate_column =
    GTK_TREE_VIEW_COLUMN (gtk_builder_get_object
			  (builder, "remote_tree_view_sample_rate_column"));
  browser->tree_view_sample_duration_column =
    GTK_TREE_VIEW_COLUMN (gtk_builder_get_object
			  (builder,
			   "remote_tree_view_sample_duration_column"));
  browser->tree_view_sample_channels_column =
    GTK_TREE_VIEW_COLUMN (gtk_builder_get_object
			  (builder,
			   "remote_tree_view_sample_channels_column"));
  browser->tree_view_sample_bits_column =
    GTK_TREE_VIEW_COLUMN (gtk_builder_get_object
			  (builder, "remote_tree_view_sample_bits_column"));
  browser->tree_view_sample_midi_note_column =
    GTK_TREE_VIEW_COLUMN (gtk_builder_get_object
			  (builder,
			   "remote_tree_view_sample_midi_note_column"));

  browser->tree_view_info_column =
    GTK_TREE_VIEW_COLUMN (gtk_builder_get_object
			  (builder, "remote_tree_view_info_column"));

  browser_init (browser);
}
