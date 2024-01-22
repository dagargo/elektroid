/*
 *   default.c
 *   Copyright (C) 2023 David García Goñi <dagargo@gmail.com>
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
#include "default.h"
#include "common.h"

#define DEFAULT_MAX_PROGRAMS 128

static gint
default_next_dentry (struct item_iterator *iter)
{
  guint *data = iter->data;

  if (*data >= DEFAULT_MAX_PROGRAMS)
    {
      return -ENOENT;
    }

  iter->item.id = *data;
  snprintf (iter->item.name, LABEL_MAX, "%d", *data);
  iter->item.type = ELEKTROID_FILE;
  iter->item.size = -1;
  iter->item.slot_used = TRUE;
  (*data)++;

  return 0;
}

static gint
default_read_dir (struct backend *backend, struct item_iterator *iter,
		  const gchar *path, const gchar **extensions)
{
  if (strcmp (path, "/"))
    {
      return -ENOTDIR;
    }

  guint *data = g_malloc (sizeof (guint));
  *data = 0;
  iter->data = data;
  iter->next = default_next_dentry;
  iter->free = g_free;
  return 0;
}

const struct fs_operations FS_PROGRAM_DEFAULT_OPERATIONS = {
  .fs = FS_PROGRAM_DEFAULT,
  .options = FS_OPTION_SINGLE_OP | FS_OPTION_SLOT_STORAGE |
    FS_OPTION_SORT_BY_ID,
  .name = "program",
  .gui_name = "Programs",
  .gui_icon = BE_FILE_ICON_SND,
  .readdir = default_read_dir,
  .print_item = common_print_item,
  .select_item = common_midi_program_change
};

static const struct fs_operations *FS_DEFAULT_OPERATIONS[] = {
  &FS_PROGRAM_DEFAULT_OPERATIONS, NULL
};

gint
default_handshake (struct backend *backend)
{
  backend_midi_handshake (backend);
  backend->filesystems = 0;
  backend->fs_ops = NULL;
  backend->filesystems = FS_PROGRAM_DEFAULT;
  backend->fs_ops = FS_DEFAULT_OPERATIONS;
  snprintf (backend->name, LABEL_MAX, "%s", _("MIDI device"));
  return 0;
}
