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

#define OTHER_BROWSER(b) (b == &local_browser.browser ? &remote_browser.browser : &local_browser.browser)

#define DND_TIMEOUT 800

struct local_browser local_browser;
struct remote_browser remote_browser;
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
browser_selection_changed (GtkTreeSelection *selection, gpointer data)
{
  struct browser *browser = data;
  browser->check_selection (NULL);
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
      enum path_type type = path_type_from_backend (browser->backend);
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
      browser->fs_ops->options & FS_OPTION_SAMPLE_ATTRS &&
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
      if (!g_str_match_string (browser->filter, iter->item.name, TRUE))
	{
	  return;
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
			       const gchar **extensions)
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
  const gchar **extensions = NULL;
  const gchar *icon = browser->fs_ops->gui_icon;
  gboolean search_mode;

  if (browser->fs_ops == &FS_LOCAL_GENERIC_OPERATIONS &&
      remote_browser.browser.fs_ops->get_ext)
    {
      extensions = g_malloc (sizeof (gchar *) * 2);
      extensions[0] =
	remote_browser.browser.fs_ops->get_ext (remote_browser.
						browser.backend,
						remote_browser.
						browser.fs_ops);
      extensions[1] = NULL;
      icon = remote_browser.browser.fs_ops->gui_icon;
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

  g_mutex_lock (&browser->mutex);
  search_mode = browser->search_mode;
  g_mutex_unlock (&browser->mutex);
  if (search_mode)
    {
      browser_iterate_dir_recursive (browser, "", &iter, icon, extensions);
    }
  else
    {
      browser_iterate_dir (browser, &iter, icon);
    }

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
}

static void
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

void
browser_local_set_columns_visibility ()
{
  gboolean sample_columns = (local_browser.browser.fs_ops->options &
			     FS_OPTION_SAMPLE_ATTRS) != 0;

  gtk_tree_view_column_set_visible
    (local_browser.tree_view_sample_frames_column, sample_columns);
  gtk_tree_view_column_set_visible
    (local_browser.tree_view_sample_rate_column, sample_columns);
  gtk_tree_view_column_set_visible
    (local_browser.tree_view_sample_duration_column, sample_columns);
  gtk_tree_view_column_set_visible
    (local_browser.tree_view_sample_channels_column, sample_columns);
  gtk_tree_view_column_set_visible
    (local_browser.tree_view_sample_bits_column, sample_columns);
  gtk_tree_view_column_set_visible
    (local_browser.tree_view_sample_midi_note_column, sample_columns);
}

void
browser_remote_set_columns_visibility ()
{
  if (remote_browser.browser.fs_ops)
    {
      gtk_tree_view_column_set_visible (remote_browser.tree_view_id_column,
					remote_browser.browser.
					fs_ops->options &
					FS_OPTION_SHOW_ID_COLUMN);
      gtk_tree_view_column_set_visible (remote_browser.tree_view_slot_column,
					remote_browser.browser.
					fs_ops->options &
					FS_OPTION_SHOW_SLOT_COLUMN);
      gtk_tree_view_column_set_visible (remote_browser.tree_view_size_column,
					remote_browser.browser.
					fs_ops->options &
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

void
browser_disable_sample_menuitems (struct browser *browser)
{
  gtk_widget_set_sensitive (browser->open_menuitem, FALSE);
  gtk_widget_set_sensitive (browser->play_menuitem, FALSE);
}

static void
elektroid_clear_other_browser_if_system (struct browser *browser)
{
  if ((browser == &local_browser.browser
       && (remote_browser.browser.backend
	   && remote_browser.browser.backend->type == BE_TYPE_SYSTEM))
      || browser == &remote_browser.browser)
    {
      browser_clear_selection (OTHER_BROWSER (browser));
    }
}

static void
elektroid_check_and_load_sample (struct browser *browser, gint count)
{
  struct item item;
  GtkTreeIter iter;
  GtkTreeModel *model;
  gboolean sample_editor = !remote_browser.browser.fs_ops
    || (remote_browser.browser.fs_ops->options & FS_OPTION_SAMPLE_EDITOR);

  if (count == 1)
    {
      browser_set_selected_row_iter (browser, &iter);
      model = GTK_TREE_MODEL (gtk_tree_view_get_model (browser->view));
      browser_set_item (model, &iter, &item);

      gtk_widget_set_sensitive (browser == &local_browser.browser ?
				local_browser.browser.open_menuitem :
				remote_browser.browser.open_menuitem,
				item.type == ELEKTROID_FILE);

      if (item.type == ELEKTROID_FILE && sample_editor)
	{
	  enum path_type type = path_type_from_backend (browser->backend);
	  gchar *sample_path = path_chain (type, browser->dir, item.name);
	  elektroid_clear_other_browser_if_system (browser);
	  editor_reset (&editor, browser);
	  g_free (editor.audio.path);
	  editor.audio.path = sample_path;
	  editor_start_load_thread (&editor);
	}
    }
  else
    {
      elektroid_clear_other_browser_if_system (browser);
      editor_reset (&editor, NULL);
    }
}

static gboolean
browser_local_check_selection (gpointer data)
{
  gint count = browser_get_selected_items_count (&local_browser.browser);

  elektroid_check_and_load_sample (&local_browser.browser, count);

  gtk_widget_set_sensitive (local_browser.browser.show_menuitem, count <= 1);
  gtk_widget_set_sensitive (local_browser.browser.rename_menuitem,
			    count == 1);
  gtk_widget_set_sensitive (local_browser.browser.delete_menuitem, count > 0);
  gtk_widget_set_sensitive (local_browser.browser.transfer_menuitem, count > 0
			    && remote_browser.browser.fs_ops
			    && remote_browser.browser.fs_ops->upload);

  return FALSE;
}

static gboolean
browser_remote_check_selection (gpointer data)
{
  gint count = browser_get_selected_items_count (&remote_browser.browser);
  gboolean dl_impl = remote_browser.browser.fs_ops
    && remote_browser.browser.fs_ops->download ? TRUE : FALSE;
  gboolean ren_impl = remote_browser.browser.fs_ops
    && remote_browser.browser.fs_ops->rename ? TRUE : FALSE;
  gboolean del_impl = remote_browser.browser.fs_ops
    && remote_browser.browser.fs_ops->delete ? TRUE : FALSE;
  gboolean sel_impl = remote_browser.browser.fs_ops
    && remote_browser.browser.fs_ops->select_item ? TRUE : FALSE;

  if (remote_browser.browser.backend->type == BE_TYPE_SYSTEM)
    {
      elektroid_check_and_load_sample (&remote_browser.browser, count);
    }

  gtk_widget_set_sensitive (remote_browser.browser.show_menuitem, count <= 1);
  gtk_widget_set_sensitive (remote_browser.browser.rename_menuitem, count == 1
			    && ren_impl);
  gtk_widget_set_sensitive (remote_browser.browser.delete_menuitem, count > 0
			    && del_impl);
  gtk_widget_set_sensitive (remote_browser.browser.transfer_menuitem,
			    count > 0 && dl_impl);

  if (count == 1 && sel_impl)
    {
      GtkTreeIter iter;
      GtkTreeModel *model;
      struct item item;
      browser_set_selected_row_iter (&remote_browser.browser, &iter);
      model = GTK_TREE_MODEL (gtk_tree_view_get_model
			      (remote_browser.browser.view));
      browser_set_item (model, &iter, &item);
      remote_browser.browser.fs_ops->select_item (remote_browser.
						  browser.backend,
						  remote_browser.browser.dir,
						  &item);
    }

  return FALSE;
}

void
browser_local_init (struct local_browser *local_browser, GtkBuilder *builder,
		    gchar *local_dir)
{
  local_browser->browser.name = "local";
  local_browser->browser.view =
    GTK_TREE_VIEW (gtk_builder_get_object (builder, "local_tree_view"));
  local_browser->browser.buttons_stack =
    GTK_WIDGET (gtk_builder_get_object (builder, "local_buttons_stack"));
  local_browser->browser.up_button =
    GTK_WIDGET (gtk_builder_get_object (builder, "local_up_button"));
  local_browser->browser.add_dir_button =
    GTK_WIDGET (gtk_builder_get_object (builder, "local_add_dir_button"));
  local_browser->browser.refresh_button =
    GTK_WIDGET (gtk_builder_get_object (builder, "local_refresh_button"));
  local_browser->browser.search_button =
    GTK_WIDGET (gtk_builder_get_object (builder, "local_search_button"));
  local_browser->browser.search_entry =
    GTK_WIDGET (gtk_builder_get_object (builder, "local_search_entry"));
  local_browser->browser.dir_entry =
    GTK_ENTRY (gtk_builder_get_object (builder, "local_dir_entry"));
  local_browser->browser.menu =
    GTK_MENU (gtk_builder_get_object (builder, "local_menu"));
  local_browser->browser.dir = local_dir;
  local_browser->browser.check_selection = browser_local_check_selection;
  local_browser->browser.fs_ops = &FS_LOCAL_SAMPLE_OPERATIONS;
  local_browser->browser.backend = NULL;
  local_browser->browser.check_callback = NULL;
  local_browser->browser.set_columns_visibility =
    browser_local_set_columns_visibility;
  local_browser->browser.sensitive_widgets = NULL;
  local_browser->browser.list_stack =
    GTK_WIDGET (gtk_builder_get_object (builder, "local_list_stack"));
  local_browser->browser.spinner =
    GTK_WIDGET (gtk_builder_get_object (builder, "local_spinner"));
  local_browser->browser.transfer_menuitem =
    GTK_WIDGET (gtk_builder_get_object (builder, "upload_menuitem"));
  local_browser->browser.play_separator =
    GTK_WIDGET (gtk_builder_get_object (builder, "local_play_separator"));
  local_browser->browser.play_menuitem =
    GTK_WIDGET (gtk_builder_get_object (builder, "local_play_menuitem"));
  local_browser->browser.open_menuitem =
    GTK_WIDGET (gtk_builder_get_object (builder, "local_open_menuitem"));
  local_browser->browser.show_menuitem =
    GTK_WIDGET (gtk_builder_get_object (builder, "local_show_menuitem"));
  local_browser->browser.rename_menuitem =
    GTK_WIDGET (gtk_builder_get_object (builder, "local_rename_menuitem"));
  local_browser->browser.delete_menuitem =
    GTK_WIDGET (gtk_builder_get_object (builder, "local_delete_menuitem"));
  local_browser->browser.tree_view_name_column =
    GTK_TREE_VIEW_COLUMN (gtk_builder_get_object
			  (builder, "local_tree_view_name_column"));

  local_browser->browser.sensitive_widgets =
    g_slist_append (local_browser->browser.sensitive_widgets,
		    local_browser->browser.view);
  local_browser->browser.sensitive_widgets =
    g_slist_append (local_browser->browser.sensitive_widgets,
		    local_browser->browser.up_button);
  local_browser->browser.sensitive_widgets =
    g_slist_append (local_browser->browser.sensitive_widgets,
		    local_browser->browser.add_dir_button);
  local_browser->browser.sensitive_widgets =
    g_slist_append (local_browser->browser.sensitive_widgets,
		    local_browser->browser.refresh_button);
  local_browser->browser.sensitive_widgets =
    g_slist_append (local_browser->browser.sensitive_widgets,
		    local_browser->browser.search_button);

  local_browser->tree_view_sample_frames_column =
    GTK_TREE_VIEW_COLUMN (gtk_builder_get_object
			  (builder, "local_tree_view_sample_frames_column"));
  local_browser->tree_view_sample_rate_column =
    GTK_TREE_VIEW_COLUMN (gtk_builder_get_object
			  (builder, "local_tree_view_sample_rate_column"));
  local_browser->tree_view_sample_duration_column =
    GTK_TREE_VIEW_COLUMN (gtk_builder_get_object
			  (builder,
			   "local_tree_view_sample_duration_column"));
  local_browser->tree_view_sample_channels_column =
    GTK_TREE_VIEW_COLUMN (gtk_builder_get_object
			  (builder,
			   "local_tree_view_sample_channels_column"));
  local_browser->tree_view_sample_bits_column =
    GTK_TREE_VIEW_COLUMN (gtk_builder_get_object
			  (builder, "local_tree_view_sample_bits_column"));
  local_browser->tree_view_sample_midi_note_column =
    GTK_TREE_VIEW_COLUMN (gtk_builder_get_object
			  (builder,
			   "local_tree_view_sample_midi_note_column"));

  browser_init (&local_browser->browser);
}

void
browser_remote_init (struct remote_browser *remote_browser,
		     GtkBuilder *builder, struct backend *backend)
{
  remote_browser->browser.name = "remote";
  remote_browser->browser.view =
    GTK_TREE_VIEW (gtk_builder_get_object (builder, "remote_tree_view"));
  remote_browser->browser.buttons_stack =
    GTK_WIDGET (gtk_builder_get_object (builder, "remote_buttons_stack"));
  remote_browser->browser.up_button =
    GTK_WIDGET (gtk_builder_get_object (builder, "remote_up_button"));
  remote_browser->browser.add_dir_button =
    GTK_WIDGET (gtk_builder_get_object (builder, "remote_add_dir_button"));
  remote_browser->browser.refresh_button =
    GTK_WIDGET (gtk_builder_get_object (builder, "remote_refresh_button"));
  remote_browser->browser.search_button =
    GTK_WIDGET (gtk_builder_get_object (builder, "remote_search_button"));
  remote_browser->browser.search_entry =
    GTK_WIDGET (gtk_builder_get_object (builder, "remote_search_entry"));
  remote_browser->browser.dir_entry =
    GTK_ENTRY (gtk_builder_get_object (builder, "remote_dir_entry"));
  remote_browser->browser.menu =
    GTK_MENU (gtk_builder_get_object (builder, "remote_menu"));
  remote_browser->browser.dir = NULL;
  remote_browser->browser.check_selection = browser_remote_check_selection;
  remote_browser->browser.fs_ops = NULL;
  remote_browser->browser.backend = backend;
  remote_browser->browser.check_callback = elektroid_check_backend;
  remote_browser->browser.set_columns_visibility =
    browser_remote_set_columns_visibility;
  remote_browser->browser.sensitive_widgets = NULL;
  remote_browser->browser.list_stack =
    GTK_WIDGET (gtk_builder_get_object (builder, "remote_list_stack"));
  remote_browser->browser.spinner =
    GTK_WIDGET (gtk_builder_get_object (builder, "remote_spinner"));
  remote_browser->browser.transfer_menuitem =
    GTK_WIDGET (gtk_builder_get_object (builder, "download_menuitem"));
  remote_browser->browser.play_separator =
    GTK_WIDGET (gtk_builder_get_object (builder, "remote_play_separator"));
  remote_browser->browser.play_menuitem =
    GTK_WIDGET (gtk_builder_get_object (builder, "remote_play_menuitem"));
  remote_browser->browser.options_separator =
    GTK_WIDGET (gtk_builder_get_object (builder, "remote_options_separator"));
  remote_browser->browser.open_menuitem =
    GTK_WIDGET (gtk_builder_get_object (builder, "remote_open_menuitem"));
  remote_browser->browser.show_menuitem =
    GTK_WIDGET (gtk_builder_get_object (builder, "remote_show_menuitem"));
  remote_browser->browser.actions_separator =
    GTK_WIDGET (gtk_builder_get_object (builder, "remote_actions_separator"));
  remote_browser->browser.rename_menuitem =
    GTK_WIDGET (gtk_builder_get_object (builder, "remote_rename_menuitem"));
  remote_browser->browser.delete_menuitem =
    GTK_WIDGET (gtk_builder_get_object (builder, "remote_delete_menuitem"));
  remote_browser->browser.tree_view_name_column =
    GTK_TREE_VIEW_COLUMN (gtk_builder_get_object
			  (builder, "remote_tree_view_name_column"));

  remote_browser->browser.sensitive_widgets =
    g_slist_append (remote_browser->browser.sensitive_widgets,
		    GTK_WIDGET (gtk_builder_get_object
				(builder, "devices_combo")));
  remote_browser->browser.sensitive_widgets =
    g_slist_append (remote_browser->browser.sensitive_widgets,
		    GTK_WIDGET (gtk_builder_get_object
				(builder, "refresh_devices_button")));
  remote_browser->browser.sensitive_widgets =
    g_slist_append (remote_browser->browser.sensitive_widgets,
		    GTK_WIDGET (gtk_builder_get_object
				(builder, "fs_combo")));
  remote_browser->browser.sensitive_widgets =
    g_slist_append (remote_browser->browser.sensitive_widgets,
		    remote_browser->browser.view);
  remote_browser->browser.sensitive_widgets =
    g_slist_append (remote_browser->browser.sensitive_widgets,
		    remote_browser->browser.up_button);
  remote_browser->browser.sensitive_widgets =
    g_slist_append (remote_browser->browser.sensitive_widgets,
		    remote_browser->browser.add_dir_button);
  remote_browser->browser.sensitive_widgets =
    g_slist_append (remote_browser->browser.sensitive_widgets,
		    remote_browser->browser.refresh_button);
  remote_browser->browser.sensitive_widgets =
    g_slist_append (remote_browser->browser.sensitive_widgets,
		    remote_browser->browser.search_button);

  remote_browser->tree_view_id_column =
    GTK_TREE_VIEW_COLUMN (gtk_builder_get_object
			  (builder, "remote_tree_view_id_column"));
  remote_browser->tree_view_slot_column =
    GTK_TREE_VIEW_COLUMN (gtk_builder_get_object
			  (builder, "remote_tree_view_slot_column"));
  remote_browser->tree_view_size_column =
    GTK_TREE_VIEW_COLUMN (gtk_builder_get_object
			  (builder, "remote_tree_view_size_column"));

  browser_init (&remote_browser->browser);
}
