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

#ifndef GUIRECORDER_H
#define GUIRECORDER_H

#include <gtk/gtk.h>
#include "connector.h"
#include "audio.h"

enum channels_list_store_columns
{
  CHANNELS_LIST_STORE_CAPTION_FIELD,
  CHANNELS_LIST_STORE_ID_FIELD
};

struct guirecorder
{
  GtkWidget *channels_combo;
  GtkListStore *channels_list_store;
  GtkLevelBar *monitor_levelbar_l;
  GtkLevelBar *monitor_levelbar_r;
  gdouble level_l;
  gdouble level_r;
};

void guirecorder_monitor_notifier (void *guirecorder, gdouble l, gdouble r);

guint guirecorder_get_channel_mask (struct guirecorder *guirecorder);

void guirecorder_set_channels_masks (struct guirecorder *guirecorder,
				     guint32);

void guirecorder_channels_changed (GtkWidget * widget, gpointer user_data);

#endif
