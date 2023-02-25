/*
 *   editor.h
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

#ifndef EDITOR_H
#define EDITOR_H

#include "audio.h"
#include "browser.h"
#include "preferences.h"

struct editor
{
  struct audio audio;
  struct preferences *preferences;
  struct browser *remote_browser;
  GThread *thread;
  guint target_channels;	//Channels to load from file
  GtkWidget *box;
  GtkWidget *waveform_draw_area;
  GtkWidget *play_button;
  GtkWidget *stop_button;
  GtkWidget *loop_button;
  GtkWidget *autoplay_switch;
  GtkWidget *mix_switch;
  GtkWidget *volume_button;
  gulong volume_changed_handler;
  GtkWidget *sample_info_box;
  GtkWidget *sample_length;
  GtkWidget *sample_duration;
  GtkWidget *sample_channels;
  GtkWidget *sample_samplerate;
  GtkWidget *sample_bitdepth;
};

void editor_set_source (struct editor *editor, enum audio_src audio_src);

void editor_set_audio_controls_on_load (struct editor *editor,
					gboolean sensitive);

void editor_audio_widgets_set_status (struct editor *editor);

gboolean editor_draw_waveform (GtkWidget * widget, cairo_t * cr,
			       gpointer data);

void editor_redraw (struct job_control *control, gpointer data);

void editor_play_clicked (GtkWidget * object, gpointer data);

void editor_stop_clicked (GtkWidget * object, gpointer data);

void editor_loop_clicked (GtkWidget * object, gpointer data);

gboolean editor_autoplay_clicked (GtkWidget * object, gboolean state,
				  gpointer data);

gboolean editor_mix_clicked (GtkWidget * object, gboolean state,
			     gpointer data);

void editor_set_volume (GtkScaleButton * button, gdouble value,
			gpointer data);

void editor_set_volume_callback (gpointer editor, gdouble volume);

void editor_start_load_thread (struct editor *editor);

void editor_stop_load_thread (struct editor *editor);

#endif
