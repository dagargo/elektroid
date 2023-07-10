/*
 *   guirecorder.c
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

#include "guirecorder.h"
#include "audio.h"

struct guirecorder guirecorder;

static gboolean
guirecorder_set_monitor_level (gpointer data)
{
  struct guirecorder *guirecorder = data;
  gtk_level_bar_set_value (guirecorder->monitor_levelbar, guirecorder->level);
  return FALSE;
}

void
guirecorder_monitor_notifier (gpointer recorder, gdouble value)
{
  struct guirecorder *guirecorder = recorder;
  guirecorder->level = value;
  g_idle_add (guirecorder_set_monitor_level, guirecorder);
}

guint
guirecorder_get_channel_mask (GtkWidget * widget)
{
  guint channel_mask;
  GtkTreeIter iter;
  GtkComboBox *combo = GTK_COMBO_BOX (widget);
  GtkTreeModel *model = gtk_combo_box_get_model (combo);
  gtk_combo_box_get_active_iter (combo, &iter);
  gtk_tree_model_get (model, &iter, 1, &channel_mask, -1);
  return channel_mask;
}

void
guirecorder_channels_changed (GtkWidget * object, gpointer data)
{
  struct audio *audio = data;
  guint options = guirecorder_get_channel_mask (object) | RECORD_MONITOR_ONLY;
  g_mutex_lock (&audio->control.mutex);
  audio->record_options = options;
  g_mutex_unlock (&audio->control.mutex);
}
