/*
 *   microbrute.c
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
#include "maction.h"
#include "browser.h"
#include "connectors/microbrute.h"
#include "utils.h"

extern GtkWindow *main_window;
extern struct browser remote_browser;

static guint8 channel;
static GtkWidget *config_window;
static GtkWidget *calibration_assistant;
static GtkWidget *note_priority;
static GtkWidget *vel_response;
static GtkWidget *lfo_key_retrigger;
static GtkWidget *envelope_legato;
static GtkWidget *bend_range;
static GtkWidget *gate_length;
static GtkWidget *synchronization;
static GtkWidget *tx_channel;
static GtkWidget *rx_channel;
static GtkWidget *retriggering;
static GtkWidget *play;
static GtkWidget *next_sequence;
static GtkWidget *step_on;
static GtkWidget *step_length;
static GtkWidget *persistent_changes;
static gboolean loading;

static void
microbrute_set_combo_value (GtkWidget *combo, guint8 value)
{
  guint v;
  gint index = 0;
  GtkTreeIter iter;
  gboolean found = FALSE;
  GtkTreeModel *model = gtk_combo_box_get_model (GTK_COMBO_BOX (combo));
  gboolean valid = gtk_tree_model_get_iter_first (model, &iter);

  while (valid)
    {
      gtk_tree_model_get (model, &iter, 1, &v, -1);
      if (v == value)
	{
	  found = TRUE;
	  break;

	}
      valid = gtk_tree_model_iter_next (model, &iter);
      index += 1;
    }
  if (found)
    {
      gtk_combo_box_set_active (GTK_COMBO_BOX (combo), index);
    }
}

static void
microbrute_configure_callback (GtkWidget *object, gpointer data)
{
  guint8 v;

  loading = TRUE;
  microbrute_get_parameter (remote_browser.backend, MICROBRUTE_NOTE_PRIORITY,
			    &v);
  microbrute_set_combo_value (note_priority, v);
  microbrute_get_parameter (remote_browser.backend, MICROBRUTE_VEL_RESPONSE,
			    &v);
  microbrute_set_combo_value (vel_response, v);

  microbrute_get_parameter (remote_browser.backend,
			    MICROBRUTE_LFO_KEY_RETRIGGER, &v);
  gtk_switch_set_state (GTK_SWITCH (lfo_key_retrigger), v);
  gtk_switch_set_active (GTK_SWITCH (lfo_key_retrigger), v);
  microbrute_get_parameter (remote_browser.backend,
			    MICROBRUTE_ENVELOPE_LEGATO, &v);
  gtk_switch_set_state (GTK_SWITCH (envelope_legato), v);
  gtk_switch_set_active (GTK_SWITCH (envelope_legato), v);
  microbrute_get_parameter (remote_browser.backend, MICROBRUTE_BEND_RANGE,
			    &v);
  gtk_spin_button_set_value (GTK_SPIN_BUTTON (bend_range), v);
  microbrute_get_parameter (remote_browser.backend, MICROBRUTE_STEP_LENGTH,
			    &v);
  microbrute_set_combo_value (step_length, v);
  microbrute_get_parameter (remote_browser.backend, MICROBRUTE_GATE_LENGTH,
			    &v);
  microbrute_set_combo_value (gate_length, v);

  microbrute_get_parameter (remote_browser.backend, MICROBRUTE_TX_CHANNEL,
			    &v);
  microbrute_set_combo_value (tx_channel, v);
  microbrute_get_parameter (remote_browser.backend, MICROBRUTE_RX_CHANNEL,
			    &channel);
  microbrute_set_combo_value (rx_channel, channel);

  microbrute_get_parameter (remote_browser.backend, MICROBRUTE_PLAY_ON, &v);
  microbrute_set_combo_value (play, v);
  microbrute_get_parameter (remote_browser.backend, MICROBRUTE_RETRIGGERING,
			    &v);
  microbrute_set_combo_value (retriggering, v);
  microbrute_get_parameter (remote_browser.backend, MICROBRUTE_NEXT_SEQUENCE,
			    &v);
  microbrute_set_combo_value (next_sequence, v);
  microbrute_get_parameter (remote_browser.backend, MICROBRUTE_STEP_ON, &v);
  microbrute_set_combo_value (step_on, v);
  microbrute_get_parameter (remote_browser.backend, MICROBRUTE_SYNC, &v);
  microbrute_set_combo_value (synchronization, v);
  loading = FALSE;

  gtk_widget_show (config_window);
}

static void
microbrute_combo_changed (GtkComboBox *combo, guint8 param)
{
  guint value;
  GtkTreeIter iter;
  gboolean sysex;
  GtkTreeModel *model;
  if (!loading)
    {
      model = gtk_combo_box_get_model (combo);
      sysex = gtk_switch_get_active (GTK_SWITCH (persistent_changes));
      gtk_combo_box_get_active_iter (combo, &iter);
      gtk_tree_model_get (model, &iter, 1, &value, -1);
      microbrute_set_parameter (remote_browser.backend, param, value, channel,
				sysex);
    }
}

static gboolean
microbrute_switch_state_set (guint8 param, guint8 state)
{
  gboolean sysex;
  if (!loading)
    {
      sysex = gtk_switch_get_active (GTK_SWITCH (persistent_changes));
      microbrute_set_parameter (remote_browser.backend, param, state, channel,
				sysex);
    }
  return FALSE;
}

static void
microbrute_note_priority_changed (GtkComboBox *combo, gpointer data)
{
  microbrute_combo_changed (combo, MICROBRUTE_NOTE_PRIORITY);
}

static void
microbrute_vel_response_changed (GtkComboBox *combo, gpointer data)
{
  microbrute_combo_changed (combo, MICROBRUTE_VEL_RESPONSE);
}

static gboolean
microbrute_lfo_key_retrigger_state_set (GtkSwitch *s, gboolean state,
					gpointer data)
{
  return microbrute_switch_state_set (MICROBRUTE_LFO_KEY_RETRIGGER, state);
}

static gboolean
microbrute_envelope_legato_state_set (GtkSwitch *s, gboolean state,
				      gpointer data)
{
  return microbrute_switch_state_set (MICROBRUTE_ENVELOPE_LEGATO, state);
}

static void
microbrute_bend_range_value_changed (GtkSpinButton *spin, gpointer data)
{
  gboolean sysex;
  guint8 value;
  if (!loading)
    {
      sysex = gtk_switch_get_active (GTK_SWITCH (persistent_changes));
      value = gtk_spin_button_get_value (spin);
      microbrute_set_parameter (remote_browser.backend, MICROBRUTE_BEND_RANGE,
				value, channel, sysex);
    }
}

static void
microbrute_gate_length_changed (GtkComboBox *combo, gpointer data)
{
  microbrute_combo_changed (combo, MICROBRUTE_GATE_LENGTH);
}

static void
microbrute_synchronization_changed (GtkComboBox *combo, gpointer data)
{
  microbrute_combo_changed (combo, MICROBRUTE_SYNC);
}

static void
microbrute_tx_channel_changed (GtkComboBox *combo, gpointer data)
{
  microbrute_combo_changed (combo, MICROBRUTE_TX_CHANNEL);
}

static void
microbrute_rx_channel_changed (GtkComboBox *combo, gpointer data)
{
  microbrute_combo_changed (combo, MICROBRUTE_RX_CHANNEL);
}

static void
microbrute_play_changed (GtkComboBox *combo, gpointer data)
{
  microbrute_combo_changed (combo, MICROBRUTE_PLAY_ON);
}

static void
microbrute_retriggering_changed (GtkComboBox *combo, gpointer data)
{
  microbrute_combo_changed (combo, MICROBRUTE_RETRIGGERING);
}

static void
microbrute_next_sequence_changed (GtkComboBox *combo, gpointer data)
{
  microbrute_combo_changed (combo, MICROBRUTE_NEXT_SEQUENCE);
}

static void
microbrute_step_on_changed (GtkComboBox *combo, gpointer data)
{
  microbrute_combo_changed (combo, MICROBRUTE_STEP_ON);
}

static void
microbrute_step_length_changed (GtkComboBox *combo, gpointer data)
{
  microbrute_combo_changed (combo, MICROBRUTE_STEP_LENGTH);
}

static void
microbrute_assistant_close (GtkWidget *assistant, gpointer data)
{
  gtk_widget_hide (assistant);
}

static void
microbrute_assistant_prepare (GtkAssistant *assistant, GtkWidget *page,
			      gpointer data)
{
  gint npage = gtk_assistant_get_current_page (assistant);
  switch (npage)
    {
    case 2:
      microbrute_set_parameter (remote_browser.backend,
				MICROBRUTE_CALIB_PB_CENTER, 0, channel, TRUE);
      break;
    case 3:
      microbrute_set_parameter (remote_browser.backend,
				MICROBRUTE_CALIB_BOTH_BOTTOM, 0, channel,
				TRUE);
      break;
    case 4:
      microbrute_set_parameter (remote_browser.backend,
				MICROBRUTE_CALIB_BOTH_TOP, 0, channel, TRUE);
      sleep (1);
      microbrute_set_parameter (remote_browser.backend, MICROBRUTE_CALIB_END,
				0, channel, TRUE);
      break;
    }
}

void
microbrute_init ()
{
  GtkBuilder *builder = gtk_builder_new ();
  gtk_builder_add_from_file (builder, DATADIR "/microbrute/microbrute.ui",
			     NULL);
  config_window = GTK_WIDGET (gtk_builder_get_object (builder,
						      "config_window"));
  gtk_window_resize (GTK_WINDOW (config_window), 1, 1);
  gtk_window_set_transient_for (GTK_WINDOW (config_window), main_window);

  note_priority =
    GTK_WIDGET (gtk_builder_get_object (builder, "note_priority"));
  vel_response =
    GTK_WIDGET (gtk_builder_get_object (builder, "vel_response"));

  lfo_key_retrigger =
    GTK_WIDGET (gtk_builder_get_object (builder, "lfo_key_retrigger"));
  envelope_legato =
    GTK_WIDGET (gtk_builder_get_object (builder, "envelope_legato"));
  bend_range = GTK_WIDGET (gtk_builder_get_object (builder, "bend_range"));
  gate_length = GTK_WIDGET (gtk_builder_get_object (builder, "gate_length"));
  synchronization =
    GTK_WIDGET (gtk_builder_get_object (builder, "synchronization"));

  tx_channel = GTK_WIDGET (gtk_builder_get_object (builder, "tx_channel"));
  rx_channel = GTK_WIDGET (gtk_builder_get_object (builder, "rx_channel"));

  play = GTK_WIDGET (gtk_builder_get_object (builder, "play"));
  retriggering =
    GTK_WIDGET (gtk_builder_get_object (builder, "retriggering"));
  next_sequence =
    GTK_WIDGET (gtk_builder_get_object (builder, "next_sequence"));
  step_on = GTK_WIDGET (gtk_builder_get_object (builder, "step_on"));
  step_length = GTK_WIDGET (gtk_builder_get_object (builder, "step_length"));

  persistent_changes =
    GTK_WIDGET (gtk_builder_get_object (builder, "persistent_changes"));

  g_signal_connect (note_priority, "changed",
		    G_CALLBACK (microbrute_note_priority_changed), NULL);
  g_signal_connect (note_priority, "changed",
		    G_CALLBACK (microbrute_vel_response_changed), NULL);

  g_signal_connect (lfo_key_retrigger, "state-set",
		    G_CALLBACK (microbrute_lfo_key_retrigger_state_set),
		    NULL);
  g_signal_connect (envelope_legato, "state-set",
		    G_CALLBACK (microbrute_envelope_legato_state_set), NULL);
  g_signal_connect (bend_range, "value-changed",
		    G_CALLBACK (microbrute_bend_range_value_changed), NULL);
  g_signal_connect (gate_length, "changed",
		    G_CALLBACK (microbrute_gate_length_changed), NULL);
  g_signal_connect (synchronization, "changed",
		    G_CALLBACK (microbrute_synchronization_changed), NULL);

  g_signal_connect (tx_channel, "changed",
		    G_CALLBACK (microbrute_tx_channel_changed), NULL);
  g_signal_connect (rx_channel, "changed",
		    G_CALLBACK (microbrute_rx_channel_changed), NULL);

  g_signal_connect (play, "changed",
		    G_CALLBACK (microbrute_play_changed), NULL);
  g_signal_connect (retriggering, "changed",
		    G_CALLBACK (microbrute_retriggering_changed), NULL);
  g_signal_connect (next_sequence, "changed",
		    G_CALLBACK (microbrute_next_sequence_changed), NULL);
  g_signal_connect (step_on, "changed",
		    G_CALLBACK (microbrute_step_on_changed), NULL);
  g_signal_connect (step_length, "changed",
		    G_CALLBACK (microbrute_step_length_changed), NULL);

  g_signal_connect (config_window, "delete-event",
		    G_CALLBACK (gtk_widget_hide_on_delete), NULL);

  //Assistant

  calibration_assistant = GTK_WIDGET (gtk_builder_get_object (builder,
							      "calibration_assistant"));
  gtk_window_set_transient_for (GTK_WINDOW (calibration_assistant),
				main_window);

  g_signal_connect (calibration_assistant, "close",
		    G_CALLBACK (microbrute_assistant_close), NULL);
  g_signal_connect (calibration_assistant, "cancel",
		    G_CALLBACK (microbrute_assistant_close), NULL);
  g_signal_connect (calibration_assistant, "escape",
		    G_CALLBACK (microbrute_assistant_close), NULL);
  g_signal_connect (calibration_assistant, "prepare",
		    G_CALLBACK (microbrute_assistant_prepare), NULL);

  g_object_unref (G_OBJECT (builder));
}

void
microbrute_destroy ()
{
  debug_print (1, "Destroying microbrute...");
  gtk_widget_destroy (calibration_assistant);
  gtk_widget_destroy (GTK_WIDGET (config_window));
}

struct maction *
microbrute_maction_conf_builder (struct maction_context *context)
{
  struct maction *ma;

  if (!remote_browser.backend->conn_name ||
      strcmp (remote_browser.backend->conn_name, MICROBRUTE_NAME))
    {
      return NULL;
    }

  ma = g_malloc (sizeof (struct maction));
  ma->type = MACTION_BUTTON;
  ma->name = _("_Configuration");
  ma->sensitive = TRUE;
  ma->callback = G_CALLBACK (microbrute_configure_callback);

  return ma;
}

static void
microbrute_calibration_callback (GtkWidget *object, gpointer data)
{
  gtk_widget_show (calibration_assistant);
}

struct maction *
microbrute_maction_cal_builder (struct maction_context *context)
{
  struct maction *ma;

  if (!remote_browser.backend->conn_name ||
      strcmp (remote_browser.backend->conn_name, MICROBRUTE_NAME))
    {
      return NULL;
    }

  ma = g_malloc (sizeof (struct maction));
  ma->type = MACTION_BUTTON;
  ma->name = _("_Calibration");
  ma->sensitive = TRUE;
  ma->callback = G_CALLBACK (microbrute_calibration_callback);

  return ma;
}
