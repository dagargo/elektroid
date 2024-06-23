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
#include "utils.h"

#ifndef PROGRESS_H
#define PROGRESS_H

struct progress
{
  struct sysex_transfer sysex_transfer;
  GtkDialog *dialog;
  GtkWidget *bar;
  GtkWidget *label;
  GThread *thread;
  gint64 start;
};

extern struct progress progress;

void progress_stop_thread ();

void progress_dialog_close (gpointer data);

void progress_set_fraction (gdouble fraction);

gboolean progress_pulse (gpointer data);

gboolean progress_update (gpointer data);

gpointer progress_run (GThreadFunc f, gpointer user_data, const gchar * name,
		       const gchar * text, gint * res);

void progress_init (GtkBuilder * builder);

void progress_response (gint response);

#endif
