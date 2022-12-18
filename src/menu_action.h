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

typedef void (*f_menu_action) (struct backend *);

struct menu_action
{
  const gchar *name;
  f_menu_action action;
};

void ma_clear_device_menu_actions ();

void ma_set_device_menu_actions ();

#endif
