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

extern struct sysex_transfer sysex_transfer;
extern GtkDialog *progress_dialog;
extern GtkWidget *progress_bar;
extern GtkWidget *progress_label;
extern GtkWidget *progress_dialog_cancel_button;

gpointer progress_join_sysex_thread ();

void progress_stop_running_sysex (GtkDialog * dialog, gint response_id,
				  gpointer data);

void progress_stop_sysex_thread ();

void progress_progress_dialog_close (gpointer data);

gboolean progress_pulse (gpointer data);

gboolean progress_update (gpointer data);

void progress_new_sysex_thread (GThreadFunc f, gpointer user_data);

void progress_init (GtkBuilder * builder);
