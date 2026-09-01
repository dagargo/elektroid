/*
 *   maction.h
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

#ifndef MACTION_H
#define MACTION_H

#include <gtk/gtk.h>
#include "backend.h"
#include "audio.h"

struct maction
{
  const gchar *name;
  const gchar *action_name;
};

extern GSList *mactions;
extern GMenu *mactions_menu;

typedef struct maction *(*t_maction_builder) (GMenu * menu);

void maction_menu_clear ();

void maction_menu_setup ();

#endif
