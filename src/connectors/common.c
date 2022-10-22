/*
 *   common.h
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

#include "common.h"

gchar *
common_slot_get_upload_path (struct backend *backend,
			     struct item_iterator *remote_iter,
			     const struct fs_operations *ops,
			     const gchar * dst_dir, const gchar * src_path,
			     gint32 * next_index)
{
  //In SLOT mode, dst_dir includes the index, ':' and the item name.
  return strdup (dst_dir);
}

int
common_slot_get_id_name_from_path (const char *path, guint * id,
				   gchar ** name)
{
  gint err = 0;
  gchar *path_copy, *index_name, *remainder;

  path_copy = strdup (path);
  index_name = basename (path_copy);
  *id = (gint) strtol (index_name, &remainder, 10);
  if (strncmp (remainder, BE_SAMPLE_ID_NAME_SEPARATOR,
	       strlen (BE_SAMPLE_ID_NAME_SEPARATOR)) == 0)
    {
      remainder++;		//Skip ':'
    }
  else
    {
      if (name)
	{
	  error_print ("Path name not provided properly\n");
	  err = -EINVAL;
	  goto end;
	}
    }

  if (name)
    {
      if (*remainder)
	{
	  *name = strdup (remainder);
	}
      else
	{
	  *name = NULL;
	}
    }

end:
  g_free (path_copy);
  return err;
}
