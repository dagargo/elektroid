/*
 *   editor.h
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

#ifndef EDITOR_H
#define EDITOR_H

#include "browser.h"

struct browser *editor_get_browser ();

void editor_reset (struct browser *browser);

void editor_play_clicked (GtkWidget * object, gpointer data);

void editor_start_load_thread (gchar * sample_path);

void editor_stop_load_thread ();

void editor_init (GtkBuilder * builder);

void editor_destroy ();

void editor_set_audio_mono_mix ();

void editor_reset_audio ();

#endif
