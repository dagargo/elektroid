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
#include "maction.h"
#include "../progress.h"
#include "../browser.h"
#include "connectors/microfreak.h"

extern GtkWidget *main_window;
extern struct browser remote_browser;

static gboolean
microfreak_refresh (gpointer data)
{
  browser_refresh (NULL, &remote_browser);
  return FALSE;
}

static gpointer
microfreak_defragment_runner (gpointer data)
{
  struct backend *backend = data;
  progress.sysex_transfer.active = TRUE;
  microfreak_sample_defragment (backend);
  progress_end ();
  g_idle_add (microfreak_refresh, NULL);
  return NULL;
}

static void
microfreak_defragment_callback_defragment (GObject *source_object,
					   GAsyncResult *res, gpointer data)
{
  struct backend *backend = data;
  GtkAlertDialog *dialog = GTK_ALERT_DIALOG (source_object);
  int button = gtk_alert_dialog_choose_finish (dialog, res, NULL);

  g_object_unref (source_object);

  if (button == 1)
    {
      progress_run (microfreak_defragment_runner, PROGRESS_TYPE_PULSE,
		    backend, _("Defragmenting Sample Memory"), NULL, FALSE,
		    NULL);
    }
}

static void
microfreak_defragment_callback (GtkWidget *object, gpointer data)
{
  const char *buttons[] = { "_Cancel", "_Defragment", NULL };
  GtkAlertDialog *dialog =
    gtk_alert_dialog_new (_
			  ("The defragmentation process could take several minutes and could not be canceled. Are you sure you want to defragment the sample memory?"));
  gtk_alert_dialog_set_buttons (dialog, buttons);
  gtk_alert_dialog_choose (dialog, GTK_WINDOW (main_window), NULL,
			   microfreak_defragment_callback_defragment, data);
}

struct maction *
microfreak_maction_defrag_builder (struct maction_context *context)
{
  struct maction *ma;

  if (!context->backend->conn_name ||
      strcmp (context->backend->conn_name, MICROFREAK_NAME))
    {
      return NULL;
    }

  ma = g_malloc (sizeof (struct maction));
  ma->type = MACTION_BUTTON;
  ma->name = _("_Defragment");
  ma->sensitive = TRUE;
  ma->callback = G_CALLBACK (microfreak_defragment_callback);

  return ma;
}
