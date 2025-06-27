/*
 *   name.h
 *   Copyright (C) 2024 David García Goñi <dagargo@gmail.com>
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
#include "editor.h"

#ifndef NAME_WINDOW_H
#define NAME_WINDOW_H

typedef void (*name_accept_cb) (gpointer source, const gchar * name);

void name_window_init (GtkBuilder * builder);

void name_window_edit_text (const gchar * title,
			    gint max_len, const gchar * text, gint sel_start,
			    gint sel_end, name_accept_cb accept_cb,
			    gpointer source);

void name_window_new_text (const gchar * title, gint max_len,
			   name_accept_cb accept_cb, gpointer source);

void name_window_destroy ();

#endif
