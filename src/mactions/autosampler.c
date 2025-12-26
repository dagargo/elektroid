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
#include "../config.h"
#include "audio.h"
#include "browser.h"
#include "connectors/system.h"
#include "guirecorder.h"
#include "maction.h"
#include "progress_window.h"
#include "sample.h"

#define SAMPLES_DIR "samples"

static struct guirecorder guirecorder;

static GtkWindow *window;
static GtkEntry *name_entry;
static GtkWidget *normalize_switch;
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
  gboolean normalize;
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
autosampler_get_down_up_distance (gint distance, gint *up, gint *down)
{
  gint additional_notes = distance - 1;
  *up = additional_notes / 2;
  *down = additional_notes - *up;
}

static void
autosampler_runner (gpointer user_data)
{
  struct autosampler_data *data = user_data;
  const gchar *note;
  gint s, total, i, up, down;
  guint32 start, duration, trail_length;
  GValue value = G_VALUE_INIT;
  gdouble fract;
  gchar filename[LABEL_MAX], *path;
  struct sample_info *sample_info;
  GString *sfz;
  gchar *dir, *samples_dir, *sfz_path, *sfz_filename;

  autosampler_get_down_up_distance (data->semitones, &up, &down);

  progress_window_set_fraction (0.0);

  dir = path_chain (PATH_SYSTEM, local_browser.dir, data->name);
  samples_dir = path_chain (PATH_SYSTEM, dir, SAMPLES_DIR);
  system_mkdir (NULL, samples_dir);

  sfz = g_string_new_len (NULL, 64 * KI);

  g_string_append_printf (sfz, "//%s SFZ v1\n", data->name);
  g_string_append (sfz, "//Created with Elektroid " PACKAGE_VERSION "\n");
  g_string_append (sfz, "\n");

  g_string_append (sfz, "<group>\n");
  g_string_append (sfz, "loop_mode=loop_continuous");
  g_string_append (sfz, "\n");

  g_string_append_printf (sfz, "tune=%d\n", data->tuning);
  g_string_append (sfz, "\n");

  g_string_append (sfz, "amp_veltrack=0\n");
  g_string_append (sfz, "fil_keycenter=0\n");
  g_string_append (sfz, "fil_keytrack=0\n");
  g_string_append (sfz, "ampeg_attack=0\n");
  g_string_append (sfz, "ampeg_decay=0\n");
  g_string_append (sfz, "ampeg_sustain=100\n");
  g_string_append (sfz, "ampeg_release=0\n");
  g_string_append (sfz, "\n");

  g_string_append_printf (sfz, "cutoff=%2d\n", audio.rate / 2);
  g_string_append (sfz, "resonance=0\n");
  g_string_append (sfz, "fil_veltrack=0\n");
  g_string_append (sfz, "fil_keycenter=0\n");
  g_string_append (sfz, "fil_keytrack=0\n");
  g_string_append (sfz, "fileg_attack=0\n");
  g_string_append (sfz, "fileg_decay=0\n");
  g_string_append (sfz, "fileg_sustain=100\n");
  g_string_append (sfz, "fileg_release=0\n");
  g_string_append (sfz, "\n");

  total = ((data->end - data->start) / data->semitones) + 1;
  s = 0;
  i = data->start;
  while (1)
    {
      gtk_tree_model_get_value (GTK_TREE_MODEL (notes_list_store),
				&data->iter, 0, &value);
      note = g_value_get_string (&value);
      debug_print (1, "Recording note %s (%d)...", note, i);

      snprintf (filename, LABEL_MAX, "%s %03d %s.wav", data->name, i, note);
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
      sample_info->tags = NULL;

      g_mutex_lock (&audio.control.controllable.mutex);

      if (data->normalize)
	{
	  audio_normalize (&audio.sample, 0, sample_info->frames);
	}

      duration = (data->press + data->release) * audio.rate;
      start = audio_detect_start (&audio.sample);

      if (sample_info->frames - start >= duration)
	{
	  audio_delete_range (&audio.sample, 0, start);
	  trail_length = sample_info->frames - duration;
	  audio_delete_range (&audio.sample, duration, trail_length);
	}
      else
	{
	  debug_print (1,
		       "Bad start detection due to signal being too weak. Skipping trimming sample...");
	}

      g_mutex_unlock (&audio.control.controllable.mutex);

      //We add the note number to ensure lexicographical order.
      path = path_chain (PATH_SYSTEM, samples_dir, filename);
      debug_print (1, "Saving sample to %s...", path);
      sample_save_to_file (path, &audio.sample, &audio.control,
			   SF_FORMAT_WAV | sample_get_internal_format ());
      g_free (path);

      g_string_append (sfz, "<region>\n");
      g_string_append_printf (sfz, "sample=%s%c%s\n", SAMPLES_DIR,
			      G_DIR_SEPARATOR, filename);
      g_string_append_printf (sfz, "lokey=%d hikey=%d\n", i - down, i + up);
      g_string_append_printf (sfz, "pitch_keycenter=%d\n", i);
      g_string_append (sfz, "\n");

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
	  usleep (250000);	// Time to let the progress move to 100 %
	  break;
	}

      if (!progress_window_is_active ())
	{
	  break;
	}

      sleep (1);
    }

  sfz_filename = g_strdup_printf ("%s.sfz", data->name);
  sfz_path = path_chain (PATH_SYSTEM, dir, sfz_filename);
  debug_print (1, "Writing sfz file...");
  if (file_save_data (sfz_path, (guint8 *) sfz->str, sfz->len))
    {
      error_print ("Error while saving sfz file to \"%s\"", sfz_path);
    }
  g_string_free (sfz, TRUE);
  g_free (sfz_path);
  g_free (sfz_filename);

  g_free (samples_dir);
  g_free (dir);

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
  if (gtk_widget_get_visible (GTK_WIDGET (window)))
    {
      audio_stop_recording ();	//Stop monitoring
      while (gtk_events_pending ())
	{
	  gtk_main_iteration_do (TRUE);	//Wait for drawings
	}
    }
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
  autosampler_data->normalize =
    gtk_switch_get_active (GTK_SWITCH (normalize_switch));

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
  normalize_switch =
    GTK_WIDGET (gtk_builder_get_object
		(builder, "autosampler_window_normalize_switch"));

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
