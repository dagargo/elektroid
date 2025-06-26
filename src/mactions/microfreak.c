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

extern GtkWidget *dialog;
extern struct browser remote_browser;

static gpointer
microfreak_defragment_runner (gpointer data)
{
  struct backend *backend = data;
  progress.sysex_transfer.active = TRUE;
  microfreak_sample_defragment (backend);
  progress_response (GTK_RESPONSE_ACCEPT);
  return NULL;
}

static void
microfreak_defragment_callback (GtkWidget *object, gpointer data)
{
  gint res;
  struct maction_context *context = data;

  dialog = gtk_message_dialog_new (GTK_WINDOW (context->parent),
				   GTK_DIALOG_MODAL,
				   GTK_MESSAGE_ERROR,
				   GTK_BUTTONS_NONE,
				   _
				   ("The defragmentation process could take several minutes and could not be canceled. Are you sure you want to defragment the sample memory?"));
  gtk_dialog_add_buttons (GTK_DIALOG (dialog), _("_Cancel"),
			  GTK_RESPONSE_CANCEL, _("_Defragment"),
			  GTK_RESPONSE_ACCEPT, NULL);
  gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_CANCEL);
  res = gtk_dialog_run (GTK_DIALOG (dialog));
  gtk_widget_destroy (dialog);
  dialog = NULL;

  if (res != GTK_RESPONSE_ACCEPT)
    {
      return;
    }

  progress_run (microfreak_defragment_runner, PROGRESS_TYPE_PULSE,
		remote_browser.backend, _("Defragmenting Sample Memory"),
		NULL, FALSE, NULL);

  browser_refresh (NULL, &remote_browser);
}

struct maction *
microfreak_maction_defrag_builder (struct maction_context *context)
{
  struct maction *ma;

  if (!remote_browser.backend->conn_name ||
      strcmp (remote_browser.backend->conn_name, MICROFREAK_NAME))
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
