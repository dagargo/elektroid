/*
 *   backend.c
 *   Copyright (C) 2022 David García Goñi <dagargo@gmail.com>
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

//This is a bit of a hack as the backend function are implemented inside
//elektroid.c. However, as these actions depend on the backend initialization,
//it's convenient to implement the menus this way.
extern gpointer elektroid_tx_upgrade_os_runner (gpointer data);
extern gpointer elektroid_tx_sysex_files_runner (gpointer data);
extern void elektroid_tx_sysex_common (GThreadFunc func, gboolean multiple);
extern void elektroid_rx_sysex ();
extern void elektroid_refresh_devices (gboolean startup);

static void
os_upgrade_callback (GtkWidget *object, gpointer data)
{
  elektroid_tx_sysex_common (elektroid_tx_upgrade_os_runner, FALSE);
  elektroid_refresh_devices (FALSE);
}

static void
tx_sysex_callback (GtkWidget *object, gpointer data)
{
  elektroid_tx_sysex_common (elektroid_tx_sysex_files_runner, TRUE);
}

static void
rx_sysex_callback (GtkWidget *object, gpointer data)
{
  elektroid_rx_sysex ();
}

struct maction *
backend_maction_os_upgrade_builder (struct maction_context *context)
{
  struct maction *ma = NULL;
  if (context->backend->upgrade_os)
    {
      ma = g_malloc (sizeof (struct maction));
      ma->type = MACTION_BUTTON;
      ma->name = _("OS _Upgrade");
      ma->sensitive = TRUE;
      ma->callback = G_CALLBACK (os_upgrade_callback);
    }
  return ma;
}

struct maction *
backend_maction_rx_sysex_builder (struct maction_context *context)
{
  struct maction *ma = NULL;
  if (context->backend->type == BE_TYPE_MIDI)
    {
      ma = g_malloc (sizeof (struct maction));
      ma->type = MACTION_BUTTON;
      ma->name = _("_Receive SysEx");
      ma->sensitive = TRUE;
      ma->callback = G_CALLBACK (rx_sysex_callback);
    }
  return ma;
}

struct maction *
backend_maction_tx_sysex_builder (struct maction_context *context)
{
  struct maction *ma = NULL;
  if (context->backend->type == BE_TYPE_MIDI)
    {
      ma = g_malloc (sizeof (struct maction));
      ma->type = MACTION_BUTTON;
      ma->name = _("_Send SysEx");
      ma->sensitive = TRUE;
      ma->callback = G_CALLBACK (tx_sysex_callback);
    }
  return ma;
}
