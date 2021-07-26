/*
 *   local.h
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

#include <glib.h>
#include "utils.h"

struct local_iterator_data
{
  DIR *dir;
  gchar *path;
};

extern const struct fs_operations FS_LOCAL_OPERATIONS;

gint local_mkdir (const gchar *, void *);

gint local_delete (const gchar *, void *);

gint local_rename (const gchar *, const gchar *, void *);

struct item_iterator *local_read_dir (const gchar *, void *);
