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
#include "guirecorder.h"
#include "preferences.h"

struct editor
{
  struct audio audio;
  struct preferences *preferences;
  struct browser *local_browser;
  struct browser *remote_browser;
  enum audio_src audio_src;
  GThread *thread;
  GtkWidget *box;
  GtkWidget *waveform_scrolled_window;
  GtkWidget *waveform;
  GtkWidget *play_button;
  GtkWidget *stop_button;
  GtkWidget *loop_button;
  GtkWidget *record_button;
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
  GtkMenu *menu;
  GtkWidget *play_menuitem;
  GtkWidget *delete_menuitem;
  GtkWidget *save_menuitem;
  GtkDialog *record_dialog;
  struct guirecorder guirecorder;
  GtkWidget *record_dialog_cancel_button;
  GtkWidget *record_dialog_record_button;
  guint zoom;
  gboolean selecting;
  gboolean dirty;
};

extern struct editor editor;

void editor_set_source (struct editor *editor, enum audio_src audio_src);

void editor_play_clicked (GtkWidget * object, gpointer data);

void editor_start_load_thread (struct editor *editor);

void editor_stop_load_thread (struct editor *editor);

void editor_init (GtkBuilder * builder);

void editor_destroy (struct editor *);

void editor_set_audio_mono_mix (struct editor *editor);

#endif
