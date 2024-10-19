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

enum editor_operation
{
  EDITOR_OP_NONE,
  EDITOR_OP_MOVE_LOOP_START,
  EDITOR_OP_MOVE_LOOP_END,
  EDITOR_OP_MOVE_SEL_START,
  EDITOR_OP_MOVE_SEL_END
};

struct editor
{
  struct audio audio;
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
  GtkWidget *mix_switch_box;
  GtkWidget *grid_length_spin;
  GtkWidget *show_grid_switch;
  gulong volume_changed_handler;
  GtkListStore *notes_list_store;
  GtkWidget *popover;
  GtkWidget *popover_play_button;
  GtkWidget *popover_delete_button;
  GtkWidget *popover_undo_button;
  GtkWidget *popover_save_button;
  GtkWindow *record_window;
  GtkWidget *record_window_record_button;
  GtkWidget *record_window_cancel_button;
  struct guirecorder guirecorder;
  gdouble zoom;
  enum editor_operation operation;
  gboolean dirty;
  gboolean ready;
  struct browser *browser;
};

void editor_reset (struct editor *editor, struct browser *browser);

void editor_play_clicked (GtkWidget * object, gpointer data);

void editor_start_load_thread (struct editor *editor, gchar * sample_path);

void editor_stop_load_thread (struct editor *editor);

void editor_init (struct editor *editor, GtkBuilder * builder);

void editor_destroy (struct editor *);

void editor_set_audio_mono_mix (struct editor *editor);

void editor_reset_audio (struct editor *editor);

#endif
