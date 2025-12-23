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
#include "backend.h"
#include "browser.h"
#include "editor.h"
#include "elektroid.h"
#include "local.h"
#include "name_window.h"
#include "notifier.h"
#include "preferences.h"
#include "progress_window.h"
#include "sample.h"
#include "maction.h"
#include "utils.h"

#define OTHER_BROWSER(b) (b == &local_browser ? &remote_browser : &local_browser)
#define EDITOR_IS_AVAILABLE (local_browser.fs_ops && \
                             local_browser.fs_ops->options & FS_OPTION_SAMPLE_EDITOR ? TRUE : FALSE)

#define DND_TIMEOUT 800

#define TREEVIEW_SCROLL_LINES 2
#define TREEVIEW_EDGE_SIZE 20

#define PROGRESS_DELETE_THRESHOLD 25

#define DIR_ICON "elektroid-folder-symbolic"

#define SEARCH_PARAM_SEPARATOR_CHAR ':'
#define SEARCH_TEMPO_DIFF 10
#define SEARCH_PARAM_UNSET -1
#define SEARCH_PARAM_NOTE_NOT_FOUND -2

// ITEM_TYPE_DIR do not have an initialized sample_info.
// The remote editor might not have tags even though the filesystem is an editor.
#define ITEM_HAS_TAGS(i,b) ((i)->type == ITEM_TYPE_FILE && \
                            ((b)->fs_ops->options & FS_OPTION_SHOW_SAMPLE_COLUMNS) && \
                             (i)->sample_info.tags != NULL)

static void browser_remote_reset_dnd ();

enum
{
  TARGET_STRING,
};

static const GtkTargetEntry TARGET_ENTRIES_LOCAL_DST[] = {
  {TEXT_URI_LIST_STD, 0, TARGET_STRING},
  {TEXT_URI_LIST_ELEKTROID, GTK_TARGET_SAME_APP | GTK_TARGET_OTHER_WIDGET,
   TARGET_STRING}
};

static const GtkTargetEntry TARGET_ENTRIES_LOCAL_SRC[] = {
  {TEXT_URI_LIST_STD, 0, TARGET_STRING}
};

static const GtkTargetEntry TARGET_ENTRIES_REMOTE_SYSTEM_DST[] = {
  {TEXT_URI_LIST_STD, 0, TARGET_STRING},
  {TEXT_URI_LIST_ELEKTROID, GTK_TARGET_SAME_APP, TARGET_STRING}
};

static const GtkTargetEntry TARGET_ENTRIES_REMOTE_SYSTEM_SRC[] = {
  {TEXT_URI_LIST_ELEKTROID, 0, TARGET_STRING}
};

static const GtkTargetEntry TARGET_ENTRIES_REMOTE_MIDI_DST[] = {
  {TEXT_URI_LIST_STD, 0, TARGET_STRING},
  {TEXT_URI_LIST_ELEKTROID, GTK_TARGET_SAME_APP, TARGET_STRING}
};

static const GtkTargetEntry TARGET_ENTRIES_REMOTE_MIDI_DST_SLOT[] = {
  {TEXT_URI_LIST_STD, 0, TARGET_STRING}
};

static const GtkTargetEntry TARGET_ENTRIES_REMOTE_MIDI_SRC[] = {
  {TEXT_URI_LIST_ELEKTROID, GTK_TARGET_SAME_APP, TARGET_STRING}
};

static const GtkTargetEntry TARGET_ENTRIES_UP_BUTTON_DST[] = {
  {TEXT_URI_LIST_STD, 0, TARGET_STRING},
  {TEXT_URI_LIST_ELEKTROID, GTK_TARGET_SAME_APP, TARGET_STRING}
};

extern guint batch_id;
extern GtkWindow *main_window;

struct browser local_browser;
struct browser remote_browser;
static struct backend backend;

static GtkListStore *notes_list_store;

struct browser_add_dentry_item_data
{
  struct browser *browser;
  struct item item;
  const gchar *icon;
  gchar *rel_path;
};

static void
browser_clear_dnd_function (struct browser *browser)
{
  if (browser->dnd_timeout_function_id)
    {
      g_source_remove (browser->dnd_timeout_function_id);
      browser->dnd_timeout_function_id = 0;
    }
}

static void
browser_set_dnd_function (struct browser *browser, GSourceFunc function)
{
  browser_clear_dnd_function (browser);
  browser->dnd_timeout_function_id = g_timeout_add (DND_TIMEOUT, function,
						    browser);
}

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

void
browser_set_item (GtkTreeModel *model, GtkTreeIter *iter, struct item *item)
{
  gchar *name;
  gtk_tree_model_get (model, iter, BROWSER_LIST_STORE_TYPE_FIELD, &item->type,
		      BROWSER_LIST_STORE_NAME_FIELD, &name,
		      BROWSER_LIST_STORE_SIZE_FIELD, &item->size,
		      BROWSER_LIST_STORE_ID_FIELD, &item->id, -1);
  item_set_name (item, name);
  g_free (name);
}

static gint
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

static gint
browser_set_selected_row_iter (struct browser *browser, GtkTreeIter *iter)
{
  gint index, *indices;
  GtkTreeModel *model;
  GtkTreeSelection *selection =
    gtk_tree_view_get_selection (GTK_TREE_VIEW (browser->view));
  GList *paths = gtk_tree_selection_get_selected_rows (selection, &model);
  GtkTreePath *row = g_list_nth_data (paths, 0);

  indices = gtk_tree_path_get_indices (row);
  index = *indices;
  gtk_tree_model_get_iter (model, iter, row);
  g_list_free_full (paths, (GDestroyNotify) gtk_tree_path_free);
  return index;
}

static gint
browser_get_selected_items_count (struct browser *browser)
{
  GtkTreeSelection *selection =
    gtk_tree_view_get_selection (GTK_TREE_VIEW (browser->view));
  return gtk_tree_selection_count_selected_rows (selection);
}

static void
browser_clear_other_browser_selection_if_system (struct browser *browser)
{
  struct browser *other = OTHER_BROWSER (browser);
  if (BROWSER_IS_SYSTEM (other))
    {
      browser_clear_selection (other);
    }
}

static void
browser_check_selection (gpointer data)
{
  gint index;
  struct item item;
  GtkTreeIter iter;
  GtkTreeModel *model;
  struct browser *browser = data;
  gint count = browser_get_selected_items_count (browser);
  gboolean sel_impl = browser->fs_ops
    && browser->fs_ops->select_item ? TRUE : FALSE;

  if (!browser->selection_active)
    {
      return;
    }

  if (count != 1)
    {
      if (EDITOR_IS_AVAILABLE && BROWSER_IS_SYSTEM (browser))
	{
	  browser_clear_other_browser_selection_if_system (browser);
	  editor_reset (NULL);
	}
      browser->last_selected_index = -1;
      return;
    }

  index = browser_set_selected_row_iter (browser, &iter);
  model = GTK_TREE_MODEL (gtk_tree_view_get_model (browser->view));
  browser_set_item (model, &iter, &item);

  if (item.type == ITEM_TYPE_DIR)
    {
      return;
    }

  if (index == browser->last_selected_index)
    {
      return;
    }

  browser->last_selected_index = index;

  if (EDITOR_IS_AVAILABLE && BROWSER_IS_SYSTEM (browser))
    {
      enum path_type type = backend_get_path_type (browser->backend);
      gchar *sample_path = path_chain (type, browser->dir, item.name);

      browser_clear_other_browser_selection_if_system (browser);
      editor_reset (browser);
      editor_start_load_thread (sample_path);
    }

  if (!sel_impl)
    {
      return;
    }

  remote_browser.fs_ops->select_item (browser->backend, browser->dir, &item);
}

static void
browser_local_set_popup_visibility ()
{
  gboolean ul_avail = remote_browser.fs_ops &&
    !(remote_browser.fs_ops->options & FS_OPTION_SLOT_STORAGE)
    && remote_browser.fs_ops->upload;
  gboolean edit_avail =
    local_browser.fs_ops->options & FS_OPTION_SAMPLE_EDITOR;

  gtk_widget_set_visible (local_browser.popover_transfer_button, ul_avail);
  gtk_widget_set_visible (local_browser.popover_play_separator, ul_avail);
  gtk_widget_set_visible (local_browser.popover_play_button, edit_avail);
  gtk_widget_set_visible (local_browser.popover_options_separator,
			  edit_avail);
}

static void
browser_local_set_popup_sensitivity (gint count, gboolean file)
{
  gboolean ul_avail = remote_browser.fs_ops &&
    !(remote_browser.fs_ops->options & FS_OPTION_SLOT_STORAGE)
    && remote_browser.fs_ops->upload;
  gboolean editing = editor_get_browser () == &local_browser;

  gtk_widget_set_sensitive (local_browser.popover_transfer_button, count > 0
			    && ul_avail);
  gtk_widget_set_sensitive (local_browser.popover_play_button, file
			    && editing);
  gtk_widget_set_sensitive (local_browser.popover_open_button, file);
  gtk_widget_set_sensitive (local_browser.popover_show_button, count <= 1);
  gtk_widget_set_sensitive (local_browser.popover_rename_button, count == 1);
  gtk_widget_set_sensitive (local_browser.popover_delete_button, count > 0);
}

static void
browser_remote_set_popup_visibility ()
{
  gboolean dl_impl = remote_browser.fs_ops
    && remote_browser.fs_ops->download ? TRUE : FALSE;
  gboolean edit_avail = remote_browser.fs_ops
    && remote_browser.fs_ops->options & FS_OPTION_SAMPLE_EDITOR;
  gboolean system = remote_browser.fs_ops
    && remote_browser.backend->type == BE_TYPE_SYSTEM;

  gtk_widget_set_visible (remote_browser.popover_transfer_button, dl_impl);
  gtk_widget_set_visible (remote_browser.popover_play_separator, dl_impl);
  gtk_widget_set_visible (remote_browser.popover_play_button, system
			  && edit_avail);
  gtk_widget_set_visible (remote_browser.popover_options_separator, system
			  && edit_avail);
  gtk_widget_set_visible (remote_browser.popover_open_button, system);
  gtk_widget_set_visible (remote_browser.popover_show_button, system);
  gtk_widget_set_visible (remote_browser.popover_actions_separator, system);
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
  gboolean editing = editor_get_browser () == &remote_browser;
  gboolean system = remote_browser.fs_ops
    && remote_browser.backend->type == BE_TYPE_SYSTEM;

  gtk_widget_set_sensitive (remote_browser.popover_transfer_button, count > 0
			    && dl_impl);
  gtk_widget_set_sensitive (remote_browser.popover_play_button, file
			    && editing);
  gtk_widget_set_sensitive (remote_browser.popover_open_button, file);
  gtk_widget_set_sensitive (remote_browser.popover_show_button, count <= 1
			    && system);
  gtk_widget_set_sensitive (remote_browser.popover_rename_button, count == 1
			    && ren_impl);
  gtk_widget_set_sensitive (remote_browser.popover_delete_button, count > 0
			    && del_impl);
}

static void
browser_set_popup_sensitivity (struct browser *browser)
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
      file = item.type == ITEM_TYPE_FILE;
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

static void
browser_selection_changed (GtkTreeSelection *selection, gpointer data)
{
  struct browser *browser = data;
  browser_check_selection (browser);
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
  browser->last_selected_index = -1;
}

static void
browser_clear (struct browser *browser)
{
  GtkListStore *list_store =
    GTK_LIST_STORE (gtk_tree_view_get_model (browser->view));
  GtkTreeSelection *selection =
    gtk_tree_view_get_selection (GTK_TREE_VIEW (browser->view));
  GtkEntryBuffer *buf = gtk_entry_get_buffer (GTK_ENTRY (browser->dir_entry));

  gtk_entry_buffer_set_text (buf, browser->dir ? browser->dir : "", -1);
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

static gchar *
path_dir_get_parent (const gchar *dir)
{
#if defined(__MINGW32__) | defined(__MINGW64__)
  if (!strcmp (dir, TOPMOST_DIR_WINDOWS))
    {
      return NULL;
    }
#else
  if (!strcmp (dir, TOPMOST_DIR_UNIX))
    {
      return NULL;
    }
#endif

  gchar *parent = g_path_get_dirname (dir);

#if defined(__MINGW32__) | defined(__MINGW64__)
  if (!strcmp (dir, parent))
    {
      g_free (parent);
      return strdup (TOPMOST_DIR_WINDOWS);
    }
#endif

  return parent;
}

static void
browser_load_preferences_dir (struct browser *browser)
{
  const gchar *aux = preferences_get_string (browser->pref_key_dir);
  preferences_set_string (browser->pref_key_dir,
			  get_system_startup_path (aux));
  browser->dir = strdup (preferences_get_string (browser->pref_key_dir));
}

static void
browser_save_preferences_dir (struct browser *browser)
{
  if (BROWSER_IS_SYSTEM (browser))
    {
      preferences_set_string (browser->pref_key_dir, strdup (browser->dir));
    }
}

void
browser_remote_set_fs_operations (const struct fs_operations *fs_ops)
{
  gboolean editor_visible;

  if (fs_ops)
    {
      editor_visible =
	fs_ops->options & FS_OPTION_SAMPLE_EDITOR ? TRUE : FALSE;

      remote_browser.fs_ops = fs_ops;

      if (editor_visible)
	{
	  local_browser.fs_ops = &FS_LOCAL_SAMPLE_OPERATIONS;
	}
      else
	{
	  local_browser.fs_ops = &FS_LOCAL_GENERIC_OPERATIONS;
	  editor_reset (NULL);
	}

      if (remote_browser.backend->type == BE_TYPE_SYSTEM)
	{
	  if (!remote_browser.dir)
	    {
	      remote_browser.dir =
		strdup (get_system_startup_path
			(preferences_get_string (PREF_KEY_REMOTE_DIR)));
	    }
	}
      else
	{
	  if (remote_browser.dir)
	    {
	      g_free (remote_browser.dir);
	    }
	  remote_browser.dir = strdup ("/");
	}

      browser_remote_reset_dnd ();

      browser_close_search (NULL, &local_browser);	//This triggers a refresh
      browser_update_fs_options (&local_browser);

      browser_close_search (NULL, &remote_browser);	//This triggers a refresh
      browser_update_fs_options (&remote_browser);
    }
  else
    {
      editor_visible = TRUE;
      local_browser.fs_ops = &FS_LOCAL_SAMPLE_OPERATIONS;
      browser_update_fs_options (&local_browser);
      browser_load_dir (&local_browser);

      browser_reset (&remote_browser);
      browser_update_fs_options (&remote_browser);
    }

  editor_set_visible (editor_visible);
}

void
browser_go_up (GtkWidget *object, gpointer data)
{
  struct browser *browser = data;
  gboolean reload = FALSE;

  g_mutex_lock (&browser->mutex);
  if (!browser->loading)
    {
      gchar *new_path = path_dir_get_parent (browser->dir);
      if (new_path)
	{
	  g_free (browser->dir);
	  browser->dir = new_path;
	  browser_save_preferences_dir (browser);
	  reload = TRUE;
	}
    }
  g_mutex_unlock (&browser->mutex);

  if (reload || !browser->notifier)
    {
      browser_load_dir (browser);
    }
}

gboolean
browser_no_progress_needed (struct browser *browser)
{
  return (BROWSER_IS_SYSTEM (browser) &&
	  browser_get_selected_items_count (browser) <=
	  PROGRESS_DELETE_THRESHOLD);
}

static void
browser_delete_items_response (GtkDialog *dialog, gint response_id,
			       gpointer user_data)
{
  struct browser *browser = user_data;

  gtk_widget_destroy (GTK_WIDGET (dialog));

  if (response_id == GTK_RESPONSE_ACCEPT)
    {
      struct browser_delete_items_data *delete_data;

      delete_data = g_malloc (sizeof (struct browser_delete_items_data));
      delete_data->browser = browser;

      if (browser_no_progress_needed (browser))
	{
	  delete_data->has_progress_window = FALSE;
	  elektroid_delete_items_runner (delete_data);
	  browser_load_dir_if_needed (browser);
	}
      else
	{
	  delete_data->has_progress_window = TRUE;
	  progress_window_open (elektroid_delete_items_runner, NULL, NULL,
				delete_data, PROGRESS_TYPE_PULSE,
				_("Deleting Files"), _("Deleting..."), TRUE);
	}
    }
}

static void
browser_delete_items (GtkWidget *object, gpointer user_data)
{
  GtkWidget *dialog;
  struct browser *browser = user_data;

  dialog = gtk_message_dialog_new (main_window, GTK_DIALOG_MODAL,
				   GTK_MESSAGE_ERROR, GTK_BUTTONS_NONE,
				   _
				   ("Are you sure you want to delete the selected items?"));
  gtk_dialog_add_buttons (GTK_DIALOG (dialog), _("_Cancel"),
			  GTK_RESPONSE_CANCEL, _("_Delete"),
			  GTK_RESPONSE_ACCEPT, NULL);
  gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_ACCEPT);

  g_signal_connect (dialog, "response",
		    G_CALLBACK (browser_delete_items_response), browser);

  gtk_widget_set_visible (dialog, TRUE);
}

static gchar *
browser_get_item_path (struct browser *browser, struct item *item)
{
  gchar *filename = item_get_filename (item, browser->fs_ops->options);
  enum path_type type = backend_get_path_type (browser->backend);
  gchar *path = path_chain (type, browser->dir, filename);
  debug_print (1, "Using %s path for item %s (id %d)...", path, item->name,
	       item->id);
  g_free (filename);
  return path;
}

gchar *
browser_get_name_path (struct browser *browser, const gchar *name)
{
  if (browser->fs_ops->options & FS_OPTION_SLOT_STORAGE)
    {
      return strdup (name);
    }
  else
    {
      enum path_type type = backend_get_path_type (browser->backend);
      return path_chain (type, browser->dir, name);
    }
}

static gchar *
browser_get_selected_item_path (struct browser *browser)
{
  GtkTreeIter iter;
  struct item item;
  GtkTreeModel *model;

  browser_set_selected_row_iter (browser, &iter);
  model = GTK_TREE_MODEL (gtk_tree_view_get_model (browser->view));
  browser_set_item (model, &iter, &item);

  return browser_get_item_path (browser, &item);
}

static void
browser_rename_accept (gpointer source, const gchar *name)
{
  gint err;
  gchar *old_path, *new_path;
  struct browser *browser = source;

  old_path = browser_get_selected_item_path (browser);
  new_path = browser_get_name_path (browser, name);

  err = browser->fs_ops->rename (browser->backend, old_path, new_path);
  if (err)
    {
      elektroid_show_error_msg (_("Error while renaming to “%s”: %s."),
				new_path, g_strerror (-err));
    }
  else
    {
      browser_load_dir_if_needed (browser);
    }

  g_free (new_path);
  g_free (old_path);
}

static void
browser_rename_item (GtkWidget *object, gpointer data)
{
  const gchar *ext;
  gint sel_len, ext_len;
  GtkTreeIter iter;
  struct item item;
  struct browser *browser = data;
  GtkTreeModel *model =
    GTK_TREE_MODEL (gtk_tree_view_get_model (browser->view));

  browser_set_selected_row_iter (browser, &iter);
  browser_set_item (model, &iter, &item);

  sel_len = strlen (item.name);
  ext = filename_get_ext (item.name);
  ext_len = strlen (ext);
  if (ext_len)
    {
      sel_len -= ext_len + 1;
    }

  name_window_edit_text (_("Rename"),
			 browser->fs_ops->max_name_len, item.name, 0,
			 sel_len, browser_rename_accept, browser);
}

static void
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

  if (item.type == ITEM_TYPE_DIR)
    {
      enum path_type type = backend_get_path_type (browser->backend);
      gchar *new_dir = path_chain (type, browser->dir, item.name);
      g_free (browser->dir);
      browser->dir = new_dir;
      browser_save_preferences_dir (browser);
      browser_close_search (NULL, browser);	//This triggers a refresh
    }
}

static void
item_ref_tags_if_avail (struct item *item, struct browser *browser)
{
  if (ITEM_HAS_TAGS (item, browser))
    {
      g_hash_table_ref (item->sample_info.tags);
    }
}

static void
item_unref_tags_if_avail (struct item *item, struct browser *browser)
{
  if (ITEM_HAS_TAGS (item, browser))
    {
      g_hash_table_unref (item->sample_info.tags);
    }
}

static gchar *
browser_get_tags (const struct sample_info *sample_info)
{
  const gchar *ikey;
  gchar **tags, **t;
  GString *tagss;

  if (!sample_info->tags)
    {
      return NULL;
    }

  ikey = sample_info_get_tag (sample_info, SAMPLE_INFO_TAG_IKEY);
  if (!ikey)
    {
      return NULL;
    }

  tags = g_strsplit (ikey, "; ", 0);
  tagss = g_string_new (NULL);
  t = tags;
  while (*t)
    {
      gchar *escaped = g_markup_escape_text (*t, -1);
      g_string_append_printf (tagss,
			      "<span bgcolor=\"gray\" bgalpha=\"20%%\">%s</span>%s",
			      escaped, *(t + 1) ? "  " : "");
      g_free (escaped);
      t++;
    }

  g_strfreev (tags);

  return g_string_free_and_steal (tagss);
}

static void
browser_get_note_name (guint32 note, GValue *v)
{
  GtkTreeIter note_iter;

  gtk_tree_model_get_iter_first (GTK_TREE_MODEL (notes_list_store),
				 &note_iter);
  for (gint i = 0; i < note; i++)
    {
      gtk_tree_model_iter_next (GTK_TREE_MODEL (notes_list_store),
				&note_iter);
    }
  gtk_tree_model_get_value (GTK_TREE_MODEL (notes_list_store),
			    &note_iter, 0, v);
}

// Note names are not translated

static gint
browser_get_note_num (const gchar *any)
{
  gint i, num;
  gchar *upper;
  GtkTreeIter note_iter;
  GValue v = G_VALUE_INIT;

  //As note names are stored in upper case, ensuring the first letter is in upper case is enough;
  upper = strdup (any);
  if (upper[0] >= 0x61)
    {
      upper[0] -= 0x20;
    }

  i = 0;
  num = SEARCH_PARAM_NOTE_NOT_FOUND;
  gtk_tree_model_get_iter_first (GTK_TREE_MODEL (notes_list_store),
				 &note_iter);
  do
    {
      gtk_tree_model_get_value (GTK_TREE_MODEL (notes_list_store),
				&note_iter, 0, &v);
      const gchar *note = g_value_get_string (&v);
      gint cmp = strcmp (note, upper);
      g_value_unset (&v);
      if (!cmp)
	{
	  num = i;
	  break;
	}
      i++;
    }
  while (gtk_tree_model_iter_next (GTK_TREE_MODEL (notes_list_store),
				   &note_iter));

  g_free (upper);
  return num;
}

static gint
browser_add_dentry_item (gpointer data)
{
  gchar *hsize;
  gdouble time;
  gchar *name;
  gchar label[LABEL_MAX];
  GtkTreeIter iter;
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
				     ITEM_TYPE_DIR ? DIR_ICON :
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

  if (item->type == ITEM_TYPE_FILE &&
      item->sample_info.frames &&
      browser->fs_ops->options & FS_OPTION_SHOW_SAMPLE_COLUMNS)
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

      if (item->sample_info.midi_note <= 127)
	{
	  browser_get_note_name (item->sample_info.midi_note, &v);
	}
      else
	{
	  g_value_init (&v, G_TYPE_STRING);
	  g_value_set_string (&v, "-");
	}
      if (item->sample_info.midi_fraction)
	{
	  gchar note[LABEL_MAX];
	  snprintf (note, LABEL_MAX, "%s +%d %s", g_value_get_string (&v),
		    midi_fraction_to_cents (item->sample_info.midi_fraction),
		    _("cents"));
	  g_value_unset (&v);
	  g_value_init (&v, G_TYPE_STRING);
	  g_value_set_string (&v, note);
	}
      gtk_list_store_set_value (list_store, &iter,
				BROWSER_LIST_STORE_SAMPLE_NOTE_FIELD, &v);
      g_value_unset (&v);

      gchar *tags = browser_get_tags (&item->sample_info);
      if (tags)
	{
	  g_value_init (&v, G_TYPE_STRING);
	  g_value_set_string (&v, tags);
	  gtk_list_store_set_value (list_store, &iter,
				    BROWSER_LIST_STORE_SAMPLE_TAGS_FIELD, &v);
	  g_value_unset (&v);
	  g_free (tags);
	}
    }

  if (item->type == ITEM_TYPE_FILE &&
      browser->fs_ops->options & FS_OPTION_SHOW_INFO_COLUMN)
    {
      g_value_init (&v, G_TYPE_STRING);
      g_value_set_string (&v, item->object_info);
      gtk_list_store_set_value (list_store, &iter,
				BROWSER_LIST_STORE_INFO_FIELD, &v);
      g_value_unset (&v);
    }

  if (audio.path && editor_get_browser () == browser)
    {
      // The reload might be triggered from the GUI, some user action (i.e. saving) or by the notifier.
      name = path_chain (PATH_SYSTEM, browser->dir, add_data->rel_path);
      if (!strcmp (audio.path, name))
	{
	  if (!browser->reload_item_in_editor)
	    {
	      g_signal_handlers_block_by_func (selection,
					       G_CALLBACK
					       (browser_selection_changed),
					       browser);
	    }
	  gtk_tree_selection_select_iter (selection, &iter);	// item selection and sample reload
	  if (!browser->reload_item_in_editor)
	    {
	      g_signal_handlers_unblock_by_func (selection,
						 G_CALLBACK
						 (browser_selection_changed),
						 browser);
	    }
	}
      g_free (name);
    }

  item_unref_tags_if_avail (item, browser);

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
  while (browser->thread)
    {
      g_thread_join (browser->thread);
      browser->thread = NULL;
      //Wait for every pending call to browser_add_dentry_item scheduled from the thread
      while (gtk_events_pending ())
	{
	  gtk_main_iteration ();
	}
    }
}

static gboolean
browser_load_dir_runner_update_ui (gpointer data)
{
  guint pending_req;
  struct browser *browser = data;
  GtkTreeSelection *selection =
    gtk_tree_view_get_selection (GTK_TREE_VIEW (browser->view));
  gboolean active = BROWSER_IS_SYSTEM (browser);

  browser_wait (browser);

  if (browser->check_callback)
    {
      browser->check_callback ();
    }
  gtk_tree_view_columns_autosize (browser->view);

  if (!browser->search_mode)
    {
      gtk_widget_grab_focus (GTK_WIDGET (browser->view));
      notifier_update_dir (browser->notifier, active);
    }

  //Unlock browser
  g_slist_foreach (browser->sensitive_widgets, browser_widget_set_sensitive,
		   NULL);

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
      if (!browser->search_mode)
	{
	  GtkTreeModel *model =
	    GTK_TREE_MODEL (gtk_tree_view_get_model (browser->view));
	  if (gtk_tree_model_iter_n_children (model, NULL))
	    {
	      GtkTreePath *first = gtk_tree_path_new_first ();
	      gtk_tree_view_scroll_to_cell (browser->view, first, NULL, FALSE,
					    0, 0);
	      gtk_tree_path_free (first);
	      gtk_widget_grab_focus (GTK_WIDGET (browser->view));
	    }
	}

      //If audio.path is empty is a recording buffer.
      if (editor_get_browser () == browser && audio.path)
	{
	  editor_reset (NULL);
	}
    }

  g_mutex_lock (&browser->mutex);
  pending_req = browser->pending_req;
  browser->loading = FALSE;
  g_mutex_unlock (&browser->mutex);

  if (pending_req)
    {
      debug_print (1, "Processing pending requests...");
      browser_load_dir (browser);
    }

  return FALSE;
}

static gboolean
browser_values_match_filter (struct browser_search_options *search_options,
			     const gchar *name, const gchar *rel_path,
			     const gchar *object_info,
			     const struct sample_info *sample_info)
{
  gboolean matched;

  // When set, tempo is always a valid number even when the user has written nothing or an invalid number in the GUI.
  // When set, note is either valid or invalid (not found).

  if (search_options->note == SEARCH_PARAM_NOTE_NOT_FOUND)
    {
      return FALSE;
    }

  if (search_options->tempo != SEARCH_PARAM_UNSET)
    {
      if (!sample_info)
	{
	  return FALSE;
	}
      if (sample_info->tempo > search_options->tempo + SEARCH_TEMPO_DIFF ||
	  sample_info->tempo < search_options->tempo - SEARCH_TEMPO_DIFF)
	{
	  return FALSE;
	}
    }

  if (search_options->note != SEARCH_PARAM_UNSET)
    {
      if (!sample_info)
	{
	  return FALSE;
	}
      if (sample_info->midi_note != search_options->note)
	{
	  return FALSE;
	}
    }

  matched = TRUE;
  for (GSList * l = search_options->tokens; l; l = l->next)
    {
      if (token_is_in_text (l->data, name))
	{
	  continue;
	}

      if (token_is_in_text (l->data, rel_path))
	{
	  continue;
	}

      if (object_info)
	{
	  if (token_is_in_text (l->data, object_info))
	    {
	      continue;
	    }
	}

      if (sample_info && sample_info->tags)
	{
	  const gchar *ikey = sample_info_get_tag (sample_info,
						   SAMPLE_INFO_TAG_IKEY);
	  if (ikey && token_is_in_text (l->data, ikey))
	    {
	      continue;
	    }
	}

      matched = FALSE;
      break;
    }

  return matched;
}

static void
browser_iterate_dir_add (struct browser *browser,
			 struct item_iterator *iter,
			 const gchar *icon, struct item *item,
			 gchar *rel_path)
{
  const gchar *object_info;
  const struct sample_info *sample_info;
  struct browser_add_dentry_item_data *data;

  if (browser->search_options.ready)
    {
      if (iter->item.type == ITEM_TYPE_FILE)
	{
	  object_info =
	    browser->fs_ops->options & FS_OPTION_SHOW_INFO_COLUMN ?
	    iter->item.object_info : NULL;
	  sample_info =
	    browser->fs_ops->options & FS_OPTION_SAMPLE_EDITOR ?
	    &iter->item.sample_info : NULL;
	}
      else
	{
	  object_info = NULL;
	  sample_info = NULL;
	}

      if (!browser_values_match_filter (&browser->search_options,
					iter->item.name, rel_path,
					object_info, sample_info))
	{
	  return;
	}
    }

  data = g_malloc (sizeof (struct browser_add_dentry_item_data));
  data->browser = browser;
  memcpy (&data->item, &iter->item, sizeof (struct item));
  item_ref_tags_if_avail (&iter->item, browser);
  data->icon = icon;
  data->rel_path = rel_path;
  g_idle_add (browser_add_dentry_item, data);
}

static void
browser_iterate_dir (struct browser *browser, struct item_iterator *iter,
		     const gchar *icon)
{
  gboolean loading = TRUE;

  while (loading && !item_iterator_next (iter))
    {
      browser_iterate_dir_add (browser, iter, icon, &iter->item,
			       strdup (iter->item.name));

      item_unref_tags_if_avail (&iter->item, browser);

      g_mutex_lock (&browser->mutex);
      loading = browser->loading;
      g_mutex_unlock (&browser->mutex);
    }
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
  enum path_type type = backend_get_path_type (browser->backend);

  while (loading && !item_iterator_next (iter))
    {
      child_rel_dir = path_chain (type, rel_dir, iter->item.name);

      browser_iterate_dir_add (browser, iter, icon, &iter->item,
			       strdup (child_rel_dir));

      if (iter->item.type == ITEM_TYPE_DIR)
	{
	  child_dir = path_chain (type, browser->dir, child_rel_dir);
	  err = browser->fs_ops->readdir (browser->backend, &child_iter,
					  child_dir, extensions);
	  if (!err)
	    {
	      browser_iterate_dir_recursive (browser, child_rel_dir,
					     &child_iter, icon, extensions);
	      item_iterator_free (&child_iter);
	    }
	  g_free (child_dir);
	}
      g_free (child_rel_dir);

      g_mutex_lock (&browser->mutex);
      loading = browser->loading;
      g_mutex_unlock (&browser->mutex);
    }
}

const gchar *
browser_get_icon (struct browser *browser)
{
  const gchar *icon;

  if (browser == &remote_browser)
    {
      icon = remote_browser.fs_ops->gui_icon;
    }
  else
    {
      if (remote_browser.fs_ops)
	{
	  if (remote_browser.fs_ops->upload)
	    {
	      icon = remote_browser.fs_ops->gui_icon;
	    }
	  else
	    {
	      icon = local_browser.fs_ops->gui_icon;
	    }
	}
      else
	{
	  icon = local_browser.fs_ops->gui_icon;
	}
    }

  return icon;
}

const gchar **
browser_get_exts (struct browser *browser)
{
  const gchar **exts;

  if (browser == &remote_browser)
    {
      exts = remote_browser.fs_ops->get_exts (remote_browser.backend,
					      remote_browser.fs_ops);
    }
  else
    {
      if (remote_browser.fs_ops)
	{
	  exts = remote_browser.fs_ops->get_exts (remote_browser.backend,
						  remote_browser.fs_ops);
	}
      else
	{
	  //If !remote_browser.fs_ops, only FS_LOCAL_SAMPLE_OPERATIONS is used, which implements get_exts.
	  exts = local_browser.fs_ops->get_exts (remote_browser.backend,
						 remote_browser.fs_ops);
	}
    }

  return exts;
}

static gpointer
browser_load_dir_runner (gpointer data)
{
  gint err;
  struct browser *browser = data;
  struct item_iterator iter;
  const gchar **exts;
  const gchar *icon;
  gboolean search_mode;

  exts = browser_get_exts (browser);
  icon = browser_get_icon (browser);

  g_idle_add (browser_load_dir_runner_show_spinner_and_lock_browser, browser);
  err = browser->fs_ops->readdir (browser->backend, &iter, browser->dir,
				  exts);
  g_idle_add (browser_load_dir_runner_hide_spinner, browser);
  if (err)
    {
      error_print ("Error while opening '%s' dir", browser->dir);
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

  item_iterator_free (&iter);

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
      browser->pending_req++;
      debug_print (1,
		   "Browser already loading. %d pending requests. Skipping load...",
		   browser->pending_req);
      g_mutex_unlock (&browser->mutex);
      return FALSE;
    }
  else
    {
      browser->last_selected_index = -1;
      browser->loading = TRUE;
      browser->pending_req = 0;
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
      !browser->notifier->monitor)
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
  gboolean slot = browser->fs_ops &&
    browser->fs_ops->options & FS_OPTION_SLOT_STORAGE;

  if (browser->search_mode || !slot)
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
  browser->set_popup_buttons_visibility ();
}

static void
browser_show_clicked (GtkWidget *object, gpointer data)
{
  GtkTreeIter iter;
  GtkTreeModel *model;
  gchar *uri;
  GVariant *params, *result;
  GVariantBuilder builder;
  GFile *file;
  GDBusProxy *proxy;
  struct item item;
  gchar *path = NULL;
  gboolean done = FALSE;
  struct browser *browser = data;
  gint count = browser_get_selected_items_count (browser);
  enum path_type type = backend_get_path_type (browser->backend);

  if (count == 0)
    {
      path = path_chain (type, browser->dir, NULL);
    }
  else if (count == 1)
    {
      browser_set_selected_row_iter (browser, &iter);
      model = GTK_TREE_MODEL (gtk_tree_view_get_model (browser->view));
      browser_set_item (model, &iter, &item);
      path = path_chain (type, browser->dir, item.name);
    }
  else
    {
      return;
    }

  file = g_file_new_for_path (path);
  g_free (path);
  uri = g_file_get_uri (file);
  g_object_unref (file);

  proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SESSION,
					 G_DBUS_PROXY_FLAGS_DO_NOT_CONNECT_SIGNALS
					 |
					 G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES,
					 NULL, "org.freedesktop.FileManager1",
					 "/org/freedesktop/FileManager1",
					 "org.freedesktop.FileManager1", NULL,
					 NULL);
  if (proxy)
    {
      g_variant_builder_init (&builder, G_VARIANT_TYPE ("as"));
      g_variant_builder_add (&builder, "s", uri);

      params = g_variant_new ("(ass)", &builder, "");

      result = g_dbus_proxy_call_sync (proxy, "ShowItems",
				       params, G_DBUS_CALL_FLAGS_NONE,
				       -1, NULL, NULL);

      if (result != NULL)
	{
	  done = TRUE;
	  g_variant_unref (result);
	}

      g_object_unref (proxy);
    }

  if (!done)
    {
      g_app_info_launch_default_for_uri (uri, NULL, NULL);
    }

  g_free (uri);
}

static void
browser_play_clicked (GtkWidget *object, gpointer data)
{
  editor_play ();
}

static void
browser_open_clicked (GtkWidget *object, gpointer data)
{
  gchar *path;
  gchar *uri;
  GFile *file;
  struct browser *browser = data;

  path = browser_get_selected_item_path (browser);
  file = g_file_new_for_path (path);
  g_free (path);
  uri = g_file_get_uri (file);
  g_object_unref (file);

  g_app_info_launch_default_for_uri_async (uri, NULL, NULL, NULL, NULL);
  g_free (uri);
}

static void
browser_add_dir_accept (gpointer source, const gchar *name)
{
  struct browser *browser = source;
  gchar *path = browser_get_name_path (browser, name);
  gint err = browser->fs_ops->mkdir (browser->backend, path);

  if (err)
    {
      elektroid_show_error_msg (_("Error while creating dir “%s”: %s."),
				name, g_strerror (-err));
    }
  else
    {
      browser_load_dir_if_needed (browser);
    }

  g_free (path);
}

static void
browser_add_dir (GtkWidget *object, gpointer data)
{
  struct browser *browser = data;

  name_window_new_text (_("Add Directory"),
			browser->fs_ops->max_name_len,
			browser_add_dir_accept, browser);
}

static gboolean
browser_selection_function_true (GtkTreeSelection *selection,
				 GtkTreeModel *model,
				 GtkTreePath *path,
				 gboolean path_currently_selected,
				 gpointer data)
{
  return TRUE;
}

static gboolean
browser_selection_function_false (GtkTreeSelection *selection,
				  GtkTreeModel *model,
				  GtkTreePath *path,
				  gboolean path_currently_selected,
				  gpointer data)
{
  return FALSE;
}

static gboolean
browser_button_press (GtkWidget *treeview, GdkEventButton *event,
		      gpointer data)
{
  GtkTreePath *path;
  GtkTreeSelection *selection;
  struct browser *browser = data;
  gboolean val = FALSE;

  selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (browser->view));

  gtk_tree_selection_set_select_function (selection,
					  browser_selection_function_true,
					  NULL, NULL);

  if (event->state & (GDK_SHIFT_MASK | GDK_CONTROL_MASK))
    {
      return FALSE;
    }

  if (event->button == GDK_BUTTON_PRIMARY
      || event->button == GDK_BUTTON_SECONDARY)
    {
      gtk_tree_view_get_path_at_pos (browser->view, event->x, event->y, &path,
				     NULL, NULL, NULL);

      if (path)
	{

	  if (gtk_tree_selection_path_is_selected (selection, path))
	    {
	      if (event->button == GDK_BUTTON_PRIMARY)
		{
		  gtk_tree_selection_set_select_function (selection,
							  browser_selection_function_false,
							  NULL, NULL);
		}
	      else if (event->button == GDK_BUTTON_SECONDARY)
		{
		  val = TRUE;
		}
	    }
	  else
	    {
	      gtk_tree_selection_unselect_all (selection);
	      gtk_tree_selection_select_path (selection, path);
	    }

	  gtk_tree_path_free (path);
	}
      else
	{
	  gtk_tree_selection_unselect_all (selection);
	}

      if (event->button == GDK_BUTTON_SECONDARY)
	{
	  GdkRectangle r;
	  browser_set_popup_sensitivity (browser);
	  r.width = 1;
	  r.height = 1;
	  gtk_tree_view_convert_bin_window_to_widget_coords (browser->view,
							     event->x,
							     event->y, &r.x,
							     &r.y);
	  gtk_popover_set_pointing_to (GTK_POPOVER (browser->popover), &r);
	  gtk_popover_popup (GTK_POPOVER (browser->popover));
	}
    }

  return val;
}

static gboolean
browser_button_release (GtkWidget *treeview, GdkEventButton *event,
			gpointer data)
{
  GtkTreePath *path;
  GtkTreeSelection *selection;
  struct browser *browser = data;

  if (event->state & (GDK_SHIFT_MASK | GDK_CONTROL_MASK))
    {
      return FALSE;
    }

  if (event->button == GDK_BUTTON_PRIMARY)
    {
      gtk_tree_view_get_path_at_pos (browser->view, event->x, event->y,
				     &path, NULL, NULL, NULL);

      if (path)
	{
	  selection = gtk_tree_view_get_selection (browser->view);

	  if (gtk_tree_selection_path_is_selected (selection, path))
	    {
	      gtk_tree_selection_set_select_function (selection,
						      browser_selection_function_true,
						      NULL, NULL);
	      if (browser_get_selected_items_count (browser) != 1)
		{
		  gtk_tree_selection_unselect_all (selection);
		  gtk_tree_selection_select_path (selection, path);
		}
	    }
	  gtk_tree_path_free (path);
	}
    }

  return FALSE;
}

static gboolean
browser_key_press (GtkWidget *widget, GdkEventKey *event, gpointer data)
{
  struct browser *browser = data;

  if (event->type != GDK_KEY_PRESS)
    {
      return FALSE;
    }

  if (event->keyval == GDK_KEY_Menu)
    {
      browser_set_popup_sensitivity (browser);
      gtk_popover_popup (GTK_POPOVER (browser->popover));
      return TRUE;
    }
  else if (event->keyval == GDK_KEY_space)
    {
      editor_play ();
      return TRUE;
    }
  else if (event->keyval == GDK_KEY_F2)
    {
      if (browser_get_selected_items_count (browser) == 1 &&
	  browser->fs_ops->rename)
	{
	  browser_rename_item (NULL, browser);
	}
      return TRUE;
    }
  else if (event->keyval == GDK_KEY_Delete)
    {
      if (browser_get_selected_items_count (browser) > 0
	  && browser->fs_ops->delete)
	{
	  browser_delete_items (NULL, browser);
	}
      return TRUE;
    }
  else if (event->state & GDK_CONTROL_MASK && event->keyval == GDK_KEY_r)
    {
      browser_load_dir (browser);
      return TRUE;
    }
  else if (event->state & GDK_CONTROL_MASK
	   && (event->keyval == GDK_KEY_U || event->keyval == GDK_KEY_u))
    {
      browser_go_up (NULL, browser);
      return TRUE;
    }
  else if (event->state & GDK_CONTROL_MASK
	   && event->state & GDK_SHIFT_MASK && (event->keyval == GDK_KEY_N
						|| event->keyval ==
						GDK_KEY_n))
    {
      browser_add_dir (NULL, browser);
      return TRUE;
    }
  else if (event->state & GDK_CONTROL_MASK && event->keyval == GDK_KEY_Right)
    {
      if (remote_browser.fs_ops->options & FS_OPTION_SLOT_STORAGE)
	{
	  //Slot mode needs a slot destination.
	  return FALSE;
	}

      if (!remote_browser.fs_ops->upload)
	{
	  return FALSE;
	}

      elektroid_add_upload_tasks (NULL, NULL);

      return TRUE;
    }
  else if (event->state & GDK_CONTROL_MASK && event->keyval == GDK_KEY_Left)
    {

      if (!remote_browser.fs_ops->download)
	{
	  return FALSE;
	}

      elektroid_add_download_tasks (NULL, NULL);

      return TRUE;
    }
  else
    {
      return FALSE;
    }
}

static gboolean
browser_drag_begin (GtkWidget *widget, GdkDragContext *context, gpointer data)
{
  GtkTreeIter iter;
  GList *tree_path_list;
  GList *list;
  gchar *uri, *path;
  struct item item;
  struct browser *browser = data;
  enum path_type type = backend_get_path_type (browser->backend);
  GtkTreeView *view = GTK_TREE_VIEW (widget);
  GtkTreeModel *model = gtk_tree_view_get_model (view);
  GtkTreeSelection *selection = gtk_tree_view_get_selection (view);

  tree_path_list = gtk_tree_selection_get_selected_rows (selection, &model);

  browser->dnd_data = g_string_new ("");
  for (list = tree_path_list; list != NULL; list = g_list_next (list))
    {
      gtk_tree_model_get_iter (model, &iter, list->data);
      browser_set_item (model, &iter, &item);
      path = browser_get_item_path (browser, &item);
      uri = path_filename_to_uri (type, path);
      g_free (path);
      g_string_append (browser->dnd_data, uri);
      g_free (uri);
      g_string_append (browser->dnd_data, "\n");
    }
  g_list_free_full (tree_path_list, (GDestroyNotify) gtk_tree_path_free);
  browser->dnd = TRUE;

  debug_print (1, "Drag begin data:\n%s", browser->dnd_data->str);

  return FALSE;
}

static gboolean
browser_drag_end (GtkWidget *widget, GdkDragContext *context, gpointer data)
{
  GtkTreeSelection *selection;
  struct browser *browser = data;

  debug_print (1, "Drag end");

  g_string_free (browser->dnd_data, TRUE);
  browser->dnd = FALSE;

  //Performing a DND that was ending in the same browser and directory has no
  //effect. But the selection function was disabled on the button press and
  //never enabled again as there is no button release signal.
  selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (browser->view));
  gtk_tree_selection_set_select_function (selection,
					  browser_selection_function_true,
					  NULL, NULL);

  return FALSE;
}

static void
browser_dnd_get (GtkWidget *widget,
		 GdkDragContext *context,
		 GtkSelectionData *selection_data,
		 guint info, guint time, gpointer user_data)
{
  struct browser *browser = user_data;
  debug_print (1, "Creating DND data...");
  gtk_selection_data_set (selection_data,
			  gtk_selection_data_get_target
			  (selection_data), 8,
			  (guchar *) browser->dnd_data->str,
			  browser->dnd_data->len);
}

static void
browser_drag_data_received_data (GtkWidget *widget, GdkDragContext *context,
				 gint x, gint y,
				 GtkSelectionData *selection_data,
				 guint info, guint time, gpointer user_data)
{
  gchar *data;
  GdkAtom type;
  const gchar *title, *text;
  struct browser *browser = user_data;
  struct browser *other = OTHER_BROWSER (browser);
  gchar *filename, *src_dir, *dst_dir = NULL;
  struct browser_drag_data_received_data *drag_data;

  if (!gtk_selection_data_get_length (selection_data))
    {
      gtk_drag_finish (context, TRUE, TRUE, time);
      error_print ("DND invalid data");
      return;
    }

  if (browser == &remote_browser &&
      (remote_browser.fs_ops->options & FS_OPTION_SLOT_STORAGE) &&
      remote_browser.dnd_motion_path == NULL)
    {
      gtk_drag_finish (context, TRUE, TRUE, time);
      error_print ("DND destination needs a slot");
      return;
    }

  drag_data = g_malloc (sizeof (struct browser_drag_data_received_data));
  drag_data->has_progress_window = !browser_no_progress_needed (other);
  drag_data->widget = widget;

  type = gtk_selection_data_get_data_type (selection_data);
  drag_data->type_name = gdk_atom_name (type);

  data = (gchar *) gtk_selection_data_get_data (selection_data);
  debug_print (1, "DND received data (%s):\n%s", drag_data->type_name, data);

  drag_data->uris = g_uri_list_extract_uris (data);

  gtk_drag_finish (context, TRUE, TRUE, time);

  enum path_type path_type = PATH_TYPE_FROM_DND_TYPE (drag_data->type_name);
  filename = path_filename_from_uri (path_type, drag_data->uris[0]);
  src_dir = g_path_get_dirname (filename);

  //Checking if it's a local move.
  if (browser == &local_browser &&
      !strcmp (drag_data->type_name, TEXT_URI_LIST_STD))
    {
      dst_dir = local_browser.dir;	//Move
    }

  //Checking if it's a remote move.
  if (browser == &remote_browser &&
      !strcmp (drag_data->type_name, TEXT_URI_LIST_ELEKTROID))
    {
      dst_dir = remote_browser.dir;	//Move
    }

  if (dst_dir)
    {
      // If we are moving a file (source and destination is the same browser) and the
      // basedir of the first URI (every URI will share the same basename), equals
      // the browser directory, there's nothing to do.
      if (!strcmp (src_dir, dst_dir))
	{
	  debug_print (1, MSG_WARN_SAME_SRC_DST);
	  goto end;
	}

      title = _("Moving Files");
      text = _("Moving...");

      if (!strcmp (drag_data->type_name, TEXT_URI_LIST_STD) ||
	  (!strcmp (drag_data->type_name, TEXT_URI_LIST_ELEKTROID) &&
	   remote_browser.backend->type == BE_TYPE_SYSTEM))
	{
	  //Moving inside the local browser takes no time.
	  drag_data->has_progress_window = FALSE;
	}
    }
  else
    {
      title = _("Preparing Tasks");
      text = _("Waiting...");
    }

  if (drag_data->has_progress_window)
    {
      progress_window_open (elektroid_browser_drag_data_received_runner,
			    NULL, NULL, drag_data, PROGRESS_TYPE_PULSE, title,
			    text, TRUE);
    }
  else
    {
      elektroid_browser_drag_data_received_runner (drag_data);
    }

end:
  g_free (filename);
  g_free (src_dir);
}

static gboolean
browser_drag_scroll_down_timeout (gpointer user_data)
{
  GtkTreePath *end;
  struct browser *browser = user_data;
  debug_print (2, "Scrolling down...");
  gtk_tree_view_get_visible_range (browser->view, NULL, &end);
  for (guint i = 0; i < TREEVIEW_SCROLL_LINES; i++)
    {
      gtk_tree_path_next (end);
    }
  gtk_tree_view_scroll_to_cell (browser->view, end, NULL, FALSE, .0, .0);
  gtk_tree_path_free (end);
  browser_set_dnd_function (browser, browser_drag_scroll_down_timeout);
  return TRUE;
}

static gboolean
browser_drag_scroll_up_timeout (gpointer user_data)
{
  GtkTreePath *start;
  struct browser *browser = user_data;
  debug_print (2, "Scrolling up...");
  gtk_tree_view_get_visible_range (browser->view, &start, NULL);
  for (guint i = 0; i < TREEVIEW_SCROLL_LINES; i++)
    {
      gtk_tree_path_prev (start);
    }
  gtk_tree_view_scroll_to_cell (browser->view, start, NULL, FALSE, .0, .0);
  gtk_tree_path_free (start);
  browser_set_dnd_function (browser, browser_drag_scroll_up_timeout);
  return TRUE;
}

static gboolean
browser_drag_list_timeout (gpointer user_data)
{
  struct browser *browser = user_data;
  gchar *spath;

  spath = gtk_tree_path_to_string (browser->dnd_motion_path);
  debug_print (2, "Getting into path: %s...", spath);
  g_free (spath);

  browser_item_activated (browser->view, browser->dnd_motion_path, NULL,
			  browser);

  gtk_tree_path_free (browser->dnd_motion_path);
  browser_clear_dnd_function (browser);
  browser->dnd_motion_path = NULL;
  return FALSE;
}


static gboolean
browser_drag_motion_list (GtkWidget *widget,
			  GdkDragContext *context,
			  gint wx, gint wy, guint time, gpointer user_data)
{
  GtkTreePath *path;
  GtkTreeModel *model;
  GtkTreeIter iter;
  gchar *spath;
  gint tx, ty;
  gboolean slot;
  GtkTreeViewColumn *column;
  GtkTreeSelection *selection;
  struct item item;
  struct browser *browser = user_data;

  slot = GTK_TREE_VIEW (widget) == remote_browser.view &&
    remote_browser.fs_ops->options & FS_OPTION_SLOT_STORAGE;

  gtk_tree_view_convert_widget_to_bin_window_coords (browser->view, wx, wy,
						     &tx, &ty);

  if (gtk_tree_view_get_path_at_pos (browser->view, tx, ty, &path, &column,
				     NULL, NULL))
    {
      GtkAllocation allocation;
      gtk_widget_get_allocation (widget, &allocation);

      spath = gtk_tree_path_to_string (path);
      debug_print (2, "Drag motion path: %s", spath);
      g_free (spath);

      if (slot)
	{
	  gtk_tree_view_set_drag_dest_row (browser->view, path,
					   GTK_TREE_VIEW_DROP_INTO_OR_BEFORE);
	}
      else
	{
	  selection = gtk_tree_view_get_selection (browser->view);
	  if (gtk_tree_selection_path_is_selected (selection, path))
	    {
	      browser_clear_dnd_function (browser);
	      return TRUE;
	    }
	}

      model = gtk_tree_view_get_model (browser->view);
      gtk_tree_model_get_iter (model, &iter, path);
      browser_set_item (model, &iter, &item);

      if (column == browser->tree_view_name_column)
	{
	  if (item.type == ITEM_TYPE_DIR && (!browser->dnd_motion_path ||
					     (browser->dnd_motion_path &&
					      gtk_tree_path_compare
					      (browser->dnd_motion_path,
					       path))))
	    {
	      browser_set_dnd_function (browser, browser_drag_list_timeout);
	    }
	}
      else
	{
	  if (ty < TREEVIEW_EDGE_SIZE)
	    {
	      browser_set_dnd_function (browser,
					browser_drag_scroll_up_timeout);
	    }
	  else if (wy > allocation.height - TREEVIEW_EDGE_SIZE)
	    {
	      browser_set_dnd_function (browser,
					browser_drag_scroll_down_timeout);
	    }
	}
    }
  else
    {
      browser_clear_dnd_function (browser);
      if (slot)
	{
	  gtk_tree_view_set_drag_dest_row (browser->view, NULL,
					   GTK_TREE_VIEW_DROP_INTO_OR_BEFORE);
	}
    }

  if (browser->dnd_motion_path)
    {
      gtk_tree_path_free (browser->dnd_motion_path);
      browser->dnd_motion_path = NULL;
    }
  browser->dnd_motion_path = path;

  return TRUE;
}

static void
browser_drag_leave_list (GtkWidget *widget,
			 GdkDragContext *context,
			 guint time, gpointer user_data)
{
  browser_clear_dnd_function (user_data);
}

static gboolean
browser_drag_up_timeout (gpointer user_data)
{
  struct browser *browser = user_data;
  browser_go_up (NULL, browser);
  return TRUE;
}

static gboolean
browser_drag_motion_up (GtkWidget *widget,
			GdkDragContext *context,
			gint wx, gint wy, guint time, gpointer user_data)
{
  struct browser *browser = user_data;
  browser_set_dnd_function (browser, browser_drag_up_timeout);
  return TRUE;
}

static void
browser_drag_leave_up (GtkWidget *widget,
		       GdkDragContext *context,
		       guint time, gpointer user_data)
{
  browser_clear_dnd_function (user_data);
}

static void
browser_destroy (struct browser *browser)
{
  while (browser->thread)
    {
      browser_reset (&remote_browser);	// This waits too.
      gtk_main_iteration_do (TRUE);
    }

  notifier_destroy (browser->notifier);
  g_slist_free (browser->sensitive_widgets);
}

void
browser_destroy_all ()
{
  browser_destroy (&local_browser);
  browser_destroy (&remote_browser);

  g_object_unref (G_OBJECT (notes_list_store));
}

//This cancels search and load.
void
browser_cancel (struct browser *browser)
{
  GtkEntryBuffer *buf =
    gtk_entry_get_buffer (GTK_ENTRY (browser->search_entry));
  gtk_stack_set_visible_child_name (GTK_STACK (browser->buttons_stack),
				    "buttons");

  g_mutex_lock (&browser->mutex);
  browser->loading = FALSE;
  browser->search_mode = FALSE;
  g_mutex_unlock (&browser->mutex);
  browser_wait (browser);

  gtk_entry_buffer_set_text (buf, "", -1);

  g_slist_free_full (browser->search_options.tokens, g_free);
  browser->search_options.tokens = NULL;
  browser->search_options.tempo = SEARCH_PARAM_UNSET;
  browser->search_options.note = SEARCH_PARAM_UNSET;
  browser->search_options.ready = FALSE;
}

void
browser_reset (struct browser *browser)
{
  browser_cancel (browser);
  browser->fs_ops = NULL;
  g_free (browser->dir);
  browser->dir = NULL;
  browser->pending_req = 0;
  browser_clear (browser);
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
    (browser->tree_view_sample_format_column, sample_columns);
  gtk_tree_view_column_set_visible
    (browser->tree_view_sample_note_column, sample_columns);
  gtk_tree_view_column_set_visible
    (browser->tree_view_sample_tags_column, sample_columns);
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

static void
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
  browser_cancel (browser);
  browser_update_fs_sorting_options (browser);
  browser_load_dir (browser);
}

static const gchar *
browser_search_get_param (const gchar *word, const gchar *prefix)
{
  const gchar *param = NULL;
  gint prefix_len = strlen (prefix);
  if (strncmp (word, prefix, prefix_len) == 0)
    {
      const gchar *next = &word[prefix_len];
      if (*next == SEARCH_PARAM_SEPARATOR_CHAR)
	{
	  param = next + 1;
	}
    }
  return param;
}

static void
browser_search_changed (GtkSearchEntry *entry, gpointer data)
{
  struct browser *browser = data;
  GtkEntryBuffer *buf = gtk_entry_get_buffer (GTK_ENTRY (entry));
  const gchar *filter = gtk_entry_buffer_get_text (buf);

  g_mutex_lock (&browser->mutex);
  browser->loading = FALSE;
  g_mutex_unlock (&browser->mutex);
  browser_wait (browser);
  browser_clear (browser);

  usleep (250000);

  gchar *tempo_prefix = g_utf8_casefold (_("Tempo"), -1);
  gchar *note_prefix = g_utf8_casefold (_("Note"), -1);

  if (strlen (filter))
    {
      gchar **words = g_strsplit_set (filter, " ", -1);
      gchar **w = words;
      const gchar *param;

      browser->search_options.tokens = NULL;
      browser->search_options.tempo = SEARCH_PARAM_UNSET;
      browser->search_options.note = SEARCH_PARAM_UNSET;
      browser->search_options.ready = TRUE;

      while (*w)
	{
	  gchar *folded = g_utf8_casefold (*w, -1);

	  if ((param = browser_search_get_param (folded, tempo_prefix)))
	    {
	      browser->search_options.tempo = atoi (param);
	      g_free (folded);
	    }
	  else if ((param = browser_search_get_param (folded, note_prefix)))
	    {
	      browser->search_options.note = browser_get_note_num (param);
	      g_free (folded);
	    }
	  else
	    {
	      browser->search_options.tokens =
		g_slist_append (browser->search_options.tokens, folded);
	    }

	  w++;
	}

      g_free (tempo_prefix);
      g_free (note_prefix);
      g_strfreev (words);

      browser_load_dir (browser);
    }
}

static void
browser_remote_reset_dnd ()
{
  gtk_drag_source_unset ((GtkWidget *) remote_browser.view);
  gtk_drag_dest_unset ((GtkWidget *) remote_browser.view);

  if (remote_browser.fs_ops->upload)
    {
      if (remote_browser.backend->type == BE_TYPE_SYSTEM)
	{
	  gtk_drag_dest_set ((GtkWidget *) remote_browser.view,
			     GTK_DEST_DEFAULT_ALL,
			     TARGET_ENTRIES_REMOTE_SYSTEM_DST,
			     G_N_ELEMENTS
			     (TARGET_ENTRIES_REMOTE_SYSTEM_DST),
			     GDK_ACTION_COPY | GDK_ACTION_MOVE);
	}
      else
	{
	  if (remote_browser.fs_ops->options & FS_OPTION_SLOT_STORAGE)
	    {
	      gtk_drag_dest_set ((GtkWidget *) remote_browser.view,
				 GTK_DEST_DEFAULT_ALL,
				 TARGET_ENTRIES_REMOTE_MIDI_DST_SLOT,
				 G_N_ELEMENTS
				 (TARGET_ENTRIES_REMOTE_MIDI_DST_SLOT),
				 GDK_ACTION_COPY);
	    }
	  else
	    {
	      gtk_drag_dest_set ((GtkWidget *) remote_browser.view,
				 GTK_DEST_DEFAULT_ALL,
				 TARGET_ENTRIES_REMOTE_MIDI_DST,
				 G_N_ELEMENTS
				 (TARGET_ENTRIES_REMOTE_MIDI_DST),
				 GDK_ACTION_COPY);
	    }
	}
    }

  if (remote_browser.fs_ops->download)
    {
      if (remote_browser.backend->type == BE_TYPE_SYSTEM)
	{
	  gtk_drag_source_set ((GtkWidget *) remote_browser.view,
			       GDK_BUTTON1_MASK,
			       TARGET_ENTRIES_REMOTE_SYSTEM_SRC,
			       G_N_ELEMENTS
			       (TARGET_ENTRIES_REMOTE_SYSTEM_SRC),
			       GDK_ACTION_COPY | GDK_ACTION_MOVE);
	}
      else
	{
	  if (remote_browser.fs_ops->options & FS_OPTION_SLOT_STORAGE)
	    {
	      gtk_drag_source_set ((GtkWidget *) remote_browser.view,
				   GDK_BUTTON1_MASK,
				   TARGET_ENTRIES_REMOTE_MIDI_SRC,
				   G_N_ELEMENTS
				   (TARGET_ENTRIES_REMOTE_MIDI_SRC),
				   GDK_ACTION_COPY);
	    }
	  else
	    {
	      gtk_drag_source_set ((GtkWidget *) remote_browser.view,
				   GDK_BUTTON1_MASK,
				   TARGET_ENTRIES_REMOTE_MIDI_SRC,
				   G_N_ELEMENTS
				   (TARGET_ENTRIES_REMOTE_MIDI_SRC),
				   GDK_ACTION_COPY);
	    }
	}
    }
}

static void
browser_init (struct browser *browser)
{
  g_signal_connect (browser->popover_play_button, "clicked",
		    G_CALLBACK (browser_play_clicked), NULL);
  g_signal_connect (browser->popover_open_button, "clicked",
		    G_CALLBACK (browser_open_clicked), browser);
  g_signal_connect (browser->popover_show_button, "clicked",
		    G_CALLBACK (browser_show_clicked), browser);
  g_signal_connect (browser->popover_rename_button, "clicked",
		    G_CALLBACK (browser_rename_item), browser);
  g_signal_connect (browser->popover_delete_button, "clicked",
		    G_CALLBACK (browser_delete_items), browser);

  g_signal_connect (gtk_tree_view_get_selection (browser->view),
		    "changed", G_CALLBACK (browser_selection_changed),
		    browser);
  g_signal_connect (browser->view, "row-activated",
		    G_CALLBACK (browser_item_activated), browser);
  g_signal_connect (browser->up_button, "clicked",
		    G_CALLBACK (browser_go_up), browser);
  g_signal_connect (browser->add_dir_button, "clicked",
		    G_CALLBACK (browser_add_dir), browser);
  g_signal_connect (browser->refresh_button, "clicked",
		    G_CALLBACK (browser_refresh), browser);
  g_signal_connect (browser->search_button, "clicked",
		    G_CALLBACK (browser_open_search), browser);
  g_signal_connect (browser->search_entry, "stop-search",
		    G_CALLBACK (browser_close_search), browser);
  g_signal_connect (browser->search_entry, "search-changed",
		    G_CALLBACK (browser_search_changed), browser);
  g_signal_connect (browser->view, "button-press-event",
		    G_CALLBACK (browser_button_press), browser);
  g_signal_connect (browser->view, "button-release-event",
		    G_CALLBACK (browser_button_release), browser);
  g_signal_connect (browser->view, "key-press-event",
		    G_CALLBACK (browser_key_press), browser);
  g_signal_connect (browser->view, "drag-begin",
		    G_CALLBACK (browser_drag_begin), browser);
  g_signal_connect (browser->view, "drag-end",
		    G_CALLBACK (browser_drag_end), browser);
  g_signal_connect (browser->view, "drag-data-get",
		    G_CALLBACK (browser_dnd_get), browser);
  g_signal_connect (browser->view, "drag-data-received",
		    G_CALLBACK (browser_drag_data_received_data), browser);
  g_signal_connect (browser->view, "drag-motion",
		    G_CALLBACK (browser_drag_motion_list), browser);
  g_signal_connect (browser->view, "drag-leave",
		    G_CALLBACK (browser_drag_leave_list), browser);
  g_signal_connect (browser->up_button, "drag-motion",
		    G_CALLBACK (browser_drag_motion_up), browser);
  g_signal_connect (browser->up_button, "drag-leave",
		    G_CALLBACK (browser_drag_leave_up), browser);

  browser->selection_active = TRUE;

  gtk_drag_dest_set ((GtkWidget *) browser->up_button,
		     GTK_DEST_DEFAULT_MOTION | GTK_DEST_DEFAULT_HIGHLIGHT,
		     TARGET_ENTRIES_UP_BUTTON_DST,
		     G_N_ELEMENTS (TARGET_ENTRIES_UP_BUTTON_DST),
		     GDK_ACTION_COPY | GDK_ACTION_MOVE);

  browser->reload_item_in_editor = TRUE;
  notifier_init (&browser->notifier, browser);
}

static void
browser_local_init (struct browser *browser, GtkBuilder *builder)
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
  browser->popover =
    GTK_POPOVER (gtk_builder_get_object (builder, "local_popover"));
  browser->pref_key_dir = PREF_KEY_LOCAL_DIR;
  browser->fs_ops = &FS_LOCAL_SAMPLE_OPERATIONS;
  browser->backend = NULL;
  browser->check_callback = NULL;
  browser->set_popup_buttons_visibility = browser_local_set_popup_visibility;
  browser->set_columns_visibility = browser_local_set_columns_visibility;
  browser->sensitive_widgets = NULL;
  browser->list_stack =
    GTK_WIDGET (gtk_builder_get_object (builder, "local_list_stack"));
  browser->spinner =
    GTK_WIDGET (gtk_builder_get_object (builder, "local_spinner"));
  browser->popover_transfer_button =
    GTK_WIDGET (gtk_builder_get_object
		(builder, "local_popover_upload_button"));
  browser->popover_play_separator =
    GTK_WIDGET (gtk_builder_get_object
		(builder, "local_popover_play_separator"));
  browser->popover_play_button =
    GTK_WIDGET (gtk_builder_get_object
		(builder, "local_popover_play_button"));
  browser->popover_options_separator =
    GTK_WIDGET (gtk_builder_get_object
		(builder, "local_popover_options_separator"));
  browser->popover_open_button =
    GTK_WIDGET (gtk_builder_get_object
		(builder, "local_popover_open_button"));
  browser->popover_show_button =
    GTK_WIDGET (gtk_builder_get_object
		(builder, "local_popover_show_button"));
  browser->popover_actions_separator =
    GTK_WIDGET (gtk_builder_get_object
		(builder, "local_popover_actions_separator"));
  browser->popover_rename_button =
    GTK_WIDGET (gtk_builder_get_object
		(builder, "local_popover_rename_button"));
  browser->popover_delete_button =
    GTK_WIDGET (gtk_builder_get_object
		(builder, "local_popover_delete_button"));
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
  browser->tree_view_sample_format_column =
    GTK_TREE_VIEW_COLUMN (gtk_builder_get_object
			  (builder, "local_tree_view_sample_format_column"));
  browser->tree_view_sample_note_column =
    GTK_TREE_VIEW_COLUMN (gtk_builder_get_object
			  (builder, "local_tree_view_sample_note_column"));
  browser->tree_view_sample_tags_column =
    GTK_TREE_VIEW_COLUMN (gtk_builder_get_object
			  (builder, "local_tree_view_sample_tags_column"));

  browser->tree_view_info_column =
    GTK_TREE_VIEW_COLUMN (gtk_builder_get_object
			  (builder, "local_tree_view_info_column"));

  browser->tree_view_id_column = NULL;
  browser->tree_view_slot_column = NULL;
  browser->tree_view_size_column = NULL;

  g_signal_connect (browser->popover_transfer_button, "clicked",
		    G_CALLBACK (elektroid_add_upload_tasks), NULL);

  gtk_drag_source_set ((GtkWidget *) browser->view,
		       GDK_BUTTON1_MASK, TARGET_ENTRIES_LOCAL_SRC,
		       G_N_ELEMENTS (TARGET_ENTRIES_LOCAL_SRC),
		       GDK_ACTION_COPY | GDK_ACTION_MOVE);
  gtk_drag_dest_set ((GtkWidget *) browser->view,
		     GTK_DEST_DEFAULT_ALL, TARGET_ENTRIES_LOCAL_DST,
		     G_N_ELEMENTS (TARGET_ENTRIES_LOCAL_DST),
		     GDK_ACTION_COPY | GDK_ACTION_MOVE);

  browser_load_preferences_dir (browser);

  browser_init (browser);
}

static void
browser_remote_init (struct browser *browser, GtkBuilder *builder)
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
  browser->popover =
    GTK_POPOVER (gtk_builder_get_object (builder, "remote_popover"));
  browser->pref_key_dir = PREF_KEY_REMOTE_DIR;
  browser->dir = NULL;
  browser->fs_ops = NULL;
  browser->backend = &backend;
  browser->check_callback = elektroid_check_backend;
  browser->set_popup_buttons_visibility = browser_remote_set_popup_visibility;
  browser->set_columns_visibility = browser_remote_set_columns_visibility;
  browser->sensitive_widgets = NULL;
  browser->list_stack =
    GTK_WIDGET (gtk_builder_get_object (builder, "remote_list_stack"));
  browser->spinner =
    GTK_WIDGET (gtk_builder_get_object (builder, "remote_spinner"));
  browser->popover_transfer_button =
    GTK_WIDGET (gtk_builder_get_object
		(builder, "remote_popover_download_button"));
  browser->popover_play_separator =
    GTK_WIDGET (gtk_builder_get_object
		(builder, "remote_popover_play_separator"));
  browser->popover_play_button =
    GTK_WIDGET (gtk_builder_get_object
		(builder, "remote_popover_play_button"));
  browser->popover_options_separator =
    GTK_WIDGET (gtk_builder_get_object
		(builder, "remote_popover_options_separator"));
  browser->popover_open_button =
    GTK_WIDGET (gtk_builder_get_object
		(builder, "remote_popover_open_button"));
  browser->popover_show_button =
    GTK_WIDGET (gtk_builder_get_object
		(builder, "remote_popover_show_button"));
  browser->popover_actions_separator =
    GTK_WIDGET (gtk_builder_get_object
		(builder, "remote_popover_actions_separator"));
  browser->popover_rename_button =
    GTK_WIDGET (gtk_builder_get_object
		(builder, "remote_popover_rename_button"));
  browser->popover_delete_button =
    GTK_WIDGET (gtk_builder_get_object
		(builder, "remote_popover_delete_button"));
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
  browser->sensitive_widgets =
    g_slist_append (browser->sensitive_widgets, maction_context.box);

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
  browser->tree_view_sample_format_column =
    GTK_TREE_VIEW_COLUMN (gtk_builder_get_object
			  (builder, "remote_tree_view_sample_format_column"));
  browser->tree_view_sample_note_column =
    GTK_TREE_VIEW_COLUMN (gtk_builder_get_object
			  (builder, "remote_tree_view_sample_note_column"));
  browser->tree_view_sample_tags_column =
    GTK_TREE_VIEW_COLUMN (gtk_builder_get_object
			  (builder, "remote_tree_view_sample_tags_column"));

  browser->tree_view_info_column =
    GTK_TREE_VIEW_COLUMN (gtk_builder_get_object
			  (builder, "remote_tree_view_info_column"));

  g_signal_connect (browser->popover_transfer_button, "clicked",
		    G_CALLBACK (elektroid_add_download_tasks), NULL);

  browser_init (browser);
}

void
browser_init_all (GtkBuilder *builder)
{
  browser_local_init (&local_browser, builder);
  browser_remote_init (&remote_browser, builder);

  notes_list_store =
    GTK_LIST_STORE (gtk_builder_get_object (builder, "notes_list_store"));
  g_object_ref (G_OBJECT (notes_list_store));
}

void
browser_set_selection_active (struct browser *browser,
			      gboolean selection_active)
{
  browser->selection_active = selection_active;
  if (selection_active)
    {
      browser_check_selection (browser);
    }
}

static gboolean
browser_set_reload_item_in_editor_f (gpointer data)
{
  struct browser *browser = data;
  g_mutex_lock (&browser->mutex);
  browser->reload_item_in_editor = TRUE;
  g_mutex_unlock (&browser->mutex);
  return G_SOURCE_REMOVE;
}

void
browser_set_reload_item_in_editor (struct browser *browser, gboolean reload)
{
  // Ignoring the notifier calls needs to be immediate.
  // Changing it back needs to be delayed as filesystem notifications are delayed.
  if (reload)
    {
      // 1 s might seem much but the user is actually interacting with the application.
      g_timeout_add (1000, browser_set_reload_item_in_editor_f, browser);
    }
  else
    {
      g_mutex_lock (&browser->mutex);
      browser->reload_item_in_editor = FALSE;
      g_mutex_unlock (&browser->mutex);
    }
}
