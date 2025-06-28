/*
 *   tasks.h
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

#ifndef TASKS_H
#define TASKS_H

#include <gtk/gtk.h>
#include "connector.h"

enum task_list_store_columns
{
  TASK_LIST_STORE_STATUS_FIELD,
  TASK_LIST_STORE_TYPE_FIELD,
  TASK_LIST_STORE_SRC_FIELD,
  TASK_LIST_STORE_DST_FIELD,
  TASK_LIST_STORE_PROGRESS_FIELD,
  TASK_LIST_STORE_STATUS_HUMAN_FIELD,
  TASK_LIST_STORE_TYPE_HUMAN_FIELD,
  TASK_LIST_STORE_REMOTE_FS_ID_FIELD,
  TASK_LIST_STORE_REMOTE_FS_ICON_FIELD,
  TASK_LIST_STORE_BATCH_ID_FIELD,
  TASK_LIST_STORE_MODE_FIELD
};

enum task_status
{
  TASK_STATUS_QUEUED,
  TASK_STATUS_RUNNING,
  TASK_STATUS_COMPLETED_OK,
  TASK_STATUS_COMPLETED_ERROR,
  TASK_STATUS_CANCELED
};

enum task_mode
{
  TASK_MODE_ASK,
  TASK_MODE_REPLACE,
  TASK_MODE_SKIP
};

enum task_type
{
  TASK_TYPE_UPLOAD,
  TASK_TYPE_DOWNLOAD
};

struct task_transfer
{
  struct job_control control;
  gchar *src;			//Contains a path to a file
  gchar *dst;			//Contains a path to a file
  enum task_status status;	//Contains the final status
  const struct fs_operations *fs_ops;	//Contains the fs_operations to use in this transfer
  guint mode;
  guint batch_id;
};

struct tasks
{
  struct task_transfer transfer;
  GThread *thread;
  gint batch_id;
  GtkListStore *list_store;
  GtkWidget *tree_view;
  GtkWidget *cancel_task_button;
  GtkWidget *remove_tasks_button;
  GtkWidget *clear_tasks_button;
};

extern struct tasks tasks;

void tasks_init (GtkBuilder * builder);

gboolean tasks_get_next_queued (GtkTreeIter * iter,
				enum task_type *type, gchar ** src,
				gchar ** dst, gint * fs, guint * batch_id,
				guint * mode);

gboolean tasks_complete_current (gpointer data);

void tasks_cancel_all (GtkWidget * object, gpointer data);

void tasks_visitor_set_batch_canceled (GtkTreeIter * iter);

void tasks_batch_visitor_set_skip (GtkTreeIter * iter);

void tasks_batch_visitor_set_replace (GtkTreeIter * iter);

void tasks_stop_thread ();

const gchar *tasks_get_human_status (enum task_status status);

void tasks_check_buttons ();

void tasks_visit_pending (void (*visitor) (GtkTreeIter * iter));

void tasks_add (enum task_type type,
		const char *src, const char *dst, gint remote_fs_id,
		struct backend *backend);

gboolean tasks_update_current_progress (gpointer data);

#endif
