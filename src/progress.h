/*
 *   progress.h
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

#include <gtk/gtk.h>
#include "connector.h"

#ifndef PROGRESS_H
#define PROGRESS_H

enum progress_type
{
  PROGRESS_TYPE_NONE,
  PROGRESS_TYPE_PULSE,
  PROGRESS_TYPE_UPDATE
};

typedef void (*progress_end_cb) (gpointer user_data, gpointer func_value,
				 gboolean completed);

struct progress
{
  struct sysex_transfer sysex_transfer;
  GtkWindow *window;
  GtkWidget *bar;
  GtkWidget *label;
  GtkWidget *cancel_button;
  GThread *thread;
  gint64 start;
  gpointer data;
  enum progress_type type;
  progress_end_cb end_cb;
};

extern struct progress progress;

void progress_set_fraction (gdouble fraction);

gboolean progress_is_active ();

void progress_run (GThreadFunc f, enum progress_type type, gpointer user_data,
		   const gchar * name, const gchar * text,
		   gboolean cancellable, progress_end_cb end_cb);

void progress_init (GtkBuilder * builder);

void progress_end ();

#endif
