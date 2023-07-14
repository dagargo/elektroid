/*
 *   autosampler.c
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
#include <string.h>
#include "menu_action.h"
#include "editor.h"
#include "audio.h"
#include "sample.h"
#include "progress.h"
#include "local.h"
#include "guirecorder.h"

extern struct editor editor;
extern struct browser local_browser;

static GtkDialog *autosampler_dialog;
static GtkEntry *autosampler_dialog_name_entry;
static struct guirecorder autosampler_guirecorder;
static GtkWidget *autosampler_dialog_channel_spin;
static GtkWidget *autosampler_dialog_start_combo;
static GtkWidget *autosampler_dialog_end_combo;
static GtkWidget *autosampler_dialog_distance_spin;
static GtkWidget *autosampler_dialog_velocity_spin;
static GtkWidget *autosampler_dialog_press_spin;
static GtkWidget *autosampler_dialog_release_spin;
static GtkWidget *autosampler_dialog_cancel_button;
static GtkWidget *autosampler_dialog_start_button;
static GtkListStore *notes_list_store;

struct autosampler_data
{
  const gchar *name;
  guint channel_mask;
  gint channel;
  gint first;
  gint last;
  gint semitones;
  gint velocity;
  gdouble press;
  gdouble release;
  struct backend *backend;
  GtkTreeIter iter;
};

static void
autosampler_run (GtkWidget * object, gpointer data)
{
  gtk_dialog_response (autosampler_dialog, GTK_RESPONSE_ACCEPT);
}

static void
autosampler_cancel (GtkWidget * object, gpointer data)
{
  gtk_dialog_response (autosampler_dialog, GTK_RESPONSE_CANCEL);
}

static gpointer
autosampler_runner (gpointer user_data)
{
  struct autosampler_data *data = user_data;
  const gchar *note;
  gboolean active;
  gint s, total, i;
  GValue value = G_VALUE_INIT;
  gdouble fract;
  gchar filename[LABEL_MAX];

  sysex_transfer.active = TRUE;
  progress_set_fraction (0.0);

  total = ((data->last - data->first) / data->semitones) + 1;
  s = 0;
  i = data->first;
  while (1)
    {
      gtk_tree_model_get_value (GTK_TREE_MODEL (notes_list_store),
				&data->iter, 0, &value);
      note = g_value_get_string (&value);
      debug_print (1, "Recording note %s (%d)...\n", note, i);

      audio_start_recording (&editor.audio, data->channel_mask, NULL, NULL);
      backend_send_note_on (data->backend, data->channel, i, data->velocity);
      //Add some extra time to deal with runtime delays.
      usleep ((data->press + 0.25) * 1000000);
      backend_send_note_off (data->backend, data->channel, i, data->velocity);
      usleep (data->release * 1000000);
      audio_stop_recording (&editor.audio);

      //Remove the heading silent frames.
      guint start = audio_detect_start (&editor.audio);
      audio_delete_range (&editor.audio, 0, start);
      //Cut off the frames after the requested time.
      start = (data->press + data->release) * editor.audio.samplerate;
      guint len = editor.audio.frames - start;
      audio_delete_range (&editor.audio, start, len);

      gchar *dir = path_chain (PATH_SYSTEM, local_browser.dir, data->name);
      local_mkdir (NULL, dir);
      //We add the note number to ensure lexicographical order.
      snprintf (filename, LABEL_MAX, "%03d %s %s.wav", s, data->name, note);
      gchar *path = path_chain (PATH_SYSTEM, dir, filename);
      debug_print (1, "Saving sample to %s...\n", path);
      sample_save_from_array (path, editor.audio.sample,
			      &editor.audio.control);
      g_free (dir);
      g_free (path);

      g_value_unset (&value);

      for (gint j = 0; j < data->semitones; j++, i++)
	{
	  gtk_tree_model_iter_next (GTK_TREE_MODEL (notes_list_store),
				    &data->iter);
	}

      s++;
      fract = s / (gdouble) total;
      progress_set_fraction (fract);

      if (i > data->last)
	{
	  break;
	}

      g_mutex_lock (&sysex_transfer.mutex);
      active = sysex_transfer.active;
      g_mutex_unlock (&sysex_transfer.mutex);

      if (!active)
	{
	  break;
	}

      sleep (1);
    }

  g_free (data);
  progress_response (GTK_RESPONSE_ACCEPT);
  return NULL;
}

static void
autosampler_callback (GtkWidget * object, gpointer user_data)
{
  gint res;
  struct autosampler_data *data = g_malloc (sizeof (struct autosampler_data));
  data->backend = user_data;
  guint options =
    guirecorder_get_channel_mask (autosampler_guirecorder.channels_combo) |
    RECORD_MONITOR_ONLY;

  audio_stop_playback (&editor.audio);
  audio_stop_recording (&editor.audio);
  audio_start_recording (&editor.audio, options, guirecorder_monitor_notifier,
			 &autosampler_guirecorder);

  gtk_entry_set_text (autosampler_dialog_name_entry, "");
  gtk_widget_grab_focus (GTK_WIDGET (autosampler_dialog_name_entry));
  gtk_widget_set_sensitive (autosampler_dialog_start_button, FALSE);
  res = gtk_dialog_run (GTK_DIALOG (autosampler_dialog));
  gtk_widget_hide (GTK_WIDGET (autosampler_dialog));
  audio_stop_recording (&editor.audio);
  if (res != GTK_RESPONSE_ACCEPT)
    {
      return;
    }

  data->channel_mask =
    guirecorder_get_channel_mask (autosampler_guirecorder.channels_combo);
  data->name = gtk_entry_get_text (autosampler_dialog_name_entry);
  data->channel =
    gtk_spin_button_get_value (GTK_SPIN_BUTTON
			       (autosampler_dialog_channel_spin));
  data->velocity =
    gtk_spin_button_get_value (GTK_SPIN_BUTTON
			       (autosampler_dialog_velocity_spin));
  data->first =
    gtk_combo_box_get_active (GTK_COMBO_BOX (autosampler_dialog_start_combo));
  data->last =
    gtk_combo_box_get_active (GTK_COMBO_BOX (autosampler_dialog_end_combo));
  data->semitones =
    gtk_spin_button_get_value (GTK_SPIN_BUTTON
			       (autosampler_dialog_distance_spin));
  data->press =
    gtk_spin_button_get_value (GTK_SPIN_BUTTON
			       (autosampler_dialog_press_spin));
  data->release =
    gtk_spin_button_get_value (GTK_SPIN_BUTTON
			       (autosampler_dialog_release_spin));

  gtk_combo_box_get_active_iter (GTK_COMBO_BOX
				 (autosampler_dialog_start_combo),
				 &data->iter);

  progress_run (autosampler_runner, data, _("Auto Sampler"),
		_("Recording..."), NULL);
}

static void
autosampler_dialog_name_changed (GtkWidget * object, gpointer data)
{
  size_t len = strlen (gtk_entry_get_text (autosampler_dialog_name_entry));
  gtk_widget_set_sensitive (autosampler_dialog_start_button, len > 0);
}

static void
autosampler_configure_gui (struct backend *backend, GtkBuilder * builder)
{
  if (autosampler_dialog)
    {
      return;
    }

  autosampler_dialog =
    GTK_DIALOG (gtk_builder_get_object (builder, "autosampler_dialog"));
  autosampler_dialog_name_entry =
    GTK_ENTRY (gtk_builder_get_object
	       (builder, "autosampler_dialog_name_entry"));
  autosampler_guirecorder.channels_combo =
    GTK_WIDGET (gtk_builder_get_object
		(builder, "autosampler_dialog_channels_combo"));
  autosampler_guirecorder.monitor_levelbar =
    GTK_LEVEL_BAR (gtk_builder_get_object
		   (builder, "autosampler_dialog_monitor_levelbar"));
  autosampler_dialog_channel_spin =
    GTK_WIDGET (gtk_builder_get_object
		(builder, "autosampler_dialog_channel_spin"));
  autosampler_dialog_start_combo =
    GTK_WIDGET (gtk_builder_get_object
		(builder, "autosampler_dialog_start_combo"));
  autosampler_dialog_end_combo =
    GTK_WIDGET (gtk_builder_get_object
		(builder, "autosampler_dialog_end_combo"));
  autosampler_dialog_distance_spin =
    GTK_WIDGET (gtk_builder_get_object
		(builder, "autosampler_dialog_distance_spin"));
  autosampler_dialog_velocity_spin =
    GTK_WIDGET (gtk_builder_get_object
		(builder, "autosampler_dialog_velocity_spin"));
  autosampler_dialog_press_spin =
    GTK_WIDGET (gtk_builder_get_object
		(builder, "autosampler_dialog_press_spin"));
  autosampler_dialog_release_spin =
    GTK_WIDGET (gtk_builder_get_object
		(builder, "autosampler_dialog_release_spin"));
  autosampler_dialog_cancel_button =
    GTK_WIDGET (gtk_builder_get_object
		(builder, "autosampler_dialog_cancel_button"));
  autosampler_dialog_start_button =
    GTK_WIDGET (gtk_builder_get_object
		(builder, "autosampler_dialog_start_button"));
  notes_list_store =
    GTK_LIST_STORE (gtk_builder_get_object (builder, "notes_list_store"));

  g_signal_connect (autosampler_dialog_start_button, "clicked",
		    G_CALLBACK (autosampler_run), NULL);
  g_signal_connect (autosampler_dialog_cancel_button, "clicked",
		    G_CALLBACK (autosampler_cancel), NULL);

  g_signal_connect (autosampler_dialog_name_entry, "changed",
		    G_CALLBACK (autosampler_dialog_name_changed), NULL);
  g_signal_connect (autosampler_guirecorder.channels_combo, "changed",
		    G_CALLBACK (guirecorder_channels_changed), &editor.audio);
}

struct menu_action *
autosampler_init (struct backend *backend, GtkBuilder * builder,
		  GtkWindow * parent)
{
  struct menu_action *ma = NULL;

  if (backend->type == BE_TYPE_MIDI)
    {
      ma = g_malloc (sizeof (struct menu_action));
      ma->type = MENU_ACTION_ITEM;
      ma->name = _("_Auto Sampler");
      ma->callback = G_CALLBACK (autosampler_callback);
    }

  autosampler_configure_gui (backend, builder);

  return ma;
}
