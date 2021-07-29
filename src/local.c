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

static gint local_mkdir (const gchar *, void *);

static gint local_delete (const gchar *, void *);

static gint local_rename (const gchar *, const gchar *, void *);

static struct item_iterator *local_read_dir (const gchar *, void *);

const struct fs_operations FS_LOCAL_OPERATIONS = {
  .fs = 0,
  .readdir = local_read_dir,
  .mkdir = local_mkdir,
  .delete = local_delete,
  .move = local_rename,
  .copy = NULL,
  .clear = NULL,
  .swap = NULL,
  .download = NULL,
  .upload = NULL,
  .download_ext = NULL
};

gint
local_mkdir (const gchar * name, void *data)
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
      error = local_mkdir (parent, data);
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

static gint
local_delete (const gchar * path, void *data)
{
  DIR *dir;
  gchar *new_path;
  struct dirent *dirent;

  if ((dir = opendir (path)))
    {
      debug_print (1, "Deleting local %s dir...\n", path);

      while ((dirent = readdir (dir)) != NULL)
	{
	  if (strcmp (dirent->d_name, ".") == 0 ||
	      strcmp (dirent->d_name, "..") == 0)
	    {
	      continue;
	    }
	  new_path = chain_path (path, dirent->d_name);
	  local_delete (new_path, data);
	  free (new_path);
	}

      closedir (dir);

      return rmdir (path);
    }
  else
    {
      debug_print (1, "Deleting local %s file...\n", path);
      return unlink (path);
    }
}

static gint
local_rename (const gchar * old, const gchar * new, void *data)
{
  debug_print (1, "Renaming locally from %s to %s...\n", old, new);
  return rename (old, new);
}

static void
local_free_iterator_data (void *iter_data)
{
  struct local_iterator_data *data = iter_data;
  closedir (data->dir);
  g_free (data->path);
  g_free (data);
}

static guint
local_next_dentry (struct item_iterator *iter)
{
  gchar *full_path;
  struct dirent *dirent;
  struct stat st;
  struct local_iterator_data *data = iter->data;
  guint ret = -ENOENT;

  if (iter->entry != NULL)
    {
      g_free (iter->entry);
    }

  while ((dirent = readdir (data->dir)) != NULL)
    {
      if (dirent->d_name[0] == '.')
	{
	  continue;
	}

      if (dirent->d_type == DT_DIR || dirent->d_type == DT_REG)
	{
	  iter->entry = strdup (dirent->d_name);

	  if (dirent->d_type == DT_DIR)
	    {
	      iter->type = ELEKTROID_DIR;
	      iter->size = 0;
	    }
	  else
	    {
	      iter->type = ELEKTROID_FILE;
	      full_path = chain_path (data->path, dirent->d_name);
	      if (stat (full_path, &st) == 0)
		{
		  iter->size = st.st_size;
		}
	      else
		{
		  iter->size = 0;
		}
	      free (full_path);
	    }
	  ret = 0;
	  break;
	}
    }

  return ret;
}

static struct item_iterator *
local_read_dir (const gchar * path, void *data_)
{
  DIR *dir;
  struct item_iterator *iter;
  struct local_iterator_data *data;

  if (!(dir = opendir (path)))
    {
      return NULL;
    }

  data = malloc (sizeof (struct local_iterator_data));
  data->dir = dir;
  data->path = strdup (path);

  iter = malloc (sizeof (struct item_iterator));
  iter->data = data;
  iter->entry = NULL;
  iter->next = local_next_dentry;
  iter->free = local_free_iterator_data;

  return iter;
}
