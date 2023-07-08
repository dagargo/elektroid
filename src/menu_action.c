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

struct menu_action *
menu_action_separator (struct backend *backend, GtkBuilder * builder,
		       GtkWindow * parent)
{
  struct menu_action *ma = g_malloc (sizeof (struct menu_action));
  ma->type = MENU_ACTION_SEPARATOR;
  return ma;
}

const t_menu_action_initializer MENU_ACTIONS[] = {
  microbrute_configuration_init, microbrute_calibration_init,
  menu_action_separator, rx_sysex_init, tx_sysex_init, menu_action_separator,
  os_upgrade_init, menu_action_separator, autosampler_init,
  menu_action_separator, NULL
};

static GSList *
ma_get_menu_actions (struct ma_data *ma_data, GtkWindow * parent)
{
  GSList *actions = NULL;
  const t_menu_action_initializer *initializer = MENU_ACTIONS;
  while (*initializer)
    {
      struct menu_action *ma = (*initializer) (ma_data->backend,
					       ma_data->builder, parent);
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
  if (ma->type == MENU_ACTION_ITEM)
    {
      ma_data->separator = TRUE;
      GtkWidget *button = gtk_model_button_new ();
      g_object_set (button, "text", ma->name, NULL);
      gtk_widget_show (button);
      gtk_container_add (GTK_CONTAINER (ma_data->box), button);
      g_signal_connect (button, "clicked", ma->callback, ma_data->backend);
    }
  else
    {
      if (ma_data->separator)
	{
	  GtkWidget *separator =
	    gtk_separator_new (GTK_ORIENTATION_HORIZONTAL);
	  gtk_container_add (GTK_CONTAINER (ma_data->box), separator);
	  gtk_widget_show (separator);
	}
      ma_data->separator = FALSE;
    }
}

void
ma_set_device_menu_actions (struct ma_data *ma_data, GtkWindow * parent)
{
  GSList *src = ma_get_menu_actions (ma_data, parent);
  ma_clear_device_menu_actions (ma_data->box);
  ma_data->separator = FALSE;
  g_slist_foreach (src, ma_add_device_menu_action, ma_data);
  g_slist_free_full (src, g_free);
}
