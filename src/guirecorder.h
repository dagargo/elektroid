/*
 *   guirecorder.h
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

struct guirecorder
{
  GtkWidget *channels_combo;
  GtkLevelBar *monitor_levelbar;
  gdouble level;
};

void guirecorder_monitor_notifier (void *, gdouble);

guint guirecorder_get_channel_mask (GtkWidget *);

void guirecorder_channels_changed (GtkWidget *, gpointer);
