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

#include <glib/gi18n.h>
#include "guirecorder.h"
#include "audio.h"

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
guirecorder_get_channel_mask (struct guirecorder *guirecorder)
{
  guint channel_mask = 0;
  GtkTreeIter iter;
  GtkComboBox *combo = GTK_COMBO_BOX (guirecorder->channels_combo);
  GtkTreeModel *model = gtk_combo_box_get_model (combo);
  if (gtk_combo_box_get_active_iter (combo, &iter))
    {
      gtk_tree_model_get (model, &iter, CHANNELS_LIST_STORE_ID_FIELD,
			  &channel_mask, -1);
    }
  return channel_mask;
}

void
guirecorder_set_channels_masks (struct guirecorder *guirecorder, guint32 opts)
{
  gtk_list_store_clear (guirecorder->channels_list_store);
  gtk_combo_box_set_active (GTK_COMBO_BOX (guirecorder->channels_combo), -1);

  if (opts & FS_OPTION_STEREO)
    {
      gtk_list_store_insert_with_values (guirecorder->channels_list_store,
					 NULL, -1,
					 CHANNELS_LIST_STORE_CAPTION_FIELD,
/* TRANSLATORS: Stereo recording */
					 _("Stereo"),
					 CHANNELS_LIST_STORE_ID_FIELD,
					 RECORD_STEREO, -1);
    }
  if (opts & FS_OPTION_MONO)
    {
      gtk_list_store_insert_with_values (guirecorder->channels_list_store,
					 NULL, -1,
					 CHANNELS_LIST_STORE_CAPTION_FIELD,
/* TRANSLATORS: Mono recording from left channel */
					 _("Left"),
					 CHANNELS_LIST_STORE_ID_FIELD,
					 RECORD_LEFT, -1);
      gtk_list_store_insert_with_values (guirecorder->channels_list_store,
					 NULL, -1,
					 CHANNELS_LIST_STORE_CAPTION_FIELD,
/* TRANSLATORS: Mono recording from right channel */
					 _("Right"),
					 CHANNELS_LIST_STORE_ID_FIELD,
					 RECORD_RIGHT, -1);
    }

  gtk_combo_box_set_active (GTK_COMBO_BOX (guirecorder->channels_combo), 0);
}

void
guirecorder_channels_changed (GtkWidget *object, gpointer data)
{
  struct guirecorder *guirecorder = data;
  guint options = guirecorder_get_channel_mask (guirecorder) |
    RECORD_MONITOR_ONLY;
  g_mutex_lock (&audio.control.mutex);
  audio.record_options = options;
  g_mutex_unlock (&audio.control.mutex);
}
