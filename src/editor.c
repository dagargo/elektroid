/*
 *   editor.c
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
#include <glib/gi18n.h>
#include "editor.h"
#include "sample.h"

#define EDITOR_PREF_CHANNELS (!editor->remote_browser->fs_ops || (editor->remote_browser->fs_ops->options & FS_OPTION_STEREO) || !editor->preferences->mix ? 2 : 1)

#define MAX_DRAW_X 10000

#define EDITOR_LOADED_CHANNELS(n) (n == 2 && sample_info->channels == 2 ? 2 : 1)

struct editor_set_volume_data
{
  struct editor *editor;
  gdouble volume;
};

extern void elektroid_set_audio_controls_on_load (gboolean sensitive);

static void
editor_set_widget_source (GtkWidget * widget, enum audio_src audio_src)
{
  const char *class;
  GtkStyleContext *context = gtk_widget_get_style_context (widget);
  GList *classes, *list = gtk_style_context_list_classes (context);

  for (classes = list; classes != NULL; classes = g_list_next (classes))
    {
      gtk_style_context_remove_class (context, classes->data);
    }
  g_list_free (list);

  if (audio_src == AUDIO_SRC_NONE)
    {
      return;
    }

  if (GTK_IS_SWITCH (widget))
    {
      class = audio_src == AUDIO_SRC_LOCAL ? "local_switch" : "remote_switch";
    }
  else
    {
      class = audio_src == AUDIO_SRC_LOCAL ? "local" : "remote";
    }
  gtk_style_context_add_class (context, class);
}

void
editor_set_source (struct editor *editor, enum audio_src audio_src)
{
  editor_set_widget_source (editor->autoplay_switch, audio_src);
  editor_set_widget_source (editor->mix_switch, audio_src);
  editor_set_widget_source (editor->play_button, audio_src);
  editor_set_widget_source (editor->stop_button, audio_src);
  editor_set_widget_source (editor->loop_button, audio_src);
  editor_set_widget_source (editor->volume_button, audio_src);
  editor_set_widget_source (editor->waveform_draw_area, audio_src);
}

static void
editor_set_sample_properties_on_load (struct editor *editor)
{
  double time;
  gchar label[LABEL_MAX];
  struct sample_info *sample_info = editor->audio.control.data;

  gtk_widget_set_visible (editor->sample_info_box, sample_info->frames > 0);

  if (!sample_info->frames)
    {
      return;
    }

  time = sample_info->frames / (double) sample_info->samplerate;

  gtk_widget_set_visible (editor->sample_info_box, TRUE);

  snprintf (label, LABEL_MAX, "%d", sample_info->frames);
  gtk_label_set_text (GTK_LABEL (editor->sample_length), label);

  if (time >= 60)
    {
      snprintf (label, LABEL_MAX, "%.2f %s", time / 60.0, _("minutes"));
    }
  else
    {
      snprintf (label, LABEL_MAX, "%.2f s", time);
    }
  gtk_label_set_text (GTK_LABEL (editor->sample_duration), label);

  snprintf (label, LABEL_MAX, "%.2f kHz", sample_info->samplerate / 1000.f);
  gtk_label_set_text (GTK_LABEL (editor->sample_samplerate), label);

  snprintf (label, LABEL_MAX, "%d", sample_info->channels);
  gtk_label_set_text (GTK_LABEL (editor->sample_channels), label);

  if (sample_info->bitdepth)
    {
      snprintf (label, LABEL_MAX, "%d", sample_info->bitdepth);
      gtk_label_set_text (GTK_LABEL (editor->sample_bitdepth), label);
    }
}

static gboolean
editor_update_ui_on_load (gpointer data)
{
  gboolean ready_to_play;
  struct editor *editor = data;
  struct audio *audio = &editor->audio;
  struct sample_info *sample_info = audio->control.data;

  g_mutex_lock (&audio->control.mutex);
  ready_to_play = audio->frames >= LOAD_BUFFER_LEN || (!audio->control.active
						       && audio->frames > 0);
  audio->channels = EDITOR_LOADED_CHANNELS (editor->target_channels);
  g_mutex_unlock (&audio->control.mutex);

  editor_set_sample_properties_on_load (editor);

  if (ready_to_play)
    {
      if (audio_check (&editor->audio))
	{
	  elektroid_set_audio_controls_on_load (TRUE);
	}
      if (editor->preferences->autoplay)
	{
	  audio_play (&editor->audio);
	}
      return FALSE;
    }

  return TRUE;
}

gboolean
editor_draw_waveform (GtkWidget * widget, cairo_t * cr, gpointer data)
{
  guint width, height;
  GdkRGBA color;
  GtkStyleContext *context;
  gint x_widget, x_sample;
  double x_ratio, mid_y1, mid_y2, value;
  short *sample;
  gboolean stereo;
  double y_scale = 1.0 / (double) SHRT_MIN;
  double x_scale = 1.0 / (double) MAX_DRAW_X;
  struct editor *editor = data;
  struct audio *audio = &editor->audio;
  struct sample_info *sample_info = audio->control.data;

  g_mutex_lock (&audio->control.mutex);

  stereo = EDITOR_LOADED_CHANNELS (editor->target_channels) == 2;

  context = gtk_widget_get_style_context (widget);
  width = gtk_widget_get_allocated_width (widget) - 2;
  height = gtk_widget_get_allocated_height (widget) - 2;
  y_scale *= height;
  if (stereo)
    {
      mid_y1 = height * 0.25;
      mid_y2 = height * 0.75;
      y_scale *= 0.25;
    }
  else
    {
      mid_y1 = height * 0.50;
      y_scale *= 0.5;
    }
  gtk_render_background (context, cr, 0, 0, width, height);
  gtk_style_context_get_color (context, gtk_style_context_get_state (context),
			       &color);
  gdk_cairo_set_source_rgba (cr, &color);

  sample = (short *) audio->sample->data;
  x_ratio = audio->frames / (double) MAX_DRAW_X;
  for (gint i = 0; i < MAX_DRAW_X; i++)
    {
      x_sample = i * x_ratio * (stereo ? 2 : 1);
      x_widget = i * width * x_scale;
      if (x_sample < audio->sample->len >> 1)
	{
	  value = mid_y1 - sample[x_sample] * y_scale;
	  cairo_move_to (cr, x_widget, mid_y1);
	  cairo_line_to (cr, x_widget, value);
	  cairo_stroke (cr);
	  if (stereo)
	    {
	      value = mid_y2 - sample[x_sample + 1] * y_scale;
	      cairo_move_to (cr, x_widget, mid_y2);
	      cairo_line_to (cr, x_widget, value);
	      cairo_stroke (cr);
	    }
	}
    }

  g_mutex_unlock (&audio->control.mutex);

  return FALSE;
}

static gboolean
editor_queue_draw (gpointer data)
{
  struct editor *editor = data;
  gtk_widget_queue_draw (editor->waveform_draw_area);
  return FALSE;
}

static void
editor_redraw (struct job_control *control, gpointer data)
{
  g_idle_add (editor_queue_draw, data);
}

static void
editor_load_sample_cb (struct job_control *control, gdouble p, gpointer data)
{
  set_job_control_progress_no_sync (control, p, NULL);
  editor_redraw (control, data);
}

static gpointer
editor_load_sample_runner (gpointer data)
{
  struct sample_params sample_params;
  struct editor *editor = data;
  struct audio *audio = &editor->audio;
  struct sample_info *sample_info = audio->control.data;

  sample_params.samplerate = audio->samplerate;
  sample_params.channels = editor->target_channels;

  g_timeout_add (100, editor_update_ui_on_load, editor);

  g_mutex_lock (&audio->control.mutex);
  audio->control.active = TRUE;
  g_mutex_unlock (&audio->control.mutex);

  if (sample_load_from_file_with_cb
      (audio->path, audio->sample, &audio->control, &sample_params,
       &audio->frames, editor_load_sample_cb, editor) >= 0)
    {
      debug_print (1,
		   "Samples: %d (%d B); channels: %d; loop start at %d; loop end at %d; sample rate: %.2f kHz; bit depth: %d\n",
		   audio->frames, audio->sample->len, sample_info->channels,
		   sample_info->loopstart, sample_info->loopend,
		   sample_info->samplerate / 1000.0, sample_info->bitdepth);
    }

  g_mutex_lock (&audio->control.mutex);
  audio->control.active = FALSE;
  g_mutex_unlock (&audio->control.mutex);

  return NULL;
}

void
editor_play_clicked (GtkWidget * object, gpointer data)
{
  struct editor *editor = data;
  audio_play (&editor->audio);
}

void
editor_stop_clicked (GtkWidget * object, gpointer data)
{
  struct editor *editor = data;
  audio_stop (&editor->audio);
}

void
editor_loop_clicked (GtkWidget * object, gpointer data)
{
  struct editor *editor = data;
  editor->audio.loop =
    gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (object));
}

gboolean
editor_autoplay_clicked (GtkWidget * object, gboolean state, gpointer data)
{
  struct editor *editor = data;
  editor->preferences->autoplay = state;
  return FALSE;
}

void
editor_start_load_thread (struct editor *editor)
{
  debug_print (1, "Creating load thread...\n");
  editor->target_channels = EDITOR_PREF_CHANNELS;
  editor->thread = g_thread_new ("load_sample", editor_load_sample_runner,
				 editor);
}

void
editor_stop_load_thread (struct editor *editor)
{
  struct audio *audio = &editor->audio;

  debug_print (1, "Stopping load thread...\n");
  g_mutex_lock (&audio->control.mutex);
  audio->control.active = FALSE;
  g_mutex_unlock (&audio->control.mutex);

  if (editor->thread)
    {
      g_thread_join (editor->thread);
      editor->thread = NULL;
    }
}

gboolean
editor_mix_clicked (GtkWidget * object, gboolean state, gpointer data)
{
  struct editor *editor = data;
  struct audio *audio = &editor->audio;
  editor->preferences->mix = state;
  if (strlen (audio->path))
    {
      audio_stop (audio);
      editor_stop_load_thread (editor);
      editor_start_load_thread (editor);
    }
  return FALSE;
}

void
editor_set_volume (GtkScaleButton * button, gdouble value, gpointer data)
{
  struct editor *editor = data;
  audio_set_volume (&editor->audio, value);
}

static gboolean
editor_set_volume_callback_bg (gpointer user_data)
{
  struct editor_set_volume_data *data = user_data;
  struct editor *editor = data->editor;
  gdouble volume = data->volume;
  g_free (data);
  debug_print (1, "Setting volume to %f...\n", volume);
  g_signal_handler_block (editor->volume_button,
			  editor->volume_changed_handler);
  gtk_scale_button_set_value (GTK_SCALE_BUTTON (editor->volume_button),
			      volume);
  g_signal_handler_unblock (editor->volume_button,
			    editor->volume_changed_handler);
  return FALSE;
}

void
editor_set_volume_callback (gpointer editor, gdouble volume)
{
  struct editor_set_volume_data *data =
    g_malloc (sizeof (struct editor_set_volume_data));
  data->editor = editor;
  data->volume = volume;
  g_idle_add (editor_set_volume_callback_bg, data);
}
