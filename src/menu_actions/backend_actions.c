/*
 *   backend_actions.c
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
#include "backend_actions.h"

//This is a bit of a hack as the backend function are implemented inside
//elektroid.c. However, as these actions depend on the backend initialization,
//it's convenient to implement the menus this way.
extern gpointer elektroid_tx_upgrade_os_thread (gpointer data);
extern gpointer elektroid_tx_sysex_files_thread (gpointer data);
extern void elektroid_tx_sysex_common (GThreadFunc func, gboolean multiple);
extern void elektroid_check_backend ();
extern void elektroid_rx_sysex (GtkWidget * object, gpointer data);

static void
os_upgrade_action (struct backend *backend)
{
  elektroid_tx_sysex_common (elektroid_tx_upgrade_os_thread, FALSE);
  backend_destroy (backend);
  ma_clear_device_menu_actions ();
  elektroid_check_backend ();
}

static void
tx_sysex_action (struct backend *backend)
{
  elektroid_tx_sysex_common (elektroid_tx_sysex_files_thread, TRUE);
}

static void
rx_sysex_action (struct backend *backend)
{
  elektroid_rx_sysex (NULL, NULL);
}

struct menu_action *
os_upgrade_init (struct backend *backend)
{
  struct menu_action *ma = NULL;
  if (backend->upgrade_os)
    {
      ma = g_malloc (sizeof (struct menu_action));
      ma->name = _("OS _Upgrade");
      ma->action = os_upgrade_action;
    }
  return ma;
}

struct menu_action *
rx_sysex_init (struct backend *backend)
{
  struct menu_action *ma = NULL;
  if (backend->type == BE_TYPE_MIDI)
    {
      ma = g_malloc (sizeof (struct menu_action));
      ma->name = _("_Receive SysEx");
      ma->action = rx_sysex_action;
    }
  return ma;
}

struct menu_action *
tx_sysex_init (struct backend *backend)
{
  struct menu_action *ma = NULL;
  if (backend->type == BE_TYPE_MIDI)
    {
      ma = g_malloc (sizeof (struct menu_action));
      ma->name = _("_Send SysEx");
      ma->action = tx_sysex_action;
    }
  return ma;
}
