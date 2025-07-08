/*
 *   progress_window_window.h
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

#ifndef PROGRESS_WINDOW_H
#define PROGRESS_WINDOW_H

enum progress_type
{
  PROGRESS_TYPE_NO_AUTO,	//Progress must be set by calling progress_window_window_set_fraction.
  PROGRESS_TYPE_PULSE,		//Progress pulses.
  PROGRESS_TYPE_SYSEX_TRANSFER	//Progress pulses and the label tracks the sysex transfer status.
};

typedef void (*progress_window_runner) (gpointer data);
typedef void (*progress_window_consumer) (gpointer data);
typedef void (*progress_window_cancel_cb) (gpointer data);

void progress_window_stop_thread ();

void progress_window_cancel ();

void progress_window_set_fraction (gdouble fraction);

void progress_window_set_label (const gchar * label);

gboolean progress_window_is_active ();


// Depending on the caller, the runner might be checking the status via progress_window_is_active or might setup a cancel callback.
// cancel_cb will be called when the progress window is cancelled.
// runner_ is run in another thread.
// consumer_ and cancel_cb_ are called from the main loop.
// consumer_ should free the passed data and it is responsible for checking the status of the data.
void progress_window_open (progress_window_runner runner,
			   progress_window_consumer consumer,
			   progress_window_cancel_cb cancel_cb, gpointer data,
			   enum progress_type type, const gchar * name,
			   const gchar * label_text, gboolean cancellable);

void progress_window_init (GtkBuilder * builder);

void progress_window_destroy ();

void progress_window_response (gint response);

#endif
