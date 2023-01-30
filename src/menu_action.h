/*
 *   actions.h
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

#ifndef MENU_ACTION_H
#define MENU_ACTION_H

#include <gtk/gtk.h>
#include "backend.h"

enum menu_action_type
{
  MENU_ACTION_ITEM,
  MENU_ACTION_SEPARATOR
};

struct menu_action
{
  enum menu_action_type type;
  const gchar *name;
  GCallback callback;
};

struct ma_data
{
  GtkWidget *box;
  struct backend *backend;
  gboolean separator;		//This does not need to be initialized as it's used internally.
};

typedef struct menu_action *(*t_menu_action_initializer) (struct backend *,
							  GtkWindow *);

void ma_clear_device_menu_actions (GtkWidget *);

void ma_set_device_menu_actions (struct ma_data *, GtkWindow *);

#endif
