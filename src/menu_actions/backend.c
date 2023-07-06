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
#include "menu_action.h"

//This is a bit of a hack as the backend function are implemented inside
//elektroid.c. However, as these actions depend on the backend initialization,
//it's convenient to implement the menus this way.
extern gpointer elektroid_tx_upgrade_os_runner (gpointer data);
extern gpointer elektroid_tx_sysex_files_runner (gpointer data);
extern void elektroid_tx_sysex_common (GThreadFunc func, gboolean multiple);
extern void elektroid_rx_sysex ();
extern void elektroid_refresh_devices (GtkWidget * object, gpointer data);

static void
os_upgrade_callback (GtkWidget * object, gpointer data)
{
  elektroid_tx_sysex_common (elektroid_tx_upgrade_os_runner, FALSE);
  elektroid_refresh_devices (NULL, NULL);
}

static void
tx_sysex_callback (GtkWidget * object, gpointer data)
{
  elektroid_tx_sysex_common (elektroid_tx_sysex_files_runner, TRUE);
}

static void
rx_sysex_callback (GtkWidget * object, gpointer data)
{
  elektroid_rx_sysex ();
}

struct menu_action *
os_upgrade_init (struct backend *backend, GtkBuilder * builder,
		 GtkWindow * parent)
{
  struct menu_action *ma = NULL;
  if (backend->upgrade_os)
    {
      ma = g_malloc (sizeof (struct menu_action));
      ma->type = MENU_ACTION_ITEM;
      ma->name = _("OS _Upgrade");
      ma->callback = G_CALLBACK (os_upgrade_callback);
    }
  return ma;
}

struct menu_action *
rx_sysex_init (struct backend *backend, GtkBuilder * builder,
	       GtkWindow * parent)
{
  struct menu_action *ma = NULL;
  if (backend->type == BE_TYPE_MIDI)
    {
      ma = g_malloc (sizeof (struct menu_action));
      ma->type = MENU_ACTION_ITEM;
      ma->name = _("_Receive SysEx");
      ma->callback = G_CALLBACK (rx_sysex_callback);
    }
  return ma;
}

struct menu_action *
tx_sysex_init (struct backend *backend, GtkBuilder * builder,
	       GtkWindow * parent)
{
  struct menu_action *ma = NULL;
  if (backend->type == BE_TYPE_MIDI)
    {
      ma = g_malloc (sizeof (struct menu_action));
      ma->type = MENU_ACTION_ITEM;
      ma->name = _("_Send SysEx");
      ma->callback = G_CALLBACK (tx_sysex_callback);
    }
  return ma;
}
