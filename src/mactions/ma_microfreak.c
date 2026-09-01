/*
 *   microfreak.c
 *   Copyright (C) 2024 David García Goñi <dagargo@gmail.com>
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
#include "browser.h"
#include "connectors/microfreak.h"
#include "elektroid.h"
#include "maction.h"
#include "progress_window.h"

extern GtkWindow *main_window;
extern struct browser remote_browser;

static void
ma_microfreak_defragment_runner (gpointer data)
{
  microfreak_sample_defragment (remote_browser.backend);
}

static void
ma_microfreak_defragment_consumer (gpointer data)
{
  //Some operations fail unless everything is initiated again.
  elektroid_refresh_devices ();
}

static void
ma_microfreak_defragment_window_open ()
{
  progress_window_open (ma_microfreak_defragment_runner,
			ma_microfreak_defragment_consumer, NULL, NULL,
			PROGRESS_TYPE_PULSE,
			_("Defragmenting Sample Memory"), "", FALSE);
}

static void
ma_microfreak_defragment_response (GtkDialog *dialog, gint response_id,
				   gpointer user_data)
{
  if (response_id == GTK_RESPONSE_ACCEPT)
    {
      ma_microfreak_defragment_window_open ();
    }

  gtk_window_destroy (GTK_WINDOW (dialog));
}

static void
ma_microfreak_defragment (GSimpleAction *simple_action, GVariant *parameter,
			  gpointer user_data)
{
  if (preferences_get_boolean (PREF_KEY_USE_SAFETY_QUESTIONS))
    {
      GtkWidget *dialog = gtk_message_dialog_new (GTK_WINDOW (main_window),
						  GTK_DIALOG_MODAL,
						  GTK_MESSAGE_ERROR,
						  GTK_BUTTONS_NONE,
						  _
						  ("The defragmentation process could take several minutes and could not be canceled. Are you sure you want to defragment the sample memory?"));
      gtk_dialog_add_buttons (GTK_DIALOG (dialog), _("_Cancel"),
			      GTK_RESPONSE_CANCEL, _("_Defragment"),
			      GTK_RESPONSE_ACCEPT, NULL);
      gtk_dialog_set_default_response (GTK_DIALOG (dialog),
				       GTK_RESPONSE_CANCEL);

      g_signal_connect (dialog, "response",
			G_CALLBACK (ma_microfreak_defragment_response), NULL);

      gtk_widget_set_visible (dialog, TRUE);
    }
  else
    {
      ma_microfreak_defragment_window_open ();
    }
}

static const GActionEntry MICROFREAK_ENTRIES[] = {
  {"microfreak_defragment", ma_microfreak_defragment, NULL, NULL, NULL}
};

void
ma_microfreak_init (GtkApplication *app)
{
  g_action_map_add_action_entries (G_ACTION_MAP (app), MICROFREAK_ENTRIES,
				   G_N_ELEMENTS (MICROFREAK_ENTRIES), app);
}

struct maction *
ma_microfreak_defrag_builder ()
{
  struct maction *ma;

  if (!remote_browser.backend->conn_name ||
      strcmp (remote_browser.backend->conn_name, MICROFREAK_NAME))
    {
      return NULL;
    }

  ma = g_malloc (sizeof (struct maction));
  ma->name = _("_Defragment");
  ma->action_name = "app.microfreak_defragment";

  return ma;
}
