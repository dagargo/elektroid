/*
 *   connector.c
 *   Copyright (C) 2019 David García Goñi <dagargo@gmail.com>
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

#include "connector.h"

void
item_iterator_init (struct item_iterator *iter, const gchar *dir, void *data,
		    iterator_next next, iterator_free free)
{
  iter->dir = strdup (dir);
  iter->data = data;
  iter->next = next;
  iter->free = free;
}

gint
item_iterator_next (struct item_iterator *iter)
{
  return iter->next (iter);
}

void
item_iterator_free (struct item_iterator *iter)
{
  g_free (iter->dir);
  if (iter->free)
    {
      iter->free (iter->data);
    }
}

gboolean
item_iterator_is_dir_or_matches_extensions (struct item_iterator *iter,
					    const GSList *extensions)
{
  if (iter->item.type == ITEM_TYPE_DIR)
    {
      return TRUE;
    }

  return file_matches_extensions (iter->item.name, extensions);
}

gchar *
get_filename (guint32 fs_options, struct item *item)
{
  if (fs_options & FS_OPTION_SLOT_STORAGE && item->type == ITEM_TYPE_FILE)
    {
      gchar *id = g_malloc (LABEL_MAX);
      snprintf (id, LABEL_MAX, "%d", item->id);
      return id;
    }

  return strdup (item->name);
}
