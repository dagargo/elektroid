/*
 *   package.h
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

#ifndef PACKAGE_H
#define PACKAGE_H

#define ELEKTRON_SAMPLE_RATE 48000

#define ELEKTRON_MAX_STORAGE 8	//Limited to 8 by guint8 type in t_get_storage_stats
#define ELEKTRON_MAX_FS 32	//Limiter to 32 by guint32 id in fs_operations
#define ELEKTRON_MAX_EXTENSIONS 32

#define FS_DATA_METADATA_EXT "metadata"
#define FS_DATA_METADATA_FILE "." FS_DATA_METADATA_EXT

enum package_resource_type
{
  PKG_RES_TYPE_NONE,
  PKG_RES_TYPE_PAYLOAD,
  PKG_RES_TYPE_MANIFEST,
  PKG_RES_TYPE_SAMPLE
};

struct package_resource
{
  enum package_resource_type type;
  guint32 hash;
  guint32 size;
  gchar *path;
  GByteArray *data;
  GSList *tags;			//Used for PKG_RES_TYPE_MANIFEST only
};

enum package_type
{
  PKG_FILE_TYPE_NONE,
  PKG_FILE_TYPE_DATA_SOUND,
  PKG_FILE_TYPE_DATA_PROJECT,
  PKG_FILE_TYPE_DATA_PRESET,
  PKG_FILE_TYPE_RAW_PRESET
};

struct fs_desc
{
  gchar name[LABEL_MAX];
  gchar *extensions[ELEKTRON_MAX_EXTENSIONS];
};

struct device_desc
{
  guint32 id;
  gchar name[LABEL_MAX];
  guint8 storage;
  guint fs_descs_len;
  struct fs_desc fs_descs[ELEKTRON_MAX_FS];
};

struct elektron_data
{
  guint16 seq;
  struct device_desc device_desc;
};

struct package
{
  gchar *name;
  enum package_type type;
  gchar *fw_version;
  const struct device_desc *device_desc;
  gchar *buff;
  zip_source_t *zip_source;
  zip_t *zip;
  GList *resources;
  struct package_resource *manifest;
};

GSList *package_get_tags_from_snd_metadata (GByteArray * metadata);

gint package_begin (struct package *pkg, gchar * name,
		    const gchar * fw_version,
		    const struct device_desc *device_desc,
		    enum package_type type);

gint package_receive_pkg_resources (struct package *pkg,
				    const gchar * payload_path,
				    struct job_control *control,
				    struct backend *backend,
				    fs_remote_file_op download_data,
				    fs_remote_file_op download_sample,
				    enum package_type type);

gint package_end (struct package *pkg, struct idata *idata);

void package_destroy (struct package *pkg);

gint package_open (struct package *pkg, struct idata *idata,
		   const struct device_desc *device_desc);

gint package_send_pkg_resources (struct package *pkg,
				 const gchar * payload_path,
				 struct job_control *control,
				 struct backend *backend,
				 fs_remote_file_op upload_data);

void package_close (struct package *pkg);

#endif
