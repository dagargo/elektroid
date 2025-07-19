/*
 *   elektroid.c
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

#ifndef ELEKTROID_H
#define ELEKTROID_H

void elektroid_add_upload_tasks (GtkWidget * object, gpointer data);

void elektroid_add_download_tasks (GtkWidget * object, gpointer data);

void elektroid_browser_drag_data_received_runner (gpointer data);

gboolean elektroid_check_backend ();

void elektroid_delete_items_runner (gpointer data);

void elektroid_refresh_devices ();

void elektroid_update_audio_status (gboolean status);

void elektroid_show_error_msg (const char *format, ...);

#endif
