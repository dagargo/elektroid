/*
 *   elektron_pkg.h
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
#include <zip.h>
#include "connector.h"

#ifndef ELEKTRON_PKG_H
#define ELEKTRON_PKG_H

#define ELEKTRON_SAMPLE_RATE 48000

#define ELEKTRON_MAX_STORAGE 8	//Limited to 8 by guint8 type in t_get_storage_stats
#define ELEKTRON_MAX_FS 32	//Limiter to 32 by guint32 id in fs_operations
#define ELEKTRON_MAX_EXTENSIONS 32

#define FS_DATA_METADATA_EXT "metadata"
#define FS_DATA_METADATA_FILE "." FS_DATA_METADATA_EXT

enum elektron_pkg_resource_type
{
  PKG_RES_TYPE_NONE,
  PKG_RES_TYPE_PAYLOAD,
  PKG_RES_TYPE_MANIFEST,
  PKG_RES_TYPE_SAMPLE
};

struct elektron_pkg_resource
{
  enum elektron_pkg_resource_type type;
  guint32 hash;
  guint32 size;
  gchar *path;
  GByteArray *data;
  GSList *tags;			//Used for PKG_RES_TYPE_MANIFEST only
};

enum elektron_pkg_type
{
  PKG_FILE_TYPE_NONE,
  PKG_FILE_TYPE_DATA_SOUND,
  PKG_FILE_TYPE_DATA_PROJECT,
  PKG_FILE_TYPE_DATA_PRESET,	//Analog Heat family (no tags)
  PKG_FILE_TYPE_RAW_PRESET
};

struct elektron_fs_desc
{
  gchar name[LABEL_MAX];
  gchar *extensions[ELEKTRON_MAX_EXTENSIONS];
};

struct elektron_dev_desc
{
  guint32 id;
  gchar name[LABEL_MAX];
  guint8 storage;
  guint fs_descs_len;
  struct elektron_fs_desc fs_descs[ELEKTRON_MAX_FS];
};

struct elektron_data
{
  guint16 seq;
  struct elektron_dev_desc dev_desc;
};

struct elektron_pkg
{
  gchar *name;
  enum elektron_pkg_type type;
  gchar *fw_version;
  const struct elektron_dev_desc *dev_desc;
  gchar *buff;
  zip_source_t *zip_source;
  zip_t *zip;
  GList *resources;
  struct elektron_pkg_resource *manifest;
};

GSList *elektron_pkg_get_tags_from_snd_metadata (GByteArray * metadata);

gint elektron_pkg_begin (struct elektron_pkg *pkg, gchar * name,
			 const gchar * fw_version,
			 const struct elektron_dev_desc *dev_desc,
			 enum elektron_pkg_type type);

gint elektron_pkg_receive_pkg_resources (struct elektron_pkg *pkg,
					 const gchar * payload_path,
					 struct task_control *control,
					 struct backend *backend,
					 fs_remote_file_op download_data,
					 enum elektron_pkg_type type);

gint elektron_pkg_end (struct elektron_pkg *pkg, struct idata *idata);

void elektron_pkg_destroy (struct elektron_pkg *pkg);

gint elektron_pkg_open (struct elektron_pkg *pkg, struct idata *idata,
			const struct elektron_dev_desc *dev_desc);

gint elektron_pkg_send_pkg_resources (struct elektron_pkg *pkg,
				      const gchar * payload_path,
				      struct task_control *control,
				      struct backend *backend,
				      fs_remote_file_op upload_data);

void elektron_pkg_close (struct elektron_pkg *pkg);

#endif
