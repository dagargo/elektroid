/*
 *   local.c
 *   Copyright (C) 2021 David García Goñi <dagargo@gmail.com>
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

#include <dirent.h>
#include <errno.h>
#include <libgen.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include "local.h"

gint
local_mkdir (const gchar * name)
{
  DIR *dir;
  gint error = 0;
  gchar *dup;
  gchar *parent;

  dup = strdup (name);
  parent = dirname (dup);

  dir = opendir (parent);
  if (dir)
    {
      closedir (dir);
    }
  else
    {
      error = local_mkdir (parent);
      if (error)
	{
	  goto cleanup;
	}
    }

  if (mkdir (name, 0755) == 0 || errno == EEXIST)
    {
      error = 0;
    }
  else
    {
      error_print ("Error while creating dir %s\n", name);
      error = errno;
    }

cleanup:
  g_free (dup);
  return error;
}

gint
local_delete (const gchar * path, enum item_type type)
{
  DIR *dir;
  struct dirent *dirent;
  gchar *new_path;
  gchar new_type;

  if (type == ELEKTROID_DIR)
    {
      debug_print (1, "Deleting local %s dir...\n", path);
      dir = opendir (path);
      if (dir)
	{
	  while ((dirent = readdir (dir)) != NULL)
	    {
	      if (strcmp (dirent->d_name, ".") == 0 ||
		  strcmp (dirent->d_name, "..") == 0)
		{
		  continue;
		}
	      new_path = chain_path (path, dirent->d_name);
	      new_type =
		dirent->d_type == DT_DIR ? ELEKTROID_DIR : ELEKTROID_FILE;
	      local_delete (new_path, new_type);
	      free (new_path);
	    }
	  closedir (dir);
	}
      else
	{
	  error_print ("Error while opening local %s dir\n", path);
	}
      return rmdir (path);
    }
  else
    {
      debug_print (1, "Deleting local %s file...\n", path);
      return unlink (path);
    }
}

gint
local_rename (const gchar * old, const gchar * new)
{
  debug_print (1, "Renaming locally from %s to %s...\n", old, new);
  return rename (old, new);
}
