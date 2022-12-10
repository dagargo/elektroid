/*
 *   common.c
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

#include "backend.h"

gchar *common_slot_get_upload_path (struct backend *backend,
				    const struct fs_operations *ops,
				    const gchar * dst_dir,
				    const gchar * src_path,
				    gint32 * next_index);

int common_slot_get_id_name_from_path (const char *path, guint * id,
				       gchar ** name);

gchar *common_get_id_as_slot (struct item *item, struct backend *backend);

void common_print_item (struct item_iterator *iter, struct backend *backend,
			const struct fs_operations *fs_ops);
