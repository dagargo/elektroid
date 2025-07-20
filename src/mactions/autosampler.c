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
#include "audio.h"
#include "browser.h"
#include "connectors/system.h"
#include "guirecorder.h"
#include "maction.h"
#include "progress_window.h"
#include "sample.h"

static struct guirecorder guirecorder;

static GtkWindow *window;
static GtkEntry *name_entry;
static GtkWidget *channel_spin;
static GtkWidget *start_combo;
static GtkWidget *end_combo;
static GtkWidget *distance_spin;
static GtkWidget *velocity_spin;
static GtkWidget *tuning_spin;
static GtkWidget *press_spin;
static GtkWidget *release_spin;
static GtkWidget *start_button;
static GtkWidget *cancel_button;
static GtkListStore *notes_list_store;

struct autosampler_data
{
  const gchar *name;
  guint channel_mask;
  gint channel;
  gint start;
  gint end;
  gint semitones;
  gint velocity;
  gint tuning;
  gdouble press;
  gdouble release;
  GtkTreeIter iter;
};

static void
autosampler_runner (gpointer user_data)
{
  struct autosampler_data *data = user_data;
  const gchar *note;
  gint s, total, i;
  guint32 start;
  GValue value = G_VALUE_INIT;
  gdouble fract;
  gchar filename[LABEL_MAX], *path;
  struct sample_info *sample_info;

  progress_window_set_fraction (0.0);

  total = ((data->end - data->start) / data->semitones) + 1;
  s = 0;
  i = data->start;
  while (1)
    {
      gtk_tree_model_get_value (GTK_TREE_MODEL (notes_list_store),
				&data->iter, 0, &value);
      note = g_value_get_string (&value);
      debug_print (1, "Recording note %s (%d)...", note, i);

      snprintf (filename, LABEL_MAX, "%03d %s %s.wav", s, data->name, note);
      progress_window_set_label (filename);

      audio_start_recording (data->channel_mask, NULL, NULL);
      backend_send_note_on (remote_browser.backend, data->channel, i,
			    data->velocity);
      //Add some extra time to deal with runtime delays.
      usleep ((data->press + 0.25) * 1000000);
      backend_send_note_off (remote_browser.backend, data->channel, i,
			     data->velocity);
      usleep (data->release * 1000000);

      audio_stop_recording ();

      sample_info = audio.sample.info;
      sample_info->midi_note = i;
      sample_info->midi_fraction = cents_to_midi_fraction (data->tuning);

      //Cut off the frames after the requested time.
      start = (data->press + data->release) * audio.rate;
      guint len = sample_info->frames - start;
      audio_delete_range (start, len);

      gchar *dir = path_chain (PATH_SYSTEM, local_browser.dir, data->name);
      system_mkdir (NULL, dir);
      //We add the note number to ensure lexicographical order.
      path = path_chain (PATH_SYSTEM, dir, filename);
      debug_print (1, "Saving sample to %s...", path);
      sample_save_to_file (path, &audio.sample, &audio.control,
			   SF_FORMAT_WAV | sample_get_internal_format ());
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
      progress_window_set_fraction (fract);

      if (i > data->end)
	{
	  break;
	}

      if (!progress_window_is_active ())
	{
	  break;
	}

      sleep (1);
    }

  audio_reset_sample ();

  g_free (data);
}

static void
autosampler_callback (GtkWidget *object, gpointer user_data)
{
  guint options;
  GtkEntryBuffer *buf = gtk_entry_get_buffer (GTK_ENTRY (name_entry));

  guirecorder_set_channels_masks (&guirecorder,
				  FS_OPTION_STEREO | FS_OPTION_MONO);

  options = guirecorder_get_channel_mask (&guirecorder) | RECORD_MONITOR_ONLY;

  audio_stop_playback ();
  audio_stop_recording ();
  audio_start_recording (options, guirecorder_monitor_notifier, &guirecorder);

  gtk_entry_buffer_set_text (buf, "", -1);
  gtk_widget_grab_focus (GTK_WIDGET (name_entry));
  gtk_widget_set_sensitive (start_button, FALSE);

  gtk_widget_show (GTK_WIDGET (window));
}

static void
autosampler_cancel (GtkWidget *object, gpointer data)
{
  audio_stop_recording ();	//Stop monitoring
  gtk_widget_hide (GTK_WIDGET (window));
}

static gboolean
name_window_key_press (GtkWidget *widget, GdkEventKey *event, gpointer data)
{
  if (event->keyval == GDK_KEY_Escape)
    {
      autosampler_cancel (NULL, NULL);
      return TRUE;
    }
  return FALSE;
}

static void
autosampler_start (GtkWidget *object, gpointer data)
{
  struct autosampler_data *autosampler_data;
  GtkEntryBuffer *buf = gtk_entry_get_buffer (GTK_ENTRY (name_entry));

  autosampler_cancel (NULL, NULL);

  autosampler_data = g_malloc (sizeof (struct autosampler_data));

  autosampler_data->channel_mask =
    guirecorder_get_channel_mask (&guirecorder);
  autosampler_data->name = gtk_entry_buffer_get_text (buf);
  autosampler_data->channel =
    gtk_spin_button_get_value (GTK_SPIN_BUTTON (channel_spin));
  autosampler_data->velocity =
    gtk_spin_button_get_value (GTK_SPIN_BUTTON (velocity_spin));
  autosampler_data->tuning =
    gtk_spin_button_get_value (GTK_SPIN_BUTTON (tuning_spin));

  autosampler_data->start =
    gtk_combo_box_get_active (GTK_COMBO_BOX (start_combo));
  autosampler_data->end =
    gtk_combo_box_get_active (GTK_COMBO_BOX (end_combo));
  autosampler_data->semitones =
    gtk_spin_button_get_value (GTK_SPIN_BUTTON (distance_spin));

  autosampler_data->press =
    gtk_spin_button_get_value (GTK_SPIN_BUTTON (press_spin));
  autosampler_data->release =
    gtk_spin_button_get_value (GTK_SPIN_BUTTON (release_spin));

  gtk_combo_box_get_active_iter (GTK_COMBO_BOX
				 (start_combo), &autosampler_data->iter);

  progress_window_open (autosampler_runner, NULL, NULL, autosampler_data,
			PROGRESS_TYPE_NO_AUTO, _("Auto Sampler"),
			_("Recording..."), TRUE);
}

static void
name_changed (GtkWidget *object, gpointer data)
{
  GtkEntryBuffer *buf = gtk_entry_get_buffer (GTK_ENTRY (name_entry));
  size_t len = strlen (gtk_entry_buffer_get_text (buf));
  gtk_widget_set_sensitive (start_button, len > 0);
}

void
autosampler_init (GtkBuilder *builder)
{
  window =
    GTK_WINDOW (gtk_builder_get_object (builder, "autosampler_window"));
  name_entry =
    GTK_ENTRY (gtk_builder_get_object
	       (builder, "autosampler_window_name_entry"));
  guirecorder.channels_combo =
    GTK_WIDGET (gtk_builder_get_object
		(builder, "autosampler_window_channels_combo"));
  guirecorder.channels_list_store =
    GTK_LIST_STORE (gtk_builder_get_object
		    (builder, "autosampler_window_channels_list_store"));
  guirecorder.monitor_levelbar_l =
    GTK_LEVEL_BAR (gtk_builder_get_object
		   (builder, "autosampler_window_monitor_levelbar_l"));
  guirecorder.monitor_levelbar_r =
    GTK_LEVEL_BAR (gtk_builder_get_object
		   (builder, "autosampler_window_monitor_levelbar_r"));

  channel_spin =
    GTK_WIDGET (gtk_builder_get_object
		(builder, "autosampler_window_channel_spin"));
  start_combo =
    GTK_WIDGET (gtk_builder_get_object
		(builder, "autosampler_window_start_combo"));
  end_combo =
    GTK_WIDGET (gtk_builder_get_object
		(builder, "autosampler_window_end_combo"));
  distance_spin =
    GTK_WIDGET (gtk_builder_get_object
		(builder, "autosampler_window_distance_spin"));
  velocity_spin =
    GTK_WIDGET (gtk_builder_get_object
		(builder, "autosampler_window_velocity_spin"));
  tuning_spin =
    GTK_WIDGET (gtk_builder_get_object
		(builder, "autosampler_window_tuning_spin"));
  press_spin =
    GTK_WIDGET (gtk_builder_get_object
		(builder, "autosampler_window_press_spin"));
  release_spin =
    GTK_WIDGET (gtk_builder_get_object
		(builder, "autosampler_window_release_spin"));
  start_button =
    GTK_WIDGET (gtk_builder_get_object
		(builder, "autosampler_window_start_button"));
  cancel_button =
    GTK_WIDGET (gtk_builder_get_object
		(builder, "autosampler_window_cancel_button"));
  notes_list_store =
    GTK_LIST_STORE (gtk_builder_get_object (builder, "notes_list_store"));

  g_signal_connect (name_entry, "changed", G_CALLBACK (name_changed), NULL);
  g_signal_connect (guirecorder.channels_combo, "changed",
		    G_CALLBACK (guirecorder_channels_changed), &guirecorder);

  g_signal_connect (start_button, "clicked",
		    G_CALLBACK (autosampler_start), NULL);
  g_signal_connect (cancel_button, "clicked",
		    G_CALLBACK (autosampler_cancel), NULL);

  g_signal_connect (window, "key_press_event",
		    G_CALLBACK (name_window_key_press), NULL);
}

void
autosampler_destroy ()
{
  debug_print (1, "Destroying autosampler...");
  autosampler_cancel (NULL, NULL);
  gtk_main_iteration_do (TRUE);	//Wait for guirecorder
  gtk_widget_destroy (GTK_WIDGET (window));
}

struct maction *
autosampler_maction_builder (struct maction_context *context)
{
  struct maction *ma = NULL;

  if (remote_browser.backend && remote_browser.backend->type == BE_TYPE_MIDI)
    {
      ma = g_malloc (sizeof (struct maction));
      ma->type = MACTION_BUTTON;
      ma->name = _("_Auto Sampler");
      ma->sensitive = audio_check ();
      ma->callback = G_CALLBACK (autosampler_callback);
    }

  return ma;
}
