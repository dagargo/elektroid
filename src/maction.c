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

GSList *mactions = NULL;
struct maction_context maction_context;

struct maction *
maction_separator_builder (struct maction_context *context)
{
  struct maction *ma = g_malloc (sizeof (struct maction));
  ma->type = MACTION_SEPARATOR;
  return ma;
}

static GSList *
maction_context_build_all (struct maction_context *context)
{
  GSList *actions = NULL;
  GSList *i = mactions;
  struct maction *ma;

  while (i)
    {
      t_maction_builder builder = i->data;
      ma = builder (context);
      if (ma)
	{
	  actions = g_slist_append (actions, ma);
	}
      i = i->next;
    }

  ma = maction_separator_builder (context);
  actions = g_slist_append (actions, ma);

  return actions;
}

static void
maction_remove_widget (GtkWidget *widget, gpointer data)
{
  struct maction_context *context = data;
  gtk_container_remove (GTK_CONTAINER (context->box), widget);
}

void
maction_menu_clear (struct maction_context *context)
{
  gtk_container_foreach (GTK_CONTAINER (context->box), maction_remove_widget,
			 context);
}

static void
maction_add (gpointer data, gpointer user_data)
{
  struct maction *ma = data;
  struct maction_context *context = user_data;
  if (ma->type == MACTION_BUTTON)
    {
      context->separator = TRUE;
      GtkWidget *button = gtk_model_button_new ();
      g_object_set (button, "text", ma->name, NULL);
      gtk_widget_set_sensitive (button, ma->sensitive);
      gtk_widget_show (button);
      gtk_container_add (GTK_CONTAINER (context->box), button);
      g_signal_connect (button, "clicked", ma->callback, context);
    }
  else
    {
      if (context->separator)
	{
	  GtkWidget *separator =
	    gtk_separator_new (GTK_ORIENTATION_HORIZONTAL);
	  gtk_container_add (GTK_CONTAINER (context->box), separator);
	  gtk_widget_show (separator);
	}
      context->separator = FALSE;
    }
}

void
maction_menu_setup (struct maction_context *context)
{
  GSList *src = maction_context_build_all (context);
  context->separator = FALSE;
  g_slist_foreach (src, maction_add, context);
  g_slist_free_full (src, g_free);
}
