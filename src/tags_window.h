/*
 *   tags_window.h
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

#ifndef TAGS_WINDOW_H
#define TAGS_WINDOW_H

enum tag_source
{
  TAG_SOURCE_NONE,
  TAG_SOURCE_LOCAL,
  TAG_SOURCE_REMOTE
};

void tags_window_init (GtkBuilder * builder);

void tags_window_open (enum tag_source tag_source);

void tags_window_destroy ();

GtkWidget *tags_label_new (const gchar * name, enum tag_source tag_source);

void tags_clear_container (GtkWidget * container);

#endif
