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

#define EDITOR_OP_NONE 0
#define EDITOR_OP_SELECT 1
#define EDITOR_OP_MOVE_LOOP_START 2
#define EDITOR_OP_MOVE_LOOP_END 3

#define EDITOR_LOOP_MARKER_WIDTH 7
#define EDITOR_LOOP_MARKER_HALF_HEIGHT 4
#define EDITOR_LOOP_MARKER_FULL_HEIGHT (EDITOR_LOOP_MARKER_HALF_HEIGHT * 2)

#if defined(__linux__)
#define FRAMES_TO_PLAY (16 * 1024)
#else
#define FRAMES_TO_PLAY (64 * 1024)
#endif

#define MAX_FRAMES_PER_PIXEL 300

extern struct browser local_browser;
extern struct browser remote_browser;

extern void elektroid_update_audio_status ();

struct editor_y_frame_state
{
  gdouble *wp;
  gdouble *wn;
  guint *wpc;
  guint *wnc;
};

struct editor_set_volume_data
{
  struct editor *editor;
  gdouble volume;
};

struct editor_record_clicked_data
{
  struct editor *editor;
  struct browser *browser;
};

extern gchar *elektroid_ask_name (const gchar * title, const gchar * value,
				  struct browser *browser);

static void
editor_set_layout_width_to_val (struct editor *editor, guint w)
{
  guint h;
  gtk_layout_get_size (GTK_LAYOUT (editor->waveform), NULL, &h);
  gtk_layout_set_size (GTK_LAYOUT (editor->waveform), w, h);
}

static void
editor_set_layout_width (struct editor *editor)
{
  guint w = gtk_widget_get_allocated_width (editor->waveform_scrolled_window);
  w = w * editor->zoom - 2;	//2 border pixels
  editor_set_layout_width_to_val (editor, w);
}

static void
editor_set_widget_source (GtkWidget * widget, gpointer data)
{
  const char *class;
  struct editor *editor = data;
  GtkStyleContext *context = gtk_widget_get_style_context (widget);
  GList *classes, *list = gtk_style_context_list_classes (context);

  for (classes = list; classes != NULL; classes = g_list_next (classes))
    {
      gtk_style_context_remove_class (context, classes->data);
    }
  g_list_free (list);

  if (editor->browser == NULL)
    {
      return;
    }

  if (GTK_IS_SWITCH (widget))
    {
      class = editor->browser == &local_browser ? "local_switch" :
	"remote_switch";
    }
  else
    {
      class = editor->browser == &local_browser ? "local" : "remote";
    }
  gtk_style_context_add_class (context, class);
}

void
editor_reset (struct editor *editor, struct browser *browser)
{
  editor_set_layout_width_to_val (editor, 1);
  audio_stop_playback (&editor->audio);
  audio_stop_recording (&editor->audio);
  editor_stop_load_thread (editor);
  audio_reset_sample (&editor->audio);
  editor->browser = browser;

  gtk_widget_queue_draw (editor->waveform);

  editor_set_widget_source (editor->autoplay_switch, editor);
  editor_set_widget_source (editor->mix_switch, editor);
  editor_set_widget_source (editor->play_button, editor);
  editor_set_widget_source (editor->stop_button, editor);
  editor_set_widget_source (editor->loop_button, editor);
  editor_set_widget_source (editor->record_button, editor);
  editor_set_widget_source (editor->volume_button, editor);
  editor_set_widget_source (editor->waveform, editor);

  gtk_widget_set_sensitive (editor->play_button, FALSE);
  gtk_widget_set_visible (editor->sample_info_box, FALSE);
}

static void
editor_set_start_frame (struct editor *editor, gint start)
{
  gint max = editor->audio.sample_info.frames - 1;
  start = start < 0 ? 0 : start;
  start = start > max ? max : start;

  gdouble widget_w =
    gtk_widget_get_allocated_width (editor->waveform_scrolled_window);
  GtkAdjustment *adj =
    gtk_scrolled_window_get_hadjustment (GTK_SCROLLED_WINDOW
					 (editor->waveform_scrolled_window));
  gdouble upper = widget_w * editor->zoom - 3;	//Base 0 and 2 border pixels
  gdouble lower = 0;
  gdouble value = upper * start / (double) editor->audio.sample_info.frames;

  debug_print (1, "Setting waveform scrollbar to %f [%f, %f]...\n", value,
	       lower, upper);
  gtk_adjustment_set_lower (adj, 0);
  gtk_adjustment_set_upper (adj, upper);
  gtk_adjustment_set_value (adj, value);
}

static guint
editor_get_start_frame (struct editor *editor)
{
  GtkAdjustment *adj =
    gtk_scrolled_window_get_hadjustment (GTK_SCROLLED_WINDOW
					 (editor->waveform_scrolled_window));
  return editor->audio.sample_info.frames * gtk_adjustment_get_value (adj) /
    (gdouble) gtk_adjustment_get_upper (adj);
}

static void
editor_show_sample_time_properties (struct editor *editor)
{
  gchar label[LABEL_MAX];
  struct sample_info *sample_info = editor->audio.control.data;
  double time = sample_info->frames / (double) sample_info->rate;

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

  snprintf (label, LABEL_MAX, "%d", sample_info->loop_start);
  gtk_label_set_text (GTK_LABEL (editor->sample_loop_start), label);

  snprintf (label, LABEL_MAX, "%d", sample_info->loop_end);
  gtk_label_set_text (GTK_LABEL (editor->sample_loop_end), label);
}

static void
editor_show_sample_properties_on_load (struct editor *editor)
{
  GValue value = G_VALUE_INIT;
  gchar label[LABEL_MAX];
  const gchar *note;
  GtkTreeIter iter;
  struct sample_info *sample_info = editor->audio.control.data;

  gtk_widget_set_visible (editor->sample_info_box, sample_info->frames > 0);

  if (!sample_info->frames)
    {
      return;
    }

  editor_show_sample_time_properties (editor);

  snprintf (label, LABEL_MAX, "%.2f kHz", sample_info->rate / 1000.f);
  gtk_label_set_text (GTK_LABEL (editor->sample_rate), label);

  snprintf (label, LABEL_MAX, "%d", sample_info->channels);
  gtk_label_set_text (GTK_LABEL (editor->sample_channels), label);

  snprintf (label, LABEL_MAX, "%d", sample_info->bit_depth);
  gtk_label_set_text (GTK_LABEL (editor->sample_bit_depth), label);

  gtk_tree_model_get_iter_first (GTK_TREE_MODEL (editor->notes_list_store),
				 &iter);
  for (gint i = 0; i < sample_info->midi_note; i++)
    {
      gtk_tree_model_iter_next (GTK_TREE_MODEL (editor->notes_list_store),
				&iter);
    }
  gtk_tree_model_get_value (GTK_TREE_MODEL (editor->notes_list_store),
			    &iter, 0, &value);
  note = g_value_get_string (&value);
  snprintf (label, LABEL_MAX, "%s (%d)", note, sample_info->midi_note);
  g_value_unset (&value);
  gtk_label_set_text (GTK_LABEL (editor->sample_midi_note), label);

  editor_set_start_frame (editor, 0);
}

void
editor_set_audio_mono_mix (struct editor *editor)
{
  if (editor->audio.sample_info.frames > 0)
    {
      gboolean remote_mono = remote_browser.fs_ops &&
	!(remote_browser.fs_ops->options & FS_OPTION_STEREO);
      gboolean mono_mix = (editor->preferences->mix && remote_mono) ||
	editor->audio.sample_info.channels != 2;

      g_mutex_lock (&editor->audio.control.mutex);
      editor->audio.mono_mix = mono_mix;
      g_mutex_unlock (&editor->audio.control.mutex);
    }
}

static gboolean
editor_loading_completed_no_lock (struct editor *editor,
				  guint32 * actual_frames)
{
  gboolean completed;
  guint32 actual;
  gint bytes_per_frame = BYTES_PER_FRAME (editor->audio.sample_info.channels);
  actual = bytes_per_frame ? editor->audio.sample->len / bytes_per_frame : 0;
  completed = actual == editor->audio.sample_info.frames && actual;
  if (actual_frames)
    {
      *actual_frames = actual;
    }
  return completed;
}

static gboolean
editor_update_ui_on_load (gpointer data)
{
  struct editor *editor = data;

  editor_show_sample_properties_on_load (editor);
  editor_set_audio_mono_mix (editor);
  editor_set_layout_width (editor);

  if (audio_check (&editor->audio))
    {
      gtk_widget_set_sensitive (local_browser.play_menuitem,
				editor->browser == &local_browser);
      gtk_widget_set_sensitive (remote_browser.play_menuitem,
				editor->browser == &remote_browser);
      gtk_widget_set_sensitive (editor->play_button, TRUE);
    }
  if (editor->preferences->autoplay)
    {
      audio_start_playback (&editor->audio);
    }

  return FALSE;
}

static void
editor_init_y_frame_state (struct editor_y_frame_state *state, guint channels)
{
  state->wp = g_malloc (sizeof (gdouble) * channels);
  state->wn = g_malloc (sizeof (gdouble) * channels);
  state->wpc = g_malloc (sizeof (guint) * channels);
  state->wnc = g_malloc (sizeof (guint) * channels);
}

static void
editor_destroy_y_frame_state (struct editor_y_frame_state *state)
{
  g_free (state->wp);
  g_free (state->wn);
  g_free (state->wpc);
  g_free (state->wnc);
}

static gboolean
editor_get_y_frame (GByteArray * sample, guint channels, guint frame,
		    guint len, struct editor_y_frame_state *state)
{
  guint loaded_frames = sample->len / BYTES_PER_FRAME (channels);
  gshort *data = (gshort *) sample->data;
  gshort *s = &data[frame * channels];
  len = len < MAX_FRAMES_PER_PIXEL ? len : MAX_FRAMES_PER_PIXEL;

  for (guint i = 0; i < channels; i++)
    {
      state->wp[i] = 0.0;
      state->wn[i] = 0.0;
      state->wpc[i] = 0;
      state->wnc[i] = 0;
    }

  for (guint i = 0, f = frame; i < len; i++, f++)
    {
      if (f >= loaded_frames)
	{
	  return FALSE;
	}

      for (guint j = 0; j < channels; j++, s++)
	{
	  if (*s > 0)
	    {
	      state->wp[j] += *s;
	      state->wpc[j]++;
	    }
	  else
	    {
	      state->wn[j] += *s;
	      state->wnc[j]++;
	    }
	}
    }

  for (guint i = 0; i < channels; i++)
    {
      state->wp[i] = state->wpc[i] == 0 ? 0.0 : state->wp[i] / state->wpc[i];
      state->wn[i] = state->wnc[i] == 0 ? 0.0 : state->wn[i] / state->wnc[i];
    }

  return TRUE;
}

static gdouble
editor_get_x_ratio (struct editor *editor)
{
  guint layout_width;
  struct audio *audio = &editor->audio;
  gtk_layout_get_size (GTK_LAYOUT (editor->waveform), &layout_width, NULL);
  return audio->sample_info.frames / (gdouble) layout_width;
}

gboolean
editor_draw_waveform (GtkWidget * widget, cairo_t * cr, gpointer data)
{
  GdkRGBA color, bgcolor;
  guint width, height, x_count, layout_width, c_height, c_height_half;
  GtkStyleContext *context;
  gdouble x_ratio, x_frame, x_frame_next, y_scale, value;
  struct editor_y_frame_state y_frame_state;
  struct editor *editor = data;
  struct audio *audio = &editor->audio;
  guint start = editor_get_start_frame (editor);

  debug_print (3, "Drawing waveform from %d with %dx zoom...\n",
	       start, editor->zoom);

  width = gtk_widget_get_allocated_width (widget);
  height = gtk_widget_get_allocated_height (widget);

  g_mutex_lock (&audio->control.mutex);

  gtk_layout_get_size (GTK_LAYOUT (editor->waveform), &layout_width, NULL);
  x_ratio = audio->sample_info.frames / (gdouble) layout_width;

  y_scale = height / (double) SHRT_MIN;
  y_scale /= (gdouble) audio->sample_info.channels * 2;
  c_height = height / (gdouble) audio->sample_info.channels;
  c_height_half = c_height / 2;

  editor_init_y_frame_state (&y_frame_state, audio->sample_info.channels);

  context = gtk_widget_get_style_context (widget);
  gtk_render_background (context, cr, 0, 0, width, height);

  cairo_set_line_width (cr, 1);

  if (audio->sample_info.frames)
    {
      GtkStateFlags state = gtk_style_context_get_state (context);
      gtk_style_context_get_color (context, state, &color);
      gtk_style_context_get_color (context, state, &bgcolor);
      bgcolor.alpha = 0.15;

      if (editor->audio.sel_len)
	{
	  gdouble x_len = editor->audio.sel_len / x_ratio;
	  gdouble x_start =
	    (editor->audio.sel_start - (gdouble) start) / x_ratio;
	  gdk_cairo_set_source_rgba (cr, &bgcolor);
	  cairo_rectangle (cr, x_start, 0, x_len, height);
	  cairo_fill (cr);
	}

      gdk_cairo_set_source_rgba (cr, &color);

      for (gint i = 0; i < width; i++)
	{
	  x_frame = start + i * x_ratio;
	  x_frame_next = x_frame + x_ratio;
	  x_count = x_frame_next - (guint) x_frame;
	  if (!x_count)
	    {
	      continue;
	    }

	  if (!editor_get_y_frame (audio->sample, audio->sample_info.channels,
				   x_frame, x_count, &y_frame_state))
	    {
	      debug_print (3,
			   "Last available frame before the sample end. Stopping...\n");
	      break;
	    }

	  gdouble mid_c = c_height_half;
	  for (gint j = 0; j < audio->sample_info.channels; j++)
	    {
	      value = mid_c + y_frame_state.wp[j] * y_scale + 0.5;
	      cairo_move_to (cr, i, value);
	      value = mid_c + y_frame_state.wn[j] * y_scale + 0.5;
	      cairo_line_to (cr, i, value);
	      cairo_stroke (cr);
	      mid_c += c_height;
	    }
	}

      // gtk_style_context_lookup_color (context, "text-color", &color);
      context = gtk_widget_get_style_context (editor->sample_length);
      gtk_style_context_get_color (context, GTK_STATE_FLAG_NORMAL, &color);
      bgcolor.alpha = 0.15;
      gdk_cairo_set_source_rgba (cr, &color);

      value = ((gint) ((audio->sample_info.loop_start - start) / x_ratio)) +
	.5;
      cairo_move_to (cr, value, 0);
      cairo_line_to (cr, value, height - 1);
      cairo_stroke (cr);
      cairo_move_to (cr, value, 0);
      cairo_line_to (cr, value + EDITOR_LOOP_MARKER_WIDTH,
		     EDITOR_LOOP_MARKER_HALF_HEIGHT);
      cairo_line_to (cr, value, EDITOR_LOOP_MARKER_FULL_HEIGHT);
      cairo_fill (cr);

      value = ((gint) ((audio->sample_info.loop_end - start) / x_ratio)) + .5;
      cairo_move_to (cr, value, 0);
      cairo_line_to (cr, value, height - 1);
      cairo_stroke (cr);
      cairo_move_to (cr, value, 0);
      cairo_line_to (cr, value - EDITOR_LOOP_MARKER_WIDTH,
		     EDITOR_LOOP_MARKER_HALF_HEIGHT);
      cairo_line_to (cr, value, EDITOR_LOOP_MARKER_FULL_HEIGHT);
      cairo_fill (cr);
    }

  g_mutex_unlock (&audio->control.mutex);

  editor_destroy_y_frame_state (&y_frame_state);

  return FALSE;
}

static gboolean
editor_queue_draw (gpointer data)
{
  struct editor *editor = data;
  gtk_widget_queue_draw (editor->waveform);
  return FALSE;
}

static void
editor_load_sample_cb (struct job_control *control, gdouble p, gpointer data)
{
  guint32 actual_frames;
  gboolean completed, ready_to_play;
  struct editor *editor = data;

  set_job_control_progress_no_sync (control, p, NULL);
  g_idle_add (editor_queue_draw, data);
  if (!editor->ready)
    {
      completed = editor_loading_completed_no_lock (editor, &actual_frames);
      ready_to_play = completed || actual_frames >= FRAMES_TO_PLAY;
      if (ready_to_play)
	{
	  g_idle_add (editor_update_ui_on_load, data);
	  editor->ready = TRUE;
	}
    }
}

static gpointer
editor_load_sample_runner (gpointer data)
{
  struct editor *editor = data;
  struct audio *audio = &editor->audio;

  editor->dirty = FALSE;
  editor->ready = FALSE;
  editor->zoom = 1;
  editor->audio.sel_start = 0;

  audio->sample_info.channels = 0;	//Automatic

  g_mutex_lock (&audio->control.mutex);
  audio->control.active = TRUE;
  g_mutex_unlock (&audio->control.mutex);

  sample_load_from_file_with_cb
    (audio->path, audio->sample, &audio->control,
     &audio->sample_info, editor_load_sample_cb, editor);

  editor->audio.sel_len = 0;

  g_mutex_lock (&audio->control.mutex);
  audio->control.active = FALSE;
  g_mutex_unlock (&audio->control.mutex);

  return NULL;
}

void
editor_play_clicked (GtkWidget * object, gpointer data)
{
  struct editor *editor = data;
  audio_stop_recording (&editor->audio);
  audio_start_playback (&editor->audio);
}

static void
editor_update_ui_on_record (gpointer data, gdouble value)
{
  struct editor *editor = data;

  g_idle_add (editor_queue_draw, data);
  if (!editor->ready && editor_loading_completed_no_lock (data, NULL))
    {
      g_idle_add (editor_update_ui_on_load, data);
      editor->ready = TRUE;
    }
}

static void
editor_stop_clicked (GtkWidget * object, gpointer data)
{
  struct editor *editor = data;
  audio_stop_playback (&editor->audio);
  audio_stop_recording (&editor->audio);
}

static void
editor_start_record (GtkWidget * object, gpointer data)
{
  struct editor *editor = data;
  gtk_dialog_response (editor->record_dialog, GTK_RESPONSE_ACCEPT);
}

static void
editor_cancel_record (GtkWidget * object, gpointer data)
{
  struct editor *editor = data;
  gtk_dialog_response (editor->record_dialog, GTK_RESPONSE_CANCEL);
}

static gboolean
editor_reset_for_recording (gpointer data)
{
  guint options;
  struct editor_record_clicked_data *record_data = data;
  struct editor *editor = record_data->editor;
  struct browser *browser = record_data->browser;
  editor_reset (editor, browser ? browser : &local_browser);
  editor->ready = FALSE;
  editor->dirty = TRUE;
  editor->zoom = 1;
  editor->audio.sel_start = 0;
  editor->audio.sel_len = 0;
  options = guirecorder_get_channel_mask (editor->guirecorder.channels_combo);
  audio_start_recording (&editor->audio, options | RECORD_MONITOR_ONLY,
			 guirecorder_monitor_notifier, &editor->guirecorder);
  return FALSE;
}

static void
editor_record_clicked (GtkWidget * object, gpointer data)
{
  gint res;
  guint options;
  struct editor *editor = data;
  static struct editor_record_clicked_data record_data;

  record_data.browser = editor->browser;
  record_data.editor = editor;
  browser_clear_selection (&local_browser);
  browser_clear_selection (&remote_browser);
  //Running editor_reset_for_recording asynchronously is needed as calling
  //browser_clear_selection might raise some signals that will eventually call
  //editor_reset and clear the browser member.
  //If using g_idle_add, a call to editor_reset will happen always later than
  //those. All these calls will happen at the time the dialog is shown.
  g_idle_add (editor_reset_for_recording, &record_data);

  res = gtk_dialog_run (editor->record_dialog);
  gtk_widget_hide (GTK_WIDGET (editor->record_dialog));
  if (res == GTK_RESPONSE_CANCEL)
    {
      audio_stop_recording (&editor->audio);
      editor_reset (editor, NULL);
      return;
    }

  options = guirecorder_get_channel_mask (editor->guirecorder.channels_combo);
  audio_start_recording (&editor->audio, options,
			 editor_update_ui_on_record, data);
}

static void
editor_loop_clicked (GtkWidget * object, gpointer data)
{
  struct editor *editor = data;
  editor->audio.loop =
    gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (object));
}

static gboolean
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

static gboolean
editor_mix_clicked (GtkWidget * object, gboolean state, gpointer data)
{
  struct editor *editor = data;
  editor->preferences->mix = state;
  editor_set_audio_mono_mix (editor);
  return FALSE;
}

static void
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

static void
editor_set_volume_callback (gpointer editor, gdouble volume)
{
  struct editor_set_volume_data *data =
    g_malloc (sizeof (struct editor_set_volume_data));
  data->editor = editor;
  data->volume = volume;
  g_idle_add (editor_set_volume_callback_bg, data);
}

static void
editor_get_frame_at_position (struct editor *editor, gdouble x,
			      guint * cursor_frame, gdouble * rel_pos)
{
  guint lw;
  guint start = editor_get_start_frame (editor);
  gtk_layout_get_size (GTK_LAYOUT (editor->waveform), &lw, NULL);
  x = x > lw ? lw : x < 0.0 ? 0.0 : x;
  *cursor_frame = (editor->audio.sample_info.frames - 1) * (x / (gdouble) lw);
  if (rel_pos)
    {
      *rel_pos = (*cursor_frame - start) /
	(editor->audio.sample_info.frames / (double) editor->zoom);
    }
}

static gboolean
editor_zoom (struct editor *editor, GdkEventScroll * event, gdouble dy)
{
  gdouble rel_pos;
  guint start, cursor_frame;
  gboolean ctrl = ((event->state) & GDK_CONTROL_MASK) != 0;

  if (!ctrl)
    {
      return FALSE;
    }

  if (dy == 0.0)
    {
      return FALSE;
    }

  g_mutex_lock (&editor->audio.control.mutex);

  editor_get_frame_at_position (editor, event->x, &cursor_frame, &rel_pos);
  debug_print (1, "Zooming at frame %d...\n", cursor_frame);

  if (dy == -1.0)
    {
      guint w;
      gtk_layout_get_size (GTK_LAYOUT (editor->waveform), &w, NULL);
      if (w >= editor->audio.sample_info.frames)
	{
	  goto end;
	}
      editor->zoom = editor->zoom << 1;
    }
  else
    {
      if (editor->zoom == 1)
	{
	  goto end;
	}
      editor->zoom = editor->zoom >> 1;
    }

  debug_print (1, "Setting zoon to %dx...\n", editor->zoom);

  start = cursor_frame - rel_pos * editor->audio.sample_info.frames /
    (gdouble) editor->zoom;
  editor_set_layout_width (editor);
  editor_set_start_frame (editor, start);

end:
  g_mutex_unlock (&editor->audio.control.mutex);

  return TRUE;
}

gboolean
editor_waveform_scroll (GtkWidget * widget, GdkEventScroll * event,
			gpointer data)
{
  if (event->direction == GDK_SCROLL_SMOOTH)
    {
      gdouble dx, dy;
      gdk_event_get_scroll_deltas ((GdkEvent *) event, &dx, &dy);
      if (editor_zoom (data, event, dy))
	{
	  g_idle_add (editor_queue_draw, data);
	}
    }
  return FALSE;
}

static void
editor_on_size_allocate (GtkWidget * self, GtkAllocation * allocation,
			 struct editor *editor)
{
  if (editor->audio.sample_info.frames == 0)
    {
      return;
    }

  guint start = editor_get_start_frame (editor);
  editor_set_layout_width (editor);
  editor_set_start_frame (editor, start);
}

static gboolean
editor_loading_completed (struct editor *editor)
{
  gboolean res;

  g_mutex_lock (&editor->audio.control.mutex);
  res = editor_loading_completed_no_lock (editor, NULL);
  g_mutex_unlock (&editor->audio.control.mutex);

  return res;
}

static gboolean
editor_cursor_frame_over_frame (struct editor *editor,
				guint cursor_frame, guint frame)
{
  gdouble x_ratio = editor_get_x_ratio (editor);
  return cursor_frame >= frame - x_ratio && cursor_frame <= frame + x_ratio;
}

static void
editor_set_cursor (struct editor *editor, const gchar * cursor_name)
{
  GdkDisplay *display = gdk_display_get_default ();
  GdkCursor *cursor = gdk_cursor_new_from_name (display,
						cursor_name);
  gdk_window_set_cursor (gtk_widget_get_window (editor->waveform), cursor);
  g_object_unref (cursor);
}

static gboolean
editor_button_press (GtkWidget * widget, GdkEventButton * event,
		     gpointer data)
{
  guint cursor_frame;
  struct editor *editor = data;

  if (!editor_loading_completed (editor))
    {
      return FALSE;
    }

  if (event->button == GDK_BUTTON_PRIMARY)
    {
      editor_get_frame_at_position (editor, event->x, &cursor_frame, NULL);
      debug_print (2, "Pressing at frame %d...\n", cursor_frame);
      if (editor_cursor_frame_over_frame (editor, cursor_frame,
					  editor->audio.
					  sample_info.loop_start))
	{
	  debug_print (2, "Clicking on loop start...\n");
	  editor->operation = EDITOR_OP_MOVE_LOOP_START;
	  editor_set_cursor (editor, "col-resize");
	}
      else if (editor_cursor_frame_over_frame (editor, cursor_frame,
					       editor->audio.
					       sample_info.loop_end))
	{
	  debug_print (2, "Clicking on loop end...\n");
	  editor->operation = EDITOR_OP_MOVE_LOOP_END;
	  editor_set_cursor (editor, "col-resize");
	}
      else
	{
	  audio_stop_playback (&editor->audio);
	  editor->operation = EDITOR_OP_SELECT;
	  editor->audio.sel_len = 0;
	  gtk_widget_grab_focus (editor->waveform_scrolled_window);
	  editor->audio.sel_start = cursor_frame;
	  g_idle_add (editor_queue_draw, editor);
	}
    }
  else if (event->button == GDK_BUTTON_SECONDARY)
    {
      gtk_widget_set_sensitive (editor->delete_menuitem,
				editor->audio.sel_len > 0);
      gtk_widget_set_sensitive (editor->save_menuitem, editor->dirty);
      gtk_menu_popup_at_pointer (editor->menu, (GdkEvent *) event);
    }

  return FALSE;
}

static gboolean
editor_button_release (GtkWidget * widget, GdkEventButton * event,
		       gpointer data)
{
  struct editor *editor = data;

  if (!editor->operation)
    {
      return FALSE;
    }

  if (editor->operation == EDITOR_OP_SELECT)
    {
      gtk_widget_grab_focus (editor->waveform_scrolled_window);

      if (editor->audio.sel_len < 0)
	{
	  gint64 aux = ((gint64) editor->audio.sel_start) +
	    editor->audio.sel_len;
	  editor->audio.sel_start = (guint32) aux;
	  editor->audio.sel_len = -editor->audio.sel_len;
	}

      debug_print (2, "Audio selected from %d with len %ld...\n",
		   editor->audio.sel_start, editor->audio.sel_len);

      if (editor->audio.sel_len)
	{
	  gtk_widget_set_sensitive (editor->delete_menuitem, TRUE);
	}


      if (editor->preferences->autoplay && editor->audio.sel_len)
	{
	  audio_start_playback (&editor->audio);
	}
    }

  g_idle_add (editor_queue_draw, data);
  editor->operation = EDITOR_OP_NONE;

  return FALSE;
}

static gboolean
editor_motion_notify (GtkWidget * widget, GdkEventMotion * event,
		      gpointer data)
{
  guint cursor_frame;
  struct editor *editor = data;
  struct audio *audio = &editor->audio;
  struct sample_info *sample_info_src = audio->control.data;
  gint16 *samples = (gint16 *) editor->audio.sample->data;

  editor_get_frame_at_position (editor, event->x, &cursor_frame, NULL);

  if (editor->operation == EDITOR_OP_SELECT)
    {
      editor->audio.sel_len = ((gint64) cursor_frame) -
	((gint64) editor->audio.sel_start);
      debug_print (2, "Setting selection size to %" PRId64 "...\n",
		   editor->audio.sel_len);
    }
  else if (editor->operation == EDITOR_OP_MOVE_LOOP_START)
    {
      gdouble r = sample_info_src->frames /
	(gdouble) editor->audio.sample_info.frames;
      editor->audio.sample_info.loop_start = cursor_frame;
      sample_info_src->loop_start = cursor_frame * r;
      debug_print (2,
		   "Setting loop start to %d frame and %d value (%d file frame)...\n",
		   editor->audio.sample_info.loop_start,
		   samples[editor->audio.sample_info.loop_start *
			   editor->audio.sample_info.channels],
		   sample_info_src->loop_start);
      editor->dirty = TRUE;
      editor_show_sample_time_properties (editor);
    }
  else if (editor->operation == EDITOR_OP_MOVE_LOOP_END)
    {
      gdouble r = sample_info_src->frames /
	(gdouble) editor->audio.sample_info.frames;
      editor->audio.sample_info.loop_end = cursor_frame;
      sample_info_src->loop_end = cursor_frame * r;
      debug_print (2,
		   "Setting loop end to %d frame and %d value (%d file frame)...\n",
		   editor->audio.sample_info.loop_end,
		   samples[editor->audio.sample_info.loop_end *
			   editor->audio.sample_info.channels],
		   sample_info_src->loop_end);
      editor->dirty = TRUE;
      editor_show_sample_time_properties (editor);
    }
  else
    {
      if (editor_cursor_frame_over_frame (editor, cursor_frame,
					  editor->audio.
					  sample_info.loop_start))
	{
	  editor_set_cursor (editor, "col-resize");
	}
      else if (editor_cursor_frame_over_frame (editor, cursor_frame,
					       editor->audio.
					       sample_info.loop_end))
	{
	  editor_set_cursor (editor, "col-resize");
	}
      else
	{
	  editor_set_cursor (editor, "default");
	}
    }

  g_idle_add (editor_queue_draw, data);

  return FALSE;
}

static void
editor_delete_clicked (GtkWidget * object, gpointer data)
{
  enum audio_status status;
  struct editor *editor = data;

  if (!editor_loading_completed (editor))
    {
      return;
    }

  if (!editor->audio.sel_len)
    {
      return;
    }

  //As the playback pointer could be in the selected range, it's safer to stop.
  //Later, playback will be restarted.
  g_mutex_lock (&editor->audio.control.mutex);
  status = editor->audio.status;
  g_mutex_unlock (&editor->audio.control.mutex);
  if (status == AUDIO_STATUS_PLAYING)
    {
      audio_stop_playback (&editor->audio);
    }

  audio_delete_range (&editor->audio, editor->audio.sel_start,
		      editor->audio.sel_len);
  editor->dirty = TRUE;
  editor_show_sample_time_properties (editor);
  g_idle_add (editor_queue_draw, data);

  if (status == AUDIO_STATUS_PLAYING)
    {
      audio_start_playback (&editor->audio);
    }
}

static void
editor_save_clicked (GtkWidget * object, gpointer data)
{
  struct editor *editor = data;

  if (!editor_loading_completed (editor))
    {
      return;
    }

  if (!*editor->audio.path)
    {
      //This is a recording.
      gchar *name = elektroid_ask_name (_("Save Sample"), "sample.wav",
					editor->browser);
      if (!name)
	{
	  return;
	}
      strcat (editor->audio.path, name);
      g_free (name);
    }

  debug_print (2, "Saving sample to %s...\n", editor->audio.path);
  editor->browser->fs_ops->upload (editor->browser->backend,
				   editor->audio.path, editor->audio.sample,
				   &editor->audio.control);
}

static gboolean
editor_key_press (GtkWidget * widget, GdkEventKey * event, gpointer data)
{
  struct editor *editor = data;

  if (event->type != GDK_KEY_PRESS)
    {
      return FALSE;
    }

  if (event->keyval == GDK_KEY_space)
    {
      audio_start_playback (&editor->audio);
    }
  else if (event->keyval == GDK_KEY_Delete)
    {
      editor_delete_clicked (NULL, editor);
    }
  else if (event->state & GDK_CONTROL_MASK && event->keyval == GDK_KEY_s)
    {
      editor_save_clicked (NULL, editor);
    }

  return TRUE;
}

void
editor_init (struct editor *editor, GtkBuilder * builder)
{
  editor->box = GTK_WIDGET (gtk_builder_get_object (builder, "editor_box"));
  editor->waveform_scrolled_window =
    GTK_WIDGET (gtk_builder_get_object (builder, "waveform_scrolled_window"));
  editor->waveform =
    GTK_WIDGET (gtk_builder_get_object (builder, "waveform"));
  editor->play_button =
    GTK_WIDGET (gtk_builder_get_object (builder, "play_button"));
  editor->stop_button =
    GTK_WIDGET (gtk_builder_get_object (builder, "stop_button"));
  editor->loop_button =
    GTK_WIDGET (gtk_builder_get_object (builder, "loop_button"));
  editor->record_button =
    GTK_WIDGET (gtk_builder_get_object (builder, "record_button"));
  editor->autoplay_switch =
    GTK_WIDGET (gtk_builder_get_object (builder, "autoplay_switch"));
  editor->mix_switch =
    GTK_WIDGET (gtk_builder_get_object (builder, "mix_switch"));
  editor->volume_button =
    GTK_WIDGET (gtk_builder_get_object (builder, "volume_button"));

  editor->sample_info_box =
    GTK_WIDGET (gtk_builder_get_object (builder, "sample_info_box"));
  editor->sample_length =
    GTK_WIDGET (gtk_builder_get_object (builder, "sample_length"));
  editor->sample_duration =
    GTK_WIDGET (gtk_builder_get_object (builder, "sample_duration"));
  editor->sample_loop_start =
    GTK_WIDGET (gtk_builder_get_object (builder, "sample_loop_start"));
  editor->sample_loop_end =
    GTK_WIDGET (gtk_builder_get_object (builder, "sample_loop_end"));
  editor->sample_channels =
    GTK_WIDGET (gtk_builder_get_object (builder, "sample_channels"));
  editor->sample_rate =
    GTK_WIDGET (gtk_builder_get_object (builder, "sample_rate"));
  editor->sample_bit_depth =
    GTK_WIDGET (gtk_builder_get_object (builder, "sample_bit_depth"));
  editor->sample_midi_note =
    GTK_WIDGET (gtk_builder_get_object (builder, "sample_midi_note"));

  editor->notes_list_store =
    GTK_LIST_STORE (gtk_builder_get_object (builder, "notes_list_store"));

  editor->menu = GTK_MENU (gtk_builder_get_object (builder, "editor_menu"));
  editor->play_menuitem =
    GTK_WIDGET (gtk_builder_get_object (builder, "editor_play_menuitem"));
  editor->delete_menuitem =
    GTK_WIDGET (gtk_builder_get_object (builder, "editor_delete_menuitem"));
  editor->save_menuitem =
    GTK_WIDGET (gtk_builder_get_object (builder, "editor_save_menuitem"));

  editor->record_dialog =
    GTK_DIALOG (gtk_builder_get_object (builder, "record_dialog"));
  editor->guirecorder.channels_combo =
    GTK_WIDGET (gtk_builder_get_object
		(builder, "record_dialog_channels_combo"));
  editor->guirecorder.monitor_levelbar =
    GTK_LEVEL_BAR (gtk_builder_get_object
		   (builder, "record_dialog_monitor_levelbar"));
  editor->record_dialog_cancel_button =
    GTK_WIDGET (gtk_builder_get_object
		(builder, "record_dialog_cancel_button"));
  editor->record_dialog_record_button =
    GTK_WIDGET (gtk_builder_get_object
		(builder, "record_dialog_record_button"));

  g_signal_connect (editor->waveform, "draw",
		    G_CALLBACK (editor_draw_waveform), editor);
  gtk_widget_add_events (editor->waveform, GDK_SCROLL_MASK);
  g_signal_connect (editor->waveform, "scroll-event",
		    G_CALLBACK (editor_waveform_scroll), editor);
  g_signal_connect (editor->play_button, "clicked",
		    G_CALLBACK (editor_play_clicked), editor);
  g_signal_connect (editor->stop_button, "clicked",
		    G_CALLBACK (editor_stop_clicked), editor);
  g_signal_connect (editor->loop_button, "clicked",
		    G_CALLBACK (editor_loop_clicked), editor);
  g_signal_connect (editor->record_button, "clicked",
		    G_CALLBACK (editor_record_clicked), editor);
  g_signal_connect (editor->autoplay_switch, "state-set",
		    G_CALLBACK (editor_autoplay_clicked), editor);
  g_signal_connect (editor->mix_switch, "state-set",
		    G_CALLBACK (editor_mix_clicked), editor);
  editor->volume_changed_handler = g_signal_connect (editor->volume_button,
						     "value_changed",
						     G_CALLBACK
						     (editor_set_volume),
						     editor);

  g_signal_connect (editor->waveform_scrolled_window, "size-allocate",
		    G_CALLBACK (editor_on_size_allocate), editor);
  gtk_widget_add_events (editor->waveform, GDK_BUTTON_PRESS_MASK);
  g_signal_connect (editor->waveform, "button-press-event",
		    G_CALLBACK (editor_button_press), editor);
  gtk_widget_add_events (editor->waveform, GDK_BUTTON_RELEASE_MASK);
  g_signal_connect (editor->waveform, "button-release-event",
		    G_CALLBACK (editor_button_release), editor);
  gtk_widget_add_events (editor->waveform, GDK_POINTER_MOTION_MASK);
  g_signal_connect (editor->waveform, "motion-notify-event",
		    G_CALLBACK (editor_motion_notify), editor);
  g_signal_connect (editor->waveform_scrolled_window, "key-press-event",
		    G_CALLBACK (editor_key_press), editor);

  g_signal_connect (editor->play_menuitem, "activate",
		    G_CALLBACK (editor_play_clicked), editor);
  g_signal_connect (editor->delete_menuitem, "activate",
		    G_CALLBACK (editor_delete_clicked), editor);
  g_signal_connect (editor->save_menuitem, "activate",
		    G_CALLBACK (editor_save_clicked), editor);

  editor_loop_clicked (editor->loop_button, editor);
  gtk_switch_set_active (GTK_SWITCH (editor->autoplay_switch),
			 editor->preferences->autoplay);
  gtk_switch_set_active (GTK_SWITCH (editor->mix_switch),
			 editor->preferences->mix);

  g_signal_connect (editor->guirecorder.channels_combo, "changed",
		    G_CALLBACK (guirecorder_channels_changed),
		    &editor->audio);
  g_signal_connect (editor->record_dialog_record_button, "clicked",
		    G_CALLBACK (editor_start_record), editor);
  g_signal_connect (editor->record_dialog_cancel_button, "clicked",
		    G_CALLBACK (editor_cancel_record), editor);

  audio_init (&editor->audio, editor_set_volume_callback,
	      elektroid_update_audio_status, editor);

  editor_reset (editor, NULL);
}

void
editor_destroy (struct editor *editor)
{
  audio_destroy (&editor->audio);
}
