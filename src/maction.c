/*
 *   maction.c
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

#include "maction.h"
#include "tags_window.h"

GSList *mactions = NULL;
GMenu *mactions_menu;

static GSList *
maction_context_build_all ()
{
  GSList *actions = NULL;
  GSList *i = mactions;
  struct maction *ma;

  while (i)
    {
      t_maction_builder builder = i->data;
      ma = builder (mactions_menu);
      if (ma)
	{
	  actions = g_slist_append (actions, ma);
	}
      i = i->next;
    }

  return actions;
}

void
maction_menu_clear ()
{
  g_menu_remove_all (mactions_menu);
}

static void
maction_add (gpointer data, gpointer user_data)
{
  struct maction *ma = data;
  g_menu_append (mactions_menu, ma->name, ma->action_name);
}

void
maction_menu_setup ()
{
  GSList *src = maction_context_build_all ();
  g_slist_foreach (src, maction_add, NULL);
  g_slist_free_full (src, g_free);
}
