/*
 *   c
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
#include "audio.h"
#include "connectors/system.h"
#include "editor.h"
#include "elektroid.h"
#include "name_window.h"
#include "preferences.h"
#include "record_window.h"
#include "sample.h"
#include "utils.h"

#define EDITOR_LOOP_MARKER_WIDTH 7
#define EDITOR_LOOP_MARKER_HALF_HEIGHT 4
#define EDITOR_LOOP_MARKER_FULL_HEIGHT (EDITOR_LOOP_MARKER_HALF_HEIGHT * 2)

#if defined(__linux__)
#define FRAMES_TO_PLAY (16 * KI)
#else
#define FRAMES_TO_PLAY (64 * KI)
#endif

//Some OSs do not allow ':' in the name. Same format used by the GNOME screenshot tool.
#define DATE_TIME_FILENAME_FORMAT "%Y-%m-%d %H-%M-%S"

#define MAX_FRAMES_PER_PIXEL 300

enum editor_operation
{
  EDITOR_OP_NONE,
  EDITOR_OP_MOVE_LOOP_START,
  EDITOR_OP_MOVE_LOOP_END,
  EDITOR_OP_MOVE_SEL_START,
  EDITOR_OP_MOVE_SEL_END
};

struct waveform_state
{
  gdouble *wp;
  gdouble *wn;
};

static void editor_save_accept (gpointer source, const gchar * name);

extern struct browser local_browser;
extern struct browser remote_browser;
extern GtkWindow *main_window;

static GThread *thread;
static GtkWidget *editor_box;
static GtkWidget *waveform_scrolled_window;
static GtkWidget *waveform;
static GtkWidget *play_button;
static GtkWidget *stop_button;
static GtkWidget *loop_button;
static GtkWidget *record_button;
static GtkWidget *autoplay_switch;
static GtkWidget *mix_switch;
static GtkWidget *volume_button;
static GtkWidget *mix_switch_box;
static GtkWidget *grid_length_spin;
static GtkWidget *show_grid_switch;
static gulong volume_changed_handler;
static GtkListStore *notes_list_store;
static GtkPopoverMenu *popover;
static GtkWidget *popover_play_button;
static GtkWidget *popover_delete_button;
static GtkWidget *popover_undo_button;
static GtkWidget *popover_save_button;
static gdouble zoom;
static enum editor_operation operation;
static gboolean dirty;
static gboolean ready;
static struct browser *browser;
static GMutex mutex;
static gdouble *waveform_data;
static guint waveform_width;
static guint waveform_len;	//Loaded frames available in waveform_data
static double press_event_x;
static struct waveform_state waveform_state;
static gint64 playback_cursor;	// guint32 plus -1 (invisible)

struct browser *
editor_get_browser ()
{
  return browser;
}

static void
editor_set_waveform_width (guint width)
{
  guint height;
  gtk_layout_get_size (GTK_LAYOUT (waveform), NULL, &height);
  gtk_layout_set_size (GTK_LAYOUT (waveform), width, height);
}

static void
editor_reset_waveform_width ()
{
  guint width = gtk_widget_get_allocated_width (waveform_scrolled_window);
  width *= zoom;
  if (width >= 2)
    {
      width -= 2;		//2 border pixels
    }
  editor_set_waveform_width (width);
}

static void
editor_set_widget_source (GtkWidget *widget)
{
  const char *class;
  GtkStyleContext *context = gtk_widget_get_style_context (widget);
  GList *classes, *list = gtk_style_context_list_classes (context);

  for (classes = list; classes != NULL; classes = g_list_next (classes))
    {
      gtk_style_context_remove_class (context, classes->data);
    }
  g_list_free (list);

  if (browser == NULL)
    {
      return;
    }

  if (GTK_IS_SWITCH (widget))
    {
      class = browser == &local_browser ? "local_switch" : "remote_switch";
    }
  else
    {
      class = browser == &local_browser ? "local" : "remote";
    }
  gtk_style_context_add_class (context, class);
}

static gboolean
editor_queue_draw ()
{
  gtk_widget_queue_draw (waveform);
  return FALSE;
}

static void
editor_clear_waveform_data_no_sync ()
{
  debug_print (1, "Clearing waveform data...");
  waveform_width = gtk_widget_get_allocated_width (waveform);
  g_free (waveform_data);
  waveform_data = NULL;
  waveform_len = 0;
}

static void
editor_clear_waveform_data ()
{
  g_mutex_lock (&mutex);
  editor_clear_waveform_data_no_sync ();
  g_mutex_unlock (&mutex);
}

static gboolean
editor_reset_browser (gpointer data)
{
  editor_reset_waveform_width ();
  gtk_widget_queue_draw (waveform);

  editor_set_widget_source (autoplay_switch);
  editor_set_widget_source (mix_switch);
  editor_set_widget_source (play_button);
  editor_set_widget_source (stop_button);
  editor_set_widget_source (loop_button);
  editor_set_widget_source (record_button);
  editor_set_widget_source (volume_button);
  editor_set_widget_source (show_grid_switch);
  editor_set_widget_source (waveform);

  gtk_widget_set_sensitive (play_button, FALSE);
  gtk_widget_set_sensitive (stop_button, FALSE);
  gtk_widget_set_sensitive (loop_button, FALSE);

  return FALSE;
}

void
editor_reset (struct browser *browser_)
{
  editor_stop_load_thread ();

  audio_stop_playback ();
  audio_stop_recording ();
  audio_reset_sample ();

  browser = browser_;

  editor_clear_waveform_data ();

  g_idle_add (editor_reset_browser, NULL);
}

static void
editor_set_start_frame (gint start)
{
  GtkAdjustment *adj;
  gdouble widget_w, upper, lower, value;
  struct sample_info *sample_info = audio.sample.info;
  gint max = sample_info->frames - 1;

  start = start < 0 ? 0 : start;
  start = start > max ? max : start;

  widget_w = gtk_widget_get_allocated_width (waveform_scrolled_window);
  adj = gtk_scrolled_window_get_hadjustment (GTK_SCROLLED_WINDOW
					     (waveform_scrolled_window));
  upper = widget_w * zoom - 3;	//Base 0 and 2 border pixels
  lower = 0;
  value = upper * start / (double) sample_info->frames;

  debug_print (1, "Setting waveform scrollbar to %f [%f, %f]...", value,
	       lower, upper);
  gtk_adjustment_set_lower (adj, 0);
  gtk_adjustment_set_upper (adj, upper);
  gtk_adjustment_set_value (adj, value);
}

static guint
editor_get_start_frame ()
{
  struct sample_info *sample_info = audio.sample.info;
  GtkAdjustment *adj =
    gtk_scrolled_window_get_hadjustment (GTK_SCROLLED_WINDOW
					 (waveform_scrolled_window));
  return sample_info->frames * gtk_adjustment_get_value (adj) /
    (gdouble) gtk_adjustment_get_upper (adj);
}

static void
editor_set_audio_mono_mix ()
{
  struct sample_info *sample_info = audio.sample.info;
  if (sample_info && sample_info->frames > 0)
    {
      gboolean remote_mono = remote_browser.fs_ops &&
	!(remote_browser.fs_ops->options & FS_OPTION_STEREO);
      gboolean mono_mix = (preferences_get_boolean (PREF_KEY_MIX) &&
			   remote_mono) || sample_info->channels != 2;

      g_mutex_lock (&audio.control.controllable.mutex);
      audio.mono_mix = mono_mix;
      g_mutex_unlock (&audio.control.controllable.mutex);
    }
}

static gboolean
editor_loading_completed_no_lock (guint32 *actual_frames)
{
  gboolean completed;
  guint32 actual;
  gint bytes_per_frame;
  struct sample_info *sample_info = audio.sample.info;

  if (!sample_info)
    {
      if (actual_frames)
	{
	  *actual_frames = 0;
	}
      return FALSE;
    }

  bytes_per_frame = SAMPLE_INFO_FRAME_SIZE (sample_info);
  actual = audio.sample.content->len / bytes_per_frame;
  completed = actual == sample_info->frames && actual;
  if (actual_frames)
    {
      *actual_frames = actual;
    }
  return completed;
}

static void
editor_playback_cursor_notifier (gint64 position)
{
  debug_print (3, "Setting cursor at %" PRId64 "...", position);
  playback_cursor = position;
  g_idle_add (editor_queue_draw, NULL);
}

static void
editor_start_playback ()
{
  if (preferences_get_boolean (PREF_KEY_SHOW_PLAYBACK_CURSOR))
    {
      audio_start_playback (editor_playback_cursor_notifier);
    }
  else
    {
      audio_start_playback (NULL);
    }
}

static gboolean
editor_update_ui_on_load (gpointer data)
{
  editor_set_audio_mono_mix ();
  editor_reset_waveform_width ();

  if (audio_check ())
    {
      gtk_widget_set_sensitive (play_button, TRUE);
      gtk_widget_set_sensitive (stop_button, TRUE);
      gtk_widget_set_sensitive (loop_button, TRUE);
      if (preferences_get_boolean (PREF_KEY_AUTOPLAY))
	{
	  editor_start_playback ();
	}
    }

  return FALSE;
}

static void
editor_free_waveform_state ()
{
  g_free (waveform_state.wp);
  g_free (waveform_state.wn);
}

static void
editor_reset_waveform_state (guint channels)
{
  editor_free_waveform_state ();
  waveform_state.wp = g_malloc (sizeof (gdouble) * channels);
  waveform_state.wn = g_malloc (sizeof (gdouble) * channels);
}

static gdouble
editor_get_x_ratio ()
{
  struct sample_info *sample_info = audio.sample.info;
  return sample_info->frames / (gdouble) waveform_width;
}

static gboolean
editor_set_waveform_state (guint32 x)
{
  guint8 *s;
  guint32 start, frame_start, count;
  gdouble y_scale, x_ratio, x_frame, x_frame_next, x_count;
  GByteArray *sample = audio.sample.content;
  struct sample_info *sample_info = audio.sample.info;
  guint frame_size =
    FRAME_SIZE (sample_info->channels, sample_get_internal_format ());
  guint loaded_frames = sample->len / frame_size;
  gboolean use_float = preferences_get_boolean (PREF_KEY_AUDIO_USE_FLOAT);

  start = editor_get_start_frame ();
  x_ratio = editor_get_x_ratio () / zoom;

  x_frame = start + x * x_ratio;
  frame_start = x_frame;
  x_frame_next = x_frame + x_ratio;
  x_count = x_frame_next - frame_start;
  count = x_count > 1 ? x_count : 1;

  y_scale = use_float ? -1.0 : 1.0 / (double) SHRT_MIN;
  y_scale /= sample_info->channels * 2.0;

  for (guint i = 0; i < sample_info->channels; i++)
    {
      waveform_state.wp[i] = 0.0;
      waveform_state.wn[i] = 0.0;
    }

  debug_print (3, "Calculating %d state from [ %d, %d [ (%d frames)...", x,
	       frame_start, frame_start + count, loaded_frames);

  s = &sample->data[frame_start * frame_size];
  for (guint i = 0, f = frame_start; i < count; i++, f++)
    {
      if (f == loaded_frames)
	{
	  return f == sample_info->frames;
	}

      if (i > MAX_FRAMES_PER_PIXEL)
	{
	  continue;
	}

      for (guint j = 0; j < sample_info->channels; j++)
	{
	  gdouble v;
	  if (use_float)
	    {
	      v = *((gfloat *) s);
	      s += sizeof (gfloat);
	    }
	  else
	    {
	      v = *((gint16 *) s);
	      s += sizeof (gint16);
	    }

	  if (v > 0)
	    {
	      if (v > waveform_state.wp[j])
		{
		  waveform_state.wp[j] = v;
		}
	    }
	  else
	    {
	      if (v < waveform_state.wn[j])
		{
		  waveform_state.wn[j] = v;
		}
	    }
	}
    }

  for (guint i = 0; i < sample_info->channels; i++)
    {
      waveform_state.wp[i] *= y_scale;
      waveform_state.wn[i] *= y_scale;
    }

  return TRUE;
}

static void
editor_set_text_color (GdkRGBA *color)
{
  GtkStyleContext *context;

  context = gtk_widget_get_style_context (popover_play_button);	//Any text widget is valid
  gtk_style_context_get_color (context, GTK_STATE_FLAG_NORMAL, color);
}

static inline void
editor_draw_loop_points (cairo_t *cr, guint start, guint height,
			 double x_ratio)
{
  gdouble value;
  GdkRGBA color;
  guint32 loop_start, loop_end;
  struct sample_info *sample_info = audio.sample.info;

  loop_start = sample_info->loop_start;
  loop_end = sample_info->loop_end;

  editor_set_text_color (&color);

  gdk_cairo_set_source_rgba (cr, &color);

  cairo_set_line_width (cr, 1);

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
}

static inline void
editor_draw_playback_cursor (cairo_t *cr, guint start, guint height,
			     double x_ratio)
{
  GdkRGBA color;
  guint32 x;

  if (playback_cursor < 0)
    {
      return;
    }

  x = (playback_cursor - start) / x_ratio;

  editor_set_text_color (&color);

  gdk_cairo_set_source_rgba (cr, &color);
  cairo_set_line_width (cr, 1);
  cairo_move_to (cr, x - 0.5, 0);
  cairo_line_to (cr, x - 0.5, height - 1);
  cairo_stroke (cr);
}

static inline void
editor_draw_grid (cairo_t *cr, guint start, guint height, double x_ratio)
{
  GdkRGBA color;
  gint grid_length;
  gdouble value, grid_inc;
  struct sample_info *sample_info = audio.sample.info;

  if (preferences_get_boolean (PREF_KEY_SHOW_GRID))
    {
      editor_set_text_color (&color);
      color.alpha = 0.25;

      gdk_cairo_set_source_rgba (cr, &color);

      grid_length = preferences_get_int (PREF_KEY_GRID_LENGTH);
      grid_inc = sample_info->frames / (gdouble) grid_length;

      cairo_set_line_width (cr, 1);

      for (gint i = 1; i < grid_length; i++)
	{
	  value = ((gint) ((i * grid_inc) - start) / x_ratio) + .5;
	  cairo_move_to (cr, value, 0);
	  cairo_line_to (cr, value, height - 1);
	  cairo_stroke (cr);
	}
    }
}

static inline void
editor_draw_selection (cairo_t *cr, guint start, guint height, double x_ratio)
{
  guint32 sel_len;
  gdouble x_len, x_start;
  GdkRGBA color;
  GtkStateFlags state;
  GtkStyleContext *context;

  context = gtk_widget_get_style_context (waveform);
  state = gtk_style_context_get_state (context);
  gtk_style_context_get_color (context, state, &color);

  sel_len = AUDIO_SEL_LEN;
  if (sel_len)
    {
      x_len = sel_len / x_ratio;
      x_len = x_len < 1 ? 1 : x_len;
      x_start = (audio.sel_start - (gdouble) start) / x_ratio;

      gtk_style_context_get_color (context, state, &color);
      color.alpha = 0.25;
      gdk_cairo_set_source_rgba (cr, &color);

      cairo_rectangle (cr, x_start, 0, x_len, height);
      cairo_fill (cr);
    }
}

static void
editor_set_waveform_data_no_sync ()
{
  guint i;
  gdouble *v;
  struct sample_info *sample_info = audio.sample.info;

  debug_print (1, "Setting waveform data...");

  if (!sample_info)
    {
      return;
    }

  g_mutex_lock (&mutex);

  if (!waveform_data)
    {
      debug_print (1,
		   "Initializing waveform data (%d pixels, %d channels)...",
		   waveform_width, sample_info->channels);
      gsize size = sizeof (gdouble) * waveform_width * sample_info->channels * 2;	//Positive and negative values
      waveform_data = g_malloc (size);
      memset (waveform_data, 0, size);
      waveform_len = 0;
      editor_reset_waveform_state (sample_info->channels);
    }

  //Loading is still going on
  if (waveform_len < waveform_width)
    {
      debug_print (1, "Calculating waveform [ %d, %d [", waveform_len,
		   waveform_width);
      v = &waveform_data[waveform_len * sample_info->channels * 2];	//Positive and negative values
      for (i = waveform_len; i < waveform_width; i++)
	{
	  if (!editor_set_waveform_state (i))
	    {
	      debug_print (3, "Waveform limit reached at %d", i);
	      break;
	    }

	  for (gint j = 0; j < sample_info->channels; j++)
	    {
	      *v = waveform_state.wp[j];
	      v++;
	      *v = waveform_state.wn[j];
	      v++;
	    }
	}

      waveform_len = i;
    }

  g_mutex_unlock (&mutex);
}

static void
editor_set_waveform_data ()
{
  g_mutex_lock (&audio.control.controllable.mutex);
  editor_set_waveform_data_no_sync ();
  g_mutex_unlock (&audio.control.controllable.mutex);
}

static inline void
editor_draw_waveform (cairo_t *cr, guint start, guint height, double x_ratio)
{
  gdouble *v, mid_c, x;
  guint c_height, c_height_half;
  GdkRGBA color;
  GtkStateFlags state;
  GtkStyleContext *context;
  struct sample_info *sample_info = audio.sample.info;

  debug_print (1, "Drawing waveform from %d with %.2f zoom (%d)...", start,
	       zoom, waveform_len);

  context = gtk_widget_get_style_context (waveform);

  state = gtk_style_context_get_state (context);
  gtk_style_context_get_color (context, state, &color);
  gdk_cairo_set_source_rgba (cr, &color);

  cairo_set_line_width (cr, 1);

  c_height = height / (gdouble) sample_info->channels;
  c_height_half = c_height / 2;

  if (waveform_data)
    {
      v = waveform_data;
      x = -0.5;
      for (gint i = 0; i < waveform_len; i++)
	{
	  mid_c = c_height_half;
	  for (gint j = 0; j < sample_info->channels; j++)
	    {
	      cairo_move_to (cr, x, *v * height + mid_c);
	      v++;
	      cairo_line_to (cr, x, *v * height + mid_c);
	      v++;
	      cairo_stroke (cr);
	      mid_c += c_height;
	    }
	  x += 1.0;
	}
    }
}

static gboolean
editor_draw (GtkWidget *widget, cairo_t *cr, gpointer data)
{
  gdouble x_ratio;
  guint height, width, start;
  GtkStyleContext *context;
  struct sample_info *sample_info;

  g_mutex_lock (&audio.control.controllable.mutex);
  g_mutex_lock (&mutex);

  sample_info = audio.sample.info;

  height = gtk_widget_get_allocated_height (waveform);
  width = gtk_widget_get_allocated_width (waveform);

  context = gtk_widget_get_style_context (waveform);
  gtk_render_background (context, cr, 0, 0, width, height);

  if (sample_info && waveform_data)
    {
      start = editor_get_start_frame ();
      x_ratio = editor_get_x_ratio () / zoom;

      editor_draw_grid (cr, start, height, x_ratio);
      editor_draw_selection (cr, start, height, x_ratio);
      editor_draw_waveform (cr, start, height, x_ratio);
      editor_draw_loop_points (cr, start, height, x_ratio);
      editor_draw_playback_cursor (cr, start, height, x_ratio);
    }

  g_mutex_unlock (&mutex);
  g_mutex_unlock (&audio.control.controllable.mutex);

  return FALSE;
}

static gboolean
editor_join_load_thread (gpointer data)
{
  if (thread)
    {
      g_thread_join (thread);
      thread = NULL;
    }
  return FALSE;
}

static void
editor_update_on_load_cb (struct job_control *control, gdouble p)
{
  guint32 actual_frames;
  gboolean completed, ready_to_play;

  job_control_set_sample_progress (control, p);
  editor_set_waveform_data_no_sync ();
  g_idle_add (editor_queue_draw, NULL);
  completed = editor_loading_completed_no_lock (&actual_frames);
  if (!ready)
    {
      ready_to_play = (preferences_get_boolean (PREF_KEY_PLAY_WHILE_LOADING)
		       && actual_frames >= FRAMES_TO_PLAY) || completed;
      if (ready_to_play)
	{
	  g_idle_add (editor_update_ui_on_load, NULL);
	  ready = TRUE;
	}
    }
  //If the call to sample_load_from_file_full fails, we reset the browser.
  if (!completed && !actual_frames)
    {
      browser = NULL;
      g_idle_add (editor_reset_browser, NULL);
    }
}

static gpointer
editor_load_sample_runner (gpointer data)
{
  struct sample_info sample_info_req;

  dirty = FALSE;
  ready = FALSE;
  zoom = 1;
  audio.sel_start = -1;
  audio.sel_end = -1;

  sample_info_req.channels = 0;	//Automatic
  sample_info_req.format = sample_get_internal_format ();
  sample_info_req.rate = audio.rate;

  audio.control.controllable.active = TRUE;
  sample_load_from_file_full (audio.path, &audio.sample,
			      &audio.control, &sample_info_req,
			      &audio.sample_info_src,
			      editor_update_on_load_cb);
  return NULL;
}

void
editor_play ()
{
  if (audio_check ())
    {
      audio_stop_recording ();
      editor_start_playback ();
    }
}

static void
editor_play_clicked (GtkWidget *object, gpointer data)
{
  editor_play ();
}

static void
editor_update_on_record_cb (gpointer data, gdouble l, gdouble r)
{
  editor_set_waveform_data_no_sync ();
  g_idle_add (editor_queue_draw, data);
  if (!ready && editor_loading_completed_no_lock (NULL))
    {
      g_idle_add (editor_update_ui_on_load, data);
      ready = TRUE;
    }
}

static void
editor_stop_clicked (GtkWidget *object, gpointer data)
{
  audio_stop_playback ();
  audio_stop_recording ();
}

static void
editor_record_window_record_cb (guint channel_mask)
{
  editor_clear_waveform_data ();	//Channels might have changed
  gtk_widget_set_sensitive (stop_button, TRUE);
  audio_start_recording (channel_mask, editor_update_on_record_cb, NULL);
}

static void
editor_record_window_cancel_cb ()
{
  editor_reset (NULL);
}

static void
editor_record_clicked (GtkWidget *object, gpointer data)
{
  browser_clear_selection (&local_browser);
  browser_clear_selection (&remote_browser);

  editor_reset (&local_browser);

  ready = FALSE;
  dirty = TRUE;
  zoom = 1;
  audio.sel_start = -1;
  audio.sel_end = -1;

  record_window_open (browser->fs_ops->options,
		      editor_record_window_record_cb,
		      editor_record_window_cancel_cb);
}

static void
editor_loop_clicked (GtkWidget *object, gpointer data)
{
  audio.loop = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (object));
}

static gboolean
editor_autoplay_clicked (GtkWidget *object, gboolean state, gpointer data)
{
  preferences_set_boolean (PREF_KEY_AUTOPLAY, state);
  return FALSE;
}

void
editor_start_load_thread (gchar *sample_path)
{
  debug_print (1, "Creating load thread...");
  audio.path = sample_path;
  thread = g_thread_new ("load_sample", editor_load_sample_runner, NULL);
}

void
editor_stop_load_thread ()
{
  debug_print (1, "Stopping load thread...");
  controllable_set_active (&audio.control.controllable, FALSE);
  editor_join_load_thread (NULL);
}

static gboolean
editor_mix_clicked (GtkWidget *object, gboolean state, gpointer data)
{
  preferences_set_boolean (PREF_KEY_MIX, state);
  editor_set_audio_mono_mix ();
  return FALSE;
}

static void
editor_set_volume (GtkScaleButton *button, gdouble value, gpointer data)
{
  audio_set_volume (value);
}

static gboolean
editor_set_volume_callback_bg (gpointer user_data)
{
  gdouble *data = user_data;
  gdouble volume = *data;
  g_free (data);
  debug_print (1, "Setting volume to %f...", volume);
  g_signal_handler_block (volume_button, volume_changed_handler);
  gtk_scale_button_set_value (GTK_SCALE_BUTTON (volume_button), volume);
  g_signal_handler_unblock (volume_button, volume_changed_handler);
  return FALSE;
}

static void
editor_set_volume_callback (gdouble volume)
{
  gdouble *data = g_malloc (sizeof (gdouble));
  *data = volume;
  g_idle_add (editor_set_volume_callback_bg, data);
}

static gboolean
editor_show_grid_clicked (GtkWidget *object, gboolean state, gpointer data)
{
  preferences_set_boolean (PREF_KEY_SHOW_GRID, state);
  gtk_widget_queue_draw (waveform);
  return FALSE;
}

static void
editor_grid_length_changed (GtkSpinButton *object, gpointer data)
{
  preferences_set_boolean (PREF_KEY_GRID_LENGTH,
			   gtk_spin_button_get_value (object));
  gtk_widget_queue_draw (waveform);
}

static void
editor_get_frame_at_position (gdouble x, guint *cursor_frame,
			      gdouble *rel_pos)
{
  guint width;
  guint start = editor_get_start_frame ();
  struct sample_info *sample_info = audio.sample.info;

  gtk_layout_get_size (GTK_LAYOUT (waveform), &width, NULL);
  x = x > width ? width : x < 0.0 ? 0.0 : x;
  *cursor_frame = (sample_info->frames - 1) * (x / (gdouble) width);
  if (rel_pos)
    {
      *rel_pos = (*cursor_frame - start) /
	(sample_info->frames / (double) zoom);
    }
}

static gdouble
editor_get_max_zoom ()
{
  struct sample_info *sample_info = audio.sample.info;
  guint w = gtk_widget_get_allocated_width (waveform_scrolled_window);
  gdouble max_zoom = sample_info->frames / (double) w;
  return max_zoom < 1 ? 1 : max_zoom;
}

static gboolean
editor_zoom (GdkEventScroll *event, gdouble dy)
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

  g_mutex_lock (&audio.control.controllable.mutex);

  sample_info = audio.sample.info;
  if (!sample_info)
    {
      err = FALSE;
      goto end;
    }

  editor_get_frame_at_position (event->x, &cursor_frame, &rel_pos);
  debug_print (1, "Zooming at frame %d...", cursor_frame);

  if (dy == -1.0)
    {
      gdouble max_zoom = editor_get_max_zoom ();
      if (zoom == max_zoom)
	{
	  goto end;
	}
      zoom = zoom * 2.0;
      if (zoom > max_zoom)
	{
	  zoom = max_zoom;
	}
    }
  else
    {
      if (zoom == 1)
	{
	  goto end;
	}
      zoom = zoom * 0.5;
      if (zoom < 1.0)
	{
	  zoom = 1.0;
	}
    }

  debug_print (1, "Setting zoom to %.2f...", zoom);

  start = cursor_frame - rel_pos * sample_info->frames / (gdouble) zoom;
  editor_set_start_frame (start);
  editor_reset_waveform_width ();

end:
  g_mutex_unlock (&audio.control.controllable.mutex);

  return err;
}

static gboolean
editor_waveform_scroll (GtkWidget *widget, GdkEventScroll *event,
			gpointer data)
{
  if (event->direction == GDK_SCROLL_SMOOTH)
    {
      gdouble dx, dy;
      gdk_event_get_scroll_deltas ((GdkEvent *) event, &dx, &dy);
      if (editor_zoom (event, dy))
	{
	  editor_clear_waveform_data ();
	  editor_set_waveform_data ();
	  gtk_widget_queue_draw (waveform);
	}
    }
  return FALSE;
}

static void
editor_on_size_allocate (GtkWidget *self, GtkAllocation *allocation,
			 gpointer data)
{
  struct sample_info *sample_info;
  guint start;

  g_mutex_lock (&audio.control.controllable.mutex);
  sample_info = audio.sample.info;
  if (!sample_info)
    {
      goto end;
    }

  start = editor_get_start_frame ();
  editor_set_start_frame (start);
  editor_reset_waveform_width ();
  editor_set_waveform_data_no_sync ();

end:
  g_mutex_unlock (&audio.control.controllable.mutex);
}

static gboolean
editor_loading_completed ()
{
  gboolean res;

  g_mutex_lock (&audio.control.controllable.mutex);
  res = editor_loading_completed_no_lock (NULL);
  g_mutex_unlock (&audio.control.controllable.mutex);

  return res;
}

static gboolean
editor_cursor_frame_over_frame (guint cursor_frame, guint frame)
{
  gdouble x_ratio = editor_get_x_ratio () / zoom;
  gdouble shift = x_ratio < 2 ? 2 : x_ratio * 2;
  return cursor_frame >= frame - shift && cursor_frame <= frame + shift;
}

static void
editor_set_cursor (const gchar *cursor_name)
{
  GdkDisplay *display = gdk_display_get_default ();
  GdkCursor *cursor = gdk_cursor_new_from_name (display,
						cursor_name);
  gdk_window_set_cursor (gtk_widget_get_window (waveform), cursor);
  g_object_unref (cursor);
}

static gboolean
editor_button_press (GtkWidget *widget, GdkEventButton *event, gpointer data)
{
  guint cursor_frame;
  guint32 sel_len;
  struct sample_info *sample_info;

  g_mutex_lock (&audio.control.controllable.mutex);

  if (!editor_loading_completed_no_lock (NULL))
    {
      goto end;
    }

  sample_info = audio.sample.info;
  if (!sample_info)
    {
      goto end;
    }

  sel_len = AUDIO_SEL_LEN;

  press_event_x = event->x;
  editor_get_frame_at_position (event->x, &cursor_frame, NULL);
  if (event->button == GDK_BUTTON_PRIMARY)
    {
      debug_print (2, "Pressing at frame %d...", cursor_frame);
      if (editor_cursor_frame_over_frame (cursor_frame,
					  sample_info->loop_start))
	{
	  debug_print (2, "Clicking on loop start...");
	  operation = EDITOR_OP_MOVE_LOOP_START;
	  editor_set_cursor ("col-resize");
	}
      else if (editor_cursor_frame_over_frame (cursor_frame,
					       sample_info->loop_end))
	{
	  debug_print (2, "Clicking on loop end...");
	  operation = EDITOR_OP_MOVE_LOOP_END;
	  editor_set_cursor ("col-resize");
	}
      else if (editor_cursor_frame_over_frame (cursor_frame,
					       audio.sel_start) && sel_len)
	{
	  debug_print (2, "Clicking on selection start...");
	  operation = EDITOR_OP_MOVE_SEL_START;
	  editor_set_cursor ("col-resize");
	}
      else if (editor_cursor_frame_over_frame (cursor_frame,
					       audio.sel_end) && sel_len)
	{
	  debug_print (2, "Clicking on selection end...");
	  operation = EDITOR_OP_MOVE_SEL_END;
	  editor_set_cursor ("col-resize");
	}
      else
	{
	  g_mutex_unlock (&audio.control.controllable.mutex);
	  audio_stop_playback ();
	  g_mutex_lock (&audio.control.controllable.mutex);
	  operation = EDITOR_OP_MOVE_SEL_END;
	  audio.sel_start = cursor_frame;
	  audio.sel_end = cursor_frame;
	  gtk_widget_grab_focus (waveform_scrolled_window);
	  gtk_widget_queue_draw (waveform);
	}
    }
  else if (event->button == GDK_BUTTON_SECONDARY)
    {
      GdkRectangle r;
      gboolean cursor_on_sel = sel_len > 0 &&
	cursor_frame >= audio.sel_start && cursor_frame < audio.sel_end;
      if (!cursor_on_sel)
	{
	  audio.sel_start = -1;
	  audio.sel_end = -1;
	}
      gtk_widget_set_sensitive (popover_delete_button, sel_len > 0);
      gtk_widget_set_sensitive (popover_undo_button, dirty);
      gtk_widget_set_sensitive (popover_save_button, dirty || cursor_on_sel);
      r.x = event->x;
      r.y = event->y;
      r.width = 1;
      r.height = 1;
      gtk_popover_set_pointing_to (GTK_POPOVER (popover), &r);
      gtk_popover_popup (GTK_POPOVER (popover));
    }

end:
  g_mutex_unlock (&audio.control.controllable.mutex);
  return FALSE;
}

static gboolean
editor_button_release (GtkWidget *widget, GdkEventButton *event,
		       gpointer data)
{
  if (!operation)
    {
      return FALSE;
    }

  if (operation == EDITOR_OP_MOVE_SEL_START ||
      operation == EDITOR_OP_MOVE_SEL_END)
    {
      gtk_widget_grab_focus (waveform_scrolled_window);

      if (press_event_x == event->x)
	{
	  debug_print (2, "Cleaning selection...");
	  audio.sel_start = -1;
	  audio.sel_end = -1;
	  gtk_widget_queue_draw (waveform);
	}
      else
	{
	  debug_print (2, "Selected range: [%" PRId64 " to %" PRId64 "]...",
		       audio.sel_start, audio.sel_end);

	  if (AUDIO_SEL_LEN)
	    {
	      gtk_widget_set_sensitive (popover_delete_button, TRUE);
	      if (preferences_get_boolean (PREF_KEY_AUTOPLAY) &&
		  audio_is_stopped ())
		{
		  editor_start_playback ();
		}
	    }
	}
    }

  operation = EDITOR_OP_NONE;

  return FALSE;
}

static gboolean
editor_motion_notify (GtkWidget *widget, GdkEventMotion *event, gpointer data)
{
  guint cursor_frame;
  guint32 sel_len;
  struct sample_info *sample_info;

  g_mutex_lock (&audio.control.controllable.mutex);

  sample_info = audio.sample.info;
  if (!sample_info)
    {
      goto end;
    }

  sel_len = AUDIO_SEL_LEN;

  editor_get_frame_at_position (event->x, &cursor_frame, NULL);

  if (operation == EDITOR_OP_MOVE_SEL_END)
    {
      if (cursor_frame > audio.sel_start)
	{
	  audio.sel_end = cursor_frame;
	}
      else
	{
	  operation = EDITOR_OP_MOVE_SEL_START;
	  audio.sel_end = audio.sel_start;
	  audio.sel_start = cursor_frame;
	}
      debug_print (2, "Setting selection to [%" PRId64 ", %" PRId64 "]...",
		   audio.sel_start, audio.sel_end);
    }
  else if (operation == EDITOR_OP_MOVE_SEL_START)
    {
      if (cursor_frame < audio.sel_end)
	{
	  audio.sel_start = cursor_frame;
	}
      else
	{
	  operation = EDITOR_OP_MOVE_SEL_END;
	  audio.sel_start = audio.sel_end;
	  audio.sel_end = cursor_frame;
	}
      debug_print (2, "Setting selection to [%" PRId64 ", %" PRId64 "]...",
		   audio.sel_start, audio.sel_end);
    }
  else if (operation == EDITOR_OP_MOVE_LOOP_START)
    {
      sample_info->loop_start = cursor_frame;
      debug_print (2, "Setting loop to [%d, %d]...",
		   sample_info->loop_start, sample_info->loop_end);
      dirty = TRUE;
    }
  else if (operation == EDITOR_OP_MOVE_LOOP_END)
    {
      sample_info->loop_end = cursor_frame;
      debug_print (2, "Setting loop to [%d, %d]...",
		   sample_info->loop_start, sample_info->loop_end);
      dirty = TRUE;
    }
  else
    {
      if (editor_cursor_frame_over_frame (cursor_frame,
					  sample_info->loop_start))
	{
	  editor_set_cursor ("col-resize");
	}
      else if (editor_cursor_frame_over_frame (cursor_frame,
					       sample_info->loop_end))
	{
	  editor_set_cursor ("col-resize");
	}
      else if (editor_cursor_frame_over_frame (cursor_frame,
					       audio.sel_start) && sel_len)
	{
	  editor_set_cursor ("col-resize");
	}
      else if (editor_cursor_frame_over_frame (cursor_frame,
					       audio.sel_end) && sel_len)
	{
	  editor_set_cursor ("col-resize");
	}
      else
	{
	  editor_set_cursor ("default");
	}
    }

  gtk_widget_queue_draw (waveform);

end:
  g_mutex_unlock (&audio.control.controllable.mutex);
  return FALSE;
}

static void
editor_delete_clicked (GtkWidget *object, gpointer data)
{
  enum audio_status status;
  guint32 sel_len;

  if (!editor_loading_completed ())
    {
      return;
    }

  sel_len = AUDIO_SEL_LEN;
  if (!sel_len)
    {
      return;
    }

  //As the playback pointer could be in the selected range, it's safer to stop.
  //Later, playback will be restarted.
  g_mutex_lock (&audio.control.controllable.mutex);
  status = audio.status;
  g_mutex_unlock (&audio.control.controllable.mutex);
  if (status == AUDIO_STATUS_PLAYING)
    {
      audio_stop_playback ();
    }

  audio_delete_range (audio.sel_start, sel_len);
  dirty = TRUE;

  editor_set_waveform_data ();
  gtk_widget_queue_draw (waveform);

  if (status == AUDIO_STATUS_PLAYING)
    {
      editor_start_playback ();
    }
}

static void
editor_undo_clicked (GtkWidget *object, gpointer data)
{
  if (audio.path)
    {
      //As there is only one undo level, it's enough to reload the sample.
      editor_start_load_thread (audio.path);
    }
  else
    {
      //This is a recording
      editor_reset (NULL);
    }
}

//This function does not need synchronized access as it is only called from
//editor_save which already provides this.

static gint
editor_save_with_format (const gchar *dst_path, struct idata *sample)
{
  gint err;
  struct sample_info *sample_info_src = &audio.sample_info_src;
  struct sample_info *sample_info = sample->info;

  if (sample_info->rate == sample_info_src->rate)
    {
      err = sample_save_to_file (dst_path, sample, NULL,
				 sample_info_src->format);
    }
  else
    {
      struct idata resampled;
      err = sample_reload (sample, &resampled, NULL, sample_info_src,
			   job_control_set_sample_progress);
      if (err)
	{
	  return err;
	}

      err = sample_save_to_file (dst_path, &resampled, NULL,
				 sample_info_src->format);
      idata_free (&resampled);
    }

  browser_load_dir_if_needed (browser);

  return err;
}

static void
editor_save (const gchar *name)
{
  gchar *path;
  guint32 sel_len;
  struct idata aux;
  GByteArray *selection = NULL;
  struct sample_info *aux_si;
  struct sample_info *sample_info = audio.sample.info;

  path = browser_get_name_path (browser, name);

  sel_len = AUDIO_SEL_LEN;

  debug_print (2, "Saving recording to %s...", path);

  if (sel_len)
    {
      guint fsize = SAMPLE_INFO_FRAME_SIZE (sample_info);
      guint start = audio.sel_start * fsize;
      guint len = sel_len * fsize;
      selection = g_byte_array_sized_new (len);
      g_byte_array_append (selection,
			   &audio.sample.content->data[start], len);

      aux_si = g_malloc (sizeof (struct sample_info));
      memcpy (aux_si, audio.sample.info, sizeof (struct sample_info));
      aux_si->frames = sel_len;
      aux_si->loop_start = sel_len - 1;
      aux_si->loop_end = aux_si->loop_start;

      idata_init (&aux, selection, NULL, aux_si);
    }

  if (audio.path)
    {
      if (sel_len)
	{
	  debug_print (2, "Saving selection to %s...", path);

	  aux.name = strdup (name);
	  editor_save_with_format (path, &aux);
	}
      else
	{
	  debug_print (2, "Saving changes to %s...", path);

	  g_free (audio.path);
	  g_free (audio.sample.name);
	  audio.path = path;
	  audio.sample.name = g_path_get_basename (path);
	  editor_save_with_format (audio.path, &audio.sample);
	}
    }
  else
    {
      memcpy (&audio.sample_info_src,
	      audio.sample.info, sizeof (struct sample_info));
      audio.sample_info_src.format |= SF_FORMAT_WAV;

      if (sel_len)
	{
	  //This does not set anything and leaves everything as if no sample would have been loaded.
	  debug_print (2, "Saving recorded selection to %s...", path);
	  aux.name = g_path_get_basename (path);
	  editor_save_with_format (path, &aux);
	}
      else
	{
	  //This sets everything as if a sample would have been loaded.
	  debug_print (2, "Saving recording to %s...", path);
	  audio.path = path;
	  audio.sample.name = g_path_get_basename (path);
	  editor_save_with_format (audio.path, &audio.sample);
	}
    }

  if (sel_len)
    {
      idata_free (&aux);
    }
}

static void
editor_save_accept_response (GtkDialog *dialog, gint response_id,
			     gpointer user_data)
{
  const gchar *name = user_data;

  gtk_widget_destroy (GTK_WIDGET (dialog));

  if (response_id == GTK_RESPONSE_ACCEPT)
    {
      editor_save (name);
    }
  else
    {
      gint name_sel_len = filename_get_lenght_without_ext (name);

      name_window_edit_text (_("Save Sample"),
			     browser->fs_ops->max_name_len, name, 0,
			     name_sel_len, editor_save_accept, NULL);
    }
}

static void
editor_save_accept (gpointer source, const gchar *name)
{
  GtkWidget *dialog;
  gchar *path = browser_get_name_path (browser, name);

  if (g_file_test (path, G_FILE_TEST_EXISTS))
    {
      dialog = gtk_message_dialog_new (main_window,
				       GTK_DIALOG_MODAL |
				       GTK_DIALOG_USE_HEADER_BAR,
				       GTK_MESSAGE_WARNING,
				       GTK_BUTTONS_NONE,
				       _("Replace file “%s”?"), path);
      gtk_dialog_add_buttons (GTK_DIALOG (dialog),
			      _("_Cancel"), GTK_RESPONSE_CANCEL,
			      _("_Replace"), GTK_RESPONSE_ACCEPT, NULL);
      gtk_dialog_set_default_response (GTK_DIALOG (dialog),
				       GTK_RESPONSE_ACCEPT);
      g_signal_connect (dialog, "response",
			G_CALLBACK (editor_save_accept_response),
			(gpointer) name);
      gtk_widget_set_visible (dialog, TRUE);
    }
  else
    {
      editor_save (name);
    }

  g_free (path);
}

static void
editor_save_clicked (GtkWidget *object, gpointer data)
{
  guint32 sel_len;
  gint name_sel_len;
  gchar name[PATH_MAX];

  g_mutex_lock (&audio.control.controllable.mutex);

  if (!editor_loading_completed_no_lock (NULL))
    {
      goto end;
    }

  sel_len = AUDIO_SEL_LEN;

  if (sel_len)
    {
      snprintf (name, PATH_MAX, "%s", "Sample.wav");
    }
  else
    {
      if (audio.path)
	{
	  gchar *basename = g_path_get_basename (audio.path);
	  snprintf (name, PATH_MAX, "%s", basename);
	  g_free (basename);
	}
      else
	{
	  GDateTime *dt = g_date_time_new_now_local ();
	  gchar *s = g_date_time_format (dt, DATE_TIME_FILENAME_FORMAT);
	  snprintf (name, PATH_MAX, "%s %s.wav", _("Audio"), s);
	  g_free (s);
	  g_date_time_unref (dt);
	}
    }

  name_sel_len = filename_get_lenght_without_ext (name);

  name_window_edit_text (_("Save Sample"),
			 browser->fs_ops->max_name_len, name, 0,
			 name_sel_len, editor_save_accept, NULL);

end:
  g_mutex_unlock (&audio.control.controllable.mutex);
}

static gboolean
editor_key_press (GtkWidget *widget, GdkEventKey *event, gpointer data)
{
  if (event->type != GDK_KEY_PRESS)
    {
      return FALSE;
    }

  if (event->keyval == GDK_KEY_Menu)
    {
      gtk_popover_popup (GTK_POPOVER (popover));
    }
  else if (event->keyval == GDK_KEY_space)
    {
      editor_play_clicked (NULL, NULL);
    }
  else if (event->keyval == GDK_KEY_Delete)
    {
      editor_delete_clicked (NULL, NULL);
    }
  else if (event->state & GDK_CONTROL_MASK && event->keyval == GDK_KEY_z &&
	   dirty)
    {
      editor_undo_clicked (NULL, NULL);
    }
  else if (event->state & GDK_CONTROL_MASK && event->keyval == GDK_KEY_s &&
	   dirty)
    {
      editor_save_clicked (NULL, NULL);
    }

  return TRUE;
}

static void
editor_update_audio_status (gpointer data)
{
  gboolean status = audio_check ();

  gtk_widget_set_sensitive (record_button, status);
  gtk_widget_set_sensitive (volume_button, status);

  elektroid_update_audio_status (status);
}

static void
editor_size_allocate (GtkWidget *self, GtkAllocation *allocation,
		      gpointer user_data)
{
  guint width;
  gboolean needs_refresh;

  debug_print (1, "Allocating waveform size...");

  g_mutex_lock (&mutex);
  width = gtk_widget_get_allocated_width (waveform);
  needs_refresh = waveform_width != width;
  if (needs_refresh)
    {
      editor_clear_waveform_data_no_sync ();
    }
  g_mutex_unlock (&mutex);

  if (needs_refresh)
    {
      editor_set_waveform_data ();
      gtk_widget_queue_draw (waveform);
    }
}

void
editor_set_visible (gboolean visible)
{
  gtk_widget_set_visible (editor_box, visible);
  editor_set_audio_mono_mix ();
}

void
editor_init (GtkBuilder *builder)
{
  editor_box = GTK_WIDGET (gtk_builder_get_object (builder, "editor_box"));
  waveform_scrolled_window =
    GTK_WIDGET (gtk_builder_get_object (builder, "waveform_scrolled_window"));
  waveform = GTK_WIDGET (gtk_builder_get_object (builder, "waveform"));
  play_button = GTK_WIDGET (gtk_builder_get_object (builder, "play_button"));
  stop_button = GTK_WIDGET (gtk_builder_get_object (builder, "stop_button"));
  loop_button = GTK_WIDGET (gtk_builder_get_object (builder, "loop_button"));
  record_button =
    GTK_WIDGET (gtk_builder_get_object (builder, "record_button"));
  autoplay_switch =
    GTK_WIDGET (gtk_builder_get_object (builder, "autoplay_switch"));
  mix_switch = GTK_WIDGET (gtk_builder_get_object (builder, "mix_switch"));
  volume_button =
    GTK_WIDGET (gtk_builder_get_object (builder, "volume_button"));
  mix_switch_box =
    GTK_WIDGET (gtk_builder_get_object (builder, "mix_switch_box"));
  grid_length_spin =
    GTK_WIDGET (gtk_builder_get_object (builder, "grid_length_spin"));
  show_grid_switch =
    GTK_WIDGET (gtk_builder_get_object (builder, "show_grid_switch"));

  notes_list_store =
    GTK_LIST_STORE (gtk_builder_get_object (builder, "notes_list_store"));
  g_object_ref (G_OBJECT (notes_list_store));

  popover =
    GTK_POPOVER_MENU (gtk_builder_get_object (builder, "editor_popover"));
  popover_play_button =
    GTK_WIDGET (gtk_builder_get_object
		(builder, "editor_popover_play_button"));
  popover_delete_button =
    GTK_WIDGET (gtk_builder_get_object
		(builder, "editor_popover_delete_button"));
  popover_undo_button =
    GTK_WIDGET (gtk_builder_get_object
		(builder, "editor_popover_undo_button"));
  popover_save_button =
    GTK_WIDGET (gtk_builder_get_object
		(builder, "editor_popover_save_button"));

  g_signal_connect (waveform, "draw", G_CALLBACK (editor_draw), NULL);
  gtk_widget_add_events (waveform, GDK_SCROLL_MASK);
  g_signal_connect (waveform, "scroll-event",
		    G_CALLBACK (editor_waveform_scroll), NULL);
  g_signal_connect (play_button, "clicked",
		    G_CALLBACK (editor_play_clicked), NULL);
  g_signal_connect (stop_button, "clicked",
		    G_CALLBACK (editor_stop_clicked), NULL);
  g_signal_connect (loop_button, "clicked",
		    G_CALLBACK (editor_loop_clicked), NULL);
  g_signal_connect (record_button, "clicked",
		    G_CALLBACK (editor_record_clicked), NULL);
  g_signal_connect (autoplay_switch, "state-set",
		    G_CALLBACK (editor_autoplay_clicked), NULL);
  g_signal_connect (mix_switch, "state-set",
		    G_CALLBACK (editor_mix_clicked), NULL);
  g_signal_connect (grid_length_spin, "value-changed",
		    G_CALLBACK (editor_grid_length_changed), NULL);
  g_signal_connect (show_grid_switch, "state-set",
		    G_CALLBACK (editor_show_grid_clicked), NULL);
  volume_changed_handler = g_signal_connect (volume_button,
					     "value_changed",
					     G_CALLBACK
					     (editor_set_volume), NULL);

  g_signal_connect (waveform_scrolled_window, "size-allocate",
		    G_CALLBACK (editor_on_size_allocate), NULL);
  gtk_widget_add_events (waveform, GDK_BUTTON_PRESS_MASK);
  g_signal_connect (waveform, "button-press-event",
		    G_CALLBACK (editor_button_press), NULL);
  gtk_widget_add_events (waveform, GDK_BUTTON_RELEASE_MASK);
  g_signal_connect (waveform, "button-release-event",
		    G_CALLBACK (editor_button_release), NULL);
  gtk_widget_add_events (waveform, GDK_POINTER_MOTION_MASK);
  g_signal_connect (waveform, "motion-notify-event",
		    G_CALLBACK (editor_motion_notify), NULL);
  g_signal_connect (waveform_scrolled_window, "key-press-event",
		    G_CALLBACK (editor_key_press), NULL);
  g_signal_connect (waveform, "size-allocate",
		    G_CALLBACK (editor_size_allocate), NULL);

  g_signal_connect (popover_play_button, "clicked",
		    G_CALLBACK (editor_play_clicked), NULL);
  g_signal_connect (popover_delete_button, "clicked",
		    G_CALLBACK (editor_delete_clicked), NULL);
  g_signal_connect (popover_undo_button, "clicked",
		    G_CALLBACK (editor_undo_clicked), NULL);
  g_signal_connect (popover_save_button, "clicked",
		    G_CALLBACK (editor_save_clicked), NULL);

  editor_loop_clicked (loop_button, NULL);
  gtk_switch_set_active (GTK_SWITCH (autoplay_switch),
			 preferences_get_boolean (PREF_KEY_AUTOPLAY));
  gtk_switch_set_active (GTK_SWITCH (mix_switch),
			 preferences_get_boolean (PREF_KEY_MIX));
  gtk_switch_set_active (GTK_SWITCH (show_grid_switch),
			 preferences_get_boolean (PREF_KEY_SHOW_GRID));

  gtk_spin_button_set_value (GTK_SPIN_BUTTON (grid_length_spin),
			     preferences_get_int (PREF_KEY_GRID_LENGTH));

  audio_init (editor_set_volume_callback, editor_update_audio_status, NULL);

  record_window_init (builder);

  g_mutex_init (&mutex);
  editor_reset (NULL);
}

void
editor_destroy ()
{
  gboolean wait;

  debug_print (1, "Destroying editor...");

  record_window_destroy ();

  wait = !editor_loading_completed () || !audio_is_stopped ();

  editor_stop_clicked (NULL, NULL);
  editor_stop_load_thread ();

  audio_destroy ();
  if (wait)
    {
      while (gtk_events_pending ())
	{
	  gtk_main_iteration_do (TRUE);	//Wait for drawings
	}
    }

  editor_clear_waveform_data ();
  editor_free_waveform_state ();

  g_object_unref (G_OBJECT (notes_list_store));
}

void
editor_reset_audio ()
{
  audio_destroy ();
  audio_init (editor_set_volume_callback, editor_update_audio_status, NULL);
  editor_reset (NULL);
  //Resetting the audio causes the edited sample to be cleared so that these are
  //needed to keep the selection consistent with the
  browser_clear_selection (&local_browser);
  browser_clear_selection (&remote_browser);
}
