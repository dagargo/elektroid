/*
 *   record_window.h
 *   Copyright (C) 2025 David García Goñi <dagargo@gmail.com>
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

#ifndef RECORD_WINDOW_H
#define RECORD_WINDOW_H

typedef void (*record_window_record_cb) (guint channel_mask);
typedef void (*record_window_cancel_cb) ();

void record_window_init (GtkBuilder * builder);

void record_window_open (guint32 fs_options,
			 record_window_record_cb record_cb,
			 record_window_cancel_cb cancel_cb);

void record_window_destroy ();

#endif
