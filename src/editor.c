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
#include <glib/gstdio.h>
#include <math.h>
#include "editor.h"
#include "sample.h"
#include "connectors/system.h"
#include "name.h"

#define EDITOR_LOOP_MARKER_WIDTH 7
#define EDITOR_LOOP_MARKER_HALF_HEIGHT 4
#define EDITOR_LOOP_MARKER_FULL_HEIGHT (EDITOR_LOOP_MARKER_HALF_HEIGHT * 2)

#if defined(__linux__)
#define FRAMES_TO_PLAY (16 * 1024)
#else
#define FRAMES_TO_PLAY (64 * 1024)
#endif

//Some OSs do not allow ':' in the name. Same format used by the GNOME screenshot tool.
#define DATE_TIME_FILENAME_FORMAT "%Y-%m-%d %H-%M-%S"

#define MAX_FRAMES_PER_PIXEL 300

extern struct browser local_browser;
extern struct browser remote_browser;

static double press_event_x;

void elektroid_update_audio_status (gboolean);

gint elektroid_run_dialog_and_destroy (GtkWidget *);

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
editor_set_widget_source (struct editor *editor, GtkWidget *widget)
{
  const char *class;
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

static void
editor_reset_browser (struct editor *editor, struct browser *browser)
{
  editor->browser = browser;

  editor_set_layout_width_to_val (editor, 1);

  gtk_widget_queue_draw (editor->waveform);

  editor_set_widget_source (editor, editor->autoplay_switch);
  editor_set_widget_source (editor, editor->mix_switch);
  editor_set_widget_source (editor, editor->play_button);
  editor_set_widget_source (editor, editor->stop_button);
  editor_set_widget_source (editor, editor->loop_button);
  editor_set_widget_source (editor, editor->record_button);
  editor_set_widget_source (editor, editor->volume_button);
  editor_set_widget_source (editor, editor->show_grid_switch);
  editor_set_widget_source (editor, editor->waveform);

  gtk_widget_set_sensitive (editor->play_button, FALSE);
  gtk_widget_set_sensitive (editor->stop_button, FALSE);
  gtk_widget_set_sensitive (editor->loop_button, FALSE);
}

void
editor_reset (struct editor *editor, struct browser *browser)
{
  editor_stop_load_thread (editor);
  audio_stop_playback (&editor->audio);
  audio_stop_recording (&editor->audio);
  audio_reset_sample (&editor->audio);
  editor_reset_browser (editor, browser);
}

static void
editor_set_start_frame (struct editor *editor, gint start)
{
  struct sample_info *sample_info = editor->audio.sample.info;
  gint max = sample_info->frames - 1;

  start = start < 0 ? 0 : start;
  start = start > max ? max : start;

  gdouble widget_w =
    gtk_widget_get_allocated_width (editor->waveform_scrolled_window);
  GtkAdjustment *adj =
    gtk_scrolled_window_get_hadjustment (GTK_SCROLLED_WINDOW
					 (editor->waveform_scrolled_window));
  gdouble upper = widget_w * editor->zoom - 3;	//Base 0 and 2 border pixels
  gdouble lower = 0;
  gdouble value = upper * start / (double) sample_info->frames;

  debug_print (1, "Setting waveform scrollbar to %f [%f, %f]...", value,
	       lower, upper);
  gtk_adjustment_set_lower (adj, 0);
  gtk_adjustment_set_upper (adj, upper);
  gtk_adjustment_set_value (adj, value);
}

static guint
editor_get_start_frame (struct editor *editor)
{
  struct sample_info *sample_info = editor->audio.sample.info;
  GtkAdjustment *adj =
    gtk_scrolled_window_get_hadjustment (GTK_SCROLLED_WINDOW
					 (editor->waveform_scrolled_window));
  return sample_info->frames * gtk_adjustment_get_value (adj) /
    (gdouble) gtk_adjustment_get_upper (adj);
}

void
editor_set_audio_mono_mix (struct editor *editor)
{
  struct sample_info *sample_info = editor->audio.sample.info;
  if (sample_info && sample_info->frames > 0)
    {
      gboolean remote_mono = remote_browser.fs_ops &&
	!(remote_browser.fs_ops->options & FS_OPTION_STEREO);
      gboolean mono_mix = (preferences_get_boolean (PREF_KEY_MIX) &&
			   remote_mono) || sample_info->channels != 2;

      g_mutex_lock (&editor->audio.control.mutex);
      editor->audio.mono_mix = mono_mix;
      g_mutex_unlock (&editor->audio.control.mutex);
    }
}

static gboolean
editor_loading_completed_no_lock (struct editor *editor,
				  guint32 *actual_frames)
{
  gboolean completed;
  guint32 actual;
  gint bytes_per_frame;
  struct sample_info *sample_info = editor->audio.sample.info;

  if (!sample_info)
    {
      if (actual_frames)
	{
	  *actual_frames = 0;
	}
      return FALSE;
    }

  bytes_per_frame = SAMPLE_INFO_FRAME_SIZE (sample_info);
  actual = editor->audio.sample.content->len / bytes_per_frame;
  completed = actual == sample_info->frames && actual;
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

  editor_set_audio_mono_mix (editor);
  editor_set_layout_width (editor);

  if (audio_check (&editor->audio))
    {
      gtk_widget_set_sensitive (editor->play_button, TRUE);
      gtk_widget_set_sensitive (editor->stop_button, TRUE);
      gtk_widget_set_sensitive (editor->loop_button, TRUE);
      if (preferences_get_boolean (PREF_KEY_AUTOPLAY))
	{
	  audio_start_playback (&editor->audio);
	}
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
editor_get_y_frame (GByteArray *sample, guint channels, guint frame,
		    guint len, struct editor_y_frame_state *state)
{
  guint loaded_frames = sample->len / FRAME_SIZE (channels, SF_FORMAT_PCM_16);
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
  struct sample_info *sample_info = editor->audio.sample.info;
  gtk_layout_get_size (GTK_LAYOUT (editor->waveform), &layout_width, NULL);
  return sample_info->frames / (gdouble) layout_width;
}

gboolean
editor_draw_waveform (GtkWidget *widget, cairo_t *cr, gpointer data)
{
  GdkRGBA color, bgcolor;
  guint width, height, x_count, c_height, c_height_half, start;
  guint32 loop_start, loop_end;
  GtkStyleContext *context;
  gdouble x_ratio, x_frame, x_frame_next, y_scale, value;
  struct editor_y_frame_state y_frame_state;
  struct editor *editor = data;
  struct sample_info *sample_info;
  struct audio *audio = &editor->audio;

  width = gtk_widget_get_allocated_width (widget);
  height = gtk_widget_get_allocated_height (widget);

  context = gtk_widget_get_style_context (widget);
  gtk_render_background (context, cr, 0, 0, width, height);

  g_mutex_lock (&audio->control.mutex);

  sample_info = editor->audio.sample.info;
  if (!sample_info)
    {
      goto end;
    }

  start = editor_get_start_frame (editor);

  debug_print (3, "Drawing waveform from %d with %f.2x zoom...",
	       start, editor->zoom);

  loop_start = sample_info->loop_start;
  loop_end = sample_info->loop_end;
  x_ratio = editor_get_x_ratio (editor);

  y_scale = height / (double) SHRT_MIN;
  y_scale /= (gdouble) sample_info->channels * 2;
  c_height = height / (gdouble) sample_info->channels;
  c_height_half = c_height / 2;

  editor_init_y_frame_state (&y_frame_state, sample_info->channels);

  cairo_set_line_width (cr, x_ratio < 1.0 ? 1.0 / x_ratio : 1);

  if (sample_info->frames)
    {
      GtkStateFlags state = gtk_style_context_get_state (context);
      gtk_style_context_get_color (context, state, &color);
      gtk_style_context_get_color (context, state, &bgcolor);
      bgcolor.alpha = 0.25;

      guint32 sel_len = AUDIO_SEL_LEN (&editor->audio);
      if (sel_len)
	{
	  gdouble x_len = sel_len / x_ratio;
	  x_len = x_len < 1 ? 1 : x_len;
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

	  if (!editor_get_y_frame (audio->sample.content,
				   sample_info->channels, x_frame, x_count,
				   &y_frame_state))
	    {
	      debug_print (3,
			   "Last available frame before the sample end. Stopping...");
	      break;
	    }

	  gdouble mid_c = c_height_half;
	  for (gint j = 0; j < sample_info->channels; j++)
	    {
	      value = mid_c + y_frame_state.wp[j] * y_scale;
	      cairo_move_to (cr, i + 0.5, value);
	      value = mid_c + y_frame_state.wn[j] * y_scale;
	      cairo_line_to (cr, i + 0.5, value);
	      cairo_stroke (cr);
	      mid_c += c_height;
	    }
	}
    }

  cairo_set_line_width (cr, 1);

  if (sample_info->frames)
    {
      context = gtk_widget_get_style_context (editor->popover_play_button);	//Any text widget is valid
      gtk_style_context_get_color (context, GTK_STATE_FLAG_NORMAL, &color);
      gdk_cairo_set_source_rgba (cr, &color);

      value = ((gint) ((loop_start - start) / x_ratio)) + .5;
      cairo_move_to (cr, value, 0);
      cairo_line_to (cr, value, height - 1);
      cairo_stroke (cr);
      cairo_move_to (cr, value, 0);
      cairo_line_to (cr, value + EDITOR_LOOP_MARKER_WIDTH,
		     EDITOR_LOOP_MARKER_HALF_HEIGHT);
      cairo_line_to (cr, value, EDITOR_LOOP_MARKER_FULL_HEIGHT);
      cairo_fill (cr);

      value = ((gint) ((loop_end - start) / x_ratio)) + .5;
      cairo_move_to (cr, value, 0);
      cairo_line_to (cr, value, height - 1);
      cairo_stroke (cr);
      cairo_move_to (cr, value, 0);
      cairo_line_to (cr, value - EDITOR_LOOP_MARKER_WIDTH,
		     EDITOR_LOOP_MARKER_HALF_HEIGHT);
      cairo_line_to (cr, value, EDITOR_LOOP_MARKER_FULL_HEIGHT);
      cairo_fill (cr);

      if (preferences_get_boolean (PREF_KEY_SHOW_GRID))
	{
	  gint grid_length = preferences_get_int (PREF_KEY_GRID_LENGTH);
	  color.alpha = 0.25;
	  gdk_cairo_set_source_rgba (cr, &color);

	  gdouble grid_inc = sample_info->frames / (gdouble) grid_length;
	  for (gint i = 1; i < grid_length; i++)
	    {
	      value = ((gint) ((i * grid_inc) - start) / x_ratio) + .5;
	      cairo_move_to (cr, value, 0);
	      cairo_line_to (cr, value, height - 1);
	      cairo_stroke (cr);
	    }
	}
    }

  editor_destroy_y_frame_state (&y_frame_state);

end:
  g_mutex_unlock (&audio->control.mutex);
  return FALSE;
}

static gboolean
editor_queue_draw (gpointer data)
{
  struct editor *editor = data;
  gtk_widget_queue_draw (editor->waveform);
  return FALSE;
}

static gboolean
editor_join_load_thread (gpointer data)
{
  struct editor *editor = data;
  if (editor->thread)
    {
      g_thread_join (editor->thread);
      editor->thread = NULL;
    }
  return FALSE;
}

static void
editor_load_sample_cb (struct job_control *control, gdouble p, gpointer data)
{
  guint32 actual_frames;
  gboolean completed, ready_to_play;
  struct editor *editor = data;

  job_control_set_sample_progress_no_sync (control, p, NULL);
  g_idle_add (editor_queue_draw, data);
  completed = editor_loading_completed_no_lock (editor, &actual_frames);
  if (!editor->ready)
    {
      ready_to_play = (preferences_get_boolean (PREF_KEY_PLAY_WHILE_LOADING)
		       && actual_frames >= FRAMES_TO_PLAY) || completed;
      if (ready_to_play)
	{
	  g_idle_add (editor_update_ui_on_load, data);
	  editor->ready = TRUE;
	}
    }
  //If the call to sample_load_from_file_full fails, we reset the browser.
  if (!completed && !actual_frames)
    {
      editor_reset_browser (editor, NULL);
    }
}

static gpointer
editor_load_sample_runner (gpointer data)
{
  struct editor *editor = data;
  struct audio *audio = &editor->audio;
  struct sample_info sample_info_req;

  editor->dirty = FALSE;
  editor->ready = FALSE;
  editor->zoom = 1;
  editor->audio.sel_start = -1;
  editor->audio.sel_end = -1;

  sample_info_req.channels = 0;	//Automatic
  sample_info_req.format = SF_FORMAT_PCM_16;
  sample_info_req.rate = audio->rate;

  audio->control.active = TRUE;
  sample_load_from_file_full (audio->path, &audio->sample,
			      &audio->control, &sample_info_req,
			      &audio->sample_info_src,
			      editor_load_sample_cb, editor);
  return NULL;
}

void
editor_play_clicked (GtkWidget *object, gpointer data)
{
  struct editor *editor = data;

  if (audio_check (&editor->audio))
    {
      audio_stop_recording (&editor->audio);
      audio_start_playback (&editor->audio);
    }
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
editor_stop_clicked (GtkWidget *object, gpointer data)
{
  struct editor *editor = data;
  audio_stop_playback (&editor->audio);
  audio_stop_recording (&editor->audio);
}

static void
editor_reset_for_recording (struct editor *editor)
{
  guint options;

  editor_reset (editor, &local_browser);

  guirecorder_set_channels_masks (&editor->guirecorder,
				  editor->browser->fs_ops->options);

  editor->ready = FALSE;
  editor->dirty = TRUE;
  editor->zoom = 1;
  editor->audio.sel_start = -1;
  editor->audio.sel_end = -1;
  options = guirecorder_get_channel_mask (&editor->guirecorder);
  audio_start_recording (&editor->audio, options | RECORD_MONITOR_ONLY,
			 guirecorder_monitor_notifier, &editor->guirecorder);
}

static void
editor_record_window_cancel (GtkWidget *object, gpointer data)
{
  struct editor *editor = data;

  gtk_widget_hide (GTK_WIDGET (editor->record_window));

  audio_stop_recording (&editor->audio);
  editor_reset (editor, NULL);
}

static gboolean
editor_record_window_delete (GtkWidget *widget, GdkEvent *event,
			     gpointer data)
{
  struct editor *editor = data;
  editor_record_window_cancel (editor->record_window_cancel_button, data);
  return TRUE;
}

static void
editor_record_window_start (GtkWidget *object, gpointer data)
{
  guint options;
  struct editor *editor = data;

  gtk_widget_hide (GTK_WIDGET (editor->record_window));

  gtk_widget_set_sensitive (editor->stop_button, TRUE);
  options = guirecorder_get_channel_mask (&editor->guirecorder);
  audio_start_recording (&editor->audio, options,
			 editor_update_ui_on_record, data);
}

static void
editor_record_clicked (GtkWidget *object, gpointer data)
{
  struct editor *editor = data;

  browser_clear_selection (&local_browser);
  browser_clear_selection (&remote_browser);

  editor_reset_for_recording (editor);

  gtk_widget_show (GTK_WIDGET (editor->record_window));
}

static void
editor_loop_clicked (GtkWidget *object, gpointer data)
{
  struct editor *editor = data;
  editor->audio.loop =
    gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (object));
}

static gboolean
editor_autoplay_clicked (GtkWidget *object, gboolean state, gpointer data)
{
  preferences_set_boolean (PREF_KEY_AUTOPLAY, state);
  return FALSE;
}

void
editor_start_load_thread (struct editor *editor, gchar *sample_path)
{
  debug_print (1, "Creating load thread...");
  editor->audio.path = sample_path;
  editor->thread = g_thread_new ("load_sample", editor_load_sample_runner,
				 editor);
}

void
editor_stop_load_thread (struct editor *editor)
{
  struct audio *audio = &editor->audio;

  debug_print (1, "Stopping load thread...");
  g_mutex_lock (&audio->control.mutex);
  audio->control.active = FALSE;
  g_mutex_unlock (&audio->control.mutex);
  editor_join_load_thread (editor);
}

static gboolean
editor_mix_clicked (GtkWidget *object, gboolean state, gpointer data)
{
  struct editor *editor = data;
  preferences_set_boolean (PREF_KEY_MIX, state);
  editor_set_audio_mono_mix (editor);
  return FALSE;
}

static void
editor_set_volume (GtkScaleButton *button, gdouble value, gpointer data)
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
  debug_print (1, "Setting volume to %f...", volume);
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

static gboolean
editor_show_grid_clicked (GtkWidget *object, gboolean state, gpointer data)
{
  struct editor *editor = data;
  preferences_set_boolean (PREF_KEY_SHOW_GRID, state);
  g_idle_add (editor_queue_draw, editor);
  return FALSE;
}

static void
editor_grid_length_changed (GtkSpinButton *object, gpointer data)
{
  struct editor *editor = data;
  preferences_set_boolean (PREF_KEY_GRID_LENGTH,
			   gtk_spin_button_get_value (object));
  g_idle_add (editor_queue_draw, editor);
}

static void
editor_get_frame_at_position (struct editor *editor, gdouble x,
			      guint *cursor_frame, gdouble *rel_pos)
{
  guint lw;
  guint start = editor_get_start_frame (editor);
  struct sample_info *sample_info = editor->audio.sample.info;

  gtk_layout_get_size (GTK_LAYOUT (editor->waveform), &lw, NULL);
  x = x > lw ? lw : x < 0.0 ? 0.0 : x;
  *cursor_frame = (sample_info->frames - 1) * (x / (gdouble) lw);
  if (rel_pos)
    {
      *rel_pos = (*cursor_frame - start) /
	(sample_info->frames / (double) editor->zoom);
    }
}

static gdouble
editor_get_max_zoom (struct editor *editor)
{
  struct sample_info *sample_info = editor->audio.sample.info;
  guint w = gtk_widget_get_allocated_width (editor->waveform_scrolled_window);
  gdouble max_zoom = sample_info->frames / (double) w;
  return max_zoom < 1 ? 1 : max_zoom;
}

static gboolean
editor_zoom (struct editor *editor, GdkEventScroll *event, gdouble dy)
{
  gdouble rel_pos;
  gboolean err = TRUE;
  guint start, cursor_frame;
  struct sample_info *sample_info;
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

  sample_info = editor->audio.sample.info;
  if (!sample_info)
    {
      err = FALSE;
      goto end;
    }

  editor_get_frame_at_position (editor, event->x, &cursor_frame, &rel_pos);
  debug_print (1, "Zooming at frame %d...", cursor_frame);

  if (dy == -1.0)
    {
      gdouble max_zoom = editor_get_max_zoom (editor);
      if (editor->zoom == max_zoom)
	{
	  goto end;
	}
      editor->zoom = editor->zoom * 2.0;
      if (editor->zoom > max_zoom)
	{
	  editor->zoom = max_zoom;
	}
    }
  else
    {
      if (editor->zoom == 1)
	{
	  goto end;
	}
      editor->zoom = editor->zoom / 2.0;
      if (editor->zoom < 1.0)
	{
	  editor->zoom = 1.0;
	}
    }

  debug_print (1, "Setting zoom to %f.2x...", editor->zoom);

  start = cursor_frame - rel_pos * sample_info->frames /
    (gdouble) editor->zoom;
  editor_set_start_frame (editor, start);
  editor_set_layout_width (editor);

end:
  g_mutex_unlock (&editor->audio.control.mutex);

  return err;
}

gboolean
editor_waveform_scroll (GtkWidget *widget, GdkEventScroll *event,
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
editor_on_size_allocate (GtkWidget *self, GtkAllocation *allocation,
			 struct editor *editor)
{
  struct sample_info *sample_info;
  guint start;

  g_mutex_lock (&editor->audio.control.mutex);
  sample_info = editor->audio.sample.info;
  if (!sample_info)
    {
      goto end;
    }

  start = editor_get_start_frame (editor);
  editor_set_start_frame (editor, start);
  editor_set_layout_width (editor);

end:
  g_mutex_unlock (&editor->audio.control.mutex);
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
  gdouble shift = x_ratio < 2 ? 2 : x_ratio * 2;
  return cursor_frame >= frame - shift && cursor_frame <= frame + shift;
}

static void
editor_set_cursor (struct editor *editor, const gchar *cursor_name)
{
  GdkDisplay *display = gdk_display_get_default ();
  GdkCursor *cursor = gdk_cursor_new_from_name (display,
						cursor_name);
  gdk_window_set_cursor (gtk_widget_get_window (editor->waveform), cursor);
  g_object_unref (cursor);
}

static gboolean
editor_button_press (GtkWidget *widget, GdkEventButton *event, gpointer data)
{
  guint cursor_frame;
  guint32 sel_len;
  struct editor *editor = data;
  struct sample_info *sample_info;

  g_mutex_lock (&editor->audio.control.mutex);

  if (!editor_loading_completed_no_lock (editor, NULL))
    {
      goto end;
    }

  sample_info = editor->audio.sample.info;
  if (!sample_info)
    {
      goto end;
    }

  sel_len = AUDIO_SEL_LEN (&editor->audio);

  press_event_x = event->x;
  editor_get_frame_at_position (editor, event->x, &cursor_frame, NULL);
  if (event->button == GDK_BUTTON_PRIMARY)
    {
      debug_print (2, "Pressing at frame %d...", cursor_frame);
      if (editor_cursor_frame_over_frame (editor, cursor_frame,
					  sample_info->loop_start))
	{
	  debug_print (2, "Clicking on loop start...");
	  editor->operation = EDITOR_OP_MOVE_LOOP_START;
	  editor_set_cursor (editor, "col-resize");
	}
      else if (editor_cursor_frame_over_frame (editor, cursor_frame,
					       sample_info->loop_end))
	{
	  debug_print (2, "Clicking on loop end...");
	  editor->operation = EDITOR_OP_MOVE_LOOP_END;
	  editor_set_cursor (editor, "col-resize");
	}
      else if (editor_cursor_frame_over_frame (editor, cursor_frame,
					       editor->audio.sel_start) &&
	       sel_len)
	{
	  debug_print (2, "Clicking on selection start...");
	  editor->operation = EDITOR_OP_MOVE_SEL_START;
	  editor_set_cursor (editor, "col-resize");
	}
      else if (editor_cursor_frame_over_frame (editor, cursor_frame,
					       editor->audio.sel_end) &&
	       sel_len)
	{
	  debug_print (2, "Clicking on selection end...");
	  editor->operation = EDITOR_OP_MOVE_SEL_END;
	  editor_set_cursor (editor, "col-resize");
	}
      else
	{
	  g_mutex_unlock (&editor->audio.control.mutex);
	  audio_stop_playback (&editor->audio);
	  g_mutex_lock (&editor->audio.control.mutex);
	  editor->operation = EDITOR_OP_MOVE_SEL_END;
	  editor->audio.sel_start = cursor_frame;
	  editor->audio.sel_end = cursor_frame;
	  gtk_widget_grab_focus (editor->waveform_scrolled_window);
	  g_idle_add (editor_queue_draw, editor);
	}
    }
  else if (event->button == GDK_BUTTON_SECONDARY)
    {
      GdkRectangle r;
      gboolean cursor_on_sel = sel_len > 0 &&
	cursor_frame >= editor->audio.sel_start &&
	cursor_frame < editor->audio.sel_end;

      if (!cursor_on_sel)
	{
	  editor->audio.sel_start = -1;
	  editor->audio.sel_end = -1;
	}

      gtk_widget_set_sensitive (editor->popover_delete_button, sel_len > 0);
      gtk_widget_set_sensitive (editor->popover_undo_button, editor->dirty);
      gtk_widget_set_sensitive (editor->popover_save_button, editor->dirty ||
				cursor_on_sel);

      r.x = event->x;
      r.y = event->y;
      r.width = 1;
      r.height = 1;
      gtk_popover_set_pointing_to (GTK_POPOVER (editor->popover), &r);
      gtk_popover_popup (GTK_POPOVER (editor->popover));
    }

end:
  g_mutex_unlock (&editor->audio.control.mutex);
  return FALSE;
}

static gboolean
editor_button_release (GtkWidget *widget, GdkEventButton *event,
		       gpointer data)
{
  struct editor *editor = data;

  if (!editor->operation)
    {
      return FALSE;
    }

  if (editor->operation == EDITOR_OP_MOVE_SEL_START ||
      editor->operation == EDITOR_OP_MOVE_SEL_END)
    {
      gtk_widget_grab_focus (editor->waveform_scrolled_window);

      if (press_event_x == event->x)
	{
	  debug_print (2, "Cleaning selection...");
	  editor->audio.sel_start = -1;
	  editor->audio.sel_end = -1;
	  g_idle_add (editor_queue_draw, editor);
	}
      else
	{
	  debug_print (2, "Selected range: [%" PRId64 " to %" PRId64 "]...",
		       editor->audio.sel_start, editor->audio.sel_end);

	  if (AUDIO_SEL_LEN (&editor->audio))
	    {
	      gtk_widget_set_sensitive (editor->popover_delete_button, TRUE);
	      if (preferences_get_boolean (PREF_KEY_AUTOPLAY))
		{
		  audio_start_playback (&editor->audio);
		}
	    }
	}
    }

  editor->operation = EDITOR_OP_NONE;

  return FALSE;
}

static gboolean
editor_motion_notify (GtkWidget *widget, GdkEventMotion *event, gpointer data)
{
  guint cursor_frame;
  guint32 sel_len;
  struct editor *editor = data;
  struct audio *audio = &editor->audio;
  struct sample_info *sample_info;

  g_mutex_lock (&editor->audio.control.mutex);

  sample_info = audio->sample.info;
  if (!sample_info)
    {
      goto end;
    }

  sel_len = AUDIO_SEL_LEN (&editor->audio);

  editor_get_frame_at_position (editor, event->x, &cursor_frame, NULL);

  if (editor->operation == EDITOR_OP_MOVE_SEL_END)
    {
      if (cursor_frame > editor->audio.sel_start)
	{
	  editor->audio.sel_end = cursor_frame;
	}
      else
	{
	  editor->operation = EDITOR_OP_MOVE_SEL_START;
	  editor->audio.sel_end = editor->audio.sel_start;
	  editor->audio.sel_start = cursor_frame;
	}
      debug_print (2, "Setting selection to [%" PRId64 ", %" PRId64 "]...",
		   editor->audio.sel_start, editor->audio.sel_end);
    }
  else if (editor->operation == EDITOR_OP_MOVE_SEL_START)
    {
      if (cursor_frame < editor->audio.sel_end)
	{
	  editor->audio.sel_start = cursor_frame;
	}
      else
	{
	  editor->operation = EDITOR_OP_MOVE_SEL_END;
	  editor->audio.sel_start = editor->audio.sel_end;
	  editor->audio.sel_end = cursor_frame;
	}
      debug_print (2, "Setting selection to [%" PRId64 ", %" PRId64 "]...",
		   editor->audio.sel_start, editor->audio.sel_end);
    }
  else if (editor->operation == EDITOR_OP_MOVE_LOOP_START)
    {
      sample_info->loop_start = cursor_frame;
      debug_print (2, "Setting loop to [%d, %d]...",
		   sample_info->loop_start, sample_info->loop_end);
      editor->dirty = TRUE;
    }
  else if (editor->operation == EDITOR_OP_MOVE_LOOP_END)
    {
      sample_info->loop_end = cursor_frame;
      debug_print (2, "Setting loop to [%d, %d]...",
		   sample_info->loop_start, sample_info->loop_end);
      editor->dirty = TRUE;
    }
  else
    {
      if (editor_cursor_frame_over_frame (editor, cursor_frame,
					  sample_info->loop_start))
	{
	  editor_set_cursor (editor, "col-resize");
	}
      else if (editor_cursor_frame_over_frame (editor, cursor_frame,
					       sample_info->loop_end))
	{
	  editor_set_cursor (editor, "col-resize");
	}
      else if (editor_cursor_frame_over_frame (editor, cursor_frame,
					       editor->audio.sel_start) &&
	       sel_len)
	{
	  editor_set_cursor (editor, "col-resize");
	}
      else if (editor_cursor_frame_over_frame (editor, cursor_frame,
					       editor->audio.sel_end) &&
	       sel_len)
	{
	  editor_set_cursor (editor, "col-resize");
	}
      else
	{
	  editor_set_cursor (editor, "default");
	}
    }

  g_idle_add (editor_queue_draw, data);

end:
  g_mutex_unlock (&editor->audio.control.mutex);
  return FALSE;
}

static void
editor_delete_clicked (GtkWidget *object, gpointer data)
{
  enum audio_status status;
  guint32 sel_len;
  struct editor *editor = data;

  if (!editor_loading_completed (editor))
    {
      return;
    }

  sel_len = AUDIO_SEL_LEN (&editor->audio);
  if (!sel_len)
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

  audio_delete_range (&editor->audio, editor->audio.sel_start, sel_len);
  editor->dirty = TRUE;
  g_idle_add (editor_queue_draw, data);

  if (status == AUDIO_STATUS_PLAYING)
    {
      audio_start_playback (&editor->audio);
    }
}

static void
editor_undo_clicked (GtkWidget *object, gpointer data)
{
  struct editor *editor = data;

  if (editor->audio.path)
    {
      //As there is only one undo level, it's enough to reload the sample.
      editor_start_load_thread (editor, editor->audio.path);
    }
  else
    {
      //This is a recording
      editor_reset (editor, NULL);
    }
}

static gboolean
editor_file_exists_no_overwrite (const gchar *filename)
{
  gint res = GTK_RESPONSE_ACCEPT;
  GtkWidget *dialog;

  if (g_file_test (filename, G_FILE_TEST_EXISTS))
    {
      dialog = gtk_message_dialog_new (NULL,
				       GTK_DIALOG_MODAL |
				       GTK_DIALOG_USE_HEADER_BAR,
				       GTK_MESSAGE_WARNING,
				       GTK_BUTTONS_NONE,
				       _("Replace file “%s”?"),
				       (gchar *) filename);
      gtk_dialog_add_buttons (GTK_DIALOG (dialog),
			      _("_Cancel"), GTK_RESPONSE_CANCEL,
			      _("_Replace"), GTK_RESPONSE_ACCEPT, NULL);
      gtk_dialog_set_default_response (GTK_DIALOG (dialog),
				       GTK_RESPONSE_ACCEPT);

      res = elektroid_run_dialog_and_destroy (dialog);
    }

  return res == GTK_RESPONSE_CANCEL;
}

//This function does not need synchronized access as it is only called from
//editor_save_clicked which already provides this.

static gint
editor_save_with_format (struct editor *editor, const gchar *name,
			 struct idata *sample)
{
  gint err;
  struct sample_info *sample_info_src = &editor->audio.sample_info_src;
  struct sample_info *sample_info = sample->info;

  if (sample_info->rate == sample_info_src->rate)
    {
      err = sample_save_to_file (name, sample, NULL, sample_info_src->format);
    }
  else
    {
      struct idata resampled;
      err = sample_reload (sample, &resampled, NULL, sample_info_src,
			   job_control_set_sample_progress_no_sync, NULL);
      if (err)
	{
	  return err;
	}

      err = sample_save_to_file (name, &resampled, NULL,
				 sample_info_src->format);
      idata_free (&resampled);
    }

  browser_load_dir_if_needed (editor->browser);

  return err;
}

static void
editor_save_accept (gpointer source, const gchar *name, gpointer data)
{
  gchar *path;
  guint32 sel_len;
  GByteArray *sample = data;
  struct editor *editor = source;
  struct browser *browser = editor->browser;

  if (editor_file_exists_no_overwrite (name))
    {
      return;
    }

  g_mutex_lock (&editor->audio.control.mutex);

  debug_print (2, "Saving recording to %s...", name);

  enum path_type type = backend_get_path_type (browser->backend);
  path = path_chain (type, browser->dir, name);

  sel_len = AUDIO_SEL_LEN (&editor->audio);

  if (sel_len)
    {
      struct idata aux;
      struct sample_info *si = g_malloc (sizeof (struct sample_info));
      memcpy (si, &editor->audio.sample_info_src,
	      sizeof (struct sample_info));
      si->frames = sel_len;
      si->loop_start = sel_len - 1;
      si->loop_end = si->loop_start;
      idata_init (&aux, sample, NULL, si);
      editor_save_with_format (editor, path, &aux);
      idata_free (&aux);
      g_free (path);
    }
  else
    {
      g_free (editor->audio.path);
      editor->audio.path = path;

      editor_save_with_format (editor, editor->audio.path,
			       &editor->audio.sample);
    }

  g_mutex_unlock (&editor->audio.control.mutex);
}

static void
editor_save_clicked (GtkWidget *object, gpointer data)
{
  guint32 sel_len;
  struct editor *editor = data;
  struct sample_info *sample_info;

  g_mutex_lock (&editor->audio.control.mutex);

  if (!editor_loading_completed_no_lock (editor, NULL))
    {
      goto end;
    }

  sample_info = editor->audio.sample.info;
  if (!sample_info)
    {
      goto end;
    }

  sel_len = AUDIO_SEL_LEN (&editor->audio);

  if (editor->audio.path && !sel_len)
    {
      g_mutex_unlock (&editor->audio.control.mutex);
      if (editor_file_exists_no_overwrite (editor->audio.path))
	{
	  return;
	}
      g_mutex_lock (&editor->audio.control.mutex);

      debug_print (2, "Saving changes to %s...", editor->audio.path);

      editor_save_with_format (editor, editor->audio.path,
			       &editor->audio.sample);
    }
  else
    {
      GByteArray *sample;
      gchar suggestion[PATH_MAX];

      if (sel_len)
	{
	  guint fsize = SAMPLE_INFO_FRAME_SIZE (sample_info);
	  guint start = editor->audio.sel_start * fsize;
	  guint len = sel_len * fsize;
	  sample = g_byte_array_sized_new (len);
	  g_byte_array_append (sample,
			       &editor->audio.sample.content->data[start],
			       len);
	  snprintf (suggestion, PATH_MAX, "%s", "Sample.wav");
	}
      else
	{
	  GDateTime *dt = g_date_time_new_now_local ();
	  gchar *s = g_date_time_format (dt, DATE_TIME_FILENAME_FORMAT);
	  snprintf (suggestion, PATH_MAX, "%s %s.wav", _("Audio"), s);
	  g_free (s);
	  g_date_time_unref (dt);
	  sample = NULL;
	}

      name_edit_text (editor, _("Save Sample"),
		      editor->browser->fs_ops->max_name_len, suggestion, 0,
		      strlen (suggestion) - 4, TRUE, editor_save_accept,
		      sample);
    }

end:
  g_mutex_unlock (&editor->audio.control.mutex);
}

static gboolean
editor_key_press (GtkWidget *widget, GdkEventKey *event, gpointer data)
{
  struct editor *editor = data;

  if (event->type != GDK_KEY_PRESS)
    {
      return FALSE;
    }

  if (event->keyval == GDK_KEY_space)
    {
      editor_play_clicked (NULL, editor);
    }
  else if (event->keyval == GDK_KEY_Delete)
    {
      editor_delete_clicked (NULL, editor);
    }
  else if (event->state & GDK_CONTROL_MASK && event->keyval == GDK_KEY_z &&
	   editor->dirty)
    {
      editor_undo_clicked (NULL, editor);
    }
  else if (event->state & GDK_CONTROL_MASK && event->keyval == GDK_KEY_s &&
	   editor->dirty)
    {
      editor_save_clicked (NULL, editor);
    }

  return TRUE;
}

static void
editor_update_audio_status (gpointer data)
{
  struct editor *editor = data;
  gboolean status = audio_check (&editor->audio);

  gtk_widget_set_sensitive (editor->record_button, status);
  gtk_widget_set_sensitive (editor->volume_button, status);

  elektroid_update_audio_status (status);
}

void
editor_init (struct editor *editor, GtkBuilder *builder)
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
  editor->mix_switch_box =
    GTK_WIDGET (gtk_builder_get_object (builder, "mix_switch_box"));
  editor->grid_length_spin =
    GTK_WIDGET (gtk_builder_get_object (builder, "grid_length_spin"));
  editor->show_grid_switch =
    GTK_WIDGET (gtk_builder_get_object (builder, "show_grid_switch"));

  editor->notes_list_store =
    GTK_LIST_STORE (gtk_builder_get_object (builder, "notes_list_store"));

  editor->popover =
    GTK_WIDGET (gtk_builder_get_object (builder, "editor_popover"));
  editor->popover_play_button =
    GTK_WIDGET (gtk_builder_get_object
		(builder, "editor_popover_play_button"));
  editor->popover_delete_button =
    GTK_WIDGET (gtk_builder_get_object
		(builder, "editor_popover_delete_button"));
  editor->popover_undo_button =
    GTK_WIDGET (gtk_builder_get_object
		(builder, "editor_popover_undo_button"));
  editor->popover_save_button =
    GTK_WIDGET (gtk_builder_get_object
		(builder, "editor_popover_save_button"));

  editor->record_window =
    GTK_WINDOW (gtk_builder_get_object (builder, "record_window"));
  editor->guirecorder.channels_combo =
    GTK_WIDGET (gtk_builder_get_object
		(builder, "record_window_channels_combo"));
  editor->guirecorder.channels_list_store =
    GTK_LIST_STORE (gtk_builder_get_object
		    (builder, "record_window_channels_list_store"));
  editor->guirecorder.monitor_levelbar =
    GTK_LEVEL_BAR (gtk_builder_get_object
		   (builder, "record_window_monitor_levelbar"));
  editor->guirecorder.audio = &editor->audio;
  editor->record_window_record_button =
    GTK_WIDGET (gtk_builder_get_object
		(builder, "record_window_record_button"));
  editor->record_window_cancel_button =
    GTK_WIDGET (gtk_builder_get_object
		(builder, "record_window_cancel_button"));

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
		    G_CALLBACK (editor_autoplay_clicked), NULL);
  g_signal_connect (editor->mix_switch, "state-set",
		    G_CALLBACK (editor_mix_clicked), editor);
  g_signal_connect (editor->grid_length_spin, "value-changed",
		    G_CALLBACK (editor_grid_length_changed), editor);
  g_signal_connect (editor->show_grid_switch, "state-set",
		    G_CALLBACK (editor_show_grid_clicked), editor);
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

  g_signal_connect (editor->popover_play_button, "clicked",
		    G_CALLBACK (editor_play_clicked), editor);
  g_signal_connect (editor->popover_delete_button, "clicked",
		    G_CALLBACK (editor_delete_clicked), editor);
  g_signal_connect (editor->popover_undo_button, "clicked",
		    G_CALLBACK (editor_undo_clicked), editor);
  g_signal_connect (editor->popover_save_button, "clicked",
		    G_CALLBACK (editor_save_clicked), editor);

  editor_loop_clicked (editor->loop_button, editor);
  gtk_switch_set_active (GTK_SWITCH (editor->autoplay_switch),
			 preferences_get_boolean (PREF_KEY_AUTOPLAY));
  gtk_switch_set_active (GTK_SWITCH (editor->mix_switch),
			 preferences_get_boolean (PREF_KEY_MIX));
  gtk_switch_set_active (GTK_SWITCH (editor->show_grid_switch),
			 preferences_get_boolean (PREF_KEY_SHOW_GRID));

  gtk_spin_button_set_value (GTK_SPIN_BUTTON (editor->grid_length_spin),
			     preferences_get_int (PREF_KEY_GRID_LENGTH));

  g_signal_connect (editor->guirecorder.channels_combo, "changed",
		    G_CALLBACK (guirecorder_channels_changed),
		    &editor->guirecorder);

  g_signal_connect (editor->record_window_record_button, "clicked",
		    G_CALLBACK (editor_record_window_start), editor);
  g_signal_connect (editor->record_window_cancel_button, "clicked",
		    G_CALLBACK (editor_record_window_cancel), editor);
  g_signal_connect (GTK_WIDGET (editor->record_window), "delete-event",
		    G_CALLBACK (editor_record_window_delete), editor);

  audio_init (&editor->audio, editor_set_volume_callback,
	      editor_update_audio_status, editor);

  editor_reset (editor, NULL);
}

void
editor_destroy (struct editor *editor)
{
  audio_destroy (&editor->audio);
}

void
editor_reset_audio (struct editor *editor)
{
  audio_destroy (&editor->audio);
  audio_init (&editor->audio, editor_set_volume_callback,
	      editor_update_audio_status, editor);
  editor_reset (editor, NULL);
  //Resetting the audio causes the edited sample to be cleared so that these are
  //needed to keep the selection consistent with the editor.
  browser_clear_selection (&local_browser);
  browser_clear_selection (&remote_browser);
}
