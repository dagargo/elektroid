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

#if defined(__linux__)
#define FRAMES_TO_PLAY (16 * 1024)
#else
#define FRAMES_TO_PLAY (64 * 1024)
#endif

#define MAX_FRAMES_PER_PIXEL 300
#define EDITOR_SAMPLE_CHANNELS(editor) (((struct sample_info *)editor->audio.control.data)->channels)

struct editor editor;

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

extern void elektroid_set_audio_controls_on_load (gboolean sensitive);
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
  editor->audio_src = audio_src;

  if (audio_src == AUDIO_SRC_NONE)
    {
      editor_set_layout_width_to_val (editor, 1);
    }

  editor_set_widget_source (editor->autoplay_switch, audio_src);
  editor_set_widget_source (editor->mix_switch, audio_src);
  editor_set_widget_source (editor->play_button, audio_src);
  editor_set_widget_source (editor->stop_button, audio_src);
  editor_set_widget_source (editor->loop_button, audio_src);
  editor_set_widget_source (editor->record_button, audio_src);
  editor_set_widget_source (editor->volume_button, audio_src);
  editor_set_widget_source (editor->waveform, audio_src);
}

static void
editor_set_start_frame (struct editor *editor, gint start)
{
  gint max = editor->audio.frames - 1;
  start = start < 0 ? 0 : start;
  start = start > max ? max : start;

  gdouble widget_w =
    gtk_widget_get_allocated_width (editor->waveform_scrolled_window);
  GtkAdjustment *adj =
    gtk_scrolled_window_get_hadjustment (GTK_SCROLLED_WINDOW
					 (editor->waveform_scrolled_window));
  gdouble upper = widget_w * editor->zoom - 3;	//Base 0 and 2 border pixels
  gdouble lower = 0;
  gdouble value = upper * start / (double) editor->audio.frames;

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
  return editor->audio.frames * gtk_adjustment_get_value (adj) /
    (gdouble) gtk_adjustment_get_upper (adj);
}

static void
editor_set_sample_time_properties (struct editor *editor)
{
  gchar label[LABEL_MAX];
  struct sample_info *sample_info = editor->audio.control.data;
  double time = sample_info->frames / (double) sample_info->samplerate;

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
}

static void
editor_set_sample_properties_on_load (struct editor *editor)
{
  gchar label[LABEL_MAX];
  struct sample_info *sample_info = editor->audio.control.data;

  gtk_widget_set_visible (editor->sample_info_box, sample_info->frames > 0);

  if (!sample_info->frames)
    {
      return;
    }

  editor_set_sample_time_properties (editor);

  snprintf (label, LABEL_MAX, "%.2f kHz", sample_info->samplerate / 1000.f);
  gtk_label_set_text (GTK_LABEL (editor->sample_samplerate), label);

  snprintf (label, LABEL_MAX, "%d", sample_info->channels);
  gtk_label_set_text (GTK_LABEL (editor->sample_channels), label);

  if (sample_info->bitdepth)
    {
      snprintf (label, LABEL_MAX, "%d", sample_info->bitdepth);
      gtk_label_set_text (GTK_LABEL (editor->sample_bitdepth), label);
    }

  editor_set_start_frame (editor, 0);
}

void
editor_set_audio_mono_mix (struct editor *editor)
{
  if (editor->audio.frames > 0)
    {
      gboolean remote_mono = editor->remote_browser->fs_ops &&
	!(editor->remote_browser->fs_ops->options & FS_OPTION_STEREO);
      gboolean mono_mix = (editor->preferences->mix && remote_mono) ||
	AUDIO_SAMPLE_CHANNELS (&editor->audio) != 2;

      g_mutex_lock (&editor->audio.control.mutex);
      editor->audio.mono_mix = mono_mix;
      g_mutex_unlock (&editor->audio.control.mutex);
    }
}

static gboolean
editor_update_ui_on_load (gpointer data)
{
  gboolean ready_to_play;
  struct editor *editor = data;
  struct audio *audio = &editor->audio;

  g_mutex_lock (&audio->control.mutex);
  ready_to_play = audio->frames >= FRAMES_TO_PLAY || (!audio->control.active
						      && audio->frames > 0);
  g_mutex_unlock (&audio->control.mutex);

  editor_set_sample_properties_on_load (editor);
  editor_set_audio_mono_mix (editor);

  if (ready_to_play)
    {
      if (audio_check (&editor->audio))
	{
	  elektroid_set_audio_controls_on_load (TRUE);
	}
      if (editor->preferences->autoplay)
	{
	  audio_start_playback (&editor->audio);
	}
      return FALSE;
    }

  return TRUE;
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
  guint loaded_frames = sample->len / (2 * channels);
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

gboolean
editor_draw_waveform (GtkWidget * widget, cairo_t * cr, gpointer data)
{
  GdkRGBA color, bgcolor;
  guint width, height, channels, x_count, layout_width, c_height,
    c_height_half;
  GtkStyleContext *context;
  gdouble x_ratio, x_frame, x_frame_next, y_scale;
  struct editor_y_frame_state y_frame_state;
  struct editor *editor = data;
  struct audio *audio = &editor->audio;
  guint start = editor_get_start_frame (editor);

  debug_print (3, "Drawing waveform from %d with %dx zoom...\n",
	       start, editor->zoom);

  g_mutex_lock (&audio->control.mutex);

  width = gtk_widget_get_allocated_width (widget);
  height = gtk_widget_get_allocated_height (widget);
  channels = AUDIO_SAMPLE_CHANNELS (&(editor->audio));
  gtk_layout_get_size (GTK_LAYOUT (editor->waveform), &layout_width, NULL);
  x_ratio = audio->frames / (gdouble) layout_width;

  y_scale = height / (double) SHRT_MIN;
  y_scale /= (gdouble) channels *2;
  c_height = height / (gdouble) channels;
  c_height_half = c_height / 2;

  editor_init_y_frame_state (&y_frame_state, channels);

  context = gtk_widget_get_style_context (widget);
  gtk_render_background (context, cr, 0, 0, width, height);

  if (audio->frames)
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

	  if (!editor_get_y_frame (audio->sample, channels, x_frame, x_count,
				   &y_frame_state))
	    {
	      debug_print (3,
			   "Last available frame before the sample end. Stopping...\n");
	      break;
	    }

	  gdouble mid_c = c_height_half;
	  for (gint j = 0; j < channels; j++)
	    {
	      gdouble value = mid_c + y_frame_state.wp[j] * y_scale;
	      cairo_move_to (cr, i, value);
	      value = mid_c + y_frame_state.wn[j] * y_scale;
	      cairo_line_to (cr, i, value);
	      cairo_stroke (cr);
	      mid_c += c_height;
	    }
	}
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

  editor->dirty = FALSE;
  editor->zoom = 1;
  editor->audio.sel_start = 0;

  sample_params.samplerate = audio->samplerate;
  sample_params.channels = 0;	//Automatic

  g_timeout_add (200, editor_update_ui_on_load, editor);

  g_mutex_lock (&audio->control.mutex);
  audio->control.active = TRUE;
  g_mutex_unlock (&audio->control.mutex);

  if (sample_load_from_file_with_cb
      (audio->path, audio->sample, &audio->control, &sample_params,
       &audio->frames, editor_load_sample_cb, editor) >= 0)
    {
      debug_print (1,
		   "Frames: %d (%d B); channels: %d; loop start at %d; loop end at %d; sample rate: %.2f kHz; bit depth: %d\n",
		   audio->frames, audio->sample->len, sample_info->channels,
		   sample_info->loopstart, sample_info->loopend,
		   sample_info->samplerate / 1000.0, sample_info->bitdepth);
    }

  editor->audio.sel_len = 0;

  g_mutex_lock (&audio->control.mutex);
  audio->control.active = FALSE;
  g_mutex_unlock (&audio->control.mutex);

  editor_set_layout_width (editor);

  return NULL;
}

void
editor_play_clicked (GtkWidget * object, gpointer data)
{
  struct editor *editor = data;
  audio_stop_recording (&editor->audio);
  audio_start_playback (&editor->audio);
}

static gboolean
editor_update_ui_on_record (gpointer data)
{
  struct editor *editor = data;
  guint32 read, frames, bytes;

  gtk_widget_queue_draw (editor->waveform);
  g_mutex_lock (&editor->audio.control.mutex);
  read = editor->audio.sample->len;
  frames = editor->audio.frames;
  g_mutex_unlock (&editor->audio.control.mutex);

  bytes = frames * AUDIO_SAMPLE_BYTES_PER_FRAME (&editor->audio);
  if (bytes != read)
    {
      return TRUE;
    }

  editor_update_ui_on_load (editor);
  return FALSE;
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

static void
editor_record_clicked (GtkWidget * object, gpointer data)
{
  gint res;
  guint channel_mask;
  struct editor *editor = data;

  if (editor->audio_src != AUDIO_SRC_NONE)
    {
      browser_clear_selection (editor->local_browser);
      browser_clear_selection (editor->remote_browser);
      audio_reset_sample (&editor->audio);
    }

  audio_stop_playback (&editor->audio);
  editor_set_source (editor, AUDIO_SRC_LOCAL);

  editor->dirty = TRUE;
  editor->zoom = 1;
  editor->audio.sel_start = 0;
  editor->audio.sel_len = 0;

  gtk_widget_set_sensitive (editor->play_button, FALSE);
  gtk_widget_set_visible (editor->sample_info_box, FALSE);

  guint options =
    guirecorder_get_channel_mask (editor->guirecorder.channels_combo) |
    RECORD_MONITOR_ONLY;

  audio_stop_playback (&editor->audio);
  audio_start_recording (&editor->audio, options,
			 guirecorder_monitor_notifier, &editor->guirecorder);

  res = gtk_dialog_run (editor->record_dialog);
  gtk_widget_hide (GTK_WIDGET (editor->record_dialog));
  if (res == GTK_RESPONSE_CANCEL)
    {
      editor_set_source (editor, AUDIO_SRC_NONE);
      return;
    }

  channel_mask =
    guirecorder_get_channel_mask (editor->guirecorder.channels_combo);
  audio_start_recording (&editor->audio, channel_mask, NULL, NULL);
  g_timeout_add (200, editor_update_ui_on_record, data);
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
  gint64 aux = editor->audio.frames * x / lw;
  if (aux > 0)
    {
      *cursor_frame =
	aux > editor->audio.frames ? editor->audio.frames : (guint) aux;
    }
  else
    {
      *cursor_frame = 0;
    }
  if (rel_pos)
    {
      *rel_pos = (*cursor_frame - start) /
	(editor->audio.frames / (double) editor->zoom);
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
      if (w >= editor->audio.frames)
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

  start = cursor_frame - rel_pos * editor->audio.frames /
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
  if (editor->audio.frames == 0)
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
  guint loaded_frames;
  guint channels = AUDIO_SAMPLE_CHANNELS (&(editor->audio));

  g_mutex_lock (&editor->audio.control.mutex);
  loaded_frames = editor->audio.sample->len / (channels * sizeof (gint16));
  g_mutex_unlock (&editor->audio.control.mutex);

  return editor->audio.frames == loaded_frames && editor->audio.frames;
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
      audio_stop_playback (&editor->audio);
      editor->selecting = TRUE;
      editor->audio.sel_len = 0;
      gtk_widget_grab_focus (editor->waveform_scrolled_window);

      editor_get_frame_at_position (data, event->x, &cursor_frame, NULL);
      debug_print (2, "Pressing at frame %d...\n", cursor_frame);
      editor->audio.sel_start = cursor_frame;

      g_idle_add (editor_queue_draw, data);
    }
  else if (event->button == GDK_BUTTON_SECONDARY &&
	   editor->audio_src != AUDIO_SRC_REMOTE)
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

  if (!editor->selecting)
    {
      return FALSE;
    }

  editor->selecting = FALSE;
  gtk_widget_grab_focus (editor->waveform_scrolled_window);

  if (editor->audio.sel_len < 0)
    {
      gint64 aux = ((gint64) editor->audio.sel_start) + editor->audio.sel_len;
      editor->audio.sel_start = (guint32) aux;
      editor->audio.sel_len = -editor->audio.sel_len;
    }

  debug_print (2, "Audio selected from %d with len %ld...\n",
	       editor->audio.sel_start, editor->audio.sel_len);

  if (editor->audio.sel_len)
    {
      gtk_widget_set_sensitive (editor->delete_menuitem, TRUE);
    }

  g_idle_add (editor_queue_draw, data);

  if (editor->preferences->autoplay && editor->audio.sel_len)
    {
      audio_start_playback (&editor->audio);
    }

  return FALSE;
}

static gboolean
editor_motion_notify (GtkWidget * widget, GdkEventMotion * event,
		      gpointer data)
{
  guint cursor_frame;
  struct editor *editor = data;
  if (editor->selecting)
    {
      editor_get_frame_at_position (editor, event->x, &cursor_frame, NULL);
      debug_print (3, "Motion over sample %d...\n", cursor_frame);
      editor->audio.sel_len =
	((gint64) cursor_frame) - editor->audio.sel_start;

      g_idle_add (editor_queue_draw, data);
    }
  return FALSE;
}

static void
editor_delete_clicked (GtkWidget * object, gpointer data)
{
  struct editor *editor = data;

  if (!editor_loading_completed (editor))
    {
      return;
    }

  if (editor->audio_src == AUDIO_SRC_REMOTE)
    {
      return;
    }

  if (!editor->audio.sel_len)
    {
      return;
    }

  audio_delete_range (&editor->audio, editor->audio.sel_start,
		      editor->audio.sel_len);
  editor->dirty = TRUE;
  editor_set_sample_time_properties (editor);
  g_idle_add (editor_queue_draw, data);
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
					editor->local_browser);
      if (!name)
	{
	  return;
	}
      strcat (editor->audio.path, name);
      g_free (name);
    }

  debug_print (2, "Saving sample to %s...\n", editor->audio.path);
  sample_save_from_array (editor->audio.path, editor->audio.sample,
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
editor_init (GtkBuilder * builder)
{
  editor.box = GTK_WIDGET (gtk_builder_get_object (builder, "editor_box"));
  editor.waveform_scrolled_window =
    GTK_WIDGET (gtk_builder_get_object (builder, "waveform_scrolled_window"));
  editor.waveform = GTK_WIDGET (gtk_builder_get_object (builder, "waveform"));
  editor.play_button =
    GTK_WIDGET (gtk_builder_get_object (builder, "play_button"));
  editor.stop_button =
    GTK_WIDGET (gtk_builder_get_object (builder, "stop_button"));
  editor.loop_button =
    GTK_WIDGET (gtk_builder_get_object (builder, "loop_button"));
  editor.record_button =
    GTK_WIDGET (gtk_builder_get_object (builder, "record_button"));
  editor.autoplay_switch =
    GTK_WIDGET (gtk_builder_get_object (builder, "autoplay_switch"));
  editor.mix_switch =
    GTK_WIDGET (gtk_builder_get_object (builder, "mix_switch"));
  editor.volume_button =
    GTK_WIDGET (gtk_builder_get_object (builder, "volume_button"));

  editor.sample_info_box =
    GTK_WIDGET (gtk_builder_get_object (builder, "sample_info_box"));
  editor.sample_length =
    GTK_WIDGET (gtk_builder_get_object (builder, "sample_length"));
  editor.sample_duration =
    GTK_WIDGET (gtk_builder_get_object (builder, "sample_duration"));
  editor.sample_channels =
    GTK_WIDGET (gtk_builder_get_object (builder, "sample_channels"));
  editor.sample_samplerate =
    GTK_WIDGET (gtk_builder_get_object (builder, "sample_samplerate"));
  editor.sample_bitdepth =
    GTK_WIDGET (gtk_builder_get_object (builder, "sample_bitdepth"));

  editor.menu = GTK_MENU (gtk_builder_get_object (builder, "editor_menu"));
  editor.play_menuitem =
    GTK_WIDGET (gtk_builder_get_object (builder, "editor_play_menuitem"));
  editor.delete_menuitem =
    GTK_WIDGET (gtk_builder_get_object (builder, "editor_delete_menuitem"));
  editor.save_menuitem =
    GTK_WIDGET (gtk_builder_get_object (builder, "editor_save_menuitem"));

  editor.record_dialog =
    GTK_DIALOG (gtk_builder_get_object (builder, "record_dialog"));
  editor.guirecorder.channels_combo =
    GTK_WIDGET (gtk_builder_get_object
		(builder, "record_dialog_channels_combo"));
  editor.guirecorder.monitor_levelbar =
    GTK_LEVEL_BAR (gtk_builder_get_object
		   (builder, "record_dialog_monitor_levelbar"));
  editor.record_dialog_cancel_button =
    GTK_WIDGET (gtk_builder_get_object
		(builder, "record_dialog_cancel_button"));
  editor.record_dialog_record_button =
    GTK_WIDGET (gtk_builder_get_object
		(builder, "record_dialog_record_button"));

  g_signal_connect (editor.waveform, "draw",
		    G_CALLBACK (editor_draw_waveform), &editor);
  gtk_widget_add_events (editor.waveform, GDK_SCROLL_MASK);
  g_signal_connect (editor.waveform, "scroll-event",
		    G_CALLBACK (editor_waveform_scroll), &editor);
  g_signal_connect (editor.play_button, "clicked",
		    G_CALLBACK (editor_play_clicked), &editor);
  g_signal_connect (editor.stop_button, "clicked",
		    G_CALLBACK (editor_stop_clicked), &editor);
  g_signal_connect (editor.loop_button, "clicked",
		    G_CALLBACK (editor_loop_clicked), &editor);
  g_signal_connect (editor.record_button, "clicked",
		    G_CALLBACK (editor_record_clicked), &editor);
  g_signal_connect (editor.autoplay_switch, "state-set",
		    G_CALLBACK (editor_autoplay_clicked), &editor);
  g_signal_connect (editor.mix_switch, "state-set",
		    G_CALLBACK (editor_mix_clicked), &editor);
  editor.volume_changed_handler = g_signal_connect (editor.volume_button,
						    "value_changed",
						    G_CALLBACK
						    (editor_set_volume),
						    &editor);

  g_signal_connect (editor.waveform_scrolled_window, "size-allocate",
		    G_CALLBACK (editor_on_size_allocate), &editor);
  gtk_widget_add_events (editor.waveform, GDK_BUTTON_PRESS_MASK);
  g_signal_connect (editor.waveform, "button-press-event",
		    G_CALLBACK (editor_button_press), &editor);
  gtk_widget_add_events (editor.waveform, GDK_BUTTON_RELEASE_MASK);
  g_signal_connect (editor.waveform, "button-release-event",
		    G_CALLBACK (editor_button_release), &editor);
  gtk_widget_add_events (editor.waveform, GDK_POINTER_MOTION_MASK);
  g_signal_connect (editor.waveform, "motion-notify-event",
		    G_CALLBACK (editor_motion_notify), &editor);
  g_signal_connect (editor.waveform_scrolled_window, "key-press-event",
		    G_CALLBACK (editor_key_press), &editor);

  g_signal_connect (editor.play_menuitem, "activate",
		    G_CALLBACK (editor_play_clicked), &editor);
  g_signal_connect (editor.delete_menuitem, "activate",
		    G_CALLBACK (editor_delete_clicked), &editor);
  g_signal_connect (editor.save_menuitem, "activate",
		    G_CALLBACK (editor_save_clicked), &editor);

  editor_loop_clicked (editor.loop_button, &editor);
  gtk_switch_set_active (GTK_SWITCH (editor.autoplay_switch),
			 editor.preferences->autoplay);
  gtk_switch_set_active (GTK_SWITCH (editor.mix_switch),
			 editor.preferences->mix);

  g_signal_connect (editor.guirecorder.channels_combo, "changed",
		    G_CALLBACK (guirecorder_channels_changed), &editor.audio);
  g_signal_connect (editor.record_dialog_record_button, "clicked",
		    G_CALLBACK (editor_start_record), &editor);
  g_signal_connect (editor.record_dialog_cancel_button, "clicked",
		    G_CALLBACK (editor_cancel_record), &editor);

  audio_init (&editor.audio, editor_set_volume_callback,
	      elektroid_update_audio_status, &editor);
}

void
editor_destroy (struct editor *editor)
{
  audio_destroy (&editor->audio);
}
