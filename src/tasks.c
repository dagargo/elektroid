/*
 *   tasks.c
 *   Copyright (C) 2023 David García Goñi <dagargo@gmail.com>
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
#include "tasks.h"
#include "backend.h"
#include "browser.h"
#include "utils.h"

struct tasks tasks;

const gchar *
tasks_get_human_status (enum task_status status)
{
  switch (status)
    {
    case TASK_STATUS_QUEUED:
      return _("Queued");
    case TASK_STATUS_RUNNING:
      return _("Running");
    case TASK_STATUS_COMPLETED_OK:
      return _("Completed");
    case TASK_STATUS_COMPLETED_ERROR:
      return _("Terminated with errors");
    case TASK_STATUS_CANCELED:
      return _("Canceled");
    default:
      return _("Undefined");
    }
}

static const gchar *
tasks_get_human_type (enum task_type type)
{
  switch (type)
    {
    case TASK_TYPE_UPLOAD:
      return _("Upload");
    case TASK_TYPE_DOWNLOAD:
      return _("Download");
    default:
      return _("Undefined");
    }
}

static void
tasks_stop_current (GtkWidget *object, gpointer data)
{
  controllable_set_active (&tasks.transfer.control.controllable, FALSE);
}

void
tasks_visit_pending (void (*visitor) (GtkTreeIter *iter))
{
  enum task_status status;
  GtkTreeIter iter;
  gboolean valid =
    gtk_tree_model_get_iter_first (GTK_TREE_MODEL (tasks.list_store), &iter);

  while (valid)
    {
      gtk_tree_model_get (GTK_TREE_MODEL (tasks.list_store), &iter,
			  TASK_LIST_STORE_STATUS_FIELD, &status, -1);
      if (status == TASK_STATUS_QUEUED)
	{
	  visitor (&iter);
	}
      valid = gtk_tree_model_iter_next (GTK_TREE_MODEL (tasks.list_store),
					&iter);
    }
}

static gboolean
tasks_get_current (GtkTreeIter *iter)
{
  enum task_status status;
  gboolean found = FALSE;
  gboolean valid =
    gtk_tree_model_get_iter_first (GTK_TREE_MODEL (tasks.list_store), iter);

  while (valid)
    {
      gtk_tree_model_get (GTK_TREE_MODEL (tasks.list_store), iter,
			  TASK_LIST_STORE_STATUS_FIELD, &status, -1);

      if (status == TASK_STATUS_RUNNING)
	{
	  found = TRUE;
	  break;
	}

      valid =
	gtk_tree_model_iter_next (GTK_TREE_MODEL (tasks.list_store), iter);
    }

  return found;
}

gboolean
tasks_complete_current (gpointer data)
{
  GtkTreeIter iter;
  const gchar *status = tasks_get_human_status (tasks.transfer.status);

  if (tasks_get_current (&iter))
    {
      gtk_list_store_set (tasks.list_store, &iter,
			  TASK_LIST_STORE_STATUS_FIELD,
			  tasks.transfer.status,
			  TASK_LIST_STORE_STATUS_HUMAN_FIELD, status, -1);
      tasks_stop_current (NULL, NULL);
      g_free (tasks.transfer.src);
      g_free (tasks.transfer.dst);

      gtk_widget_set_sensitive (tasks.cancel_task_button, FALSE);
    }
  else
    {
      debug_print (1, "No task running. Skipping...");
    }

  return FALSE;
}

gboolean
tasks_get_next_queued (GtkTreeIter *iter,
		       enum task_type *type, gchar **src,
		       gchar **dst, gint *fs, guint *batch_id, guint *mode)
{
  enum task_status status;
  gboolean found = FALSE;
  gboolean valid =
    gtk_tree_model_get_iter_first (GTK_TREE_MODEL (tasks.list_store), iter);

  while (valid)
    {
      if (type)
	{
	  gtk_tree_model_get (GTK_TREE_MODEL (tasks.list_store), iter,
			      TASK_LIST_STORE_STATUS_FIELD, &status,
			      TASK_LIST_STORE_TYPE_FIELD, type,
			      TASK_LIST_STORE_SRC_FIELD, src,
			      TASK_LIST_STORE_DST_FIELD, dst,
			      TASK_LIST_STORE_REMOTE_FS_ID_FIELD, fs,
			      TASK_LIST_STORE_BATCH_ID_FIELD, batch_id,
			      TASK_LIST_STORE_MODE_FIELD, mode, -1);
	}
      else
	{
	  gtk_tree_model_get (GTK_TREE_MODEL (tasks.list_store), iter,
			      TASK_LIST_STORE_STATUS_FIELD, &status, -1);
	}

      if (status == TASK_STATUS_QUEUED)
	{
	  found = TRUE;
	  break;
	}
      valid = gtk_tree_model_iter_next (GTK_TREE_MODEL (tasks.list_store),
					iter);
    }

  return found;
}

static gboolean
tasks_is_queued (enum task_status status)
{
  return (status == TASK_STATUS_QUEUED);
}

static gboolean
tasks_is_finished (enum task_status status)
{
  return (status == TASK_STATUS_COMPLETED_OK ||
	  status == TASK_STATUS_COMPLETED_ERROR ||
	  status == TASK_STATUS_CANCELED);
}

void
tasks_check_buttons ()
{
  enum task_status status;
  gboolean queued = FALSE;
  gboolean finished = FALSE;
  GtkTreeIter iter;
  gboolean valid =
    gtk_tree_model_get_iter_first (GTK_TREE_MODEL (tasks.list_store),
				   &iter);

  while (valid)
    {
      gtk_tree_model_get (GTK_TREE_MODEL (tasks.list_store), &iter,
			  TASK_LIST_STORE_STATUS_FIELD, &status, -1);

      if (tasks_is_queued (status))
	{
	  queued = TRUE;
	}

      if (tasks_is_finished (status))
	{
	  finished = TRUE;
	}

      valid =
	gtk_tree_model_iter_next (GTK_TREE_MODEL (tasks.list_store), &iter);
    }

  gtk_widget_set_sensitive (tasks.remove_tasks_button, queued);
  gtk_widget_set_sensitive (tasks.clear_tasks_button, finished);
}

static void
tasks_remove_on_cond (gboolean (*selector) (enum task_status))
{
  enum task_status status;
  GtkTreeIter iter;
  gboolean valid =
    gtk_tree_model_get_iter_first (GTK_TREE_MODEL (tasks.list_store),
				   &iter);

  while (valid)
    {
      gtk_tree_model_get (GTK_TREE_MODEL (tasks.list_store), &iter,
			  TASK_LIST_STORE_STATUS_FIELD, &status, -1);

      if (selector (status))
	{
	  gtk_list_store_remove (tasks.list_store, &iter);
	  valid = gtk_list_store_iter_is_valid (tasks.list_store, &iter);
	}
      else
	{
	  valid =
	    gtk_tree_model_iter_next (GTK_TREE_MODEL (tasks.list_store),
				      &iter);
	}
    }

  tasks_check_buttons ();
}

static void
tasks_remove_queued (GtkWidget *object, gpointer data)
{
  tasks_remove_on_cond (tasks_is_queued);
}

static void
tasks_clear_finished (GtkWidget *object, gpointer data)
{
  tasks_remove_on_cond (tasks_is_finished);
}

static void
tasks_visitor_set_canceled (GtkTreeIter *iter)
{
  const gchar *canceled = tasks_get_human_status (TASK_STATUS_CANCELED);
  gtk_list_store_set (tasks.list_store, iter,
		      TASK_LIST_STORE_STATUS_FIELD,
		      TASK_STATUS_CANCELED,
		      TASK_LIST_STORE_STATUS_HUMAN_FIELD, canceled, -1);
}

void
tasks_cancel_all (GtkWidget *object, gpointer data)
{
  tasks_visit_pending (tasks_visitor_set_canceled);
  tasks_stop_current (NULL, NULL);
  tasks_check_buttons ();
}

void
tasks_visitor_set_batch_status (GtkTreeIter *iter, enum task_mode mode)
{
  gint batch_id;
  gtk_tree_model_get (GTK_TREE_MODEL (tasks.list_store), iter,
		      TASK_LIST_STORE_BATCH_ID_FIELD, &batch_id, -1);
  if (batch_id == tasks.transfer.batch_id)
    {
      gtk_list_store_set (tasks.list_store, iter, TASK_LIST_STORE_MODE_FIELD,
			  mode, -1);
    }
}

void
tasks_visitor_set_batch_canceled (GtkTreeIter *iter)
{
  gint batch_id;
  gtk_tree_model_get (GTK_TREE_MODEL (tasks.list_store), iter,
		      TASK_LIST_STORE_BATCH_ID_FIELD, &batch_id, -1);
  if (batch_id == tasks.transfer.batch_id)
    {
      tasks_visitor_set_canceled (iter);
    }
}

void
tasks_batch_visitor_set_skip (GtkTreeIter *iter)
{
  tasks_visitor_set_batch_status (iter, TASK_MODE_SKIP);
}

void
tasks_batch_visitor_set_replace (GtkTreeIter *iter)
{
  tasks_visitor_set_batch_status (iter, TASK_MODE_REPLACE);
}

void
tasks_add (enum task_type type, const char *src, const char *dst,
	   gint remote_fs_id, struct backend *backend)
{
  const gchar *status_human = tasks_get_human_status (TASK_STATUS_QUEUED);
  const gchar *type_human = tasks_get_human_type (type);
  const struct fs_operations *ops = backend_get_fs_operations_by_id (backend,
								     remote_fs_id);

  gtk_list_store_insert_with_values (tasks.list_store, NULL, -1,
				     TASK_LIST_STORE_STATUS_FIELD,
				     TASK_STATUS_QUEUED,
				     TASK_LIST_STORE_TYPE_FIELD, type,
				     TASK_LIST_STORE_SRC_FIELD, src,
				     TASK_LIST_STORE_DST_FIELD, dst,
				     TASK_LIST_STORE_PROGRESS_FIELD, 0,
				     TASK_LIST_STORE_STATUS_HUMAN_FIELD,
				     status_human,
				     TASK_LIST_STORE_TYPE_HUMAN_FIELD,
				     type_human,
				     TASK_LIST_STORE_REMOTE_FS_ID_FIELD,
				     remote_fs_id,
				     TASK_LIST_STORE_REMOTE_FS_ICON_FIELD,
				     ops->gui_icon,
				     TASK_LIST_STORE_BATCH_ID_FIELD,
				     tasks.batch_id,
				     TASK_LIST_STORE_MODE_FIELD,
				     TASK_MODE_ASK, -1);

  gtk_widget_set_sensitive (tasks.remove_tasks_button, TRUE);
}

static void
tasks_join_thread ()
{
  debug_print (2, "Joining task thread...");

  g_mutex_lock (&tasks.transfer.control.controllable.mutex);
  g_cond_signal (&tasks.transfer.control.cond);
  g_mutex_unlock (&tasks.transfer.control.controllable.mutex);

  if (tasks.thread)
    {
      g_thread_join (tasks.thread);
      tasks.thread = NULL;
    }
}

void
tasks_stop_thread ()
{
  debug_print (1, "Stopping task thread...");
  controllable_set_active (&tasks.transfer.control.controllable, FALSE);
  tasks_join_thread ();
}

gboolean
tasks_update_current_progress (gpointer data)
{
  GtkTreeIter iter;
  gdouble progress;
  gint percent;

  if (tasks_get_current (&iter))
    {
      g_mutex_lock (&tasks.transfer.control.controllable.mutex);
      progress = tasks.transfer.control.progress;
      g_mutex_unlock (&tasks.transfer.control.controllable.mutex);

      percent = (gint) (100.0 * progress);

      gtk_list_store_set (tasks.list_store, &iter,
			  TASK_LIST_STORE_PROGRESS_FIELD, percent, -1);
    }

  return FALSE;
}

void
tasks_init (GtkBuilder *builder)
{
  tasks.thread = NULL;

  tasks.list_store =
    GTK_LIST_STORE (gtk_builder_get_object (builder, "task_list_store"));
  tasks.tree_view =
    GTK_WIDGET (gtk_builder_get_object (builder, "task_tree_view"));

  tasks.cancel_task_button =
    GTK_WIDGET (gtk_builder_get_object (builder, "cancel_task_button"));
  tasks.remove_tasks_button =
    GTK_WIDGET (gtk_builder_get_object (builder, "remove_tasks_button"));
  tasks.clear_tasks_button =
    GTK_WIDGET (gtk_builder_get_object (builder, "clear_tasks_button"));
  g_signal_connect (tasks.cancel_task_button, "clicked",
		    G_CALLBACK (tasks_cancel_all), NULL);
  g_signal_connect (tasks.remove_tasks_button, "clicked",
		    G_CALLBACK (tasks_remove_queued), NULL);
  g_signal_connect (tasks.clear_tasks_button, "clicked",
		    G_CALLBACK (tasks_clear_finished), NULL);
}
