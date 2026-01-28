/*
 *   elektron.c
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

#include <glib/gi18n.h>
#include "browser.h"
#include "connectors/elektron.h"
#include "elektroid.h"
#include "maction.h"
#include "progress_window.h"

extern GtkWindow *main_window;
extern struct browser remote_browser;

static void
elektron_ram_purge_runner (gpointer data)
{
  elektron_ram_clear_unused_slots (remote_browser.backend);
}

static void
elektron_ram_purge_consumer (gpointer data)
{
  if (remote_browser.fs_ops->id == FS_DIGITAKT_RAM)
    {
      browser_load_dir (&remote_browser);
    }
}

static void
elektron_ram_purge_callback (GtkWidget *object, gpointer data)
{
  progress_window_open (elektron_ram_purge_runner,
			elektron_ram_purge_consumer, NULL, NULL,
			PROGRESS_TYPE_PULSE,
			_("Purging Unused RAM Slots"), "", FALSE);
}

struct maction *
elektron_ram_purge_builder (struct maction_context *context)
{
  GSList *list;
  struct maction *ma;
  gboolean ram_found;

  ram_found = FALSE;
  list = remote_browser.backend->fs_ops;
  while (list)
    {
      const struct fs_operations *fs_ops = list->data;
      if (fs_ops->id == FS_DIGITAKT_RAM)
	{
	  ram_found = TRUE;
	  break;
	}
      list = list->next;
    }

  if (ram_found)
    {
      ma = g_malloc (sizeof (struct maction));
      ma->type = MACTION_BUTTON;
      ma->name = _("_Purge Unused RAM Slots");
      ma->sensitive = TRUE;
      ma->callback = G_CALLBACK (elektron_ram_purge_callback);

      return ma;
    }
  else
    {
      return NULL;
    }
}
