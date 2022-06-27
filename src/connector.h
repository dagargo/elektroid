/*
 *   connector.h
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

#include <glib.h>
#include <alsa/asoundlib.h>
#include "utils.h"

#ifndef CONNECTOR_H
#define CONNECTOR_H

struct connector
{
  GHashTable *dir_cache;
  gushort seq;
  gchar *fw_version;
};

enum connector_fs
{
  FS_SAMPLES = 0x1,
  FS_RAW_ALL = 0x2,
  FS_RAW_PRESETS = 0x4,
  FS_DATA_ALL = 0x8,
  FS_DATA_PRJ = 0x10,
  FS_DATA_SND = 0x20,
  FS_SAMPLES_SDS = 0x40
};

struct connector_iterator_data
{
  GByteArray *msg;
  guint32 pos;
  guint32 hash;
  guint16 operations;
  guint8 has_valid_data;
  guint8 has_metadata;
  enum connector_fs fs;
  gboolean cached;
};

enum connector_storage
{
  STORAGE_PLUS_DRIVE = 0x1,
  STORAGE_RAM = 0x2
};

struct connector_storage_stats
{
  const gchar *name;
  guint64 bsize;
  guint64 bfree;
};

const struct fs_operations *connector_get_fs_operations (enum connector_fs,
							 const gchar *);

gint connector_init (struct backend *, gint, const gchar *);

void connector_destroy (struct backend *);

gint connector_get_storage_stats (struct backend *,
				  enum connector_storage,
				  struct connector_storage_stats *);

gdouble connector_get_storage_stats_percent (struct connector_storage_stats
					     *);

gchar *connector_get_upload_path (struct backend *, struct item_iterator *,
				  const struct fs_operations *, const gchar *,
				  const gchar *, gint32 *);

gchar *connector_get_sample_path_from_hash_size (struct backend *,
						 guint32, guint32);

void connector_enable_dir_cache (struct backend *);

void connector_disable_dir_cache (struct backend *);

#endif
