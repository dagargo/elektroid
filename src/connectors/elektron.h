/*
 *   elektron.h
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

#ifndef ELEKTRON_H
#define ELEKTRON_H

#include "utils.h"
#include "backend.h"

enum elektron_fs
{
  FS_SAMPLES = 0x1,
  FS_RAW_ALL = 0x2,
  FS_RAW_PRESETS = 0x4,
  FS_DATA_ALL = 0x8,
  FS_DATA_PRJ = 0x10,
  FS_DATA_SND = 0x20,
  FS_DATA_PST = 0x40,
  FS_SAMPLES_STEREO = 0x80,
  FS_DATA_DT2_PST = 0x100
};

gchar *elektron_get_sample_path_from_hash_size (struct backend *backend,
						guint32 hash, guint32 size);

gint elektron_upload_sample_part (struct backend *backend, const gchar * path,
				  struct idata *sample,
				  struct job_control *control);

GByteArray *elektron_ping (struct backend *backend);

gint elektron_handshake (struct backend *backend);

gint elektron_sample_save (const gchar * path, struct idata *sample,
			   struct job_control *control);

//The following declarations are just for testing purposes.

GSList *elektron_get_dev_exts (struct backend *backend,
			       const struct fs_operations *ops);

GSList *elektron_get_dev_exts_pst (struct backend *backend,
				   const struct fs_operations *ops);

GSList *elektron_get_dev_exts_prj (struct backend *backend,
				   const struct fs_operations *ops);

GSList *elektron_get_dt2_pst_exts (struct backend *backend,
				   const struct fs_operations *ops);

#endif
