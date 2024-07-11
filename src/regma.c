/*
 *   regma.c
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

#include "regma.h"

struct menu_action *os_upgrade_init (struct backend *, GtkBuilder *,
				     GtkWindow *);
struct menu_action *rx_sysex_init (struct backend *, GtkBuilder *,
				   GtkWindow *);
struct menu_action *tx_sysex_init (struct backend *, GtkBuilder *,
				   GtkWindow *);
struct menu_action *microbrute_configuration_init (struct backend *,
						   GtkBuilder *, GtkWindow *);
struct menu_action *microbrute_calibration_init (struct backend *,
						 GtkBuilder *, GtkWindow *);
struct menu_action *autosampler_init (struct backend *, GtkBuilder *,
				      GtkWindow *);

void
regma_fill ()
{
  g_slist_fill (&menu_actions,
		microbrute_configuration_init, microbrute_calibration_init,
		menu_action_separator, rx_sysex_init, tx_sysex_init,
		menu_action_separator, os_upgrade_init, menu_action_separator,
		autosampler_init, menu_action_separator, NULL);
}

void
regma_clean ()
{
  g_slist_free (menu_actions);
  menu_actions = NULL;
}
