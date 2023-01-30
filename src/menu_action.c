/*
 *   actions.c
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

#include "menu_action.h"

struct menu_action *os_upgrade_init (struct backend *);
struct menu_action *rx_sysex_init (struct backend *);
struct menu_action *tx_sysex_init (struct backend *);

typedef struct menu_action *(*t_menu_action_initializer) (struct backend *);

const t_menu_action_initializer MENU_ACTIONS[] = {
  os_upgrade_init, rx_sysex_init, tx_sysex_init, NULL
};

static GSList *
ma_get_menu_actions (struct ma_data *ma_data)
{
  GSList *actions = NULL;
  const t_menu_action_initializer *initializer = MENU_ACTIONS;
  while (*initializer)
    {
      struct menu_action *ma = (*initializer) (ma_data->backend);
      if (ma)
	{
	  actions = g_slist_append (actions, ma);
	}
      initializer++;
    }

  return actions;
}

static void
ma_remove_device_menu_action (GtkWidget * widget, gpointer data)
{
  GtkWidget *box = data;
  gtk_container_remove (GTK_CONTAINER (box), widget);
}

void
ma_clear_device_menu_actions (GtkWidget * box)
{
  gtk_container_foreach (GTK_CONTAINER (box),
			 ma_remove_device_menu_action, box);
}

static void
ma_add_device_menu_action (gpointer data, gpointer user_data)
{
  struct menu_action *ma = data;
  struct ma_data *ma_data = user_data;
  GtkWidget *button = gtk_model_button_new ();
  g_object_set (button, "text", ma->name, NULL);
  gtk_widget_show (button);
  gtk_container_add (GTK_CONTAINER (ma_data->box), button);
  g_signal_connect (button, "clicked", ma->callback, ma_data->backend);
}

void
ma_set_device_menu_actions (struct ma_data *ma_data)
{
  GSList *src = ma_get_menu_actions (ma_data);
  ma_clear_device_menu_actions (ma_data->box);
  g_slist_foreach (src, ma_add_device_menu_action, ma_data);
  if (src)
    {
      GtkWidget *separator = gtk_separator_new (GTK_ORIENTATION_HORIZONTAL);
      gtk_container_add (GTK_CONTAINER (ma_data->box), separator);
      gtk_widget_show (separator);
    }
  g_slist_free_full (src, g_free);
}
